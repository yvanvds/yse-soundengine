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
      "Bang, or the bound array's reference \"array <name>\": send the elements in the range.";
  constexpr char kReferenceInletDoc[] =
      "Accepts the bound array's reference \"array <name>\" and ignores it, so the array's "
      "reference outlet can be wired here. Anything else is refused.";
  constexpr char kStartInletDoc[] =
      "Sets the first position of the range. A float is truncated; a negative value is "
      "refused.";
  constexpr char kEmptyOutletDoc[] =
      "Bang when the range holds no elements, for example when the array is empty or the start "
      "is past its end.";
  constexpr char kNameParamDoc[] =
      "Name of the array to read: the .array with this name in the same patcher. Empty uses a "
      "private, empty array.";
  constexpr char kStartParamDoc[] =
      "The first position of the range before one arrives on the start inlet.";
  constexpr char kInclusiveEndInletDoc[] =
      "Sets the last position of the range, which is included. Past the end of the array "
      "means the end; before the start sends the range reversed. A float is truncated; a "
      "negative value is refused.";
  constexpr char kInclusiveEndParamDoc[] =
      "The last position of the range (included). Empty, or past the end of the array, means "
      "the end. Before the start sends the range reversed.";
  constexpr char kInclusivePieceOutletDoc[] =
      "The elements from start to end, both included, as one list; reversed when the end is "
      "before the start. A single element leaves as an int, float or symbol.";

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
      "Outputs a range of elements of a named array, from a start position up to but not "
      "including an end position. The array itself is not changed.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kRangeTriggerInletDoc, "");
  INLET_DOC(1, "start", kStartInletDoc, "0-255");
  INLET_DOC(2, "end",
            "Sets the position where the range stops; that element is not included. 0, or past "
            "the end of the array, means the end. A float is truncated; a negative value is "
            "refused.",
            "0-256");
  INLET_DOC(3, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "piece",
             "The elements from start up to, but not including, end, as one list. A single "
             "element leaves as an int, float or symbol.",
             "");
  OUTLET_DOC(1, "empty", kEmptyOutletDoc, "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
  PARAM_DOC("start", "0", kStartParamDoc, "0-255");
  PARAM_DOC("end", "",
            "The position where the range stops (not included). Empty or 0 means the end of "
            "the array.",
            "0-256");
}

// ─── .array.subarray ──────────────────────────────────────────────────────────

#undef className
#define className gArraySubarray

gArraySubarray::gArraySubarray() : gArraySliceBase(true) {
  ADD_DESCRIPTION(
      "Outputs a range of elements of a named array, both ends included; with the end before "
      "the start, the range is sent in reverse. The same object as .array.sub.");
  ADD_CATEGORY(pCategory::ARRAY);
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
      "Outputs a range of elements of a named array, both ends included; with the end before "
      "the start, the range is sent in reverse. The same object as .array.subarray.");
  ADD_CATEGORY(pCategory::ARRAY);
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
      "Splits a named array in two at a position: the elements before it leave the left "
      "outlet and the rest leave the right outlet, right first. The array itself is not "
      "changed.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger",
            "Bang, or the bound array's reference \"array <name>\": split at the stored "
            "position.",
            "");
  INLET_DOC(1, "position",
            "Sets the split position: the number of elements in the left half. A float is "
            "truncated; a negative value is refused.",
            "0-256");
  INLET_DOC(2, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "head",
             "The elements before the split position, sent after the right half. Nothing is "
             "sent when this half is empty.",
             "");
  OUTLET_DOC(1, "tail",
             "The elements from the split position on, sent first. Nothing is sent when this "
             "half is empty.",
             "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
  PARAM_DOC("position", "0",
            "The split position before one arrives on the middle inlet: the number of elements "
            "in the left half.",
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
