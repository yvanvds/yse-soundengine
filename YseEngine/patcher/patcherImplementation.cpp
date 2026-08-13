
#include "patcherImplementation.h"
#include "pRegistry.h"
#include "pObjectList.hpp"
#include "../headers/enums.hpp"
#include "genericObjects/pDac.h"
#include "genericObjects/pAdc.h"
#include "genericObjects/gArray.h"
#include "genericObjects/gBag.h"
#include "genericObjects/gColl.h"
#include "genericObjects/gDict.h"
#include "genericObjects/gDictCompare.h"
#include "genericObjects/gDictDeserialize.h"
#include "genericObjects/gDictGroup.h"
#include "genericObjects/gDictIter.h"
#include "genericObjects/gDictJoin.h"
#include "genericObjects/gDictPack.h"
#include "genericObjects/gDictPrint.h"
#include "genericObjects/gDictRoute.h"
#include "genericObjects/gDictSerialize.h"
#include "genericObjects/gForward.h"
#include "genericObjects/gReceive.h"
#include "genericObjects/gSend.h"
#include "genericObjects/gTable.h"
#include "genericObjects/gValue.h"
#include "genericObjects/dInlet.h"
#include "genericObjects/dOutlet.h"
#include "genericObjects/gInlet.h"
#include "genericObjects/gOutlet.h"
#include "pHandle.hpp"
#include "../utils/json.hpp"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "../implementations/logImplementation.h"
#include "../internal/global.h"

using namespace YSE::PATCHER;

namespace {
  // Process-wide counter feeding the auto-generated "patcher_<N>" default
  // name (issue #122). Deliberately the only process-wide counter left in the
  // patcher: object storage IDs became per-patcher in #730, but a patcher's
  // *own* default name has to be distinct from every other patcher's in the
  // process, because it is the bus prefix inner gSend/gReceive route on.
  std::atomic<unsigned int> g_nextPatcherIndex{0};

  // Shared empty list argument for DispatchToReceiver on the non-list value
  // kinds (Bang/Int/Float), so those paths pass a std::string& without building
  // a temporary. Never read for those kinds.
  const std::string kEmptyList;

  // The patcher whose Calculate() this thread is currently inside, or null
  // (issue #690). This is the thread-identity half of what THREAD used to be
  // asked for: see patcherImplementation::CallingThread in the header for why
  // the tag cannot answer it.
  //
  // Per *patcher* rather than a bare "in a render frame" flag, because a stale
  // answer is worse than none: only the patcher currently rendering has a
  // GraphState pinned in currentBlockGraph_, and dispatching against another
  // patcher's unpinned (possibly retired) snapshot is exactly the use-after-free
  // #226 exists to prevent. Saved and restored rather than set and cleared, so
  // a patcher rendered from inside another one's frame restores its caller.
  //
  // RT-safety, on the same terms as outlet.cpp's send-depth guard and
  // inlet.cpp's event-depth counter: constant-initialised thread_local, so
  // touching it is a plain TLS load/store — no allocation, no lock, no syscall
  // — and each render thread (the audio callback plus every fast-pool worker
  // that renders a child channel) tracks its own frame independently.
  thread_local const YSE::PATCHER::patcherImplementation* tRenderingPatcher = nullptr;

  // RAII frame marker for Calculate.
  struct renderFrameGuard {
    const YSE::PATCHER::patcherImplementation* previous;
    explicit renderFrameGuard(const YSE::PATCHER::patcherImplementation* p)
      : previous(tRenderingPatcher) {
      tRenderingPatcher = p;
    }
    ~renderFrameGuard() {
      tRenderingPatcher = previous;
    }
    renderFrameGuard(const renderFrameGuard&) = delete;
    renderFrameGuard& operator=(const renderFrameGuard&) = delete;
  };

  // The outlet index a serialised "output N" key names, or -1 if the key is not
  // one (issue #734). pObject::DumpJson writes the outlets under these keys and
  // ParseJSON used to walk them with a loop counter, which only agrees with the
  // key while the keys happen to come back in numeric order — and they do not:
  // a nlohmann json object is a std::map<std::string, json>, so it replays them
  // in *string* order, "output 10" ahead of "output 2". Objects with more than
  // ten outlets are ordinary (.route, .sel and .trigger all grow one per
  // creation argument), and for those every edge from outlet 2 upward came back
  // on an outlet the file never named.
  constexpr const char kOutletKeyPrefix[] = "output ";
  // Longest run of digits still comfortably inside an int. No object has
  // anywhere near a billion outlets, so a longer run is a malformed key, not a
  // big one — reject it instead of overflowing.
  constexpr std::size_t kMaxOutletDigits = 9;

  int OutletIndexFromKey(const std::string& key) {
    constexpr std::size_t prefixLength = sizeof(kOutletKeyPrefix) - 1;
    if (key.compare(0, prefixLength, kOutletKeyPrefix) != 0) return -1;
    const std::size_t digits = key.size() - prefixLength;
    if (digits == 0 || digits > kMaxOutletDigits) return -1;
    int index = 0;
    for (std::size_t i = prefixLength; i < key.size(); i++) {
      const char c = key[i];
      if (c < '0' || c > '9') return -1;
      index = (index * 10) + (c - '0');
    }
    return index;
  }
} // namespace

YSE::THREAD patcherImplementation::CallingThread(YSE::THREAD tag) const {
  if (tag == YSE::T_DSP) return YSE::T_DSP;
  return tRenderingPatcher == this ? YSE::T_DSP : YSE::T_GUI;
}

patcherImplementation::patcherImplementation(int mainOutputs, YSE::patcher* head)
  : pObject(false),
    controlledBySound(false),
    head(head),
    patcherName("patcher_" +
                std::to_string(g_nextPatcherIndex.fetch_add(1, std::memory_order_relaxed))) {
  output.resize(mainOutputs);
  // Pre-size the audio-thread list-delivery scratch so SetList never allocates
  // on the callback path (issue #225).
  listScratch_.reserve(kValueListCap);
  // Wire the two reclaim jobs into a ping-pong pair (issue #227): each re-arms
  // the other so a reclaim pass with remaining work never re-enqueues itself.
  reclaimJobs_[0].owner = this;
  reclaimJobs_[0].sibling = &reclaimJobs_[1];
  reclaimJobs_[1].owner = this;
  reclaimJobs_[1].sibling = &reclaimJobs_[0];
}

void patcherImplementation::SetName(const std::string& n) {
  if (n == patcherName) return;
  mtx.lock();
  patcherName = n;
  // Re-subscribe every gReceive in this patcher so the new prefix takes
  // effect immediately. Iteration is safe because gReceive::Resubscribe
  // only touches the gReceive's own subscription handle.
  for (auto& x : objects) {
    if (strcmp(x.second->Type(), OBJ::G_RECEIVE) == 0) {
      static_cast<gReceive*>(x.second)->Resubscribe();
    } else if (strcmp(x.second->Type(), OBJ::G_SEND) == 0) {
      // Keep gSend's cached bus address in step with the receivers that just
      // re-anchored, so sends still reach them under the new name (issue #187).
      static_cast<gSend*>(x.second)->RefreshBusAddress();
    } else if (strcmp(x.second->Type(), OBJ::G_FORWARD) == 0) {
      // Same for gForward, which caches the "<patcherName>." prefix its runtime
      // destination is appended to (issue #485).
      static_cast<gForward*>(x.second)->RefreshBusAddress();
    } else if (strcmp(x.second->Type(), OBJ::G_VALUE) == 0) {
      // gValue addresses its shared cell as "<patcherName>.<name>" too, so a
      // rename moves it onto the cell the renamed patcher's sends and receives
      // now speak about (issue #486).
      static_cast<gValue*>(x.second)->RefreshBinding();
    } else if (strcmp(x.second->Type(), OBJ::G_COLL) == 0) {
      // And gColl, whose shared store is registered under the same
      // "<patcherName>.<name>" address, so a named collection re-anchors with
      // the values, sends and receives around it (issue #684).
      static_cast<gColl*>(x.second)->RefreshBinding();
    } else if (strcmp(x.second->Type(), OBJ::G_DICT) == 0) {
      // And gDict, whose shared dictionary is registered under the same
      // "<patcherName>.<name>" address, so a renamed patcher's dictionaries
      // re-anchor with its collections, values, sends and receives (issue #550).
      static_cast<gDict*>(x.second)->RefreshBinding();
    } else if (strcmp(x.second->Type(), OBJ::G_DICT_COMPARE) == 0) {
      // And gDictCompare, whose two bound dictionaries are registered under
      // the same "<patcherName>.<name>" addresses, so a renamed patcher's
      // comparisons follow the dictionaries they compare (issue #770).
      static_cast<gDictCompare*>(x.second)->RefreshBinding();
    } else if (strcmp(x.second->Type(), OBJ::G_DICT_DESERIALIZE) == 0) {
      // And gDictDeserialize, whose target dictionary is registered under the
      // same "<patcherName>.<name>" address, so a renamed patcher's parsed
      // documents land in the dictionary its other objects now speak about
      // (issue #771).
      static_cast<gDictDeserialize*>(x.second)->RefreshBinding();
    } else if (strcmp(x.second->Type(), OBJ::G_DICT_GROUP) == 0) {
      // And gDictGroup, whose source and target dictionaries are registered
      // under the same "<patcherName>.<name>" addresses, so a renamed
      // patcher's groupings follow the dictionaries they read and write
      // (issue #772).
      static_cast<gDictGroup*>(x.second)->RefreshBinding();
    } else if (strcmp(x.second->Type(), OBJ::G_DICT_ITER) == 0) {
      // And gDictIter, whose bound dictionary is registered under the same
      // "<patcherName>.<name>" address, so a renamed patcher's walks follow
      // the dictionary they stream (issue #773).
      static_cast<gDictIter*>(x.second)->RefreshBinding();
    } else if (strcmp(x.second->Type(), OBJ::G_DICT_JOIN) == 0) {
      // And gDictJoin, whose source and target dictionaries are registered
      // under the same "<patcherName>.<name>" addresses, so a renamed
      // patcher's joins follow the dictionaries they read and write
      // (issue #774).
      static_cast<gDictJoin*>(x.second)->RefreshBinding();
    } else if (strcmp(x.second->Type(), OBJ::G_DICT_PACK) == 0) {
      // And gDictPack, whose bound dictionary is registered under the same
      // "<patcherName>.<name>" address, so a renamed patcher's packs land in
      // the dictionary its other objects now speak about (issue #775).
      static_cast<gDictPack*>(x.second)->RefreshBinding();
    } else if (strcmp(x.second->Type(), OBJ::G_DICT_PRINT) == 0) {
      // And gDictPrint, whose bound dictionary is registered under the same
      // "<patcherName>.<name>" address, so a renamed patcher's printouts
      // read the dictionary its other objects now speak about (issue #776).
      static_cast<gDictPrint*>(x.second)->RefreshBinding();
    } else if (strcmp(x.second->Type(), OBJ::G_DICT_ROUTE) == 0) {
      // And gDictRoute, whose bound dictionary is registered under the same
      // "<patcherName>.<name>" address, so a renamed patcher's routes test
      // the dictionary its other objects now speak about (issue #777).
      static_cast<gDictRoute*>(x.second)->RefreshBinding();
    } else if (strcmp(x.second->Type(), OBJ::G_DICT_SERIALIZE) == 0) {
      // And gDictSerialize, whose bound dictionary is registered under the
      // same "<patcherName>.<name>" address, so a renamed patcher's
      // serialisations read the dictionary its other objects now speak about
      // (issue #778).
      static_cast<gDictSerialize*>(x.second)->RefreshBinding();
    } else if (strcmp(x.second->Type(), OBJ::G_ARRAY) == 0) {
      // And gArray, whose shared sequence is registered under the same
      // "<patcherName>.<name>" address, so a renamed patcher's arrays re-anchor
      // with its dictionaries, collections, values, sends and receives
      // (issue #548).
      static_cast<gArray*>(x.second)->RefreshBinding();
    } else if (strcmp(x.second->Type(), OBJ::G_BAG) == 0) {
      // And gBag, whose `send` message prefixes a runtime receive name with
      // "<patcherName>." exactly as gForward does, so it has to re-anchor with
      // the receivers too (issue #685).
      static_cast<gBag*>(x.second)->RefreshBusPrefix();
    } else if (strcmp(x.second->Type(), OBJ::G_TABLE) == 0) {
      // And gTable, whose `send` prefixes its runtime destination the same way
      // gBag and gForward do (issue #699).
      static_cast<gTable*>(x.second)->RefreshBusPrefix();
    }
  }
  mtx.unlock();
}

