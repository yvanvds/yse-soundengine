
#include "pHandle.hpp"
#include "patcher.hpp"
#include "patcherImplementation.h"
#include "pEnums.h"
#include "pObject.h"
#include "../implementations/logImplementation.h"

using namespace YSE;

pHandle::pHandle(PATCHER::pObject* object) : object(object) {}

namespace {
  // The inlet a host-side `Set*` on this handle has to reach (issue #545).
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
  // The null return is also the fix for a pin number that names nothing on an
  // ordinary object: that used to be an unchecked null dereference, and a
  // subpatcher makes it reachable with input that looks perfectly valid.
  YSE::PATCHER::inlet* resolveInlet(YSE::PATCHER::pObject* object, unsigned int pin) {
    if (object == nullptr) return nullptr;
    YSE::PATCHER::pObject* parent = object->Parent();
    if (parent == nullptr) return object->GetInlet(static_cast<int>(pin));
    return static_cast<YSE::PATCHER::patcherImplementation*>(parent)->ResolveInlet(
        object, static_cast<int>(pin));
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
  if (PATCHER::inlet* in = resolveInlet(object, inlet)) in->SetBang(T_GUI);
}

void YSE::pHandle::SetIntData(unsigned int inlet, int value) {
  if (PATCHER::inlet* in = resolveInlet(object, inlet)) in->SetInt(value, T_GUI);
}

void YSE::pHandle::SetFloatData(unsigned int inlet, float value) {
  if (PATCHER::inlet* in = resolveInlet(object, inlet)) in->SetFloat(value, T_GUI);
}

void YSE::pHandle::SetListData(unsigned int inlet, const std::string& value) {
  if (PATCHER::inlet* in = resolveInlet(object, inlet)) in->SetList(value, T_GUI);
}

void YSE::pHandle::SetParams(const std::string& args) {
  INTERNAL::LogImpl().emit(E_DEBUG, "Handle: Passing arguments: " + args);
  // An object owned by a patcher may be rendering right now: never re-parse
  // it in place (that mutates pin vectors and param fields the audio thread
  // reads — issue #234). Route through the patcher, which stages an RT-safe
  // scalar apply or a structural replacement. `parent` is the owning
  // patcherImplementation by construction (see pObject::CurrentBlockGraph);
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
  PATCHER::inlet* in = resolveInlet(object, inlet);
  return in != nullptr && in->AcceptsDSP();
}

YSE::OUT_TYPE YSE::pHandle::OutputDataType(unsigned int pin) {
  return object->GetOutputType(pin);
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

unsigned int YSE::pHandle::GetConnections(unsigned int outlet) {
  return object->GetConnections(outlet);
}

unsigned int YSE::pHandle::GetConnectionTarget(unsigned int outlet, unsigned int connection) {
  return object->GetConnectionTarget(outlet, connection);
}

unsigned int YSE::pHandle::GetConnectionTargetInlet(unsigned int outlet, unsigned int connection) {
  return object->GetConnectionTargetInlet(outlet, connection);
}

std::string YSE::pHandle::GetGuiValue() {
  return object->GetGuiValue();
}
