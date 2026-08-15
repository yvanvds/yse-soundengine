#include "gArrayWrap.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"

#include <cstddef>
#include <string>

using namespace YSE::PATCHER;
#define className gArrayWrap

namespace {

  // True when nothing but whitespace is left from `offset` on. gArrayAt's
  // reader; not shared, for the reason gArray.cpp's ElementToJson gives —
  // exporting a file-local helper out of a shipped object costs more than the
  // repetition.
  bool AtEnd(const std::string& text, std::size_t offset) {
    for (std::size_t i = offset; i < text.size(); i++) {
      if (!IsSelectorSeparator(text[i])) return false;
    }
    return true;
  }

  // Doc strings, hoisted out of the constructor because they are long enough
  // that it stops being readable with them inline — gArray's arrangement.
  constexpr char kIndexInletDoc[] =
      "An int fetches the element at that position modulo the array's length and stores the "
      "index; a bang re-fetches at the stored index; a float truncates to an int first — Max's "
      "float method. A negative index counts from the end — -1 the last element — and keeps "
      "wrapping past that; where .array.at refuses a negative, here it is the point. A list of "
      "indices fetches them all as one list, in the order asked, every position wrapped "
      "independently, and never moves the stored index. \"array <name>\" fetches at the stored "
      "index when it names the array bound by the first creation argument — the message an "
      ".array's reference outlet emits on a bang, so wiring that outlet here gives the family's "
      "gesture. A non-finite float, a list with anything in it that is not an integer, a "
      "reference naming anything else, or any other message is refused and counted rather than "
      "logged, since this inlet may be the audio thread.";
  constexpr char kReferenceInletDoc[] =
      "\"array <name>\" is accepted silently when it names the array bound by the first "
      "creation argument, so a patch may wire the array's reference outlet across. It sets "
      "nothing — the binding is the creation argument, resolved on the control thread — and "
      "anything else is refused and counted.";
  constexpr char kElementDoc[] =
      "The fetched element, typed the way the patcher spells it: a numeric element leaves as an "
      "int or a float by its spelling and anything else as a symbol. A list of indices leaves "
      "as one list of the named elements, in the order asked — every position wrapped against "
      "the length and read under one hold of the store's guard, so the reply is the array as it "
      "stood at the trigger.";
  constexpr char kEmptyDoc[] =
      "Bang when a fetch finds the array empty — there is nothing to wrap onto: a modulus of "
      "zero names no position, so the miss the wrapping removed everywhere else survives "
      "exactly here. An unnamed (private) array is always empty. One bang even for a list "
      "fetch — no partial reply. A lost try-lock is a counted refusal instead: the array's "
      "state is unknown, so neither outlet fires.";

} // namespace

