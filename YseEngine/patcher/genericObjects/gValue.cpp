#include "gValue.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

#include <cstring>
#include <mutex>
#include <unordered_map>
#include <utility>

using namespace YSE::PATCHER;
#define className gValue

namespace {
  // The registry behind AcquireValueSlot. Control thread only: every caller
  // reaches it from SetParams / SetParent / SetName, never from a message
  // handler, so the mutex here is never on an audio path.
  struct valueRegistry {
    std::mutex mutex;
    std::unordered_map<std::string, std::weak_ptr<valueSlot>> slots;
  };

  valueRegistry& Registry() {
    static valueRegistry registry;
    return registry;
  }

  // Expired entries are cheap to leave and cheap to sweep, so they are swept
  // when the map has grown past a size no realistic patch reaches by itself.
  // Without this a long live-coding session — every patcher a fresh
  // "patcher_<N>" prefix — would accumulate one dead key per name it ever
  // spelled.
  constexpr std::size_t kPruneThreshold = 64;

  void PruneExpired(valueRegistry& registry) {
    for (auto it = registry.slots.begin(); it != registry.slots.end();) {
      if (it->second.expired()) {
        it = registry.slots.erase(it);
      } else {
        ++it;
      }
    }
  }
} // namespace

std::shared_ptr<valueSlot> YSE::PATCHER::AcquireValueSlot(const std::string& address,
                                                          bool& created) {
  created = false;
  valueRegistry& registry = Registry();
  const std::lock_guard<std::mutex> lock(registry.mutex);

  auto it = registry.slots.find(address);
  if (it != registry.slots.end()) {
    if (std::shared_ptr<valueSlot> existing = it->second.lock()) return existing;
    // The name outlived the last .value that held it; the key is stale.
    registry.slots.erase(it);
  }

  if (registry.slots.size() >= kPruneThreshold) PruneExpired(registry);

  auto slot = std::make_shared<valueSlot>();
  registry.slots[address] = slot;
  created = true;
  return slot;
}

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(Emit);
  REG_INT_IN(StoreIntValue);
  REG_FLOAT_IN(StoreFloatValue);
  REG_LIST_IN(StoreListValue);

  ADD_OUT_ANY;

  ADD_PARAM(valueName);
  ADD_PARAM(initial);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // A private cell to start with, so `slot` is never null and no message
  // handler needs a null check. Rebind() trades it for a shared one as soon as
  // there is both a name and a patcher to prefix it with.
  Rebind();

  // The one allocation the read path would otherwise need, taken here on the
  // control thread for the longest payload the cell can hold.
  emitScratch.reserve(valueSlot::kTextCapacity);

  ADD_DESCRIPTION(
      "Named cell shared by every .value with the same name. A value arriving on the inlet is "
      "stored for all of them and emitted by none; a bang emits what is stored. The cell is "
      "addressed as \"<patcherName>.<name>\", the same address form .s and .r use, so patchers "
      "sharing a name share their values. An unnamed .value keeps a cell of its own.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "in",
            "Bang emits the stored value; int / float / list store one for every .value of this "
            "name without emitting anything. A list longer than 256 characters is refused and the "
            "stored value kept.",
            "");
  OUTLET_DOC(0, "out",
             "The stored value, on a bang. Nothing at all until something has been stored.", "");
  PARAM_DOC("valueName", "",
            "Name of the shared cell. Empty gives this object a private cell rather than pooling "
            "it with every other unnamed .value.",
            "any identifier");
  PARAM_DOC("initial", "",
            "Value the cell starts at — the rest of the argument string, so \"0 0\" is a list. "
            "Applied only by the object that creates the cell; a later .value of the same name "
            "adopts what is already stored.",
            "any int, float or list");
}

// A re-parse must not leave half of the previous configuration standing:
// Parameters::Set() calls this before parsing and returns early on an empty
// argument string, so this is the only thing that makes `SetParams("")` a real
// reset — dropping the shared name and going back to a private cell — rather
// than a no-op that leaves the object on its old name.
PARM_CLEAR() {
  valueName.clear();
  initial.clear();
  Rebind();
}

PARM_PARSE() {
  Rebind();
  // A private cell belongs to this object and nothing else, so re-parsing the
  // creation argument is exactly the moment to seed it — there is no other
  // .value whose reading would be disturbed. A shared cell is seeded only by
  // whoever brought it into existence, which Rebind() does.
  if (!IsShared()) ApplyInitial();
}