patcherImplementation::~patcherImplementation() {
  // Stop scheduling and re-arming reclaim passes first, so the joins below
  // terminate instead of chasing a self-perpetuating ping-pong.
  shuttingDown_.store(true, std::memory_order_release);
  // A destructor is implicitly noexcept, so anything escaping the teardown below
  // would call std::terminate() and take the host process down instead of
  // shutting the engine down (issue #414). Clear() locks and allocates, so it is
  // the one step here that can realistically throw — guard it on its own rather
  // than wrapping the whole body, because the joins and FreeAllRetired() beneath
  // it must run even when it fails: a reclaim pass still on the background pool
  // holds `this` and the retire lists it is draining.
  try {
    // memory cleanup
    Clear();
  } catch (...) {
    INTERNAL::EmitNoThrow(E_ERROR, "PATCHER::patcherImplementation Clear swallowed exception");
  }
  try {
    // A reclaim pass already handed to the background pool may still be draining
    // the retire lists; wait for both halves of the ping-pong to finish before we
    // free those lists from under them. shuttingDown_ guarantees neither re-arms.
    // These joins are a spin-wait on an atomic flag (INTERNAL::threadPoolJob), not
    // std::thread::join, so they carry no system_error of their own.
    reclaimJobs_[0].join();
    reclaimJobs_[1].join();
    // The audio thread is stopped at destruction, so reclaim unconditionally:
    // the remaining retire lists (and the final published snapshot) are freed
    // here rather than waiting on the block counter.
    FreeAllRetired();
  } catch (...) {
    INTERNAL::EmitNoThrow(E_ERROR, "PATCHER::patcherImplementation destructor swallowed exception");
  }
  // Last, and outside the guard above rather than inside it: ~fileScheduler
  // joins the background jobs still holding its slots and the reclaim ping-pong
  // shares that one worker thread, so the reclaim joins have to come first — but
  // the file table must be freed even when the step before it failed, exactly as
  // those joins must run even when Clear() failed (issue #683). Nothing here can
  // throw: the join is a spin on an atomic flag, and no object can still ask for
  // a file because Clear() ran above.
  delete fileIO_.exchange(nullptr, std::memory_order_acq_rel);
}

void patcherImplementation::EnsureFileIO() {
  // Control thread, under mtx (see the header). Idempotent: the first
  // file-capable object to join builds the table and every later one finds it.
  if (fileIO_.load(std::memory_order_acquire) != nullptr) return;
  fileIO_.store(new fileScheduler(), std::memory_order_release);
}

const char* patcherImplementation::Type() const {
  return YSE::OBJ::PATCHER;
}

void patcherImplementation::Calculate(YSE::THREAD thread) {
  // works a bit different in case of patchers!
  // only called by the main patcher to generate output.
  //
  // No mutex here (issue #226): the topology is read through one atomic load
  // of the published GraphState, pinned for the whole block so every send and
  // readiness query the traversal makes resolves against a coherent snapshot.
  const GraphState* g = active_.load(std::memory_order_acquire);
  audioBlock_.fetch_add(1, std::memory_order_acq_rel);
  currentBlockGraph_.store(g, std::memory_order_release);

  // Mark the frame, after the pin so "inside this patcher's Calculate" always
  // implies "this patcher has a snapshot pinned". This is what lets a handler
  // dispatched with T_GUI from one of the drains below discover that it is on
  // the audio callback and take the lock-free route (issue #690) — see
  // CallingThread in the header.
  renderFrameGuard frame(this);

  // Apply queued scalar param plans first (issue #234), then deliver queued
  // value messages (PassBang/PassData), both before rendering and against the
  // pinned snapshot. Param stores land before any handler or render read of
  // this block, so a SetParams that precedes a PassData on the control thread
  // is also observed in that order here.
  ApplyPendingParams(g);
  DeliverPendingValues(g);
  // Give any domain-clock binding still waiting on its clock another chance to
  // find it (issue #688), before the deferred drain below reads beat positions
  // through those bindings. Rate-limited inside Poll to one bounded walk every
  // clockBridge::RESOLVE_INTERVAL_BLOCKS blocks; it never allocates or locks.
  clocks_.Poll(audioBlock_.load(std::memory_order_relaxed));
  // Then everything the deferred-message scheduler has due (issue #628) — after
  // the value drain so a value delivered this block can arm a deferral that is
  // honestly "later", and before the render with the same T_GUI semantics as
  // the value drain: the delivery sets state / forwards values, and the block's
  // own traversal renders whatever it caused.
  scheduler_.DeliverDue(g, YSE::T_GUI);

  // Then whatever the background pool has finished reading or writing (issue
  // #683), in the same kind of dispatch frame and against the same pinned
  // snapshot. After the deferred drain rather than before it, so a file
  // completion is the last thing that can arm work for a later block.
  if (fileScheduler* io = fileIO_.load(std::memory_order_acquire)) {
    io->DeliverComplete(g, YSE::T_GUI);
  }

  if (g != nullptr) {
    // Then the objects fed from outside the patch (issue #529): the MIDI-input
    // family, whose events came in on RtMidi's thread and are waiting in a
    // lock-free queue. Last of the drains and before the render, so a message
    // that arrived between two blocks reaches the patch in the block that
    // follows it and everything it triggers is rendered by that same block.
    //
    // T_GUI, like every other drain above, and for the same reason: the tag is
    // dispatch semantics rather than thread identity, and an object that needs
    // to know it is really on the audio callback asks CallingThread (#690).
    for (unsigned int i = 0; i < g->pollers.size(); i++) {
      g->pollers[i]->Calculate(YSE::T_GUI);
    }

    // invalidate all dsp buffers
    for (unsigned int i = 0; i < g->objects.size(); i++) {
      g->objects[i]->ResetDSP();
    }

    // calculate all dsp start points; the push traversal fans out from here
    for (unsigned int i = 0; i < g->startPoints.size(); i++) {
      g->startPoints[i]->Calculate(thread);
    }
  }

  // clear output
  for (unsigned int i = 0; i < output.size(); i++) {
    output[i] = 0;
  }

  // sum outputs
  int counter = 0;
  if (g != nullptr) {
    for (unsigned int d = 0; d < g->dacs.size(); d++) {
      pDac* dac = static_cast<pDac*>(g->dacs[d]);
      for (unsigned int i = 0; i < output.size(); i++) {
        YSE::DSP::buffer* ptr = dac->GetBuffer(i);
        if (ptr != nullptr) {
          output[i] = *ptr;
        }
      }
      counter++;
    }
  }

  // normalize output
  if (counter > 1) {
    for (unsigned int i = 0; i < output.size(); i++) {
      output[i] /= (float)counter;
    }
  }

  // Unpin: between blocks the topology helpers fall back to the live wiring
  // (control-thread / standalone path) instead of a possibly-retired snapshot.
  currentBlockGraph_.store(nullptr, std::memory_order_release);
}

void patcherImplementation::ResetDSP() {
  for (const auto& any : objects) {
    any.second->ResetDSP();
  }
}

void patcherImplementation::ProcessAsInsert(MULTICHANNELBUFFER& io) {
  // Audio thread. Run the patcher as an in-place insert effect over `io`
  // (issue #167): feed the host's incoming audio to the graph's ~adc objects,
  // render, then copy the summed ~dac output back over `io`.
  //
  // Channel-count contract between the host buffer and the graph I/O:
  //   - Input:  ADC channel `ch` is fed io[ch] when ch < io.size(), else it is
  //             left silent (null) — the graph simply gets no input on that
  //             channel.
  //   - Output: only the channels the graph produces are written back
  //             (min(io.size(), output.size())). Host channels beyond the
  //             graph's output count pass through unchanged (dry).
  const GraphState* g = active_.load(std::memory_order_acquire);
  if (g != nullptr) {
    const unsigned int hostChannels = static_cast<unsigned int>(io.size());
    for (pObject* obj : g->adcs) {
      pAdc* adc = static_cast<pAdc*>(obj);
      const unsigned int n = adc->NumChannels();
      for (unsigned int ch = 0; ch < n; ch++) {
        // The ADC pointers survive Calculate()'s ResetDSP pass (the ADC has no
        // inlets to clear), so setting them here — just before the render — is
        // exactly the ordering the graph traversal expects.
        adc->SetChannelBuffer(ch, ch < hostChannels ? &io[ch] : nullptr);
      }
    }
  }

  // Existing render path: invalidates DSP state, fans out from the start points
  // (the ~adc objects among them), and sums the ~dac buffers into `output`.
  Calculate(YSE::T_DSP);

  const unsigned int n =
      std::min(static_cast<unsigned int>(io.size()), static_cast<unsigned int>(output.size()));
  for (unsigned int ch = 0; ch < n; ch++) {
    io[ch] = output[ch];
  }
}

