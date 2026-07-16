
#include "patcherImplementation.h"
#include "pRegistry.h"
#include "pObjectList.hpp"
#include "../headers/enums.hpp"
#include "genericObjects/pDac.h"
#include "genericObjects/pAdc.h"
#include "genericObjects/gReceive.h"
#include "genericObjects/gSend.h"
#include "pHandle.hpp"
#include "../utils/json.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "../implementations/logImplementation.h"
#include "../internal/global.h"

using namespace YSE::PATCHER;

namespace {
  // Process-wide counter feeding the auto-generated "patcher_<N>" default
  // name (issue #122). Distinct from pObject::CreateID() so the patcher
  // counter is not perturbed by inner-object construction.
  std::atomic<unsigned int> g_nextPatcherIndex{0};

  // Shared empty list argument for DispatchToReceiver on the non-list value
  // kinds (Bang/Int/Float), so those paths pass a std::string& without building
  // a temporary. Never read for those kinds.
  const std::string kEmptyList;
} // namespace

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
    }
  }
  mtx.unlock();
}

patcherImplementation::~patcherImplementation() {
  // Stop scheduling and re-arming reclaim passes first, so the joins below
  // terminate instead of chasing a self-perpetuating ping-pong.
  shuttingDown_.store(true, std::memory_order_release);
  // memory cleanup
  Clear();
  // A reclaim pass already handed to the background pool may still be draining
  // the retire lists; wait for both halves of the ping-pong to finish before we
  // free those lists from under them. shuttingDown_ guarantees neither re-arms.
  reclaimJobs_[0].join();
  reclaimJobs_[1].join();
  // The audio thread is stopped at destruction, so reclaim unconditionally:
  // the remaining retire lists (and the final published snapshot) are freed
  // here rather than waiting on the block counter.
  FreeAllRetired();
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

  // Apply queued scalar param plans first (issue #234), then deliver queued
  // value messages (PassBang/PassData), both before rendering and against the
  // pinned snapshot. Param stores land before any handler or render read of
  // this block, so a SetParams that precedes a PassData on the control thread
  // is also observed in that order here.
  ApplyPendingParams(g);
  DeliverPendingValues(g);

  if (g != nullptr) {
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

void patcherImplementation::ConnectUnlocked(YSE::pHandle* from, int outlet, YSE::pHandle* to,
                                            int inlet) {
  PATCHER::outlet* out = from->object->GetOutlet(outlet);
  PATCHER::inlet* in = to->object->GetInlet(inlet);
  if (out != nullptr && in != nullptr) {
    // Ask the inlet first: it refuses a second buffer source (and duplicate
    // edges). Only record the edge on the outlet when the inlet accepted it —
    // a one-sided outlet->inlet edge survives Disconnect/UnwireFromPeers (both
    // clean up from the inlet's records) and gets compiled into every later
    // GraphState, so once the target object is deleted and reclaimed the audio
    // thread reads a freed inlet through the *live* snapshot (issue #237).
    if (to->object->ConnectInlet(out, inlet)) {
      from->object->ConnectOutlet(in, outlet);
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
  PATCHER::outlet* out = from->object->GetOutlet(outlet);
  PATCHER::inlet* in = to->object->GetInlet(inlet);
  if (out != nullptr && in != nullptr) {
    to->object->DisconnectInlet(out, inlet);
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
    // Give the DAC its patcher parent too, so its inlets resolve DSP-readiness
    // from the pinned snapshot on the audio thread rather than the live wiring
    // (issue #226).
    object->SetParent(this);
  } else if (type == OBJ::D_ADC) {
    // Like the DAC, the ADC is built with the patcher's real channel count
    // rather than the registry's default-channel Create() (issue #167). The
    // registry entry exists only so ~adc is a valid, documented type; the
    // rendered graph always uses this channel-matched instance.
    object = new pAdc((int)output.size());
    object->SetParent(this);
  } else {
    object = Register().Get(type);
    if (object != nullptr) {
      object->SetParams(args);
      object->SetParent(this);
    }
  }

  if (object == nullptr) {
    INTERNAL::LogImpl().emit(E_ERROR, "Patcher" + type + " is not a valid patcher object.");
    return nullptr;
  }

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
  std::scoped_lock lk(mtx);

  if (handle->Type() == OBJ::G_METRO) {
    handle->object->GetInlet(0)->SetInt(0, YSE::THREAD::T_GUI);
  }

  pObject* object = handle->object;
  objects.erase(handle);
  // Detach from peers so the next snapshot holds no reference to it, but do
  // not free it yet — an in-flight audio block may still walk the retired
  // snapshot that references it. The free is deferred to the reclaimer.
  object->UnwireFromPeers();
  // Capture the id generation this object's ids belong to *before* a possible
  // recompaction below bumps it, so the reclaimer can tell whether they are
  // still recyclable when it frees the object (issue #364).
  std::uint64_t objGen;
  {
    std::scoped_lock rlk(reclaimMtx_);
    objGen = idGeneration_;
  }
  // If that was the patcher's last object, recompact the id space before the
  // next graph is built: an empty object set binds no live id (issue #355).
  CompactGraphIdsIfEmpty();
  RebuildAndPublish();
  // Tag the object only after RebuildAndPublish has retired the graph that last
  // referenced it, so its epoch is >= that graph's — the graphs-first drain then
  // guarantees no retired graph outlives an object it points into.
  {
    std::scoped_lock rlk(reclaimMtx_);
    retiredObjects_.push_back({object, audioBlock_.load(std::memory_order_acquire), objGen});
  }
  ScheduleReclaim();
  // The handle is never referenced by a GraphState, so it can go immediately.
  delete handle;
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

void patcherImplementation::Clear() {
  std::scoped_lock lk(mtx);

  std::vector<pObject*> doomed;
  doomed.reserve(objects.size());
  for (auto it = objects.begin(); it != objects.end(); ++it) {
    if (it->first->Type() == OBJ::G_METRO) {
      it->second->GetInlet(0)->SetInt(0, YSE::THREAD::T_GUI);
    }
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
  int counter = 0;

  // Read a consistent object set under mtx. mtx is control-thread only (issue
  // #226), so serialising here never blocks the audio callback.
  {
    std::scoped_lock lk(mtx);
    for (const auto& any : objects) {
      std::string name = "object " + std::to_string(counter);
      any.second->DumpJson(j[name]);
      counter++;
    }
  }

  std::string result = j.dump(2, ' ', true);
  return result;
}

void patcherImplementation::ParseJSON(const std::string& content) {
  auto j = json::parse(content);

  std::map<int, pHandle*> OldIDs;

  // Build the whole parsed graph under one lock and publish it with a single
  // atomic swap at the end (issue #228): the audio thread never sees a
  // partial graph — it keeps rendering the previously-published snapshot until
  // RebuildAndPublish below installs the finished one. The *Unlocked cores do
  // the create/connect work without re-taking mtx or publishing per edit, which
  // is what let the old fileHandlerActive re-entrancy flag be retired.
  std::scoped_lock lk(mtx);
  // restore objects first
  for (auto obj = j.begin(); obj != j.end(); ++obj) {
    std::string type = obj.value()["type"].get<std::string>();
    std::string args = obj.value()["parms"].get<std::string>();
    pHandle* handle = CreateObjectUnlocked(type, args);

    // handle can be null if called without gui context
    if (handle != nullptr) {
      auto gui = obj.value()["gui"];
      for (auto prop = gui.begin(); prop != gui.end(); ++prop) {
        handle->SetGuiProperty(prop.key(), prop.value().get<std::string>());
      }
    }

    int ID = obj.value()["ID"].get<int>();
    OldIDs.insert(std::pair<int, YSE::pHandle*>(ID, handle));
  }

  // restore connections
  for (auto obj = j.begin(); obj != j.end(); ++obj) {
    int source = obj.value()["ID"].get<int>();
    int outlet = 0;
    auto outs = obj.value()["outputs"];
    for (auto out = outs.begin(); out != outs.end(); ++out) {

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
      outlet++;
    }
  }
  // Every create/connect above mutated only the freshly-built objects (never
  // referenced by the still-active snapshot); publish the whole parsed graph in
  // a single atomic swap now.
  RebuildAndPublish();
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
  if (thread == YSE::T_DSP) {
    // Already on the audio thread (a gSend fanning out during traversal):
    // deliver synchronously against the pinned snapshot, same block, no lock.
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
  if (thread == YSE::T_DSP) {
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
  if (thread == YSE::T_DSP) {
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
  if (thread == YSE::T_DSP) {
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
  // only now (issue #226) and never blocks the audio thread.
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
  // control-thread only and never blocks the audio callback (issue #226). The
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