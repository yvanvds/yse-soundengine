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
      "A bang emits the array's length — store->count as it stood at the trigger — as one int "
      "out the length outlet. \"array <name>\" does the same when it names the array bound by "
      "the creation argument — the message an .array's reference outlet emits on a bang, so "
      "wiring that outlet here gives the family's gesture: bang the array, out comes its "
      "length. A reference naming anything else, or any other message, is refused and counted "
      "rather than logged, since this inlet may be the audio thread.";
  constexpr char kReferenceInletDoc[] =
      "\"array <name>\" is accepted silently when it names the array bound by the creation "
      "argument, so a patch may wire the array's reference outlet across. It sets nothing — "
      "the binding is the creation argument, resolved on the control thread — and anything "
      "else is refused and counted.";
  constexpr char kLengthDoc[] =
      "The length: how many elements the array held at the moment the trigger arrived, read "
      "under one hold of the store's guard. An empty array answers 0, and so does an unnamed "
      "(private) one — zero is a length, not a miss, which is why there is no miss outlet. "
      "Emitted only when asked, never on a write to the array.";

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

  ADD_DESCRIPTION(
      "Outputs an array's length — Max's array.length on the name-addressed value model "
      ".array settled: the array is bound from the creation argument, \".array.length "
      "<name>\", because an array is addressed by name and never passed down a cord. A bang "
      "emits the length as one int — store->count as it stood at the trigger — and the "
      "message an .array's reference outlet emits on a bang does the same, so wiring that "
      "outlet here gives the family's gesture: bang the array, out comes its length. An empty "
      "array answers 0, and so does an unnamed one; zero is a length, not a miss, so there is "
      "no miss outlet. The length leaves only when asked, never on a write to the array — "
      "the number every .uzi-driven walk over an array needs before it can start, and the one "
      ".zl len gives for a list.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "length", kLengthDoc, "0-256");
  PARAM_DOC("name", "",
            "The array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an "
            ".array of the same name in this patcher holds. Resolved once, on the control "
            "thread, which is why no message re-points it at run time. Empty reads a private, "
            "empty array: every ask answers 0.",
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
  // object does not pool on "<patcherName>.".
  std::string address;
  if (!arrayName.empty() && parent != nullptr) {
    auto* p = static_cast<patcherImplementation*>(parent);
    address = p->Name() + "." + arrayName;
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
