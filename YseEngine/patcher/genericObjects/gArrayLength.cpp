#include "gArrayLength.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

#include <cstddef>
#include <string>

using namespace YSE::PATCHER;
#define className gArrayLength

namespace {

  // Doc strings, hoisted out of CONSTRUCT() because they are long enough that
  // the constructor stops being readable with them inline — gArray's
  // arrangement.
  constexpr char kTriggerInletDoc[] =
      "Bang, or the bound array's reference \"array <name>\": send the array's length.";
  constexpr char kReferenceInletDoc[] =
      "Accepts the bound array's reference \"array <name>\" and ignores it, so the array's "
      "reference outlet can be wired here. Anything else is refused.";
  constexpr char kLengthDoc[] = "The number of elements in the array; 0 when it is empty.";

} // namespace

CONSTRUCT() {
  // The trigger inlet, hot, and the reference inlet, cold — gArrayAt's shape
  // with the index dropped: a length is asked for, never addressed, so the
  // hot inlet takes only the ask (a bang, or the array's own reference).
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_LIST_IN(ListIn);

  // One outlet, always an int: every array has a length, so there is no miss
  // outlet to split a failure onto — the one refusal left (a lost try-lock)
  // is counted, not signalled.
  ADD_OUT_INT;

  ADD_PARAM(arrayName);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // A private, empty store to start with, so `store` is never null and no
  // message handler needs a null check. Rebind() trades it for a shared one
  // as soon as there is both a name and a patcher to prefix it with.
  Rebind();

  ADD_DESCRIPTION("Outputs the number of elements in a named array when it receives a bang.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "length", kLengthDoc, "0-256");
  PARAM_DOC("name", "",
            "Name of the array to measure: the .array with this name in the same patcher. "
            "Empty uses a private, empty array.",
            "any identifier");
}

// A re-parse must not leave half of the previous configuration standing:
// Parameters::Set() calls this before parsing and returns early on an empty
// argument string, so this is what makes SetParams("") a real reset —
// dropping the name and going back to a private array. gArray's rule.
PARM_CLEAR() {
  arrayName.clear();
  Rebind();
}

PARM_PARSE() {
  Rebind();
}

// `parent` is a patcherImplementation by construction (the patcher hands
// itself to every object via SetParent); the cast mirrors gArray's.
void gArrayLength::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  Rebind();
}

void gArrayLength::RefreshBinding() {
  Rebind();
}

void gArrayLength::Rebind() {
  // No name, or no patcher to prefix it with, means no address — and no
  // address means a private, empty array. See gArray.h for why an unnamed
  // object does not pool on "patcher.<patcherName>.".
  std::string address;
  if (!arrayName.empty() && parent != nullptr) {
    auto* p = static_cast<patcherImplementation*>(parent);
    address = p->ScopedAddress(arrayName);
  }

  // Unchanged binding: keep the store. A live SetParams that leaves the name
  // alone must not re-anchor it, and neither must the second Rebind() a
  // Set() makes (clear, then parse).
  if (store != nullptr && address == boundAddress) return;

  bool created = false;
  store = address.empty() ? std::make_shared<arrayStore>()
                          : AcquireNamedStore<arrayStore>(address, created);
  boundAddress = address;
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  EmitLength(thread);
}

LIST_IN(ListIn) {
  if (inlet == 1) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — the binding is the creation argument, resolved on the
    // control thread, so there is nothing to set. gDictSlice's inlet rule.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The array's reference asks for the length — the message its .array emits
  // on a bang. Anything else, including a reference naming an array this
  // object is not bound to, is refused: resolving an unrecognised name means
  // the registry's mutex, and this may be the audio thread.
  if (ArrayReferenceNames(value, arrayName)) {
    EmitLength(thread);
    return;
  }
  Refuse();
}

void gArrayLength::EmitLength(YSE::THREAD thread) {
  std::size_t length = 0;
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    length = store->count;
  }

  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well write into this same array, and inside the guard that
  // write would be the one thing the try-lock drops.
  outputs[0].SendInt(static_cast<int>(length), thread);
}