bool patcherImplementation::IsSubpatcher(pObject* obj) {
  return obj != nullptr && strcmp(obj->Type(), YSE::OBJ::PATCHER) == 0;
}

bool patcherImplementation::ContainmentWouldCycle(pObject* obj, pObject* container) {
  // Walk up from the proposed container: reaching `obj` means the move would
  // close a loop. Terminates because the containment graph is acyclic before
  // every call — which is exactly the invariant this maintains.
  for (pObject* walk = container; walk != nullptr; walk = walk->Container()) {
    if (walk == obj) return true;
  }
  return false;
}

int patcherImplementation::BoundaryIndexOf(pObject* obj, BoundarySide side) {
  // The one place the four boundary object types are mapped onto the side they
  // belong to and the `Index()` they claim (issues #545, #764). Both rates sit
  // on one side because a subpatcher has one set of pins per side — see
  // BoundarySide in the header.
  const char* type = obj->Type();
  if (side == BoundarySide::INLETS) {
    if (strcmp(type, YSE::OBJ::G_INLET) == 0) return static_cast<gInlet*>(obj)->Index();
    if (strcmp(type, YSE::OBJ::D_INLET) == 0) return static_cast<dInlet*>(obj)->Index();
    return -1;
  }
  if (strcmp(type, YSE::OBJ::G_OUTLET) == 0) return static_cast<gOutlet*>(obj)->Index();
  if (strcmp(type, YSE::OBJ::D_OUTLET) == 0) return static_cast<dOutlet*>(obj)->Index();
  return -1;
}

pObject* patcherImplementation::BoundaryChild(pObject* container, BoundarySide side,
                                              int index) const {
  // Caller holds mtx. Direct contents only — a `.inlet` inside a nested
  // subpatcher belongs to *that* subpatcher's boundary, not to this one's, and
  // Container() being one level deep is exactly what says so.
  //
  // A negative index can never match: BoundaryIndexOf answers -1 for an object
  // that is not on this side, and an `index` argument of -1 must not be allowed
  // to collide with that answer.
  if (index < 0) return nullptr;
  for (const auto& any : objects) {
    pObject* obj = any.second;
    if (obj->Container() != container) continue;
    if (BoundaryIndexOf(obj, side) == index) return obj;
  }
  return nullptr;
}

int patcherImplementation::BoundaryPinCount(pObject* container, BoundarySide side) const {
  // Caller holds mtx. One past the highest claimed index rather than a count of
  // boundary objects: the number a parent can pass to Connect is an index, so
  // the shape it can address is what a caller is asking about. A subpatcher
  // whose only `.inlet` is index 2 has three inlets, two of which reach
  // nothing, and Connect will say the same.
  //
  // Both rates count towards one total, for the same reason they share the
  // index space: a subpatcher with a `.inlet 0` and a `~inlet 1` presents two
  // inlets, not one of each.
  int highest = -1;
  for (const auto& any : objects) {
    pObject* obj = any.second;
    if (obj->Container() != container) continue;
    const int claimed = BoundaryIndexOf(obj, side);
    if (claimed > highest) highest = claimed;
  }
  return highest + 1;
}

bool patcherImplementation::ResolveInletPin(pObject*& obj, int& pin) const {
  if (!IsSubpatcher(obj)) return true;
  pObject* boundary = BoundaryChild(obj, BoundarySide::INLETS, pin);
  if (boundary == nullptr) return false;
  obj = boundary;
  pin = 0;
  return true;
}

bool patcherImplementation::ResolveOutletPin(pObject*& obj, int& pin) const {
  if (!IsSubpatcher(obj)) return true;
  pObject* boundary = BoundaryChild(obj, BoundarySide::OUTLETS, pin);
  if (boundary == nullptr) return false;
  obj = boundary;
  pin = 0;
  return true;
}

void patcherImplementation::CollectSubtree(YSE::pHandle* root,
                                           std::vector<YSE::pHandle*>& out) const {
  // Caller holds mtx. Breadth-first over the containment annotation, so `out`
  // is the whole subtree with the root first. Terminates because
  // SetObjectContainer refuses to make the containment graph cyclic; the
  // `frontier` index walk (rather than recursion) keeps a deeply nested patch
  // off the stack.
  out.push_back(root);
  for (std::size_t frontier = 0; frontier < out.size(); frontier++) {
    pObject* parentObj = out[frontier]->object;
    if (!IsSubpatcher(parentObj)) continue;
    for (const auto& any : objects) {
      if (any.second->Container() == parentObj) out.push_back(any.first);
    }
  }
}

void patcherImplementation::SetObjectContainer(YSE::pHandle* obj, YSE::pHandle* container) {
  if (obj == nullptr) return;
  std::scoped_lock lk(mtx);

  if (objects.find(obj) == objects.end()) {
    INTERNAL::LogImpl().emit(E_ERROR, "Patcher: SetContainer on an object this patcher does not "
                                      "own");
    return;
  }
  if (container == nullptr) {
    obj->object->SetContainer(nullptr);
    return;
  }
  if (objects.find(container) == objects.end()) {
    INTERNAL::LogImpl().emit(E_ERROR, "Patcher: SetContainer target is not in this patcher");
    return;
  }
  if (!IsSubpatcher(container->object)) {
    INTERNAL::LogImpl().emit(E_ERROR, "Patcher: SetContainer target is not a 'patcher' object");
    return;
  }
  // A subpatcher may not end up inside itself or inside one of its own
  // descendants. Without this check the containment graph could hold a cycle,
  // and DeleteObject's subtree walk over it would never terminate.
  if (ContainmentWouldCycle(obj->object, container->object)) {
    INTERNAL::LogImpl().emit(E_ERROR, "Patcher: SetContainer would put a subpatcher inside itself");
    return;
  }
  obj->object->SetContainer(container->object);
}

YSE::pHandle* patcherImplementation::GetObjectContainer(YSE::pHandle* obj) {
  if (obj == nullptr) return nullptr;
  std::scoped_lock lk(mtx);
  pObject* owner = obj->object->Container();
  if (owner == nullptr) return nullptr;
  for (const auto& any : objects) {
    if (any.second == owner) return any.first;
  }
  return nullptr;
}

int patcherImplementation::SubpatcherInlets(YSE::pHandle* container) {
  if (container == nullptr) return 0;
  std::scoped_lock lk(mtx);
  if (!IsSubpatcher(container->object)) return 0;
  return BoundaryPinCount(container->object, BoundarySide::INLETS);
}

int patcherImplementation::SubpatcherOutlets(YSE::pHandle* container) {
  if (container == nullptr) return 0;
  std::scoped_lock lk(mtx);
  if (!IsSubpatcher(container->object)) return 0;
  return BoundaryPinCount(container->object, BoundarySide::OUTLETS);
}

YSE::PATCHER::inlet* patcherImplementation::ResolveInlet(pObject* obj, int pin) {
  if (obj == nullptr) return nullptr;
  if (!IsSubpatcher(obj)) return obj->GetInlet(pin);
  // Only the lookup takes the lock — see the header for why the delivery must
  // not.
  std::scoped_lock lk(mtx);
  pObject* boundary = BoundaryChild(obj, BoundarySide::INLETS, pin);
  return boundary == nullptr ? nullptr : boundary->GetInlet(0);
}

void patcherImplementation::ConnectUnlocked(YSE::pHandle* from, int outlet, YSE::pHandle* to,
                                            int inlet) {
  // Resolve subpatcher façades to the boundary objects that carry the pins
  // before any wiring happens (issue #545), so what gets recorded is an
  // ordinary edge between ordinary objects. A non-subpatcher passes through
  // untouched, which is why this is also correct on the ParseJSON path: a dump
  // records edges against the resolved boundary objects, so a reload resolves
  // nothing and rebuilds exactly the edge that was saved.
  pObject* source = from->object;
  pObject* target = to->object;
  int sourcePin = outlet;
  int targetPin = inlet;
  if (!ResolveOutletPin(source, sourcePin) || !ResolveInletPin(target, targetPin)) {
    INTERNAL::LogImpl().emit(E_ERROR,
                             "Patcher: subpatcher has no boundary object for that pin number");
    return;
  }

  PATCHER::outlet* out = source->GetOutlet(sourcePin);
  PATCHER::inlet* in = target->GetInlet(targetPin);
  if (out != nullptr && in != nullptr) {
    // Ask the inlet first: it refuses a second buffer source (and duplicate
    // edges). Only record the edge on the outlet when the inlet accepted it —
    // a one-sided outlet->inlet edge survives Disconnect/UnwireFromPeers (both
    // clean up from the inlet's records) and gets compiled into every later
    // GraphState, so once the target object is deleted and reclaimed the audio
    // thread reads a freed inlet through the *live* snapshot (issue #237).
    if (target->ConnectInlet(out, targetPin)) {
      source->ConnectOutlet(in, sourcePin);
    } else {
      INTERNAL::LogImpl().emit(E_ERROR,
                               "Patcher: connection refused (duplicate edge or inlet already has "
                               "a buffer source)");
    }
  } else {
    INTERNAL::LogImpl().emit(E_ERROR, "Patcher: Invalid Connection");
  }
}

void patcherImplementation::Connect(YSE::pHandle* from, int outlet, YSE::pHandle* to, int inlet) {
  std::scoped_lock lk(mtx);
  ConnectUnlocked(from, outlet, to, inlet);
  RebuildAndPublish();
}

