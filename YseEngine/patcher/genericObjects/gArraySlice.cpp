#include "gArraySlice.h"
#include "../math/gExprEval.h"
#include "../pObjectList.hpp"

#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

namespace {

  // Doc strings shared by the four objects, hoisted out of the constructors
  // because they are long enough that a constructor stops being readable with
  // them inline — gArray's arrangement. What differs per object stays with
  // that object below.
  constexpr char kRangeTriggerInletDoc[] =
      "A bang asks for the piece — the range between the stored bounds, collected from the "
      "array as it stood at the trigger under one hold of the store's guard and sent after it "
      "is released. \"array <name>\" does the same when it names the array bound by the "
      "creation argument — the message an .array's reference outlet emits on a bang, so wiring "
      "that outlet here gives the family's gesture: bang the array, out comes the piece. A "
      "reference naming anything else, or any other message, is refused and counted rather "
      "than logged, since this inlet may be the audio thread.";
  constexpr char kReferenceInletDoc[] =
      "\"array <name>\" is accepted silently when it names the array bound by the creation "
      "argument, so a patch may wire the array's reference outlet across. It sets nothing — "
      "the binding is the creation argument, resolved on the control thread — and anything "
      "else is refused and counted.";
  constexpr char kStartInletDoc[] =
      "An int stores the start of the range, silently — the cold half of the Max idiom: "
      "configuration on the right, the ask on the left. Zero-based; a start past the last "
      "element selects nothing, so the next ask bangs the empty outlet. A float truncates to "
      "an int first. A negative or non-finite value is refused and counted, and the stored "
      "start does not move — the family's indexing rule: Max's negative from-the-end indexing "
      "is the wrap the base type refuses.";
  constexpr char kEmptyOutletDoc[] =
      "Bang when the ask selected nothing — an empty or unnamed (private) array, a start past "
      "the last element, or a crossed exclusive range. \"No data\" is a state a patch must be "
      "able to route on, not an error, and an out-of-range fetch is a miss on an outlet — the "
      "family's fetch rule. A lost try-lock is a counted refusal instead: the array's state is "
      "unknown, so neither outlet fires.";
  constexpr char kNameParamDoc[] =
      "The array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an .array "
      "of the same name in this patcher holds. Resolved once, on the control thread, which is "
      "why no message re-points it at run time. Empty reads a private, empty array: every ask "
      "bangs the empty outlet.";
  constexpr char kStartParamDoc[] =
      "The initial stored start of the range — where the piece begins before any int has "
      "moved it. Zero-based, exactly as every bound the inlets take.";
  constexpr char kInclusiveEndInletDoc[] =
      "An int stores the end of the range, silently — the inclusive bound, so the element at "
      "this position is part of the piece. Any bound at or past the last element means to the "
      "end of the array; an end before the start asks for the piece reversed — Max's "
      "\"reverse slices are permitted\". A float truncates to an int first; a negative or "
      "non-finite value is refused and counted, and the stored end does not move.";
  constexpr char kInclusiveEndParamDoc[] =
      "The initial stored end of the range, inclusive. Absent means to the end of the array — "
      "and so does any bound at or past the last element. An end before the start asks for "
      "the piece reversed.";
  constexpr char kInclusivePieceOutletDoc[] =
      "The piece: the elements from start to end inclusive, as the list they spell — one "
      "element as the int, float or symbol it is, several as list text, never as a new named "
      "array (creating one would resolve a name on a message path). An end before the start "
      "leaves the piece reversed, walking from start toward end. Collected under one hold of "
      "the store's guard, sent after it is released; the intersection with the live elements, "
      "so a bound past the end means the end. Silent when the range selects nothing: the "
      "empty outlet carries that half of the answer.";

} // namespace

// ─── the shared base ──────────────────────────────────────────────────────────

#define className gArraySliceBase

gArraySliceBase::gArraySliceBase(bool inclusive) : gArrayEndsBase(), inclusiveEnd(inclusive) {
  // The trigger inlet, hot; the start, end and reference inlets, cold —
  // .array.insert's arrangement, the Max idiom kept: configuration on the
  // right, the ask on the left. The ask is a bang or the array's own
  // reference, never a number — the piece is asked for, not addressed.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  ADD_IN_2;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  ADD_IN_3;
  REG_LIST_IN(ListIn);

  // ANY on the piece outlet: one element leaves as the atom it spells,
  // several as list text — SendAtoms' rule. The empty outlet is always a
  // bang.
  ADD_OUT_ANY;
  ADD_OUT_BANG;

  // After gArrayEndsBase's name — the second and third creation arguments.
  ADD_PARAM(rangeStart);
  ADD_PARAM(rangeEnd);

  // The allocation the send path would otherwise need, taken here on the
  // control thread.
  AtomList::ReserveRender(emitScratch);
}

