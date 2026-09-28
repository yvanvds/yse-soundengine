
#include "pHandle.hpp"
#include "patcher.hpp"
#include "patcherImplementation.h"
#include "pEnums.h"
#include "pObject.h"
#include "../implementations/logImplementation.h"

using namespace YSE;

pHandle::pHandle(PATCHER::pObject* object) : object(object) {}

namespace {
  // Hand a host-side `Set*` (or an inlet query) on this handle to the inlet it
  // has to reach (issue #545).
  //
  // A `patcher` (subpatcher) object owns no pins of its own — its boundary is
  // the `.inlet` objects inside it — so `object->GetInlet(pin)` answers null for
  // one, and the four setters below would have dereferenced it. The owning
  // patcher knows how to resolve a subpatcher pin to the boundary object that
  // carries it, and it is the same resolution `Connect` performs, so pushing a
  // value in from the host and sending one down a cord into the subpatcher mean
  // the same thing.
  //
  // A standalone object (unit-test rig) has no patcher and answers for itself,
  // exactly as SetParams below splits the same two cases.
  //
  // Skipping a null inlet is also the fix for a pin number that names nothing on an
  // ordinary object: that used to be an unchecked null dereference, and a
  // subpatcher makes it reachable with input that looks perfectly valid.
  //
  // For a subpatcher the inlet belongs to a boundary object found under the
  // patcher mutex, which is released before the delivery (the delivery may
  // take it again). Another control thread may delete that boundary object in
  // between, so the delivery holds an objectPin taken before the lookup: the
  // reclaimer keeps it allocated until the send returns (issue #961).
  template <typename Send>
  void deliver(YSE::PATCHER::pObject* object, unsigned int pin, Send&& send) {
    if (object == nullptr) return;
    YSE::PATCHER::pObject* parent = object->Parent();
    if (parent == nullptr) {
      if (YSE::PATCHER::inlet* in = object->GetInlet(static_cast<int>(pin))) send(*in);
      return;
    }
    auto* owner = static_cast<YSE::PATCHER::patcherImplementation*>(parent);
    const YSE::PATCHER::patcherImplementation::objectPin keep(*owner);
    if (YSE::PATCHER::inlet* in = owner->ResolveInlet(object, static_cast<int>(pin))) send(*in);
  }
} // namespace

const char* pHandle::Type() const {
  if (!object) {
    return "Invalid Handle";
  } else {
    return object->Type();
  }
}

void YSE::pHandle::SetBang(unsigned int inlet) {
  deliver(object, inlet, [](PATCHER::inlet& in) { in.SetBang(T_GUI); });
}

void YSE::pHandle::SetIntData(unsigned int inlet, int value) {
  deliver(object, inlet, [value](PATCHER::inlet& in) { in.SetInt(value, T_GUI); });
}

void YSE::pHandle::SetFloatData(unsigned int inlet, float value) {
  deliver(object, inlet, [value](PATCHER::inlet& in) { in.SetFloat(value, T_GUI); });
}

void YSE::pHandle::SetListData(unsigned int inlet, const std::string& value) {
  deliver(object, inlet, [&value](PATCHER::inlet& in) { in.SetList(value, T_GUI); });
}

void YSE::pHandle::SetParams(const std::string& args) {
  INTERNAL::LogImpl().emit(E_DEBUG, "Handle: Passing arguments: " + args);
  // An object owned by a patcher may be rendering right now: never re-parse
  // it in place (that mutates pin vectors and param fields the audio thread
  // reads — issue #234). Route through the patcher, which stages an RT-safe
  // scalar apply or a structural replacement. `parent` is the owning
  // patcherImplementation by construction (see graphReadScope);
  // a standalone object (unit tests) keeps the synchronous parse.
  PATCHER::pObject* parent = object->Parent();
  if (parent != nullptr) {
    static_cast<PATCHER::patcherImplementation*>(parent)->SetObjectParams(this, args);
  } else {
    object->SetParams(args);
  }
}