void patcherImplementation::Disconnect(YSE::pHandle* from, int outlet, YSE::pHandle* to,
                                       int inlet) {
  std::scoped_lock lk(mtx);
  // Mirror ConnectUnlocked's guard: GetOutlet/GetInlet return null for an
  // out-of-range pin. Passing a null outlet into inlet::Disconnect segfaults
  // (a disconnected inlet has dspConnection == nullptr, so `dspConnection ==
  // out` is true and it derefs the null outlet), and an out-of-range inlet
  // indexes inputs[] out of bounds (issue #235).
  // Same façade resolution Connect does (issue #545), and it has to be here
  // too: the edge that exists is the resolved one, so a Disconnect written
  // against the subpatcher's pin numbers has to be translated the same way to
  // find it.
  pObject* source = from->object;
  pObject* target = to->object;
  int sourcePin = outlet;
  int targetPin = inlet;
  if (!ResolveOutletPin(source, sourcePin) || !ResolveInletPin(target, targetPin)) {
    INTERNAL::LogImpl().emit(E_ERROR,
                             "Patcher: subpatcher has no boundary object for that pin number");
    return;
  }

  PATCHER::outlet* out = source->GetOutlet(sourcePin);
  PATCHER::inlet* in = target->GetInlet(targetPin);
  if (out != nullptr && in != nullptr) {
    target->DisconnectInlet(out, targetPin);
  } else {
    INTERNAL::LogImpl().emit(E_ERROR, "Patcher: Invalid Disconnection");
  }
  RebuildAndPublish();
}

YSE::pHandle* patcherImplementation::CreateObjectUnlocked(const std::string& type,
                                                          const std::string& args) {
  pObject* object = nullptr;
  INTERNAL::LogImpl().emit(E_DEBUG, "Patcher: Trying to create " + type);

  if (type == OBJ::D_DAC) {
    object = new pDac((int)output.size());
  } else if (type == OBJ::D_ADC) {
    // Like the DAC, the ADC is built with the patcher's real channel count
    // rather than the registry's default-channel Create() (issue #167). The
    // registry entry exists only so ~adc is a valid, documented type; the
    // rendered graph always uses this channel-matched instance.
    object = new pAdc((int)output.size());
  } else {
    object = Register().Get(type);
    if (object != nullptr) object->SetParams(args);
  }

  if (object == nullptr) {
    INTERNAL::LogImpl().emit(E_ERROR, "Patcher" + type + " is not a valid patcher object.");
    return nullptr;
  }

  // The storage ID (issue #730) has to be on the object *before* SetParent,
  // not merely before the object is published: `.textfile` (#687) and `.seq`
  // (#692) issue a file request from their SetParent override, and DumpJSON of
  // a patch saved mid-load must not find an unnumbered object. Caller holds
  // mtx, which is what makes reading the live object set here safe.
  //
  // Note that the schedulers no longer care when this happens — they stamp a
  // request with the object's instance tag, which the constructor has already
  // assigned (issue #733). Before that split, arming from SetParent with a
  // not-yet-assigned storage ID made every such request undeliverable, which is
  // why the assignment moved here in the first place.
  object->SetStorageID(ClaimStorageID());
  // Every object gets its patcher parent, the DAC and ADC included, so their
  // inlets resolve DSP-readiness from the pinned snapshot on the audio thread
  // rather than from the live wiring (issue #226). Hoisted out of the three
  // branches above, which each used to do this for themselves.
  object->SetParent(this);

  YSE::pHandle* handle = new YSE::pHandle(object);
  objects.insert(std::pair<YSE::pHandle*, pObject*>(handle, object));
  AssignGraphIds(object);
  INTERNAL::LogImpl().emit(E_DEBUG, "Patcher: " + type + " created");
  return handle;
}

YSE::pHandle* patcherImplementation::CreateObject(const std::string& type,
                                                  const std::string& args) {
  std::scoped_lock lk(mtx);
  YSE::pHandle* handle = CreateObjectUnlocked(type, args);
  if (handle != nullptr) RebuildAndPublish();
  return handle;
}

void patcherImplementation::DeleteObject(YSE::pHandle* handle) {
  // Teardown first, before the lock and before anything is unwired (issue
  // #758). This is Clear()'s pass one for a single object, and it is here for
  // the same reason: a `.midiflush` or `.makenote` deleted on its own still
  // owes the device the releases it is holding, and the cord to `.midiout` is
  // still there to send them down — one line later it would not be. Outside
  // mtx for TeardownObjects' reason as well, which see.
  //
  // This is also where the `.metro` special case used to live, as a type check
  // that poked a 0 into inlet 0. It is now gMetro::Teardown, which does the same
  // stop; a second object needing "you are about to go away" is what turned the
  // check into a virtual, and the check itself was the argument for it — it
  // compared `const char*` **pointers** against a `static constexpr char const*`
  // declared in a header, where every other type test in the engine uses
  // strcmp, so it never fired at all. A virtual cannot be wrong that way.
  // Deleting a subpatcher deletes what is inside it, transitively (issue #545).
  // A `patcher` object *is* the containment of its contents — the objects have
  // no other owner and no way of being addressed once it is gone — so removing
  // it and leaving them behind would turn encapsulated objects into
  // unreachable ones still being rendered. The subtree is collected under mtx
  // and everything below this operates on the whole of it; for the ordinary
  // case of a non-container object it is a one-element list and the code below
  // is exactly what it was.
  std::vector<YSE::pHandle*> doomedHandles;
  {
    std::scoped_lock lk(mtx);
    if (objects.find(handle) == objects.end()) return;
    CollectSubtree(handle, doomedHandles);
  }

  // Teardown pass, before the lock and before anything is unwired (issue #758),
  // over the whole subtree rather than the one handle — same pass, same
  // undefined order within it, and for the same reason: every cord in the patch
  // is still there for all of it, so an object releasing what it left sounding
  // reaches the device whichever order the pass happens to visit in.
  for (YSE::pHandle* h : doomedHandles) {
    h->object->Teardown(YSE::T_GUI);
  }

  std::scoped_lock lk(mtx);

  std::vector<pObject*> doomed;
  std::vector<YSE::pHandle*> erased;
  doomed.reserve(doomedHandles.size());
  erased.reserve(doomedHandles.size());
  for (YSE::pHandle* h : doomedHandles) {
    // Re-check membership: the teardown pass above ran outside mtx, so another
    // control thread could in principle have removed one of these already. Same
    // window the single-object path always had, now merely visible — and only
    // what this call actually removed from the object set is freed below, so a
    // handle another thread already deleted is not deleted twice here.
    auto it = objects.find(h);
    if (it == objects.end()) continue;
    pObject* object = it->second;
    objects.erase(it);
    erased.push_back(h);
    // Detach from peers so the next snapshot holds no reference to it, but do
    // not free it yet — an in-flight audio block may still walk the retired
    // snapshot that references it. The free is deferred to the reclaimer.
    object->UnwireFromPeers();
    doomed.push_back(object);
  }
  // Capture the id generation these objects' ids belong to *before* a possible
  // recompaction below bumps it, so the reclaimer can tell whether they are
  // still recyclable when it frees them (issue #364).
  std::uint64_t objGen;
  {
    std::scoped_lock rlk(reclaimMtx_);
    objGen = idGeneration_;
  }
  // If that was the patcher's last object, recompact the id space before the
  // next graph is built: an empty object set binds no live id (issue #355).
  CompactGraphIdsIfEmpty();
  RebuildAndPublish();
  // Tag the objects only after RebuildAndPublish has retired the graph that last
  // referenced them, so their epoch is >= that graph's — the graphs-first drain
  // then guarantees no retired graph outlives an object it points into.
  {
    std::scoped_lock rlk(reclaimMtx_);
    const std::uint64_t at = audioBlock_.load(std::memory_order_acquire);
    for (pObject* object : doomed)
      retiredObjects_.push_back({object, at, objGen});
  }
  ScheduleReclaim();
  // The handles are never referenced by a GraphState, so they can go
  // immediately.
  for (YSE::pHandle* h : erased) {
    delete h;
  }
}

void patcherImplementation::SetObjectParams(YSE::pHandle* handle, const std::string& args) {
  std::scoped_lock lk(mtx);
  pObject* object = handle->object;
  if (object == nullptr) return;

  if (!object->ParamsNeedRebuild()) {
    // Scalar-only params: pre-parse into a POD plan on this thread (parse
    // errors throw here, never on the audio thread) and hand it to the audio
    // thread for an allocation-free apply at the top of the next block. The
    // stored param string is updated eagerly, so GetParams/DumpJSON reflect
    // the new args immediately.
    ParamMsg msg{};
    msg.target = object;
    const int count = object->BuildParamPlan(args, msg.ops, (int)kParamOpsCap);
    if (count == 0) return; // nothing to apply (empty args or no scalar writes)
    if (count > 0) {
      msg.count = count;
      if (!paramQueue_.try_push(msg)) {
        // Backpressure: never block or allocate — drop and log, like the
        // value queue. The queue drains every block, so sustained loss means
        // the audio thread is stalled.
        INTERNAL::LogImpl().emit(E_ERROR, "Patcher: param queue full; dropped SetParams for " +
                                              std::string(object->Type()));
      }
      return;
    }
    // count < 0: the plan overflowed the inline cap. Fall through to the
    // structural rebuild, which is correct for any object.
  }
  ReplaceObjectUnlocked(handle, args);
}

void patcherImplementation::ApplyPendingParams(const GraphState* g) {
  // Audio thread. Drain the whole queue every block so it can't grow
  // unbounded. A plan is applied only when its target is still in the pinned
  // snapshot: a replaced/deleted object is simply absent and the plan is
  // dropped. The pointer is compared, never dereferenced, and a retired
  // object outlives any block that could still pin a snapshot holding it
  // (issue #227's two-block grace), so the comparison itself is safe.
  ParamMsg msg;
  while (paramQueue_.try_pop(msg)) {
    if (g == nullptr) continue;
    bool present = false;
    for (pObject* obj : g->objects) {
      if (obj == msg.target) {
        present = true;
        break;
      }
    }
    if (!present) continue;
    for (int i = 0; i < msg.count; i++) {
      const ParamOp& op = msg.ops[i];
      switch (op.type) {
      case PARM_TYPE::FLOAT:
        *((float*)op.dest) = op.f;
        break;
      case PARM_TYPE::ATOMIC_FLOAT:
        ((std::atomic<float>*)op.dest)->store(op.f, std::memory_order_relaxed);
        break;
      case PARM_TYPE::INT:
        *((int*)op.dest) = op.i;
        break;
      case PARM_TYPE::ATOMIC_INT:
        ((std::atomic<int>*)op.dest)->store(op.i, std::memory_order_relaxed);
        break;
      default:
        break; // STRING/LIST never ride the scalar queue
      }
    }
  }
}

