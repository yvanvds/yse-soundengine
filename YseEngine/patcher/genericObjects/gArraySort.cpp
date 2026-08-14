#include "gArraySort.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"

#include <cstddef>
#include <cstring>
#include <string>

using namespace YSE::PATCHER;

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

} // namespace

#define className gArraySort

gArraySort::gArraySort() : gArrayPermuteBase() {
  // The trigger inlet, hot; the direction inlet and the reference inlet,
  // cold — .array.rotate's arrangement with a direction where the amount
  // was.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  ADD_IN_2;
  REG_LIST_IN(ListIn);

  // The reference, then the applied order — both list text. The order is
  // sent first (Max's right-to-left rule); see the class notes for the
  // .array.indexmap idiom that ordering exists for.
  ADD_OUT_LIST;
  ADD_OUT_LIST;

  // After gArrayEndsBase's name — the second creation argument.
  ADD_PARAM(direction);

  // The allocation the order send would otherwise need, taken here on the
  // control thread.
  AtomList::ReserveRender(orderRender);

  ADD_DESCRIPTION(
      "Orders an array's elements — Max's array.sort on the name-addressed value model .array "
      "settled: the array is bound from the creation argument, \".array.sort <name> "
      "[<direction>]\", because an array is addressed by name and never passed down a cord. The "
      "comparison is .zl sort's, applied to the store: each element is classified once per sort "
      "by the patcher's own token reader, numbers come before symbols in both directions (a "
      "type ordering, so ascending and descending stay one question asked two ways), numbers "
      "compare by value — 7 and 7. are the same number here, spelling deciding how an element "
      "leaves, never where it sorts — and symbols compare by their characters, the shorter "
      "first when one is a prefix. The sort is stable, so equal elements keep their arrival "
      "order, and it is a bounded merge sort under one hold of the store's guard through a "
      "scratch table the object owns. Negative direction sorts descending, anything else "
      "ascending; a bang sorts by the stored direction, seeded by the second creation argument "
      "and moved silently by the direction inlet, and an int on the trigger sorts by that "
      "direction at the moment it arrives, storing nothing. A sort that lands publishes the "
      "applied order as zero-based indices out the order outlet, then emits the array's "
      "reference: feed the order to an .array.indexmap's map inlet and the reference to its "
      "trigger, and a parallel array lands in the same new order, .zl sort's idiom.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger",
            "A bang sorts the bound array in place by the stored direction — the direction the "
            "last int on the direction inlet stored, seeded by the second creation argument (0, "
            "ascending, when absent). An int sorts by that direction at the moment it arrives "
            "and stores nothing — a bang that replayed the last inline direction would be "
            "hidden state — a float truncates to an int first, and a list spelling exactly one "
            "signed int is the direction it spells, kept equivalent to the int. \"array "
            "<name>\" sorts by the stored direction when it names the array bound by the "
            "creation argument — the message an .array's reference outlet emits on a bang, the "
            "family's gesture. An empty array sorts to itself and still announces. Anything "
            "else is refused and counted rather than logged, since this inlet may be the audio "
            "thread.",
            "any int");
  INLET_DOC(1, "direction",
            "An int stores the direction the next bang sorts by, silently — the cold half of "
            "the Max idiom, .zl sort's argument: negative sorts descending, anything else "
            "ascending. A float truncates to an int first; a non-finite one is refused rather "
            "than quietly becoming ascending.",
            "any int");
  INLET_DOC(2, "array reference",
            "\"array <name>\" is accepted silently when it names the array bound by the first "
            "creation argument, so a patch may wire the array's reference outlet across. It "
            "sets nothing — the binding is the creation argument, resolved on the control "
            "thread — and anything else is refused and counted.",
            "");
  OUTLET_DOC(0, "reference",
             "The bound array's reference, \"array <name>\", sent after a sort that landed — "
             "the way an array leaves an object on the value model, so wiring it onward chains "
             "the family: into .array.at it fetches from the new order. A lost try-lock emits "
             "nothing (one counted refusal, nothing changed), and an unnamed object stays "
             "silent — the sort happens, but there is no name to pass on.",
             "");
  OUTLET_DOC(1, "order",
             "The applied order — the picks as zero-based indices into the array as it stood "
             "at the trigger, Max's zl sort index map — sent before the reference, Max's "
             "right-to-left rule: feed this to an .array.indexmap's map inlet and the "
             "reference to its trigger, and a parallel array lands in the same new order. The "
             "sort being stable, equal elements keep their arrival order in it. A one-element "
             "order leaves as the int it spells, which .array.indexmap's map inlet accepts as "
             "the one-entry map; an empty array publishes no order.",
             "0-255 each");
  PARAM_DOC("name", "",
            "The array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an "
            ".array of the same name in this patcher holds. Resolved once, on the control "
            "thread, which is why no message re-points it at run time. Empty sorts a private, "
            "empty array: the trigger lands, but nothing shares it and no reference leaves.",
            "any identifier");
  PARAM_DOC("direction", "0",
            "The initial stored direction — what a bang sorts by before any int has moved it. "
            "Negative sorts descending, anything else ascending, so the absent argument is an "
            "ascending sort.",
            "any int");
}

// A re-parse must not leave half of the previous configuration standing:
// SetParams("") — and the clear half of every Set() — drops the stored
// direction back to 0 (ascending) along with the name. gArrayRotate's rule.
PARM_CLEAR() {
  direction.store(0, std::memory_order_relaxed);
  gArrayEndsBase::ClearParams();
}