// A re-parse must not leave half of the previous configuration standing:
// SetParams("") — and the clear half of every Set() — drops the stored bounds
// back to the whole array along with the name. gArrayPositionBase's rule.
PARM_CLEAR() {
  rangeStart.store(0, std::memory_order_relaxed);
  rangeEnd.store(OPEN_END, std::memory_order_relaxed);
  gArrayEndsBase::ClearParams();
}

BANG_IN(BangIn) {
  (void)inlet;
  Ask(thread);
}

INT_IN(IntIn) {
  // A negative bound is refused whole — the stored bound does not move
  // either, so an ask after the refusal cuts what it would have cut before
  // it. arrayStore's indexing rule, decided once for the family.
  if (value < 0) {
    Refuse();
    return;
  }
  if (inlet == 1) {
    rangeStart.store(value, std::memory_order_relaxed);
    return;
  }
  rangeEnd.store(value, std::memory_order_relaxed);
}

FLOAT_IN(FloatIn) {
  // Max's float method on an int attribute is "convert to int", .table's
  // precedent. The range test keeps a NaN or an infinity — which ExprToInt
  // folds to 0 — from quietly becoming a bound of 0.
  if (!(value >= 0.f && value < 2147483648.f)) {
    Refuse();
    return;
  }
  IntIn(ExprToInt(value), inlet, thread);
}

LIST_IN(ListIn) {
  if (inlet == 3) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — the binding is the creation argument, resolved on the
    // control thread, so there is nothing to set. gDictSlice's inlet rule.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The array's reference asks for the piece — the message its .array emits
  // on a bang, the family's gesture. Anything else, including a reference
  // naming an array this object is not bound to, is refused: resolving an
  // unrecognised name means the registry's mutex, and this may be the audio
  // thread.
  if (ArrayReferenceNames(value, arrayName)) {
    Ask(thread);
    return;
  }
  Refuse();
}

void gArraySliceBase::Ask(YSE::THREAD thread) {
  // A negative stored bound — which only a creation argument can plant, the
  // inlets refuse one before storing it — is a refusal, not a miss: negative
  // is malformed, where an empty piece is a well-formed range the array
  // happens not to cover.
  const int startRaw = rangeStart.load(std::memory_order_relaxed);
  const int endRaw = rangeEnd.load(std::memory_order_relaxed);
  if (startRaw < 0 || endRaw < 0) {
    Refuse();
    return;
  }

  bool collected = false;
  piece.Clear();
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    collected = CollectLocked(static_cast<std::size_t>(startRaw), static_cast<std::size_t>(endRaw));
  }

  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well write into this same array, and inside the guard that
  // write would be the one thing the try-lock drops.
  if (!collected) {
    // The one refusal a well-formed ask can still meet: the piece spells
    // more list text than a cord carries. Refused whole — a partial piece
    // would be truncation by another name, .array.at's whole-reply rule.
    Refuse();
    return;
  }
  if (piece.Empty()) {
    outputs[1].SendBang(thread);
    return;
  }
  SendAtoms(outputs[0], piece, emitScratch, thread);
}

bool gArraySliceBase::CollectLocked(std::size_t start, std::size_t end) {
  const std::size_t size = store->count;

  if (!inclusiveEnd) {
    // JS's [start, end), end exclusive. An end of 0 extends to the end of
    // the array — Max's documented extra over the JS form — and so does any
    // bound past it; a crossed range selects nothing, "reverse slices are
    // not permitted".
    const std::size_t stop = (end == 0 || end > size) ? size : end;
    for (std::size_t i = start; i < stop; i++) {
      const std::string& element = store->elements[i];
      if (!piece.Add(element.data(), element.size())) return false;
    }
    return true;
  }

  if (size == 0) return true;

  if (start <= end) {
    // The inclusive [start, end], intersected with the live elements — a
    // bound past the last element means the end, .zl slice's "the final
    // list may be shorter than specified".
    if (start >= size) return true;
    const std::size_t last = (end < size) ? end : size - 1;
    for (std::size_t i = start; i <= last; i++) {
      const std::string& element = store->elements[i];
      if (!piece.Add(element.data(), element.size())) return false;
    }
    return true;
  }

  // The reversed piece Max permits: an end before the start walks from start
  // toward end, so the elements leave in descending position order. A start
  // past the last element begins at the last element — the same
  // intersection, walked the other way.
  if (end >= size) return true;
  const std::size_t first = (start < size) ? start : size - 1;
  for (std::size_t i = first;; i--) {
    const std::string& element = store->elements[i];
    if (!piece.Add(element.data(), element.size())) return false;
    if (i == end) break;
  }
  return true;
}