void patcherImplementation::ReplaceObjectUnlocked(YSE::pHandle* handle, const std::string& args) {
  pObject* old = handle->object;
  pObject* fresh = Register().Get(old->Type());
  if (fresh == nullptr) {
    // Not registry-built (the DAC) — but the DAC registers no params, so a
    // re-parse can never legitimately land here. Leave the object untouched.
    INTERNAL::LogImpl().emit(E_ERROR, "Patcher: cannot rebuild " + std::string(old->Type()) +
                                          " for a live SetParams; params unchanged");
    return;
  }

  // Params first, then parent — the same order as CreateObjectUnlocked, so a
  // gReceive/gSend anchors its bus subscription/address under the *new*
  // dataName. The object is not yet published: pin callbacks may freely
  // grow/shrink its inlets/outlets here.
  fresh->SetParams(args);
  fresh->CopyStorageIdentity(*old);
  fresh->SetParent(this);
  AssignGraphIds(fresh);

  // Rewire the peers' edges onto the replacement for every pin index that
  // survives the re-parse; edges on removed pins are dropped, like the old
  // in-place pop_back did. Inlet-first (issue #237): record the edge on the
  // source outlet only when the destination inlet accepted it.
  const int ins = std::min(old->NumInputs(), fresh->NumInputs());
  for (int i = 0; i < ins; i++) {
    PATCHER::inlet* oldIn = old->GetInlet(i);
    PATCHER::inlet* freshIn = fresh->GetInlet(i);
    if (oldIn == nullptr || freshIn == nullptr) continue;
    for (PATCHER::outlet* src : oldIn->Sources()) {
      if (freshIn->Connect(src)) src->Connect(freshIn);
    }
    if (PATCHER::outlet* dsp = oldIn->DspSource()) {
      if (freshIn->Connect(dsp)) dsp->Connect(freshIn);
    }
  }
  const int outs = std::min(old->NumOutputs(), fresh->NumOutputs());
  for (int o = 0; o < outs; o++) {
    PATCHER::outlet* oldOut = old->GetOutlet(o);
    PATCHER::outlet* freshOut = fresh->GetOutlet(o);
    if (oldOut == nullptr || freshOut == nullptr) continue;
    for (PATCHER::inlet* dest : oldOut->Targets()) {
      if (dest->Connect(freshOut)) freshOut->Connect(dest);
    }
  }

  // Detach the old object so the next snapshot holds no reference to it, swap
  // the handle over, and publish. The old object is freed by the reclaimer
  // once the audio thread has provably advanced past every snapshot that
  // could still reference it — exactly like DeleteObject.
  old->UnwireFromPeers();
  objects[handle] = fresh;
  handle->object = fresh;
  // Anything that named the old object as its container now names the
  // replacement (issue #545). `CopyStorageIdentity` already moved the object's
  // own containment across; this is the other direction, and it is what stops a
  // re-parse of a container from orphaning everything inside it. A `patcher`
  // object registers no parameters, so it cannot reach this path today — the
  // loop is here so that it stays true if one ever does, rather than as a fix
  // for something reachable now.
  for (auto& any : objects) {
    if (any.second->Container() == old) any.second->SetContainer(fresh);
  }
  RebuildAndPublish();
  {
    // No recompaction happens here (the patcher is never empty during a
    // replace), so the current generation is the one the old object's ids belong
    // to; the reclaimer recycles them when it frees the old object (issue #364).
    std::scoped_lock rlk(reclaimMtx_);
    retiredObjects_.push_back({old, audioBlock_.load(std::memory_order_acquire), idGeneration_});
  }
  ScheduleReclaim();
}

void patcherImplementation::LoadbangObjects(const std::vector<pObject*>& loaded) {
  // The last pass of a load (issue #547), and `TeardownObjects`'s mirror image.
  // The same three orderings decide it, and here too each of them is a way of
  // getting it wrong:
  //
  // **After the publish, not during the build.** This is the whole of the
  // issue. ParseJSON creates every object, then wires every cord, then compiles
  // the result into a GraphState and installs it with one atomic swap (issue
  // #228). A bang fired from a constructor would leave down a cord that does
  // not exist yet; one fired at the end of the create loop would leave down
  // cords that exist but reach objects whose own creation arguments have been
  // applied and whose *downstream* wiring has not. Only after
  // RebuildAndPublish has returned is the patch the patch the file describes —
  // so that is where the caller puts this, and it is the reason a load has
  // three passes rather than two. Within the pass the order is not defined and
  // deliberately so: Max's reference says nothing about the order of two
  // `loadbang`s either, and a patch that needs one initialisation to precede
  // another says so with a `.trigger`, exactly as it would for any other
  // ordering requirement.
  //
  // **Outside mtx.** A Loadbang is an ordinary synchronous send: it runs the
  // whole subgraph behind the object's outlets, and that subgraph may hold a
  // `.forward`, `.qlist`, `.bag` or `.mtr` — every one of which calls
  // PassBang/PassData, which take mtx on the control thread. mtx is a plain
  // std::mutex, so dispatching under it would turn "load a patch that
  // initialises itself" into a hang. This is why ParseJSON's lock is scoped
  // rather than held to the end of the function.
  //
  // **T_GUI, not T_DSP.** The tag is dispatch semantics, and this really is the
  // control thread: T_DSP would make `inlet::Set*` run CalculateIfReady on an
  // active inlet, rendering a DSP object on the control thread outside any
  // block — the unsynchronised graph read issue #226 exists to prevent. T_GUI
  // says "set the state and let the block's own traversal render what you
  // caused", which is exactly what an initialisation message is for.
  //
  // **Subpatchers change nothing here, and that is the decision** (issue
  // #545). A subpatcher's contents are ordinary objects in the same flat
  // object set, so they are in `loaded` alongside the top level and fire in
  // this one pass, in no defined order relative to it. There is deliberately no
  // "inner patchers initialise first" rule. The reason is the reason above: the
  // whole tree — every level of nesting at once — is compiled and installed by
  // the single atomic swap this pass follows, so there is exactly one instant
  // at which "the patch has finished loading" becomes true, and it is equally
  // true for every subpatcher in it. An ordering by depth would be claiming a
  // distinction the publish does not make. A patch that needs one
  // initialisation to precede another says so with a `.trigger`, which crosses
  // a subpatcher boundary like any other cord.
  //
  // The object list is the caller's, taken while it still held mtx, and holds
  // only the objects *this* parse created. A ParseJSON into a patcher that
  // already had objects therefore does not re-fire the ones that were already
  // there — they were loaded once and this is not that load.
  //
  // Dispatching outside the lock costs what it costs TeardownObjects: a
  // structural edit from another control thread is not serialised against the
  // pass, so an object deleted between the unlock and the send below would be
  // reached after it was retired. That is by far the narrower hazard — it needs
  // two threads editing one patcher at the same instant, one of them mid-load,
  // where the deadlock needs only one patch cord.
  //
  // **`CreateObject` has no counterpart, on purpose.** An object built live
  // never receives this, and a `.loadbang` added to a running patch stays
  // silent until the patch is saved and loaded again. Two reasons, and they
  // agree. The first is that at CreateObject time the object has no cords: a
  // patch is built by creating an object and *then* connecting it, so a bang
  // fired at creation would have nowhere to go and the feature would be a
  // no-op dressed up as a behaviour. The second is that firing later — at the
  // first Connect, say — would mean a patch could not be edited without
  // re-running its initialisation, which is the opposite of what
  // initialisation is for: `.loadmess 0.5` re-sending its message every time
  // the patch is touched would keep overwriting the value the performer just
  // changed. Max's reference is silent on the live case; the objects it *does*
  // document for it are manual triggers (a double-click, a `loadbang` message
  // to `thispatcher`), which says the same thing — a load fires it and nothing
  // else does. Here the manual trigger is the inlet: a bang into a `.loadbang`
  // or a `.loadmess` makes it output, which is Max's documented behaviour and
  // is the whole of what a host needs to initialise a live-edited patch.
  for (pObject* obj : loaded) {
    obj->Loadbang(YSE::T_GUI);
  }
}

void patcherImplementation::TeardownObjects() {
  // Pass one of teardown (issue #758). Three orderings make it correct, and
  // each of them was a way of getting it wrong:
  //
  // **Before any unwiring.** The loop below this one unwires as it walks, so an
  // object reached *after* the `.midiout` downstream of it would send its
  // note-offs into a cord that no longer exists. Releasing what a patch left
  // sounding is only possible while the whole patch is still wired, which is
  // what makes teardown two passes rather than one. Within the pass the order
  // does not matter and is not defined: every cord is intact for all of it, so
  // a `.makenote` released before or after the `.midiflush` downstream of it
  // reaches the device either way — through the flush's pass-through in one
  // order, as an already-tracked release in the other.
  //
  // **Outside mtx.** A Teardown is an ordinary synchronous send: it runs the
  // whole subgraph behind the object's outlets, and that subgraph may hold a
  // `.forward`, `.qlist`, `.bag` or `.mtr` — every one of which calls
  // PassBang/PassData, which take mtx on the control thread. mtx is a plain
  // std::mutex, so dispatching under it would turn an ordinary patch into a
  // hang. Only the object snapshot is taken under the lock. What that costs is
  // that a concurrent structural edit from another control thread is not
  // serialised against the pass; that is by far the narrower hazard — it needs
  // two threads editing one patcher at the same instant, where the deadlock
  // needs only one patch cord.
  //
  // **T_GUI, not T_DSP.** T_DSP would sidestep mtx for free (CallingThread
  // short-circuits on it, so PassData never reaches EnqueueValue), and that is
  // not a good enough reason: the tag is dispatch semantics, and `inlet::Set*`
  // runs CalculateIfReady on an active inlet for every tag but T_GUI. A
  // note-off sent into a DSP object on T_DSP would render it on the control
  // thread outside any block — the unsynchronised graph read issue #226 exists
  // to prevent. This pass really is on the control thread and says so.
  //
  // **Subpatchers change nothing here either** (issue #545), and it is
  // `LoadbangObjects`'s decision read backwards. `objects` is flat, so a
  // subpatcher's contents are in this pass alongside the top level, in the same
  // undefined order, and every cord in the whole tree is intact for all of it.
  // Deleting one subpatcher rather than the whole patch runs the same pass over
  // that subpatcher's containment subtree — see DeleteObject.
  //
  // Opening hardware here is no longer the question the issue raised: since
  // #759 `.midiout` never opens its port inline. A port already open is written
  // to, exactly as it is for any other message; a port that was never opened
  // stays shut and the message is dropped, which is the honest answer — nothing
  // was ever sent through it, so it is holding nothing.
  std::vector<pObject*> stopping;
  {
    std::scoped_lock lk(mtx);
    stopping.reserve(objects.size());
    for (const auto& any : objects)
      stopping.push_back(any.second);
  }
  for (pObject* obj : stopping) {
    obj->Teardown(YSE::T_GUI);
  }
}

