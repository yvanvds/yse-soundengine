
#include "pHandle.hpp"
#include "patcher.hpp"
#include "patcherImplementation.h"
#include "pEnums.h"
#include "pObject.h"
#include "../implementations/logImplementation.h"
#include <optional>

using namespace YSE;

namespace {
  // The patcher an object belongs to, or null for a standalone one. A patcher
  // sets itself as the parent before it wraps the object in a handle
  // (CreateObjectUnlocked), and a replacement joins the same patcher.
  YSE::PATCHER::patcherImplementation* OwnerOf(YSE::PATCHER::pObject* object) {
    if (object == nullptr) return nullptr;
    return static_cast<YSE::PATCHER::patcherImplementation*>(object->Parent());
  }
} // namespace

pHandle::pHandle(PATCHER::pObject* object) : object(object), owner(OwnerOf(object)) {}

// A getter's read of the handle's object (issue #968). A structural SetParams
// on another control thread swaps `object` for a replacement and retires the
// old one, so a plain read could see the pointer torn mid-write and a reader
// that loaded the old one could use it after the reclaimer freed it. This pins
// the owner first and loads second — the order graphReadScope uses, with the
// same argument: the swap's seq_cst store precedes the retirement, which
// precedes the reclaim pass's seq_cst pin load; if that load read 0 it came
// before this pin in the seq_cst order, so the load below sees the swap. Any
// object it loads therefore stays allocated until the pin is dropped.
//
// Lock-free — one atomic add, one load, one atomic sub — so it is safe under
// the patcher's lock (a `.preset` DumpState asks for Type and GetID there)
// and never makes the audio thread wait. A standalone object has no owner and
// no other writer, and is read without a pin.
class YSE::pHandle::pinnedRead {
public:
  explicit pinnedRead(const pHandle& handle) {
    if (handle.owner != nullptr) pin_.emplace(*handle.owner);
    object_ = handle.object.load(std::memory_order_seq_cst);
  }
  PATCHER::pObject* operator->() const {
    return object_;
  }
  PATCHER::pObject* get() const {
    return object_;
  }

private:
  std::optional<PATCHER::patcherImplementation::objectPin> pin_;
  PATCHER::pObject* object_ = nullptr;
};

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
  // between, so the caller holds a pinnedRead across the lookup and the
  // delivery — its pin is taken before the handle's object is even loaded —
  // and the reclaimer keeps both objects allocated until the send returns
  // (issues #961, #968).
  template <typename Send>
  void deliver(YSE::PATCHER::patcherImplementation* owner, YSE::PATCHER::pObject* object,
               unsigned int pin, Send&& send) {
    if (object == nullptr) return;
    if (owner == nullptr) {
      if (YSE::PATCHER::inlet* in = object->GetInlet(static_cast<int>(pin))) send(*in);
      return;
    }
    if (YSE::PATCHER::inlet* in = owner->ResolveInlet(object, static_cast<int>(pin))) send(*in);
  }
} // namespace

const char* pHandle::Type() const {
  const pinnedRead obj(*this);
  if (obj.get() == nullptr) return "Invalid Handle";
  // A type name is a string literal, so it outlives the object and the pin.
  return obj->Type();
}

void YSE::pHandle::SetBang(unsigned int inlet) {
  const pinnedRead obj(*this);
  deliver(owner, obj.get(), inlet, [](PATCHER::inlet& in) { in.SetBang(T_GUI); });
}

void YSE::pHandle::SetIntData(unsigned int inlet, int value) {
  const pinnedRead obj(*this);
  deliver(owner, obj.get(), inlet, [value](PATCHER::inlet& in) { in.SetInt(value, T_GUI); });
}

void YSE::pHandle::SetFloatData(unsigned int inlet, float value) {
  const pinnedRead obj(*this);
  deliver(owner, obj.get(), inlet, [value](PATCHER::inlet& in) { in.SetFloat(value, T_GUI); });
}

void YSE::pHandle::SetListData(unsigned int inlet, const std::string& value) {
  const pinnedRead obj(*this);
  deliver(owner, obj.get(), inlet, [&value](PATCHER::inlet& in) { in.SetList(value, T_GUI); });
}

void YSE::pHandle::SetParams(const std::string& args) {
  INTERNAL::LogImpl().emit(E_DEBUG, "Handle: Passing arguments: " + args);
  // An object owned by a patcher may be rendering right now: never re-parse
  // it in place (that mutates pin vectors and param fields the audio thread
  // reads — issue #234). Route through the patcher, which stages an RT-safe
  // scalar apply or a structural replacement, reading `object` under its
  // lock. A standalone object (unit tests) keeps the synchronous parse.
  if (owner != nullptr) {
    owner->SetObjectParams(this, args);
  } else {
    ObjectUnderLock()->SetParams(args);
  }
}