gArrayWrap::gArrayWrap() : gArrayEndsBase() {
  // The index inlet, hot, and the reference inlet, cold — .array.at's
  // arrangement, kept so a patch can swap the strict fetch for the wrapping
  // one without rewiring.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_LIST_IN(ListIn);

  // ANY on the element outlet: what leaves it is an int, a float, a symbol or
  // a list depending on what was stored and how much was asked for. The empty
  // outlet is always a bang.
  ADD_OUT_ANY;
  ADD_OUT_BANG;

  // After gArrayEndsBase's name — the second creation argument.
  ADD_PARAM(index);

  // The allocation the send path would otherwise need, taken here on the
  // control thread.
  AtomList::ReserveRender(emitScratch);

  ADD_DESCRIPTION(
      "Outputs the element at an index with wrapping — Max's array.wrap on the name-addressed "
      "value model .array settled: the array is bound from the creation argument, \".array.wrap "
      "<name> [<index>]\", because an array is addressed by name and never passed down a cord. "
      "The family treats an index as a position — out of range is a miss on .array and "
      ".array.at, negative a refusal — and this is the object that provides the alternative: "
      "every index lands, taken modulo the length, so 5 into a three-element array reads "
      "position 2 and -1 reads the last element — the modulo addressing a sequencer does every "
      "bar. An int fetches at that wrapped position and stores the index, a bang re-fetches at "
      "the stored index, a float truncates to an int first, and a list of indices is answered "
      "whole, as one list in the order asked, every position wrapped independently against the "
      "length one hold of the store's guard read. The element leaves typed the way the patcher "
      "spells it. An empty or unnamed (private) array bangs the empty outlet instead — there is "
      "nothing to wrap onto — and the stored index is this object's own: two .array.wrap on one "
      "name fetch independently.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "index", kIndexInletDoc, "any int");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "element", kElementDoc, "");
  OUTLET_DOC(1, "empty", kEmptyDoc, "");
  PARAM_DOC("name", "",
            "The array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an "
            ".array of the same name in this patcher holds. Resolved once, on the control "
            "thread, which is why no message re-points it at run time. Empty reads a private, "
            "empty array: every fetch bangs the empty outlet.",
            "any identifier");
  PARAM_DOC("index", "0",
            "The initial stored index — the position a bang fetches before any int has moved "
            "it. Any sign: it wraps against the length at the moment of the fetch, so -1 is "
            "the last element.",
            "any int");
}

// A re-parse must not leave half of the previous configuration standing:
// SetParams("") drops the index back to 0 along with the name.
// gArrayPositionBase's rule.
PARM_CLEAR() {
  index.store(0, std::memory_order_relaxed);
  gArrayEndsBase::ClearParams();
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  FetchAtIndex(thread);
}

INT_IN(IntIn) {
  (void)inlet;
  // Any sign stores and fetches — a negative index is a position from the
  // end here, the whole reason this object exists beside .array.at.
  index.store(value, std::memory_order_relaxed);
  Fetch(&value, 1, thread);
}

FLOAT_IN(FloatIn) {
  (void)inlet;
  // Max's float method on an int attribute is "convert to int", .table's
  // precedent. The negated range test keeps a NaN or an infinity — which
  // ExprToInt folds to 0 — from quietly fetching element 0.
  if (!(value >= -2147483648.f && value < 2147483648.f)) {
    Refuse();
    return;
  }
  IntIn(ExprToInt(value), inlet, thread);
}

LIST_IN(ListIn) {
  if (inlet == 1) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — the binding is the creation argument, resolved on the
    // control thread, so there is nothing to set. gDictSlice's inlet rule.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The array's reference fetches at the stored index — the message its
  // .array emits on a bang. Anything else naming an array this object is not
  // bound to is refused: resolving an unrecognised name means the registry's
  // mutex, and this may be the audio thread.
  if (ArrayReferenceNames(value, arrayName)) {
    FetchAtIndex(thread);
    return;
  }

  // A list of indices, answered whole. Parsed before the guard is taken;
  // refused whole — nothing fetched, nothing sent — when anything in it is
  // not an integer, or there are more indices than a reply could carry. Any
  // sign reads: every position wraps. The stored index deliberately does not
  // move: a list is a compound fetch answered at the moment it arrives, not
  // a cursor move — .array.at's rule.
  std::size_t count = 0;
  std::size_t offset = 0;
  while (!AtEnd(value, offset)) {
    if (count >= MAX_INDICES || !ReadIntArgAt(value, offset, requested[count])) {
      Refuse();
      return;
    }
    count++;
  }
  if (count == 0) {
    Refuse();
    return;
  }
  Fetch(requested, count, thread);
}

void gArrayWrap::FetchAtIndex(YSE::THREAD thread) {
  // Unlike .array.at, no sign check: any stored index — the inlet's or a
  // creation argument's — wraps onto a position.
  const int at = index.load(std::memory_order_relaxed);
  Fetch(&at, 1, thread);
}

void gArrayWrap::Fetch(const int* indices, std::size_t count, YSE::THREAD thread) {
  bool empty = false;
  bool refused = false;
  emitList.Clear();
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    const long long size = static_cast<long long>(store->count);
    if (size == 0) {
      // Nothing to wrap onto: a modulus of zero names no position. The one
      // miss the wrapping cannot remove.
      empty = true;
    } else {
      for (std::size_t i = 0; i < count && !refused; i++) {
        // The wrap itself, in a wider type so INT_MIN survives the negation:
        // C++'s % truncates toward zero, so a negative remainder is one
        // length below the position it names.
        long long wrapped = static_cast<long long>(indices[i]) % size;
        if (wrapped < 0) wrapped += size;
        const std::string& element = store->elements[static_cast<std::size_t>(wrapped)];
        // The one refusal a well-formed fetch can still meet: the named
        // elements together spell more list text than a cord carries.
        // Refused whole — a partial reply would misalign every position
        // after the cut, which is truncation by another name.
        if (!emitList.Add(element.data(), element.size())) refused = true;
      }
    }
  }

  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well write into this same array, and inside the guard that
  // write would be the one thing the try-lock drops.
  if (empty) {
    outputs[1].SendBang(thread);
    return;
  }
  if (refused) {
    Refuse();
    return;
  }
  // Through SendAtoms, so one element leaves as the int, float or symbol it
  // spells rather than as a list of one — the patcher's transport rule.
  SendAtoms(outputs[0], emitList, emitScratch, thread);
}
