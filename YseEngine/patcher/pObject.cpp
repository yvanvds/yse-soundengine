
#include <atomic>
#include <cstdint>
#include "pObject.h"
#include "pHandle.hpp"
#include "patcherImplementation.h"
#include "../headers/enums.hpp"
#include "../implementations/logImplementation.h"

using namespace YSE::PATCHER;

// ``parent`` is the owning patcherImplementation by construction (set via
// SetParent when the object is added). null for a standalone object or the
// patcher itself, in which case there is no snapshot to consult.
const YSE::PATCHER::GraphState* pObject::CurrentBlockGraph() const {
  if (parent == nullptr) return nullptr;
  return static_cast<patcherImplementation*>(parent)->CurrentBlockGraph();
}

// Same hop as CurrentBlockGraph: the owning patcher's scheduler, or null when
// there is no patcher to defer into (standalone / unit-test use, issue #628).
YSE::PATCHER::messageScheduler* pObject::Scheduler() const {
  if (parent == nullptr) return nullptr;
  return static_cast<patcherImplementation*>(parent)->Scheduler();
}

// Same hop again: the owning patcher's domain-clock bindings, or null when
// there is no patcher to bind through (standalone / unit-test use, issue #688).
YSE::PATCHER::clockBridge* pObject::Clocks() const {
  if (parent == nullptr) return nullptr;
  return static_cast<patcherImplementation*>(parent)->Clocks();
}

// Default: a deferred message nobody asked for is dropped. Only objects that
// arm deferrals override this (issue #628).
void pObject::DeliverDeferred(const deferredMessage&, YSE::THREAD) {}

// Same hop again: the owning patcher's file plumbing, or null when there is no
// patcher to read a file through, or when no object in it has asked for one
// (issue #683).
YSE::PATCHER::fileScheduler* pObject::FileIO() const {
  if (parent == nullptr) return nullptr;
  return static_cast<patcherImplementation*>(parent)->FileIO();
}

void pObject::EnableFileIO() {
  if (parent == nullptr) return;
  static_cast<patcherImplementation*>(parent)->EnsureFileIO();
}

// Default: a completion nobody asked for is dropped. Only objects that request
// files override this (issue #683).
void pObject::DeliverFileResult(const fileResult&, YSE::THREAD) {}

// Default: an object with nothing left sounding outside the patch has nothing
// to do when the patch goes away, and pays one non-virtual-sized call for
// saying so (issue #758).
void pObject::Teardown(YSE::THREAD) {}

void pObject::UnwireFromPeers() {
  for (unsigned int i = 0; i < inputs.size(); i++) {
    inputs[i].UnwireFromPeers();
  }
  for (unsigned int i = 0; i < outputs.size(); i++) {
    outputs[i].UnwireFromPeers();
  }
}

namespace {
  // The instance-tag source (issue #733). Process-wide on purpose, and the one
  // place a process-wide counter is right for an object: the tag is never
  // serialised and never leaves the engine, so it cannot leak into a dump the
  // way the pre-#730 storage counter did, and being unique across the whole
  // process rather than within one patcher costs nothing while making the
  // scheduler guard hold for objects that never join a patcher at all.
  //
  // Starts at 1 so kNoInstanceTag (0) is issued to nobody. std::uint64_t
  // because it is never reset and never reused: at one object per nanosecond it
  // takes 584 years to wrap, which is the whole reason the storage ID could be
  // freed to shrink instead.
  //
  // relaxed is enough — the counter orders nothing but itself, and the tag it
  // produces is published to other threads by the release stores the object's
  // own publication path already performs (SetParent -> RebuildAndPublish, or
  // the scheduler slot's ARMED store).
  std::atomic<std::uint64_t> g_nextInstanceTag{1};
} // namespace