// GUI properties and the parameter string are written under the patcher's
// lock — by SetGuiProperty, by a scalar SetParams, and by the GUI-property
// copy a structural SetParams makes onto the replacement — so an owned object is
// read and written under it too (issue #968). Nothing here takes the lock
// again. A standalone object has no patcher and no other writer.
std::string YSE::pHandle::GetGuiProperty(const std::string& key) {
  if (owner != nullptr) return owner->ObjectGuiProperty(this, key);
  return ObjectUnderLock()->GetGuiProperty(key);
}

void YSE::pHandle::SetGuiProperty(const std::string& key, const std::string& value) {
  if (owner != nullptr) {
    owner->SetObjectGuiProperty(this, key, value);
  } else {
    ObjectUnderLock()->SetGuiProperty(key, value);
  }
}

bool YSE::pHandle::IsDSPInput(unsigned int inlet) {
  // Resolved like the setters above, and null-guarded for the same reason: a
  // subpatcher has no pins of its own, so asking one about inlet 0 used to
  // dereference null (issue #545). False for a pin that names nothing, which is
  // also the honest answer for a boundary that carries no signal.
  // Read under the same pin as a delivery (issue #961).
  bool accepts = false;
  const pinnedRead obj(*this);
  deliver(owner, obj.get(), inlet, [&accepts](PATCHER::inlet& in) { accepts = in.AcceptsDSP(); });
  return accepts;
}

YSE::OUT_TYPE YSE::pHandle::OutputDataType(unsigned int pin) {
  // The outlet side of IsDSPInput (issue #942): a subpatcher has no outlets of
  // its own, so asking the façade answered INVALID for every pin. Resolve
  // through the boundary instead — BUFFER for a `~outlet`, ANY for a `.outlet`,
  // INVALID only for a pin no boundary object claims.
  const pinnedRead obj(*this);
  if (obj.get() == nullptr) return OUT_TYPE::INVALID;
  if (owner == nullptr) return obj->GetOutputType(pin);
  return owner->ResolveOutputType(obj.get(), static_cast<int>(pin));
}

int YSE::pHandle::GetInputs() {
  const pinnedRead obj(*this);
  return obj->NumInputs();
}

int YSE::pHandle::GetOutputs() {
  const pinnedRead obj(*this);
  return obj->NumOutputs();
}

std::string YSE::pHandle::GetName() {
  const pinnedRead obj(*this);
  return obj->Type();
}

std::string YSE::pHandle::GetParams() {
  if (owner != nullptr) return owner->ObjectParams(this);
  return ObjectUnderLock()->GetParams();
}

unsigned int YSE::pHandle::GetID() {
  const pinnedRead obj(*this);
  return obj->GetID();
}

// The three wiring queries read an outlet's live `connections`, which another
// control thread's Connect / Disconnect / DeleteObject rewrites under the
// patcher mutex, so an owned object answers under it (issue #966) — the
// patcher reads `object` under the lock as well (issue #968). A standalone
// object (unit-test rig) has no patcher and no other writer.
unsigned int YSE::pHandle::GetConnections(unsigned int outlet) {
  if (owner != nullptr) return owner->OutletConnections(this, outlet);
  PATCHER::pObject* obj = ObjectUnderLock();
  return obj == nullptr ? 0 : obj->GetConnections(outlet);
}

unsigned int YSE::pHandle::GetConnectionTarget(unsigned int outlet, unsigned int connection) {
  if (owner != nullptr) return owner->OutletTarget(this, outlet, connection);
  PATCHER::pObject* obj = ObjectUnderLock();
  return obj == nullptr ? PATCHER::pObject::kNoObjectID
                        : obj->GetConnectionTarget(outlet, connection);
}

unsigned int YSE::pHandle::GetConnectionTargetInlet(unsigned int outlet, unsigned int connection) {
  if (owner != nullptr) return owner->OutletTargetInlet(this, outlet, connection);
  PATCHER::pObject* obj = ObjectUnderLock();
  return obj == nullptr ? PATCHER::pObject::kNoInletIndex
                        : obj->GetConnectionTargetInlet(outlet, connection);
}

// The GUI value calls are the host's per-frame poll, so they take no lock —
// an edit holding the patcher's lock (a long ParseJSON) never stalls a GUI
// frame. The pin keeps the object they read allocated; the object guards its
// own GUI state (see the protocol block in pObject.h).
std::string YSE::pHandle::GetGuiValue() {
  const pinnedRead obj(*this);
  return obj->GetGuiValue();
}

unsigned int YSE::pHandle::GetGuiValueCount() {
  const pinnedRead obj(*this);
  return obj->GetGuiValueCount();
}

std::string YSE::pHandle::GetGuiValueAt(unsigned int index) {
  const pinnedRead obj(*this);
  return obj->GetGuiValueAt(index);
}

bool YSE::pHandle::GuiValueIsSettable() {
  const pinnedRead obj(*this);
  return obj->GuiValueIsSettable();
}