void patcherImplementation::Clear() {
  // Pass one, before the lock: every object releases what it is holding while
  // the patch is still whole (issue #758). ~patcherImplementation calls Clear(),
  // so this covers a destroyed patcher as well as a cleared one.
  TeardownObjects();

  std::scoped_lock lk(mtx);

  std::vector<pObject*> doomed;
  doomed.reserve(objects.size());
  for (auto it = objects.begin(); it != objects.end(); ++it) {
    it->second->UnwireFromPeers();
    doomed.push_back(it->second);
    delete it->first; // handle: not referenced by a GraphState
  }
  objects.clear();
  // Capture the id generation the doomed objects' ids belong to before the
  // recompaction below bumps it (issue #364): a Clear always empties the
  // patcher, so their ids are a stale numbering the reclaimer must not recycle.
  std::uint64_t objGen;
  {
    std::scoped_lock rlk(reclaimMtx_);
    objGen = idGeneration_;
  }
  // No live object remains, so the id space carries no stability constraint:
  // recompact it before the empty graph is built so later rebuilds restart from
  // a small id range instead of extending it every Clear (issue #355).
  CompactGraphIdsIfEmpty();
  RebuildAndPublish();
  // Tag the removed objects only after their covering graph has been retired by
  // RebuildAndPublish, so each object's epoch is >= that graph's (see the
  // graphs-first invariant in ReclaimElapsed).
  {
    std::scoped_lock rlk(reclaimMtx_);
    const std::uint64_t at = audioBlock_.load(std::memory_order_acquire);
    for (pObject* obj : doomed)
      retiredObjects_.push_back({obj, at, objGen});
  }
  ScheduleReclaim();
}

using json = nlohmann::json;
std::string patcherImplementation::DumpJSON() {
  json j;

  // Read a consistent object set under mtx. mtx is control-thread only (issue
  // #226), so serialising here never blocks the audio callback.
  {
    std::scoped_lock lk(mtx);
    // `objects` is keyed by pHandle*, so walking it hands the serialiser its
    // objects in heap-address order — which is a property of the allocator, not
    // of the patch, and would leave two identically-built patchers writing the
    // same objects under different "object N" keys even now that their IDs
    // agree. Order by storage ID instead: the ID is a property of the patch,
    // reproduced exactly by the same sequence of edits on any run, which the
    // address is not (issue #730). It is not necessarily *creation* order — a
    // deleted object's number goes to the next object created (issue #733) —
    // and it does not need to be. What a dump needs is one order that two
    // identically-built patchers agree on.
    std::vector<pObject*> ordered;
    ordered.reserve(objects.size());
    for (const auto& any : objects)
      ordered.push_back(any.second);
    // The check fires on "std::sort over a container of pointers" and is exactly
    // inverted here: the comparator never looks at the pointers, only at the
    // storage IDs behind them, and replacing the pointer-ordered map walk with a
    // deterministic order is the entire point of the call.
    // NOLINTNEXTLINE(bugprone-nondeterministic-pointer-iteration-order)
    std::sort(ordered.begin(), ordered.end(),
              [](pObject* a, pObject* b) { return a->GetID() < b->GetID(); });

    int counter = 0;
    for (pObject* object : ordered) {
      object->DumpJson(j["object " + std::to_string(counter)]);
      counter++;
    }
  }

  std::string result = j.dump(2, ' ', true);
  return result;
}

void patcherImplementation::ParseJSON(const std::string& content) {
  auto j = json::parse(content);

  std::map<int, pHandle*> OldIDs;

  // Create in stored-ID order, not in the order the records come out of the
  // json object (issue #730). A dump's records are keyed "object 0", "object
  // 1", ... and nlohmann hands them back in *string* order, so "object 10"
  // arrives before "object 2": past ten objects, a parse rebuilds the patch in
  // an order the patch never had, and the fresh storage IDs it hands out land
  // on different objects than the ones the file names. Sorting on the stored ID
  // makes load a fixed point of save — dump, parse, dump again and the bytes
  // match. A patch saved before this change carries the old wide process-wide
  // IDs; those are arbitrary, but they are still totally ordered, so such a
  // patch loads in its own defined order and is renumbered from 0 the next time
  // it is saved.
  std::vector<std::pair<int, json*>> records;
  records.reserve(j.size());
  for (auto obj = j.begin(); obj != j.end(); ++obj) {
    records.emplace_back(obj.value()["ID"].get<int>(), &obj.value());
  }
  std::stable_sort(records.begin(), records.end(),
                   [](const std::pair<int, json*>& a, const std::pair<int, json*>& b) {
                     return a.first < b.first;
                   });

  // Everything this parse creates, in creation order, for the loadbang pass
  // below. Declared out here because the lock is not: the pass runs after mtx
  // is released (issue #547).
  std::vector<pObject*> loaded;
  loaded.reserve(records.size());

  // Build the whole parsed graph under one lock and publish it with a single
  // atomic swap at the end (issue #228): the audio thread never sees a
  // partial graph — it keeps rendering the previously-published snapshot until
  // RebuildAndPublish below installs the finished one. The *Unlocked cores do
  // the create/connect work without re-taking mtx or publishing per edit, which
  // is what let the old fileHandlerActive re-entrancy flag be retired.
  //
  // Scoped rather than held to the end of the function so the loadbang pass
  // that follows dispatches outside it — see LoadbangObjects for why sending
  // under mtx would hang an ordinary patch.
  {
    std::scoped_lock lk(mtx);
    // restore objects first
    for (const auto& record : records) {
      json& obj = *record.second;
      std::string type = obj["type"].get<std::string>();
      std::string args = obj["parms"].get<std::string>();
      pHandle* handle = CreateObjectUnlocked(type, args);

      // handle can be null if called without gui context
      if (handle != nullptr) {
        loaded.push_back(handle->object);
        auto gui = obj["gui"];
        for (auto prop = gui.begin(); prop != gui.end(); ++prop) {
          handle->SetGuiProperty(prop.key(), prop.value().get<std::string>());
        }

        // State the object holds beyond its creation parameters — a `.coll`'s
        // contents (issue #494). Absent for every object that has none, which is
        // why it is looked up rather than indexed: operator[] on a const-less
        // json would insert a null here for all of them.
        const auto state = obj.find("state");
        if (state != obj.end()) {
          handle->object->RestoreState(*state);
        }
      }

      OldIDs.insert(std::pair<int, YSE::pHandle*>(record.first, handle));
    }

    // restore subpatcher membership (issue #545), after every object exists and
    // before the cords go back. Its own pass rather than a line in the create
    // loop above, because a container may be written after its contents — the
    // dump is ordered by storage ID, which says nothing about nesting — and a
    // pass over the finished object set does not care.
    //
    // Membership is restored directly rather than through
    // SetObjectContainer, which would deadlock on the mtx held here — but with
    // the same three refusals, because a file is not more trustworthy than a
    // caller. A dump this engine wrote can name nothing but a `patcher` object
    // and can describe no cycle; a hand-edited or corrupted one can do both,
    // and a containment cycle would make CollectSubtree's walk never terminate.
    // Anything refused is left at the top level, which is a patch that loads
    // and can be inspected rather than one that hangs.
    for (const auto& record : records) {
      const auto stored = record.second->find("container");
      if (stored == record.second->end()) continue;
      auto self = OldIDs.find(record.first);
      auto owner = OldIDs.find(stored->get<int>());
      if (self == OldIDs.end() || owner == OldIDs.end()) continue;
      if (self->second == nullptr || owner->second == nullptr) continue;
      if (!IsSubpatcher(owner->second->object)) {
        INTERNAL::LogImpl().emit(E_ERROR, "Patcher: stored container is not a 'patcher' object");
        continue;
      }
      if (ContainmentWouldCycle(self->second->object, owner->second->object)) {
        INTERNAL::LogImpl().emit(E_ERROR, "Patcher: stored containment is cyclic; object loaded at "
                                          "the top level");
        continue;
      }
      self->second->object->SetContainer(owner->second->object);
    }

    // restore connections
    for (const auto& record : records) {
      int source = record.first;
      auto outs = (*record.second)["outputs"];
      for (auto out = outs.begin(); out != outs.end(); ++out) {
        // Take the outlet index from the key the file wrote, not from a count of
        // how many keys have gone by — same treatment the records one level up
        // got in #730, and for the same reason: the map replays "output 10"
        // before "output 2" (issue #734). A key that names no outlet is skipped
        // rather than guessed at.
        const int outlet = OutletIndexFromKey(out.key());
        if (outlet < 0) {
          continue;
        }

        if (out.value().count("Count") == 0) {
          continue;
        }
        int count = out.value()["Count"].get<int>();

        for (int i = 0; i < count; i++) {
          auto connection = out.value()[std::to_string(i)];
          int target = connection["Object"].get<int>();
          int inlet = connection["Inlet"].get<int>();

          pHandle* sourceHandle = nullptr;
          pHandle* targetHandle = nullptr;

          auto a = OldIDs.find(source);
          if (a != OldIDs.end()) {
            sourceHandle = a->second;
          }

          auto b = OldIDs.find(target);
          if (b != OldIDs.end()) {
            targetHandle = b->second;
          }

          if (targetHandle != nullptr && sourceHandle != nullptr) {
            ConnectUnlocked(sourceHandle, outlet, targetHandle, inlet);
          }
        }
      }
    }
    // Every create/connect above mutated only the freshly-built objects (never
    // referenced by the still-active snapshot); publish the whole parsed graph in
    // a single atomic swap now.
    RebuildAndPublish();
  }

  // The graph is built, wired and published: the patch is now the patch the
  // file describes, which is the only moment at which "loading finished" is
  // true (issue #547). Everything this parse created hears about it, once, on
  // this thread and outside the lock.
  LoadbangObjects(loaded);
}

