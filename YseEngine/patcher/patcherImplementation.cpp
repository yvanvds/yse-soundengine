
#include "patcherImplementation.h"
#include "pRegistry.h"
#include "pObjectList.hpp"
#include "../headers/enums.hpp"
#include "genericObjects/pDac.h"
#include "genericObjects/gReceive.h"
#include "genericObjects/gSend.h"
#include "pHandle.hpp"
#include "../utils/json.hpp"
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

  // Deliver queued value messages (PassBang/PassData) before rendering, now
  // that the snapshot is pinned so their fan-out resolves through it. This is
  // where inlet handlers run — only ever on the audio thread (issue #225).
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

void patcherImplementation::ConnectUnlocked(YSE::pHandle* from, int outlet, YSE::pHandle* to,
                                            int inlet) {
  PATCHER::outlet* out = from->object->GetOutlet(outlet);
  PATCHER::inlet* in = to->object->GetInlet(inlet);
  if (out != nullptr && in != nullptr) {
    from->object->ConnectOutlet(in, outlet);
    to->object->ConnectInlet(out, inlet);
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
  to->object->DisconnectInlet(from->object->GetOutlet(outlet), inlet);
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
  RebuildAndPublish();
  // Tag the object only after RebuildAndPublish has retired the graph that last
  // referenced it, so its epoch is >= that graph's — the graphs-first drain then
  // guarantees no retired graph outlives an object it points into.
  {
    std::scoped_lock rlk(reclaimMtx_);
    retiredObjects_.emplace_back(object, audioBlock_.load(std::memory_order_acquire));
  }
  ScheduleReclaim();
  // The handle is never referenced by a GraphState, so it can go immediately.
  delete handle;
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
  RebuildAndPublish();
  // Tag the removed objects only after their covering graph has been retired by
  // RebuildAndPublish, so each object's epoch is >= that graph's (see the
  // graphs-first invariant in ReclaimElapsed).
  {
    std::scoped_lock rlk(reclaimMtx_);
    const std::uint64_t at = audioBlock_.load(std::memory_order_acquire);
    for (pObject* obj : doomed)
      retiredObjects_.emplace_back(obj, at);
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
  for (int i = 0; i < object->NumInputs(); i++) {
    PATCHER::inlet* in = object->GetInlet(i);
    if (in != nullptr && in->GraphId() < 0) in->SetGraphId(nextInletId_++);
  }
  for (int i = 0; i < object->NumOutputs(); i++) {
    PATCHER::outlet* out = object->GetOutlet(i);
    if (out != nullptr && out->GraphId() < 0) out->SetGraphId(nextOutletId_++);
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
    if (now >= retiredObjects_[i].second + 2) {
      delete retiredObjects_[i].first;
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
    delete retiredObjects_[i].first;
  }
  retiredObjects_.clear();
}