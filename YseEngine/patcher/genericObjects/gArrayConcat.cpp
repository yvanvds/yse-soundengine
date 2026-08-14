#include "gArrayConcat.h"
#include "../pObjectList.hpp"

#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

namespace {

  // Doc strings long enough that a constructor stops being readable with
  // them inline — gArray's arrangement.
  constexpr char kConcatTriggerInletDoc[] =
      "A bang asks for the concatenation — the left array's elements followed by the right's, "
      "as they stood at the trigger, the left copied out under its guard, the result built "
      "against the right under that guard alone, and sent after every guard is released. "
      "\"array <name>\" does the same when it names the left array bound by the first creation "
      "argument — the message an .array's reference outlet emits on a bang, so wiring that "
      "outlet here gives the family's gesture. A reference naming anything else, or any other "
      "message, is refused and counted rather than logged, since this inlet may be the audio "
      "thread.";
  constexpr char kConcatRightInletDoc[] =
      "\"array <name>\" is accepted silently when it names the right array bound by the second "
      "creation argument, so a patch may wire both reference outlets across as it would in Max. "
      "It sets nothing — the binding is the creation argument, resolved on the control thread — "
      "and anything else is refused and counted.";
  constexpr char kConcatEmptyOutletDoc[] =
      "Bang when both arrays are empty (or unnamed, private) — there was nothing to put "
      "together. \"No data\" is a state a patch must be able to route on, not an error. A lost "
      "try-lock on either store is a counted refusal instead: the arrays' state is unknown, so "
      "neither outlet fires.";
  constexpr char kConcatLeftParamDoc[] =
      "The left array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an "
      ".array of the same name in this patcher holds. Resolved once, on the control thread, "
      "which is why no message re-points it at run time. Empty reads a private, empty array on "
      "that side.";
  constexpr char kConcatRightParamDoc[] =
      "The right array's shared name, bound exactly as the left one. Empty reads a private, "
      "empty array on that side.";

} // namespace

// ─── .array.concat ────────────────────────────────────────────────────────────

#define className gArrayConcat

gArrayConcat::gArrayConcat() : gArraySetOpBase(false) {
  ADD_DESCRIPTION(
      "Outputs two arrays as one — Max's array.concat on the name-addressed value model .array "
      "settled: both arrays are bound from the creation arguments, \".array.concat <left> "
      "<right>\", because an array is addressed by name and never passed down a cord, and "
      "neither may be re-pointed from a message. The left array's elements followed by the "
      "right's, everything kept — repeats included, order preserved — where the set operations "
      "thin; Max's \"the data in the array received in the right inlet will be appended to the "
      "data in the array received in the left inlet\", with the originals unmodified. The "
      "result leaves as the list it spells, never as a new named array — creating one would "
      "resolve a name on a message path — and is lossless into another .array, which is where "
      "a patch stores it. Two empty arrays bang the empty outlet. The left array is copied out "
      "under its guard and the result built against the right under that guard alone, so no "
      "two guards are ever held at once and \".array.concat seq seq\" answers the array "
      "doubled. A result that outruns what a cord carries is refused whole and counted — a "
      "partial concatenation would be truncation by another name.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger", kConcatTriggerInletDoc, "");
  INLET_DOC(1, "right reference", kConcatRightInletDoc, "");
  OUTLET_DOC(0, "concatenation",
             "The left array's elements followed by the right's, as the list they spell — one "
             "element as the int, float or symbol it is, several as list text, never as a new "
             "named array. Everything kept: repeats included, order preserved, no thinning. A "
             "result that outruns what a cord carries is refused whole and counted — a partial "
             "concatenation would be truncation by another name. Silent when both arrays are "
             "empty: the empty outlet carries that half of the answer.",
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
      "Joins an array's elements into one symbol — Max's array.join on the name-addressed "
      "value model .array settled: the array is bound from the first creation argument, "
      "\".array.join <name> <separator>\", because an array is addressed by name and never "
      "passed down a cord. Max's \"join the elements of an array together to form a string. "
      "The optional separator string will be placed between each element\" — the separator is "
      "the second creation argument, a single token, empty (Max's default) when absent. The "
      "joined text is one token, sent straight out the outlet and typed the way the patcher "
      "spells it — SendAtom's rule, so \"1 2\" joined by nothing leaves as the int 12 and "
      "\"c4 e4\" joined by \"-\" as the symbol c4-e4. It is a message down a cord, not an "
      "element: bounded by what a cord carries, never by the store's 64-character element "
      "rule — a patch that pushes it back into an .array meets that bound at the push, where "
      "it belongs. A result past the cord's ceiling is refused whole and counted. The whole "
      "join happens under one hold of the store's guard, so the answer is the array as it "
      "stood at the trigger; an empty or unnamed (private) array bangs the empty outlet.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger",
            "A bang asks for the joined text — every element in order, the separator between "
            "each pair, built under one hold of the store's guard and sent after it is "
            "released. \"array <name>\" does the same when it names the array bound by the "
            "creation argument — the message an .array's reference outlet emits on a bang, the "
            "family's gesture. A reference naming anything else, or any other message, is "
            "refused and counted rather than logged, since this inlet may be the audio thread.",
            "");
  INLET_DOC(1, "array reference",
            "\"array <name>\" is accepted silently when it names the array bound by the "
            "creation argument, so a patch may wire the array's reference outlet across. It "
            "sets nothing — the binding is the creation argument, resolved on the control "
            "thread — and anything else is refused and counted.",
            "");
  OUTLET_DOC(0, "joined",
             "The elements joined into one token, the separator between each pair, typed the "
             "way the patcher spells it — an int or a float when the joined text reads as one "
             "(SendAtom's rule), a symbol otherwise. A single-element array answers that "
             "element alone, separator unused. A result that outruns what a cord carries is "
             "refused whole and counted — a partial join would be truncation. Silent on an "
             "empty array: the empty outlet carries that half of the answer.",
             "");
  OUTLET_DOC(1, "empty",
             "Bang when the array holds nothing to join — an empty or unnamed (private) "
             "array. \"No data\" is a state a patch must be able to route on, not an error. A "
             "lost try-lock is a counted refusal instead: the array's state is unknown, so "
             "neither outlet fires.",
             "");
  PARAM_DOC("name", "",
            "The array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an "
            ".array of the same name in this patcher holds. Resolved once, on the control "
            "thread, which is why no message re-points it at run time. Empty reads a private, "
            "empty array: every ask bangs the empty outlet.",
            "any identifier");
  PARAM_DOC("separator", "",
            "The token placed between each pair of elements — Max's optional separator "
            "string, empty (elements butted together) when absent. A single token: a "
            "creation argument cannot contain a space, and space-joined elements are exactly "
            "the list text .array's own getvalue already emits.",
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