// `parent` is a patcherImplementation by construction (the patcher hands itself
// to every object via SetParent); the cast mirrors gSend's.
void gValue::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  Rebind();
}

void gValue::RefreshBinding() {
  Rebind();
}

void gValue::Rebind() {
  // No name, or no patcher to prefix it with, means no address — and no
  // address means a private cell. See the class documentation for why an
  // unnamed .value does not pool on "<patcherName>.".
  std::string address;
  if (!valueName.empty() && parent != nullptr) {
    auto* p = static_cast<patcherImplementation*>(parent);
    address = p->Name() + "." + valueName;
  }

  // Unchanged binding: keep the cell, and with it whatever is stored in it. A
  // live SetParams that leaves the name alone must not reset the value, and
  // neither must the second Rebind() a Set() makes (clear, then parse).
  if (slot != nullptr && address == boundAddress) return;

  bool created = false;
  if (address.empty()) {
    slot = std::make_shared<valueSlot>();
    created = true;
  } else {
    slot = AcquireValueSlot(address, created);
  }
  boundAddress = address;

  // Only the object that brought the cell into existence gets to say where it
  // starts; one joining an established name adopts what is there.
  if (created) ApplyInitial();
}

void gValue::ApplyInitial() {
  if (initial.empty()) return;

  if (initial.size() == 1) {
    // One token: a number if it reads as a whole finite number, classified int
    // or float by its spelling — the test .sel and .trigger apply to their own
    // constant arguments, so "120" and "120." start the cell differently.
    float number = 0.0f;
    if (ReadNumericToken(initial[0], number)) {
      if (TokenLooksLikeFloat(initial[0])) {
        StoreScalar(valueKind::Float, 0, number);
      } else {
        StoreScalar(valueKind::Int, static_cast<int>(number), 0.0f);
      }
      return;
    }
  }

  // Anything else is the list it was written as. Control thread, so building
  // the joined text here is free of the constraints the message path has.
  std::string text;
  for (std::size_t i = 0; i < initial.size(); i++) {
    if (i > 0) text += ' ';
    text += initial[i];
  }
  StoreText(text.c_str(), text.size());
}

bool gValue::StoreScalar(valueKind kind, int intPart, float floatPart) {
  const valueSlotGuard guard(*slot);
  if (!guard.Held()) return false;
  slot->kind = kind;
  slot->intValue = intPart;
  slot->floatValue = floatPart;
  return true;
}

bool gValue::StoreText(const char* text, std::size_t length) {
  // Refused rather than truncated: half a list is a different list, and the
  // stored value a patch is reading must not silently become one. Silent
  // because this may be the audio thread.
  if (length > valueSlot::kTextCapacity) return false;

  const valueSlotGuard guard(*slot);
  if (!guard.Held()) return false;
  std::memcpy(slot->text, text, length);
  slot->text[length] = '\0';
  slot->textLength = length;
  slot->kind = valueKind::List;
  return true;
}

valueKind gValue::Kind() const {
  const valueSlotGuard guard(*slot);
  if (!guard.Held()) return valueKind::None;
  return slot->kind;
}

BANG_IN(Emit) {
  // Copy out under the guard and emit after releasing it. Sending runs the
  // whole downstream graph, which may well store into this same cell — inside
  // the guard that store would be the one thing the try-lock drops.
  valueKind kind = valueKind::None;
  int intPart = 0;
  float floatPart = 0.0f;
  {
    const valueSlotGuard guard(*slot);
    if (!guard.Held()) return;
    kind = slot->kind;
    intPart = slot->intValue;
    floatPart = slot->floatValue;
    // Reserved to kTextCapacity in the constructor, so this refills the buffer
    // rather than allocating one.
    if (kind == valueKind::List) emitScratch.assign(slot->text, slot->textLength);
  }

  switch (kind) {
  case valueKind::Int:
    outputs[0].SendInt(intPart, thread);
    break;
  case valueKind::Float:
    outputs[0].SendFloat(floatPart, thread);
    break;
  case valueKind::List:
    outputs[0].SendList(emitScratch, thread);
    break;
  case valueKind::None:
    // Nothing stored yet. Emitting a zero here would be indistinguishable from
    // a value a patch actually stored.
    break;
  }
}

INT_IN(StoreIntValue) {
  StoreScalar(valueKind::Int, value, 0.0f);
}

FLOAT_IN(StoreFloatValue) {
  StoreScalar(valueKind::Float, 0, value);
}

LIST_IN(StoreListValue) {
  StoreText(value.c_str(), value.size());
}