BANG_IN(BangIn) {
  (void)inlet;
  Sort(direction.load(std::memory_order_relaxed), thread);
}

INT_IN(IntIn) {
  if (inlet == 1) {
    // Stored silently — the cold half of the Max idiom. Any int is a
    // well-formed direction: its sign is the whole of what it says.
    direction.store(value, std::memory_order_relaxed);
    return;
  }
  // An int on the trigger is applied at the moment it arrives and stores
  // nothing — gArrayIndexMap's trigger rule.
  Sort(value, thread);
}

FLOAT_IN(FloatIn) {
  // Max's float method on an int attribute is "convert to int", .table's
  // precedent. The negated range test keeps a NaN or an infinity — which
  // ExprToInt folds to 0 — from quietly becoming an ascending sort.
  if (!(value >= -2147483648.f && value < 2147483648.f)) {
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

  if (ArrayReferenceNames(value, arrayName)) {
    // The array's reference sorts by the stored direction — the message its
    // .array emits on a bang, the family's gesture.
    Sort(direction.load(std::memory_order_relaxed), thread);
    return;
  }

  // A list spelling exactly one signed int is the direction it spells,
  // applied at once — kept equivalent to the int, gArrayRotate's
  // equivalence, so a direction-producing outlet still lands. Anything else
  // — more atoms, a symbol, a reference naming an array this object is not
  // bound to — is refused whole.
  int by = 0;
  std::size_t offset = 0;
  if (!ReadIntArgAt(value, offset, by) || !AtEnd(value, offset)) {
    Refuse();
    return;
  }
  Sort(by, thread);
}

bool gArraySort::Before(std::size_t i, std::size_t j, bool descending) const {
  // Numbers come before symbols in **both** directions: which of the two an
  // element is, is a type ordering rather than a value one — .zl sort's
  // AtomsBefore, kept so ascending and descending stay one question asked
  // two ways.
  if (elementIsNumber[i] != elementIsNumber[j]) return elementIsNumber[i];

  int comparison = 0;
  if (elementIsNumber[i]) {
    // The value the element was classified with at the top of this sort —
    // read once precisely so the O(n log n) comparisons never re-read the
    // same characters.
    if (elementValue[i] == elementValue[j]) return false;
    comparison = (elementValue[i] < elementValue[j]) ? -1 : 1;
  } else {
    // Compared in place: this runs O(n log n) times per sort.
    const std::string& a = store->elements[i];
    const std::string& b = store->elements[j];
    const std::size_t shared = (a.size() < b.size()) ? a.size() : b.size();
    comparison = (shared == 0) ? 0 : std::memcmp(a.data(), b.data(), shared);
    if (comparison == 0) {
      if (a.size() == b.size()) return false;
      comparison = (a.size() < b.size()) ? -1 : 1;
    }
  }
  return descending ? (comparison > 0) : (comparison < 0);
}

void gArraySort::SortOrder(std::size_t count, bool descending) {
  // Bottom-up merge sort through the fixed scratch — gZl's SortIndices, for
  // gZl's two reasons: **stable**, so equal elements keep the order they
  // arrived in and the published order is one a patch can reason about, and
  // **O(n log n) whatever the data**, on a path the audio callback takes.
  // std::stable_sort has the first and allocates; std::sort has the second
  // and is not stable.
  for (std::size_t i = 0; i < count; i++)
    order[i] = static_cast<std::uint16_t>(i);

  for (std::size_t width = 1; width < count; width *= 2) {
    for (std::size_t left = 0; left < count; left += 2 * width) {
      const std::size_t mid = (left + width < count) ? left + width : count;
      const std::size_t right = (left + (2 * width) < count) ? left + (2 * width) : count;
      std::size_t i = left;
      std::size_t j = mid;
      std::size_t at = left;
      // Taken from the right run only when it is *strictly* before the left
      // one, which is exactly what makes the merge stable.
      while (i < mid && j < right)
        merge[at++] = Before(order[j], order[i], descending) ? order[j++] : order[i++];
      while (i < mid)
        merge[at++] = order[i++];
      while (j < right)
        merge[at++] = order[j++];
    }
    for (std::size_t i = 0; i < count; i++)
      order[i] = merge[i];
  }
}

void gArraySort::Sort(int by, YSE::THREAD thread) {
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    const std::size_t size = store->count;

    // Classify every element once, before any comparison — the same
    // classifier AtomList applies to every atom on the way in, so a stored
    // array and the list it spells sort identically. One bounded parse per
    // element; nothing here allocates.
    for (std::size_t i = 0; i < size; i++) {
      const std::string& element = store->elements[i];
      elementValue[i] = 0.f;
      elementIsNumber[i] = ReadNumericToken(element.c_str(), element.size(), elementValue[i]);
    }

    // Negative sorts descending, anything else ascending — .zl sort's
    // direction rule, so an unset argument is an ascending sort.
    SortOrder(size, by < 0);
    ApplyOrderLocked(size);

    // The applied order as a list, built under the same hold — the order
    // table is guarded state, so the send below must not read it after the
    // guard is released. AddInt cannot refuse here: 256 entries of at most
    // three digits fit both of the list's ceilings.
    orderOut.Clear();
    for (std::size_t i = 0; i < size; i++)
      orderOut.AddInt(static_cast<int>(order[i]));
  }

  // Right before left — the order, then the reference — so a second
  // .array.indexmap's map is in place before the reference sets it running.
  // Outside the guard, as every send is; an empty order (an empty array)
  // sends nothing at all, SendAtoms' rule.
  SendAtoms(outputs[1], orderOut, orderRender, thread);
  Announce(thread);
}