// ─── .array.slice ─────────────────────────────────────────────────────────────

#undef className
#define className gArraySlice

gArraySlice::gArraySlice() : gArraySliceBase(false) {
  ADD_DESCRIPTION(
      "Outputs a run of elements of an array — Max's array.slice on the name-addressed value "
      "model .array settled: the array is bound from the creation argument, \".array.slice "
      "<name> [<start> [<end>]]\", because an array is addressed by name and never passed down "
      "a cord. JavaScript's Array.slice(): the piece runs from start to end with the end "
      "excluded, an absent end — or 0, Max's documented extra — extends to the end of the "
      "array, and a crossed range selects nothing: reverse slices are not permitted, "
      ".array.subarray is the form that permits them. The piece leaves as the list it spells, "
      "never as a new named array — creating one would resolve a name on a message path — and "
      "an ask that selects nothing bangs the empty outlet. Bounds are zero-based; past the end "
      "they are bounds of a range, so the piece is the intersection with the live elements, "
      "and negative is refused — the family's indexing rule. Collected under one hold of the "
      "store's guard, so the piece is the array as it stood at the trigger.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger", kRangeTriggerInletDoc, "");
  INLET_DOC(1, "start", kStartInletDoc, "0-255");
  INLET_DOC(2, "end",
            "An int stores the end of the range, silently — the exclusive bound, JS's rule: "
            "the element at this position is not part of the piece. 0 extends the piece to the "
            "end of the array — Max's documented extra — and so does any bound past it. An end "
            "at or before the start selects nothing: reverse slices are not permitted, "
            ".array.subarray is the form that permits them. A float truncates to an int first; "
            "a negative or non-finite value is refused and counted, and the stored end does "
            "not move.",
            "0-256");
  INLET_DOC(3, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "piece",
             "The piece: the elements from start up to but not including end, as the list they "
             "spell — one element as the int, float or symbol it is, several as list text, "
             "never as a new named array. Collected under one hold of the store's guard, sent "
             "after it is released; the intersection with the live elements, so a bound past "
             "the end means the end. Silent when the range selects nothing: the empty outlet "
             "carries that half of the answer.",
             "");
  OUTLET_DOC(1, "empty", kEmptyOutletDoc, "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
  PARAM_DOC("start", "0", kStartParamDoc, "0-255");
  PARAM_DOC("end", "",
            "The initial stored end of the range, exclusive. Absent — or 0, Max's documented "
            "extra — means to the end of the array, and so does any bound past it.",
            "0-256");
}

// ─── .array.subarray ──────────────────────────────────────────────────────────

#undef className
#define className gArraySubarray

gArraySubarray::gArraySubarray() : gArraySliceBase(true) {
  ADD_DESCRIPTION(
      "Outputs a sub-range of an array, both ends inclusive — Max's array.subarray on the "
      "name-addressed value model .array settled: the array is bound from the creation "
      "argument, \".array.subarray <name> [<start> [<end>]]\", because an array is addressed "
      "by name and never passed down a cord. The non-JS form of .array.slice, Max's own "
      "distinction: the piece runs from start to end with the end included, an absent end — or "
      "any bound at or past the last element — extends to the end of the array, and reverse "
      "slices are permitted: an end before the start emits the piece reversed, walking from "
      "start toward end. The piece leaves as the list it spells, never as a new named array, "
      "and an ask that selects nothing bangs the empty outlet. Bounds are zero-based; past the "
      "end they are bounds of a range, so the piece is the intersection with the live "
      "elements, and negative is refused — the family's indexing rule. Collected under one "
      "hold of the store's guard, so the piece is the array as it stood at the trigger. "
      ".array.sub is this object under Max's second name for it.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger", kRangeTriggerInletDoc, "");
  INLET_DOC(1, "start", kStartInletDoc, "0-255");
  INLET_DOC(2, "end", kInclusiveEndInletDoc, "0-255");
  INLET_DOC(3, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "piece", kInclusivePieceOutletDoc, "");
  OUTLET_DOC(1, "empty", kEmptyOutletDoc, "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
  PARAM_DOC("start", "0", kStartParamDoc, "0-255");
  PARAM_DOC("end", "", kInclusiveEndParamDoc, "0-255");
}

// ─── .array.sub ───────────────────────────────────────────────────────────────

#undef className
#define className gArraySub

gArraySub::gArraySub() : gArraySliceBase(true) {
  ADD_DESCRIPTION(
      "Outputs a sub-range of an array, both ends inclusive — .array.subarray under Max's "
      "second name for it: the array.sub reference page is array.subarray's verbatim, so the "
      "port keeps both spellings over one implementation, .array.scramble / .array.shuffle's "
      "arrangement. The array is bound from the creation argument, \".array.sub <name> "
      "[<start> [<end>]]\"; the piece runs from start to end inclusive, an absent end extends "
      "to the end of the array, an end before the start emits the piece reversed, and an ask "
      "that selects nothing bangs the empty outlet. The piece leaves as the list it spells, "
      "never as a new named array; bounds are zero-based, intersected with the live elements "
      "past the end, and negative is refused — the family's indexing rule. Collected under one "
      "hold of the store's guard, so the piece is the array as it stood at the trigger.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger", kRangeTriggerInletDoc, "");
  INLET_DOC(1, "start", kStartInletDoc, "0-255");
  INLET_DOC(2, "end", kInclusiveEndInletDoc, "0-255");
  INLET_DOC(3, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "piece", kInclusivePieceOutletDoc, "");
  OUTLET_DOC(1, "empty", kEmptyOutletDoc, "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
  PARAM_DOC("start", "0", kStartParamDoc, "0-255");
  PARAM_DOC("end", "", kInclusiveEndParamDoc, "0-255");
}

// ─── .array.split ─────────────────────────────────────────────────────────────

#undef className
#define className gArraySplit

gArraySplit::gArraySplit() : gArrayEndsBase() {
  // The trigger inlet, hot; the position inlet and the reference inlet, cold
  // — .array.insert's arrangement, the Max idiom kept.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  ADD_IN_2;
  REG_LIST_IN(ListIn);

  // ANY on both piece outlets: one element leaves as the atom it spells,
  // several as list text, and an empty half says nothing at all — SendAtoms'
  // rule, which is what lets a recursive head/tail patch terminate by
  // absence.
  ADD_OUT_ANY;
  ADD_OUT_ANY;

  // After gArrayEndsBase's name — the second creation argument.
  ADD_PARAM(position);

  // The allocation the send path would otherwise need, taken here on the
  // control thread.
  AtomList::ReserveRender(emitScratch);

  ADD_DESCRIPTION(
      "Splits an array in two at a position — Max's array.split on the name-addressed value "
      "model .array settled: the array is bound from the creation argument, \".array.split "
      "<name> [<position>]\", because an array is addressed by name and never passed down a "
      "cord. The head/tail split every recursive patch needs: elements before the split "
      "position leave the head outlet, elements at and after it leave the tail outlet — the "
      "element at the position goes to the tail, .zl slice's rule, so the position reads as "
      "how many elements the head takes. Both pieces leave as the lists they spell, never as "
      "new named arrays, collected under one hold of the store's guard so the two halves are "
      "two views of one moment, and sent tail before head — right-to-left, the patcher's "
      "outlet order. The position is a boundary, not an element position: 0 puts everything in "
      "the tail, the length or past it puts everything in the head, and an empty half says "
      "nothing at all, so a recursive patch terminates by absence. A negative position is "
      "refused — the family's indexing rule. A bang splits at the stored position, seeded by "
      "the second creation argument and moved silently by the position inlet.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger",
            "A bang splits the array at the stored position — the position the last int on "
            "the position inlet stored, seeded by the second creation argument (0 when "
            "absent): the head leaves the head outlet, the tail the tail outlet, tail sent "
            "first. \"array <name>\" does the same when it names the array bound by the "
            "creation argument — the message an .array's reference outlet emits on a bang, "
            "the family's gesture. A reference naming anything else, or any other message, is "
            "refused and counted rather than logged, since this inlet may be the audio "
            "thread.",
            "");
  INLET_DOC(1, "position",
            "An int stores the position the next split cuts at, silently — the cold half of "
            "the Max idiom. A boundary, not an element position: zero-based, 0 puts "
            "everything in the tail, the array's length or past it puts everything in the "
            "head. A float truncates to an int first; a negative or non-finite value is "
            "refused and counted, and the stored position does not move.",
            "0-256");
  INLET_DOC(2, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "head",
             "The elements before the split position, as the list they spell — one element as "
             "the int, float or symbol it is, several as list text. Collected under the same "
             "guard hold as the tail, so the two halves are two views of one moment, and sent "
             "after the tail — right-to-left, the patcher's outlet order. An empty head — a "
             "split at 0, or an empty array — says nothing at all.",
             "");
  OUTLET_DOC(1, "tail",
             "The elements at and after the split position, as the list they spell — the "
             "element at the position goes here, .zl slice's rule. Sent before the head — "
             "right-to-left, the patcher's outlet order. An empty tail — a split at or past "
             "the array's length, or an empty array — says nothing at all, which is what lets "
             "a recursive head/tail patch terminate by absence.",
             "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
  PARAM_DOC("position", "0",
            "The initial stored split position — where the cut falls before any int has moved "
            "it. A boundary, not an element position: how many elements the head takes.",
            "0-256");
}

// A re-parse must not leave half of the previous configuration standing:
// SetParams("") — and the clear half of every Set() — drops the stored
// position back to 0 along with the name. gArrayPositionBase's rule.
PARM_CLEAR() {
  position.store(0, std::memory_order_relaxed);
  gArrayEndsBase::ClearParams();
}

BANG_IN(BangIn) {
  (void)inlet;
  Split(thread);
}

INT_IN(IntIn) {
  (void)inlet;
  // Stored silently — the cold half of the Max idiom. Negative is refused
  // whole and the stored position does not move: a boundary is still a
  // count from the front, the family's indexing rule.
  if (value < 0) {
    Refuse();
    return;
  }
  position.store(value, std::memory_order_relaxed);
}

FLOAT_IN(FloatIn) {
  // Max's float method on an int attribute is "convert to int", .table's
  // precedent. The range test keeps a NaN or an infinity — which ExprToInt
  // folds to 0 — from quietly becoming position 0.
  if (!(value >= 0.f && value < 2147483648.f)) {
    Refuse();
    return;
  }
  IntIn(ExprToInt(value), inlet, thread);
}

LIST_IN(ListIn) {
  if (inlet == 2) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — gDictSlice's inlet rule.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The array's reference splits at the stored position — the message its
  // .array emits on a bang, the family's gesture. Anything else is refused:
  // resolving an unrecognised name means the registry's mutex, and this may
  // be the audio thread.
  if (ArrayReferenceNames(value, arrayName)) {
    Split(thread);
    return;
  }
  Refuse();
}

void gArraySplit::Split(YSE::THREAD thread) {
  // A negative stored position — which only a creation argument can plant,
  // the inlet refuses one before storing it — is a refusal: malformed, the
  // family's rule.
  const int posRaw = position.load(std::memory_order_relaxed);
  if (posRaw < 0) {
    Refuse();
    return;
  }
  const std::size_t boundary = static_cast<std::size_t>(posRaw);

  bool collected = true;
  head.Clear();
  tail.Clear();
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    // Both halves under one hold, so they are two views of one moment — no
    // other thread can renumber between the head's collection and the
    // tail's. The boundary intersects with the live elements: past the end
    // it is the end.
    const std::size_t size = store->count;
    const std::size_t at = (boundary < size) ? boundary : size;
    for (std::size_t i = 0; i < size && collected; i++) {
      const std::string& element = store->elements[i];
      AtomList& half = (i < at) ? head : tail;
      collected = half.Add(element.data(), element.size());
    }
  }

  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well write into this same array, and inside the guard that
  // write would be the one thing the try-lock drops.
  if (!collected) {
    // A half that outruns what a cord carries refuses the whole split — one
    // counted refusal and neither outlet fires. A head without its tail
    // would misrepresent the array as shorter than it is.
    Refuse();
    return;
  }
  // Tail before head — right-to-left, the patcher's outlet order and .zl
  // slice's arrangement — and an empty half says nothing, SendAtoms' rule.
  SendAtoms(outputs[1], tail, emitScratch, thread);
  SendAtoms(outputs[0], head, emitScratch, thread);
}