std::string YSE::pHandle::GetGuiProperty(const std::string& key) {
  return object->GetGuiProperty(key);
}

void YSE::pHandle::SetGuiProperty(const std::string& key, const std::string& value) {
  object->SetGuiProperty(key, value);
}

bool YSE::pHandle::IsDSPInput(unsigned int inlet) {
  // Resolved like the setters above, and null-guarded for the same reason: a
  // subpatcher has no pins of its own, so asking one about inlet 0 used to
  // dereference null (issue #545). False for a pin that names nothing, which is
  // also the honest answer for a boundary that carries no signal.
  // Read under the same pin as a delivery (issue #961).
  bool accepts = false;
  deliver(object, inlet, [&accepts](PATCHER::inlet& in) { accepts = in.AcceptsDSP(); });
  return accepts;
}

YSE::OUT_TYPE YSE::pHandle::OutputDataType(unsigned int pin) {
  // The outlet side of IsDSPInput (issue #942): a subpatcher has no outlets of
  // its own, so asking the façade answered INVALID for every pin. Resolve
  // through the boundary instead — BUFFER for a `~outlet`, ANY for a `.outlet`,
  // INVALID only for a pin no boundary object claims.
  if (object == nullptr) return OUT_TYPE::INVALID;
  PATCHER::pObject* parent = object->Parent();
  if (parent == nullptr) return object->GetOutputType(pin);
  return static_cast<PATCHER::patcherImplementation*>(parent)->ResolveOutputType(
      object, static_cast<int>(pin));
}

int YSE::pHandle::GetInputs() {
  return object->NumInputs();
}

int YSE::pHandle::GetOutputs() {
  return object->NumOutputs();
}

std::string YSE::pHandle::GetName() {
  return object->Type();
}

std::string YSE::pHandle::GetParams() {
  return object->GetParams();
}

unsigned int YSE::pHandle::GetID() {
  return object->GetID();
}

// The three wiring queries read an outlet's live `connections`, which another
// control thread's Connect / Disconnect / DeleteObject rewrites under the
// patcher mutex, so an owned object answers under it (issue #966) — the
// patcher re-reads `object` under the lock as well. A standalone object
// (unit-test rig) has no patcher and no other writer.
unsigned int YSE::pHandle::GetConnections(unsigned int outlet) {
  if (object == nullptr) return 0;
  PATCHER::pObject* parent = object->Parent();
  if (parent == nullptr) return object->GetConnections(outlet);
  return static_cast<PATCHER::patcherImplementation*>(parent)->OutletConnections(this, outlet);
}

unsigned int YSE::pHandle::GetConnectionTarget(unsigned int outlet, unsigned int connection) {
  if (object == nullptr) return PATCHER::pObject::kNoObjectID;
  PATCHER::pObject* parent = object->Parent();
  if (parent == nullptr) return object->GetConnectionTarget(outlet, connection);
  return static_cast<PATCHER::patcherImplementation*>(parent)->OutletTarget(this, outlet,
                                                                            connection);
}

unsigned int YSE::pHandle::GetConnectionTargetInlet(unsigned int outlet, unsigned int connection) {
  if (object == nullptr) return PATCHER::pObject::kNoInletIndex;
  PATCHER::pObject* parent = object->Parent();
  if (parent == nullptr) return object->GetConnectionTargetInlet(outlet, connection);
  return static_cast<PATCHER::patcherImplementation*>(parent)->OutletTargetInlet(this, outlet,
                                                                                 connection);
}

std::string YSE::pHandle::GetGuiValue() {
  return object->GetGuiValue();
}

unsigned int YSE::pHandle::GetGuiValueCount() {
  return object->GetGuiValueCount();
}

std::string YSE::pHandle::GetGuiValueAt(unsigned int index) {
  return object->GetGuiValueAt(index);
}

bool YSE::pHandle::GuiValueIsSettable() {
  return object->GuiValueIsSettable();
}
