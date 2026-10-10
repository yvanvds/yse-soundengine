#include "gArrayConcat.h"
#include "../pObjectList.hpp"

#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

namespace {

  // Doc strings long enough that a constructor stops being readable with
  // them inline — gArray's arrangement.
  constexpr char kConcatTriggerInletDoc[] =
      "Bang, or the left array's reference \"array <name>\": send the two arrays as one list.";
  constexpr char kConcatRightInletDoc[] =
      "Accepts the right array's reference \"array <name>\" and ignores it, so its reference "
      "outlet can be wired here. Anything else is refused.";
  constexpr char kConcatEmptyOutletDoc[] = "Bang when both arrays are empty.";
  constexpr char kConcatLeftParamDoc[] =
      "Name of the left array: the .array with this name in the same patcher. Empty uses a "
      "private, empty array.";
  constexpr char kConcatRightParamDoc[] =
      "Name of the right array, found the same way. Empty uses a private, empty array.";

} // namespace

// ─── .array.concat ────────────────────────────────────────────────────────────

#define className gArrayConcat

gArrayConcat::gArrayConcat() : gArraySetOpBase(false) {
  ADD_DESCRIPTION(
      "Outputs the elements of two named arrays as one list: the left array followed by the "
      "right. Neither array is changed.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kConcatTriggerInletDoc, "");
  INLET_DOC(1, "right reference", kConcatRightInletDoc, "");
  OUTLET_DOC(0, "concatenation",
             "The left array's elements followed by the right's, as one list, repeats included. "
             "A single element leaves as an int, float or symbol.",
             "");
  OUTLET_DOC(1, "empty", kConcatEmptyOutletDoc, "");
  PARAM_DOC("left", "", kConcatLeftParamDoc, "any identifier");
  PARAM_DOC("right", "", kConcatRightParamDoc, "any identifier");
}

bool gArrayConcat::CollectLocked() {
  // Everything, in order, repeats kept — concatenation is not a set
  // operation, so there is no thinning and no membership test: two bounded
  // walks, each element appended as it stands. The caller holds the right
  // store's guard; the left array is the snapshot. False the moment an
  // element does not fit — the result would outrun what a cord carries, and
  // the caller then refuses whole.
  for (std::size_t i = 0; i < snapshot.count; i++) {
    const std::string& element = snapshot.elements[i];
    if (!result.Add(element.data(), element.size())) return false;
  }
  for (std::size_t j = 0; j < rightStore->count; j++) {
    const std::string& element = rightStore->elements[j];
    if (!result.Add(element.data(), element.size())) return false;
  }
  return true;
}

// ─── .array.join ──────────────────────────────────────────────────────────────

#undef className
#define className gArrayJoin

gArrayJoin::gArrayJoin() : gArrayEndsBase() {
  // The trigger inlet, hot, and the reference inlet, cold — gArrayUnique's
  // shape: the result is asked for with a bang, never addressed, so there is
  // no int or float method anywhere.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_LIST_IN(ListIn);

  // ANY on the joined outlet: the one token leaves typed the way the
  // patcher spells it — SendAtom's rule, so "1 2" joined by nothing leaves
  // as the int 12 and "c4 e4" joined by "-" as the symbol c4-e4. The empty
  // outlet is always a bang.
  ADD_OUT_ANY;
  ADD_OUT_BANG;

  // After gArrayEndsBase's name — the second creation argument. A single
  // token; a space separator is unspellable here, deliberately un-missed:
  // space-joined elements are the list text .array's getvalue already
  // emits.
  ADD_PARAM(separator);

  // The allocations the message path would otherwise need, taken here on
  // the control thread.
  joined.reserve(JOINED_CAPACITY);
  AtomList::ReserveRender(emitScratch);

  ADD_DESCRIPTION(
      "Joins the elements of a named array into a single word, with an optional separator "
      "between them: c4 e4 g4 joined with - gives c4-e4-g4.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger",
            "Bang, or the bound array's reference \"array <name>\": send the joined word.", "");
  INLET_DOC(1, "array reference",
            "Accepts the bound array's reference \"array <name>\" and ignores it, so the "
            "array's reference outlet can be wired here. Anything else is refused.",
            "");
  OUTLET_DOC(0, "joined",
             "The joined word. When it reads as a number it leaves as an int or a float, "
             "otherwise as a symbol.",
             "");
  OUTLET_DOC(1, "empty", "Bang when the array is empty.", "");
  PARAM_DOC("name", "",
            "Name of the array to use: the .array with this name in the same patcher. Empty "
            "uses a private, empty array.",
            "any identifier");
  PARAM_DOC("separator", "",
            "Text placed between each pair of elements, one word without spaces. Empty joins "
            "the elements directly.",
            "any token");
}

// A re-parse must not leave half of the previous configuration standing:
// SetParams("") — and the clear half of every Set() — drops the separator
// along with the name. gArraySliceBase's rule.
PARM_CLEAR() {
  separator.clear();
  gArrayEndsBase::ClearParams();
}

BANG_IN(BangIn) {
  (void)inlet;
  Ask(thread);
}

LIST_IN(ListIn) {
  if (inlet == 1) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — gDictSlice's inlet rule.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The array's reference asks for the joined text — the message its .array
  // emits on a bang, the family's gesture. Anything else is refused:
  // resolving an unrecognised name means the registry's mutex, and this may
  // be the audio thread.
  if (ArrayReferenceNames(value, arrayName)) {
    Ask(thread);
    return;
  }
  Refuse();
}

void gArrayJoin::Ask(YSE::THREAD thread) {
  bool collected = true;
  bool hasElements = false;
  joined.clear();
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    hasElements = store->count > 0;
    // One bounded pass: every append is checked against the ceiling before
    // it happens, into storage reserved at construction — no allocation,
    // whichever thread asked. The elements are never empty (the store
    // refuses an empty element), so a non-empty array always joins to a
    // non-empty token.
    for (std::size_t i = 0; i < store->count; i++) {
      const std::string& element = store->elements[i];
      const std::size_t needed = element.size() + (i > 0 ? separator.size() : 0);
      if (joined.size() + needed > JOINED_CAPACITY) {
        collected = false;
        break;
      }
      if (i > 0) joined.append(separator);
      joined.append(element);
    }
  }

  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well write into this same array, and inside the guard that
  // write would be the one thing the try-lock drops.
  if (!collected) {
    // The joined text spells more characters than a cord carries — an array
    // of long elements, or a long separator, can. Refused whole and
    // counted, .array.at's whole-reply rule: a partial join would be
    // truncation.
    Refuse();
    return;
  }
  if (!hasElements) {
    outputs[1].SendBang(thread);
    return;
  }
  // Through SendAtom, so the one token leaves as the int, float or symbol
  // it spells — the family's transport convention.
  SendAtom(outputs[0], joined.data(), joined.size(), emitScratch, thread);
}