unsigned int patcherImplementation::Objects() {
  return static_cast<unsigned int>(objects.size());
}

std::size_t patcherImplementation::PendingRetired() {
  std::scoped_lock lk(reclaimMtx_);
  return retiredGraphs_.size() + retiredObjects_.size();
}

std::size_t patcherImplementation::OutletIdSpace() {
  std::scoped_lock lk(mtx);
  return static_cast<std::size_t>(nextOutletId_);
}

std::size_t patcherImplementation::InletIdSpace() {
  std::scoped_lock lk(mtx);
  return static_cast<std::size_t>(nextInletId_);
}

std::size_t patcherImplementation::FreeIdCount() {
  std::scoped_lock lk(reclaimMtx_);
  return freeInletIds_.size() + freeOutletIds_.size();
}

YSE::pHandle* patcherImplementation::GetHandleFromList(unsigned int obj) {
  // TODO: not really brilliant, this code
  unsigned int pos = 0;
  for (auto& x : objects) {
    if (pos == obj) return x.first;
    pos++;
  }
  return 0;
}

YSE::pHandle* patcherImplementation::GetHandleFromID(unsigned int objID) {
  for (auto& x : objects) {
    if (x.second->GetID() == objID) return x.first;
  }
  return nullptr;
}

bool patcherImplementation::PassBang(const std::string& to, YSE::THREAD thread) {
  if (CallingThread(thread) == YSE::T_DSP) {
    // Already on the audio thread — a gSend fanning out during traversal
    // (T_DSP), or one reached from a T_GUI-tagged drain at the top of this
    // patcher's own Calculate (issue #690). Deliver synchronously against the
    // pinned snapshot, same block, no lock, no allocation. The *tag* is passed
    // on untouched, so a deferred delivery keeps its T_GUI semantics downstream
    // (set state; the block's own traversal renders it) while using the
    // audio-thread mechanism. Nothing below this branch — mtx, the log string,
    // the OSC socket — may run on the audio callback.
    return DispatchToReceiver(currentBlockGraph_.load(std::memory_order_acquire), ValueKind::Bang,
                              to.c_str(), 0, 0.f, kEmptyList, thread);
  }
  ValueMsg msg{};
  msg.kind = ValueKind::Bang;
  if (EnqueueValue(msg, to)) return true;
  if (oscHandle != nullptr) {
    oscHandle->Send(to);
    return true;
  }
  INTERNAL::LogImpl().emit(E_FILE_ERROR, "Cannot find target " + to + ". Valid targets are" +
                                             GetRecieveObjectsAsString());
  return false;
}

bool patcherImplementation::PassData(int value, const std::string& to, YSE::THREAD thread) {
  // See PassBang for why the branch asks CallingThread rather than the tag.
  if (CallingThread(thread) == YSE::T_DSP) {
    return DispatchToReceiver(currentBlockGraph_.load(std::memory_order_acquire), ValueKind::Int,
                              to.c_str(), value, 0.f, kEmptyList, thread);
  }
  ValueMsg msg{};
  msg.kind = ValueKind::Int;
  msg.intVal = value;
  if (EnqueueValue(msg, to)) return true;
  if (oscHandle != nullptr) {
    oscHandle->Send(to, value);
    return true;
  }
  INTERNAL::LogImpl().emit(E_FILE_ERROR, "Cannot find target " + to + ". Valid targets are" +
                                             GetRecieveObjectsAsString());
  return false;
}

bool patcherImplementation::PassData(float value, const std::string& to, YSE::THREAD thread) {
  // See PassBang for why the branch asks CallingThread rather than the tag.
  if (CallingThread(thread) == YSE::T_DSP) {
    return DispatchToReceiver(currentBlockGraph_.load(std::memory_order_acquire), ValueKind::Float,
                              to.c_str(), 0, value, kEmptyList, thread);
  }
  ValueMsg msg{};
  msg.kind = ValueKind::Float;
  msg.floatVal = value;
  if (EnqueueValue(msg, to)) return true;
  if (oscHandle != nullptr) {
    oscHandle->Send(to, value);
    return true;
  }
  INTERNAL::LogImpl().emit(E_FILE_ERROR, "Cannot find target " + to + ". Valid targets are" +
                                             GetRecieveObjectsAsString());
  return false;
}

bool patcherImplementation::PassData(const std::string& value, const std::string& to,
                                     YSE::THREAD thread) {
  // See PassBang for why the branch asks CallingThread rather than the tag.
  if (CallingThread(thread) == YSE::T_DSP) {
    // Synchronous audio-thread delivery takes the value by reference — no inline
    // buffer, so no length limit on this path.
    return DispatchToReceiver(currentBlockGraph_.load(std::memory_order_acquire), ValueKind::List,
                              to.c_str(), 0, 0.f, value, thread);
  }
  // The deferred payload rides the queue inline; over-long values can't take the
  // RT-safe path. Log and drop rather than truncate or allocate — but still let
  // OSC (control-thread, no length limit) handle it if wired.
  if (value.size() >= kValueListCap) {
    INTERNAL::LogImpl().emit(
        E_FILE_ERROR, "Patcher: list value too long for value queue, dropped (to " + to + ")");
    if (oscHandle != nullptr) {
      oscHandle->Send(to, value);
      return true;
    }
    return false;
  }

  ValueMsg msg{};
  msg.kind = ValueKind::List;
  std::memcpy(msg.listVal, value.c_str(), value.size() + 1);
  if (EnqueueValue(msg, to)) return true;
  if (oscHandle != nullptr) {
    oscHandle->Send(to, value);
    return true;
  }
  INTERNAL::LogImpl().emit(E_FILE_ERROR, "Cannot find target " + to + ". Valid targets are" +
                                             GetRecieveObjectsAsString());
  return false;
}

bool patcherImplementation::DispatchToReceiver(const GraphState* g, ValueKind kind, const char* to,
                                               int intVal, float floatVal,
                                               const std::string& listVal, YSE::THREAD thread) {
  if (g == nullptr) return false;
  for (pObject* obj : g->objects) {
    if (strcmp(obj->Type(), OBJ::G_RECEIVE) != 0) continue;
    if (obj->DataName() != to) continue;
    PATCHER::inlet* in = obj->GetInlet(0);
    if (in == nullptr) return true; // matched, but nothing to deliver into
    switch (kind) {
    case ValueKind::Bang:
      in->SetBang(thread);
      break;
    case ValueKind::Int:
      in->SetInt(intVal, thread);
      break;
    case ValueKind::Float:
      in->SetFloat(floatVal, thread);
      break;
    case ValueKind::List:
      in->SetList(listVal, thread);
      break;
    }
    return true;
  }
  return false;
}

bool patcherImplementation::EnqueueValue(ValueMsg& msg, const std::string& to) {
  // The target receiver is carried by name and re-resolved on the audio thread.
  // Over-long names can't ride the inline buffer; treat as "no in-patcher
  // target" so the caller can still try OSC, and log rather than truncate.
  if (to.size() >= kValueNameCap) {
    INTERNAL::LogImpl().emit(E_FILE_ERROR,
                             "Patcher: receiver name too long for value queue: " + to);
    return false;
  }
  std::memcpy(msg.target, to.c_str(), to.size() + 1);

  // Does a matching gReceive exist in this patcher? Scan under mtx so `objects`
  // is never read concurrently with a structural edit. mtx is control-thread
  // only (issue #226) and never blocks the audio thread — which held again only
  // once the callers stopped reaching here from a T_GUI-tagged drain running on
  // the audio callback (issue #690).
  bool found = false;
  mtx.lock();
  for (auto& x : objects) {
    if (strcmp(x.second->Type(), OBJ::G_RECEIVE) == 0 && to == x.second->DataName()) {
      found = true;
      break;
    }
  }
  mtx.unlock();
  if (!found) return false;

  if (!valueQueue_.try_push(msg)) {
    // Backpressure: the audio thread hasn't drained yet. Never block or allocate
    // on the control thread — drop and log. The target existed, so return true
    // regardless (don't spuriously fall back to OSC for a real in-patcher one).
    INTERNAL::LogImpl().emit(E_ERROR, "Patcher: value queue full; dropped message to " + to);
  }
  return true;
}

void patcherImplementation::DeliverPendingValues(const GraphState* g) {
  // Audio thread. Drain the whole queue every block so it can't grow unbounded,
  // even when the patcher is empty (no snapshot to deliver into -> dropped).
  // Re-resolving each target by name against the pinned snapshot is what makes
  // this safe across a concurrent delete: a removed receiver is simply absent.
  // Dispatch with T_GUI semantics (these messages originated on the control
  // thread): set the parameter / forward the value; this block's own traversal
  // renders it.
  ValueMsg msg;
  while (valueQueue_.try_pop(msg)) {
    if (g == nullptr) continue;
    if (msg.kind == ValueKind::List) {
      // assign() into the reserved scratch reuses its buffer (no allocation on
      // the audio thread) while giving SetList the std::string& it expects.
      listScratch_.assign(msg.listVal);
      DispatchToReceiver(g, msg.kind, msg.target, 0, 0.f, listScratch_, YSE::T_GUI);
    } else {
      DispatchToReceiver(g, msg.kind, msg.target, msg.intVal, msg.floatVal, kEmptyList, YSE::T_GUI);
    }
  }
}

