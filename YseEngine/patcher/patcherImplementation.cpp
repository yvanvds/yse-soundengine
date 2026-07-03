
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
#include <string>
#include "../implementations/logImplementation.h"

using namespace YSE::PATCHER;

namespace {
  // Process-wide counter feeding the auto-generated "patcher_<N>" default
  // name (issue #122). Distinct from pObject::CreateID() so the patcher
  // counter is not perturbed by inner-object construction.
  std::atomic<unsigned int> g_nextPatcherIndex{0};
} // namespace

patcherImplementation::patcherImplementation(int mainOutputs, YSE::patcher* head)
  : pObject(false),
    controlledBySound(false),
    head(head),
    fileHandlerActive(false),
    patcherName("patcher_" +
                std::to_string(g_nextPatcherIndex.fetch_add(1, std::memory_order_relaxed))) {
  output.resize(mainOutputs);
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
  // memory cleanup
  Clear();
  // The audio thread is stopped at destruction, so reclaim unconditionally:
  // the deferred retire lists (and the final published snapshot) are freed
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

void patcherImplementation::Connect(YSE::pHandle* from, int outlet, YSE::pHandle* to, int inlet) {
  if (!fileHandlerActive) mtx.lock();
  PATCHER::outlet* out = from->object->GetOutlet(outlet);
  PATCHER::inlet* in = to->object->GetInlet(inlet);
  if (out != nullptr && in != nullptr) {
    from->object->ConnectOutlet(in, outlet);
    to->object->ConnectInlet(out, inlet);
  } else {
    INTERNAL::LogImpl().emit(E_ERROR, "Patcher: Invalid Connection");
  }
  // In batch mode (ParseJSON) publish once at the end; otherwise swap now.
  if (!fileHandlerActive) {
    RebuildAndPublish();
    mtx.unlock();
  }
}

void patcherImplementation::Disconnect(YSE::pHandle* from, int outlet, YSE::pHandle* to,
                                       int inlet) {
  if (!fileHandlerActive) mtx.lock();
  to->object->DisconnectInlet(from->object->GetOutlet(outlet), inlet);
  if (!fileHandlerActive) {
    RebuildAndPublish();
    mtx.unlock();
  }
}

YSE::pHandle* patcherImplementation::CreateObject(const std::string& type,
                                                  const std::string& args) {
  YSE::pHandle* handle = nullptr;
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

  handle = new YSE::pHandle(object);

  if (!fileHandlerActive) mtx.lock();
  objects.insert(std::pair<YSE::pHandle*, pObject*>(handle, object));
  AssignGraphIds(object);
  if (!fileHandlerActive) {
    RebuildAndPublish();
    mtx.unlock();
  }
  INTERNAL::LogImpl().emit(E_DEBUG, "Patcher: " + type + " created");
  return handle;
}

void patcherImplementation::DeleteObject(YSE::pHandle* handle) {
  if (!fileHandlerActive) mtx.lock();

  if (handle->Type() == OBJ::G_METRO) {
    handle->object->GetInlet(0)->SetInt(0, YSE::THREAD::T_GUI);
  }

  pObject* object = handle->object;
  objects.erase(handle);
  // Detach from peers so the next snapshot holds no reference to it, but do
  // not free it yet — an in-flight audio block may still walk the retired
  // snapshot that references it. The free is deferred to the reclaimer.
  object->UnwireFromPeers();
  if (!fileHandlerActive) RebuildAndPublish();
  retiredObjects_.emplace_back(object, audioBlock_.load(std::memory_order_acquire));
  // The handle is never referenced by a GraphState, so it can go immediately.
  delete handle;

  if (!fileHandlerActive) mtx.unlock();
}

void patcherImplementation::Clear() {
  if (!fileHandlerActive) mtx.lock();

  std::uint64_t at = audioBlock_.load(std::memory_order_acquire);
  for (auto it = objects.begin(); it != objects.end(); ++it) {
    if (it->first->Type() == OBJ::G_METRO) {
      it->second->GetInlet(0)->SetInt(0, YSE::THREAD::T_GUI);
    }
    it->second->UnwireFromPeers();
    retiredObjects_.emplace_back(it->second, at);
    delete it->first; // handle: not referenced by a GraphState
  }
  objects.clear();
  if (!fileHandlerActive) RebuildAndPublish();
  if (!fileHandlerActive) mtx.unlock();
}

using json = nlohmann::json;
std::string patcherImplementation::DumpJSON() {
  json j;
  int counter = 0;

  mtx.lock();
  fileHandlerActive = true;
  for (const auto& any : objects) {
    std::string name = "object " + std::to_string(counter);
    any.second->DumpJson(j[name]);
    counter++;
  }
  fileHandlerActive = false;
  mtx.unlock();

  std::string result = j.dump(2, ' ', true);
  return result;
}

void patcherImplementation::ParseJSON(const std::string& content) {
  auto j = json::parse(content);

  std::map<int, pHandle*> OldIDs;

  mtx.lock();
  fileHandlerActive = true;
  // restore objects first
  for (auto obj = j.begin(); obj != j.end(); ++obj) {
    std::string type = obj.value()["type"].get<std::string>();
    std::string args = obj.value()["parms"].get<std::string>();
    pHandle* handle = CreateObject(type, args);

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
          Connect(sourceHandle, outlet, targetHandle, inlet);
        }
      }
      outlet++;
    }
  }
  fileHandlerActive = false;
  // Every create/connect above ran in batch mode (no per-edit swap); publish
  // the whole parsed graph in a single atomic swap now.
  RebuildAndPublish();
  mtx.unlock();
}