// The storage ID is deliberately *not* assigned here: it belongs to the patcher
// that owns the object, not to the process that built it, and the object does
// not know its patcher until SetParent (issue #730). An object that never joins
// a patcher keeps kNoStorageID and is never serialised.
//
// The instance tag is the opposite case and is stamped right here: it is a
// property of *this object*, not of the patch, and it has to exist before any
// scheduler can arm against the object (issue #733).
pObject::pObject(bool isDSPObject, pObject* parent)
  : parent(parent),
    DSP(isDSPObject),
    instanceTag_(g_nextInstanceTag.fetch_add(1, std::memory_order_relaxed)) {}

bool pObject::IsDSPStartPoint() {
  if (!DSP) return false;

  for (unsigned int i = 0; i < inputs.size(); i++) {
    if (inputs[i].HasActiveDSPConnection()) return false;
  }

  // at this point either the dsp pin is not connected or there is none
  return true;
}

void pObject::ResetDSP() {
  for (unsigned int i = 0; i < inputs.size(); i++) {
    inputs[i].ResetDSP();
  }
}

void pObject::CalculateIfReady(YSE::THREAD thread) {
  // make sure all dsp inputs are ready
  for (unsigned int i = 0; i < inputs.size(); i++) {
    if (inputs[i].WaitingForDSP()) {
      return;
    }
  }
  Calculate(thread);
}

void pObject::SetParent(pObject* parent) {
  this->parent = parent;
}

void pObject::SetParams(const std::string& args) {
  parms.Set(args);
}

bool pObject::ConnectInlet(outlet* from, int inlet) {
  return inputs[inlet].Connect(from);
}

void pObject::DisconnectInlet(outlet* from, int inlet) {
  inputs[inlet].Disconnect(from);
}

void pObject::ConnectOutlet(inlet* dest, int outlet) {
  outputs[outlet].Connect(dest);
}

YSE::OUT_TYPE pObject::GetOutputType(unsigned int output) const {
  if (output >= outputs.size()) return OUT_TYPE::INVALID;

  return outputs[output].Type();
}

YSE::PATCHER::inlet* pObject::GetInlet(int number) {
  if (static_cast<size_t>(number) >= inputs.size()) return nullptr;
  return &(inputs[number]);
}

YSE::PATCHER::outlet* pObject::GetOutlet(int number) {
  if (static_cast<size_t>(number) >= outputs.size()) return nullptr;
  return &(outputs[number]);
}

void pObject::DumpJson(nlohmann::json::value_type& json) {
  json["type"] = Type();
  json["ID"] = ID;
  json["parms"] = parms.Get();

  for (unsigned int i = 0; i < outputs.size(); i++) {
    outputs[i].DumpJSON(json["outputs"]["output " + std::to_string(i)]);
  }

  for (auto const& x : guiProperties) {
    json["gui"][x.first] = x.second;
  }

  // Anything the object holds beyond its creation parameters (issue #494).
  // Built into a temporary and only attached when the object wrote something,
  // so an object without state of its own serialises exactly as it always did
  // and no "state": null appears in every saved patch.
  nlohmann::json state;
  DumpState(state);
  if (!state.is_null()) json["state"] = state;
}

const std::string& pObject::GetParams() {
  return parms.Get();
}

// The outlet number comes from outside and outputs is a vector, so all three
// range-check it before indexing, the way GetOutputType does (issue #737). See
// pObject.h for what each answers when the query cannot be met.
unsigned int pObject::GetConnections(unsigned int outlet) {
  if (outlet >= outputs.size()) return 0;
  return outputs[outlet].GetConnections();
}

unsigned int pObject::GetConnectionTarget(unsigned int outlet, unsigned int connection) {
  if (outlet >= outputs.size()) return kNoObjectID;
  return outputs[outlet].GetTarget(connection);
}

unsigned int pObject::GetConnectionTargetInlet(unsigned int outlet, unsigned int connection) {
  if (outlet >= outputs.size()) return kNoInletIndex;
  return outputs[outlet].GetTargetInlet(connection);
}

std::string pObject::GetGuiProperty(const std::string& key) {
  auto pos = guiProperties.find(key);
  if (pos == guiProperties.end()) {
    return "";
  }
  return pos->second;
}

void pObject::SetGuiProperty(const std::string& key, const std::string& value) {
  guiProperties[key] = value;
}