std::string patcherImplementation::GetRecieveObjectsAsString() {
  // Reached from the PassBang/PassData not-found log path on the control thread
  // (the target receiver was concurrently removed). Scan `objects` under mtx so
  // it is never read while another control thread mutates the graph — mtx is
  // control-thread only and never blocks the audio callback (issues #226,
  // #690: a caller physically on the audio thread never gets this far). The
  // callers release mtx in EnqueueValue before falling through here, so there is
  // no re-entrancy.
  std::scoped_lock lk(mtx);
  std::string result;
  for (auto& x : objects) {

    if (strcmp(x.second->Type(), OBJ::G_RECEIVE) == 0) {
      result += " " + x.second->DataName();
    }
  }
  return result;
}

void patcherImplementation::SetHandler(YSE::oscHandler* handler) {
  oscHandle = handler;
}

void patcherImplementation::AssignGraphIds(pObject* object) {
  // Caller holds mtx. Recycled ids are produced by the background reclaimer
  // under reclaimMtx_ (issue #364); pull from the free-list before extending the
  // high-water mark so a continuously edited patcher reuses the id range of its
  // deleted objects instead of growing it with the lifetime create count. A
  // recycled id is always < the current counter, so it stays in bounds of every
  // GraphState's id-indexed tables. mtx -> reclaimMtx_ is the established order;
  // this never runs on the audio thread.
  std::scoped_lock rlk(reclaimMtx_);
  for (int i = 0; i < object->NumInputs(); i++) {
    PATCHER::inlet* in = object->GetInlet(i);
    if (in == nullptr || in->GraphId() >= 0) continue;
    if (!freeInletIds_.empty()) {
      in->SetGraphId(freeInletIds_.back());
      freeInletIds_.pop_back();
    } else {
      in->SetGraphId(nextInletId_++);
    }
  }
  for (int i = 0; i < object->NumOutputs(); i++) {
    PATCHER::outlet* out = object->GetOutlet(i);
    if (out == nullptr || out->GraphId() >= 0) continue;
    if (!freeOutletIds_.empty()) {
      out->SetGraphId(freeOutletIds_.back());
      freeOutletIds_.pop_back();
    } else {
      out->SetGraphId(nextOutletId_++);
    }
  }
}

void patcherImplementation::RecycleObjectIds(pObject* object, std::uint64_t idGen) {
  // Caller holds reclaimMtx_ (the background reclaimer, freeing this object now
  // that no live or retired snapshot can still index its ids). Only return the
  // ids to the free-list if they still belong to the current numbering: a
  // CompactGraphIdsIfEmpty since retirement reset the counters, so these ids
  // would collide with the fresh dense range if reused (issue #364).
  if (idGen != idGeneration_) return;
  for (int i = 0; i < object->NumInputs(); i++) {
    PATCHER::inlet* in = object->GetInlet(i);
    if (in != nullptr && in->GraphId() >= 0) freeInletIds_.push_back(in->GraphId());
  }
  for (int i = 0; i < object->NumOutputs(); i++) {
    PATCHER::outlet* out = object->GetOutlet(i);
    if (out != nullptr && out->GraphId() >= 0) freeOutletIds_.push_back(out->GraphId());
  }
}

int patcherImplementation::ClaimStorageID() const {
  // Caller holds mtx. The smallest non-negative number no live object holds.
  //
  // `objects.size() + 1` slots is always enough: at most `objects.size()` of
  // them can be marked, so the scan below always finds a gap. A live ID at or
  // past that bound is outside the candidate range and is simply not marked —
  // which happens whenever a patcher shrinks (build five objects, delete four,
  // and the survivor may hold ID 4 with one object live). Ignoring it is
  // correct, not a shortcut: the number this returns is below the bound and so
  // below that ID, and it was already checked against every live ID that could
  // collide with it.
  std::vector<bool> used(objects.size() + 1, false);
  for (const auto& any : objects) {
    // Unsigned throughout: kNoStorageID reads back as UINT_MAX, which fails the
    // bound like any other out-of-range ID rather than needing its own case.
    const unsigned int id = any.second->GetID();
    if (id < used.size()) used[id] = true;
  }
  for (std::size_t i = 0; i < used.size(); i++) {
    if (!used[i]) return static_cast<int>(i);
  }
  return static_cast<int>(used.size()); // unreachable, see above
}

void patcherImplementation::CompactGraphIdsIfEmpty() {
  // Only safe with no live object: a graph id must stay fixed for a live
  // object's lifetime (the audio thread indexes the pinned snapshot by it with
  // no synchronisation), so the counters may restart only when there is nothing
  // live to re-stamp. Retired snapshots and objects keep their old ids and their
  // own already-sized tables — they are never re-indexed — so this is safe even
  // while their reclamation is still pending. Restarting at 0 bounds every later
  // GraphState's id-indexed tables by the peak simultaneous object count instead
  // of the lifetime creation count (issue #355).
  if (objects.empty()) {
    nextInletId_ = 0;
    nextOutletId_ = 0;
    // The whole id space is reclaimed at once, so any recycled ids parked on the
    // free-list belong to the pre-reset numbering and must be dropped; bumping
    // the generation likewise marks the ids of still-pending retired objects as
    // stale, so their deferred free won't push them into the reset space (#364).
    std::scoped_lock rlk(reclaimMtx_);
    freeInletIds_.clear();
    freeOutletIds_.clear();
    idGeneration_++;
  }
}

YSE::PATCHER::GraphState* patcherImplementation::BuildGraph() {
  GraphState* g = new GraphState();
  g->outletTargets.resize(nextOutletId_);
  g->inletHasDsp.assign(nextInletId_, 0);

  for (const auto& any : objects) {
    pObject* object = any.second;
    g->objects.push_back(object);
    if (object->IsDSPStartPoint()) g->startPoints.push_back(object);
    // Objects driven from outside the patch rather than by an inlet or a DSP
    // edge (issue #529). Collected here so Calculate() never scans for them.
    if (object->WantsBlockPoll()) g->pollers.push_back(object);
    if (strcmp(object->Type(), YSE::OBJ::D_DAC) == 0) g->dacs.push_back(object);
    if (strcmp(object->Type(), YSE::OBJ::D_ADC) == 0) g->adcs.push_back(object);

    for (int i = 0; i < object->NumOutputs(); i++) {
      PATCHER::outlet* out = object->GetOutlet(i);
      if (out == nullptr) continue;
      int id = out->GraphId();
      if (id >= 0 && id < nextOutletId_) g->outletTargets[id] = out->Targets();
    }
    for (int i = 0; i < object->NumInputs(); i++) {
      PATCHER::inlet* in = object->GetInlet(i);
      if (in == nullptr) continue;
      int id = in->GraphId();
      if (id >= 0 && id < nextInletId_) {
        g->inletHasDsp[id] = in->HasActiveDSPConnection() ? 1 : 0;
      }
    }
  }
  return g;
}

void patcherImplementation::RebuildAndPublish() {
  GraphState* next = BuildGraph();
  // Only the control thread writes active_ (under mtx), so a relaxed load of
  // the prior pointer is fine; the audio thread reads it with acquire.
  const GraphState* old = active_.load(std::memory_order_relaxed);
  active_.store(next, std::memory_order_release);
  if (old != nullptr) {
    std::scoped_lock lk(reclaimMtx_);
    retiredGraphs_.emplace_back(old, audioBlock_.load(std::memory_order_acquire));
  }
  ScheduleReclaim();
}

void patcherImplementation::ScheduleReclaim() {
  if (shuttingDown_.load(std::memory_order_acquire)) return;
  // One pending pass is enough: it drains everything currently safe. isQueued()
  // covers both a job waiting in the ring and one mid-run, so either half of the
  // ping-pong being live suppresses a duplicate enqueue.
  if (reclaimJobs_[0].isQueued() || reclaimJobs_[1].isQueued()) return;
  // A background job: never runs inline on the caller and never blocks the audio
  // thread. If the pool is inactive the job is simply not queued; the retire
  // lists then wait for the next edit or for the destructor's FreeAllRetired.
  INTERNAL::Global().addSlowJob(&reclaimJobs_[0]);
}

bool patcherImplementation::ReclaimElapsed(std::uint64_t now) {
  // A snapshot retired at block C is safe to free once the audio thread has
  // started at least two later blocks: the last block that could still hold it
  // finished its render before block C+2's counter bump, which this pass's
  // acquire load of audioBlock_ synchronizes-with. Free graphs before the
  // objects they point into.
  for (std::size_t i = 0; i < retiredGraphs_.size();) {
    if (now >= retiredGraphs_[i].second + 2) {
      delete retiredGraphs_[i].first;
      retiredGraphs_.erase(retiredGraphs_.begin() + i);
    } else {
      ++i;
    }
  }
  for (std::size_t i = 0; i < retiredObjects_.size();) {
    if (now >= retiredObjects_[i].epoch + 2) {
      // No live or retired snapshot can still index this object's ids now, so
      // hand them back to the free-list for reuse before freeing it (issue #364).
      RecycleObjectIds(retiredObjects_[i].object, retiredObjects_[i].idGen);
      delete retiredObjects_[i].object;
      retiredObjects_.erase(retiredObjects_.begin() + i);
    } else {
      ++i;
    }
  }
  return !retiredGraphs_.empty() || !retiredObjects_.empty();
}

void patcherImplementation::RunReclaimPass(reclaimJob* next) {
  bool remaining;
  bool advancing;
  {
    std::scoped_lock lk(reclaimMtx_);
    const std::uint64_t now = audioBlock_.load(std::memory_order_acquire);
    remaining = ReclaimElapsed(now);
    // The epoch moved since the previous pass -> the audio thread is live and
    // will keep crossing retirement thresholds, so it is worth another pass. A
    // frozen epoch means the engine is paused/stopped (leftovers aren't being
    // raced) or nothing new became safe; either way, stop.
    advancing = (now != lastReclaimEpoch_);
    lastReclaimEpoch_ = now;
  }
  if (remaining && advancing && !shuttingDown_.load(std::memory_order_acquire)) {
    INTERNAL::Global().addSlowJob(next);
  }
}

void patcherImplementation::FreeAllRetired() {
  // Graphs first — they hold inlet* pointers into the objects.
  for (std::size_t i = 0; i < retiredGraphs_.size(); i++) {
    delete retiredGraphs_[i].first;
  }
  retiredGraphs_.clear();
  const GraphState* g = active_.exchange(nullptr, std::memory_order_acq_rel);
  delete g;
  for (std::size_t i = 0; i < retiredObjects_.size(); i++) {
    delete retiredObjects_[i].object;
  }
  retiredObjects_.clear();
}