unsigned int patcherImplementation::Objects() {
  return static_cast<unsigned int>(objects.size());
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
  for (auto& x : objects) {
    if (strcmp(x.second->Type(), OBJ::G_RECEIVE) == 0) {
      if (to.compare(x.second->DataName()) == 0) {
        x.second->GetInlet(0)->SetBang(thread);
        return true;
      }
    }
  }
  if (oscHandle != nullptr) {
    oscHandle->Send(to);
    return true;
  }
  INTERNAL::LogImpl().emit(E_FILE_ERROR, "Cannot find target " + to + ". Valid targets are" +
                                             GetRecieveObjectsAsString());
  return false;
}

bool patcherImplementation::PassData(int value, const std::string& to, YSE::THREAD thread) {
  for (auto& x : objects) {
    if (strcmp(x.second->Type(), OBJ::G_RECEIVE) == 0) {
      if (to.compare(x.second->DataName()) == 0) {
        x.second->GetInlet(0)->SetInt(value, thread);
        return true;
      }
    }
  }
  if (oscHandle != nullptr) {
    oscHandle->Send(to, value);
    return true;
  }
  INTERNAL::LogImpl().emit(E_FILE_ERROR, "Cannot find target " + to + ". Valid targets are" +
                                             GetRecieveObjectsAsString());
  return false;
}

bool patcherImplementation::PassData(float value, const std::string& to, YSE::THREAD thread) {
  for (auto& x : objects) {
    if (strcmp(x.second->Type(), OBJ::G_RECEIVE) == 0) {
      if (to.compare(x.second->DataName()) == 0) {
        x.second->GetInlet(0)->SetFloat(value, thread);
        return true;
      }
    }
  }
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
  for (auto& x : objects) {
    if (strcmp(x.second->Type(), OBJ::G_RECEIVE) == 0) {
      if (to.compare(x.second->DataName()) == 0) {
        x.second->GetInlet(0)->SetList(value, thread);
        return true;
      }
    }
  }
  if (oscHandle != nullptr) {
    oscHandle->Send(to, value);
    return true;
  }
  INTERNAL::LogImpl().emit(E_FILE_ERROR, "Cannot find target " + to + ". Valid targets are" +
                                             GetRecieveObjectsAsString());
  return false;
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
    retiredGraphs_.emplace_back(old, audioBlock_.load(std::memory_order_acquire));
  }
  DrainRetired();
}

void patcherImplementation::DrainRetired() {
  const std::uint64_t now = audioBlock_.load(std::memory_order_acquire);
  // Interim reclamation (issue #227 replaces this): a snapshot retired at block
  // C is safe to free once the audio thread has started at least two later
  // blocks, so no in-flight block can still hold it. Free graphs before the
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