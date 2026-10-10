#include "gArrayStats.h"
#include "../pAtomList.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>

using namespace YSE::PATCHER;

namespace {

  // Doc strings shared by the six objects, hoisted out of the constructors
  // because they are long enough that a constructor stops being readable with
  // them inline — gArray's arrangement. What differs per object stays with
  // that object below.
  constexpr char kTriggerInletDoc[] =
      "Bang, or the bound array's reference \"array <name>\": send the result.";
  constexpr char kReferenceInletDoc[] =
      "Accepts the bound array's reference \"array <name>\" and ignores it, so the array's "
      "reference outlet can be wired here. Anything else is refused.";
  constexpr char kNameParamDoc[] =
      "Name of the array to read: the .array with this name in the same patcher. Empty uses a "
      "private, empty array.";
  constexpr char kEmptyNumericOutletDoc[] = "Bang when the array holds no numbers.";
  constexpr char kEmptyModeOutletDoc[] = "Bang when the array is empty.";

} // namespace

// ─── the shared base ──────────────────────────────────────────────────────────

#define className gArrayStatsBase

gArrayStatsBase::gArrayStatsBase() : gArrayEndsBase() {
  // The trigger inlet, hot, and the reference inlet, cold — gArrayLength's
  // shape: a statistic is asked for, never addressed, so the hot inlet takes
  // only the ask (a bang, or the array's own reference) and there is no int
  // or float method anywhere.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_LIST_IN(ListIn);
}

BANG_IN(BangIn) {
  (void)inlet;
  Ask(thread);
}

LIST_IN(ListIn) {
  if (inlet == 1) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — the binding is the creation argument, resolved on the
    // control thread, so there is nothing to set. gDictSlice's inlet rule.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The array's reference asks for the statistic — the message its .array
  // emits on a bang, the family's gesture. Anything else, including a
  // reference naming an array this object is not bound to, is refused:
  // resolving an unrecognised name means the registry's mutex, and this may
  // be the audio thread.
  if (ArrayReferenceNames(value, arrayName)) {
    Ask(thread);
    return;
  }
  Refuse();
}

void gArrayStatsBase::Ask(YSE::THREAD thread) {
  bool answered = false;
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    answered = ReduceLocked();
  }

  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well write into this same array, and inside the guard that
  // write would be the one thing the try-lock drops.
  if (answered) {
    Report(thread);
    return;
  }
  outputs[1].SendBang(thread);
}

// ─── the numeric collector ────────────────────────────────────────────────────

void gArrayNumericStatsBase::CollectNumbers() {
  // One bounded scan: every element that reads whole as one finite number
  // joins the population, in arrival order, with the position it came from;
  // anything else is simply not a numeric element. The same classifier
  // AtomList applies to every atom on the way in, so a stored array and the
  // list it spells reduce identically.
  numericCount = 0;
  const std::size_t size = store->count;
  for (std::size_t i = 0; i < size; i++) {
    const std::string& element = store->elements[i];
    float value = 0.f;
    if (!ReadNumericToken(element.c_str(), element.size(), value)) continue;
    values[numericCount] = value;
    sourceIndex[numericCount] = static_cast<std::uint16_t>(i);
    numericCount++;
  }
}

// ─── the comparing pair ───────────────────────────────────────────────────────

#undef className
#define className gArrayExtremumBase

gArrayExtremumBase::gArrayExtremumBase(bool wantMax) : gArrayNumericStatsBase(), wantMax(wantMax) {
  // ANY on the element outlet: what leaves it is an int or a float by the
  // winning element's spelling — gArrayEndsRemover's shape. The empty outlet
  // is always a bang.
  ADD_OUT_ANY;
  ADD_OUT_BANG;

  // The allocation the send path would otherwise need, taken here on the
  // control thread.
  AtomList::ReserveRender(emitScratch);
}

bool gArrayExtremumBase::ReduceLocked() {
  CollectNumbers();
  if (numericCount == 0) return false;

  // Strict comparison, so an equal later value never replaces an earlier
  // winner: a tie — 7 and 7. compare equal — keeps the first occurrence,
  // which is what makes the answer deterministic by spelling too.
  std::size_t best = 0;
  for (std::size_t i = 1; i < numericCount; i++) {
    const bool wins = wantMax ? (values[i] > values[best]) : (values[i] < values[best]);
    if (wins) best = i;
  }

  // The element itself, copied under the hold so the send after release
  // reports exactly what won — no other thread can renumber in between.
  const std::string& element = store->elements[sourceIndex[best]];
  fetchedLength = element.size();
  std::memcpy(fetched, element.data(), fetchedLength);
  fetched[fetchedLength] = '\0';
  return true;
}

void gArrayExtremumBase::Report(YSE::THREAD thread) {
  // Through SendAtom, so the element leaves as the int or float it spells —
  // the patcher's transport rule.
  SendAtom(outputs[0], fetched, fetchedLength, emitScratch, thread);
}

// ─── .array.min ───────────────────────────────────────────────────────────────

#undef className
#define className gArrayMin

gArrayMin::gArrayMin() : gArrayExtremumBase(false) {
  ADD_DESCRIPTION("Outputs the smallest number in a named array. Words are skipped.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "minimum",
             "The smallest numeric element, as the int or float it is spelled as. When several "
             "are equal (7 and 7. count as equal), the first one.",
             "");
  OUTLET_DOC(1, "empty", kEmptyNumericOutletDoc, "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
}

// ─── .array.max ───────────────────────────────────────────────────────────────

#undef className
#define className gArrayMax

gArrayMax::gArrayMax() : gArrayExtremumBase(true) {
  ADD_DESCRIPTION("Outputs the largest number in a named array. Words are skipped.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "maximum",
             "The largest numeric element, as the int or float it is spelled as. When several "
             "are equal (7 and 7. count as equal), the first one.",
             "");
  OUTLET_DOC(1, "empty", kEmptyNumericOutletDoc, "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
}

// ─── .array.mean ──────────────────────────────────────────────────────────────

#undef className
#define className gArrayMean

gArrayMean::gArrayMean() : gArrayNumericStatsBase() {
  // A mean is a statistic of the values, not one of them, so the answer is
  // always a float. The empty outlet is always a bang.
  ADD_OUT_FLOAT;
  ADD_OUT_BANG;

  ADD_DESCRIPTION(
      "Outputs the average of the numbers in a named array, as a float. Words are skipped.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "mean",
             "The average of the numeric elements, always a float, also when they are all "
             "ints.",
             "");
  OUTLET_DOC(1, "empty", kEmptyNumericOutletDoc, "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
}

bool gArrayMean::ReduceLocked() {
  CollectNumbers();
  if (numericCount == 0) return false;

  double sum = 0.0;
  for (std::size_t i = 0; i < numericCount; i++)
    sum += static_cast<double>(values[i]);
  result = static_cast<float>(sum / static_cast<double>(numericCount));
  return true;
}

void gArrayMean::Report(YSE::THREAD thread) {
  outputs[0].SendFloat(result, thread);
}

// ─── .array.median ────────────────────────────────────────────────────────────

#undef className
#define className gArrayMedian

gArrayMedian::gArrayMedian() : gArrayNumericStatsBase() {
  // Always a float: an even population answers the mean of the two middle
  // values, and a statistic keeps one reporting shape rather than retyping
  // by the population's parity. The empty outlet is always a bang.
  ADD_OUT_FLOAT;
  ADD_OUT_BANG;

  ADD_DESCRIPTION(
      "Outputs the median of the numbers in a named array, as a float. Words are skipped, and "
      "the array itself is not reordered.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "median",
             "The middle value in sorted order; with an even count, the average of the two "
             "middle values. Always a float.",
             "");
  OUTLET_DOC(1, "empty", kEmptyNumericOutletDoc, "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
}

void gArrayMedian::SortValues(std::size_t count) {
  // Bottom-up merge sort through the fixed scratch — gZl's SortIndices over
  // values instead of indices, for one of gZl's two reasons: O(n log n)
  // whatever the data, on a path the audio callback takes. (Stability is
  // moot over bare values.) std::sort is not bounded that way, and
  // std::nth_element promises only an average.
  for (std::size_t width = 1; width < count; width *= 2) {
    for (std::size_t left = 0; left < count; left += 2 * width) {
      const std::size_t mid = (left + width < count) ? left + width : count;
      const std::size_t right = (left + (2 * width) < count) ? left + (2 * width) : count;
      std::size_t i = left;
      std::size_t j = mid;
      std::size_t at = left;
      while (i < mid && j < right)
        merge[at++] = (values[j] < values[i]) ? values[j++] : values[i++];
      while (i < mid)
        merge[at++] = values[i++];
      while (j < right)
        merge[at++] = values[j++];
    }
    for (std::size_t i = 0; i < count; i++)
      values[i] = merge[i];
  }
}

bool gArrayMedian::ReduceLocked() {
  CollectNumbers();
  if (numericCount == 0) return false;

  SortValues(numericCount);
  const std::size_t middle = numericCount / 2;
  if ((numericCount % 2) != 0) {
    result = values[middle];
  } else {
    // The mean of the two middle values, in double — mean's own precision
    // rule, applied to a population of two.
    result = static_cast<float>(
        (static_cast<double>(values[middle - 1]) + static_cast<double>(values[middle])) / 2.0);
  }
  return true;
}

void gArrayMedian::Report(YSE::THREAD thread) {
  outputs[0].SendFloat(result, thread);
}

// ─── .array.mode ──────────────────────────────────────────────────────────────

#undef className
#define className gArrayMode

gArrayMode::gArrayMode() : gArrayStatsBase() {
  // ANY on the element outlet: the most frequent element leaves as the int,
  // float or symbol it spells — gArrayEndsRemover's shape. The empty outlet
  // is always a bang.
  ADD_OUT_ANY;
  ADD_OUT_BANG;

  // The allocation the send path would otherwise need, taken here on the
  // control thread.
  AtomList::ReserveRender(emitScratch);

  ADD_DESCRIPTION(
      "Outputs the element that occurs most often in a named array. Words count too, and "
      "elements are compared by spelling, so 7 and 7. are different elements.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "mode",
             "The most frequent element, as an int, float or symbol by its spelling. On a tie, "
             "the one that appears first.",
             "");
  OUTLET_DOC(1, "empty", kEmptyModeOutletDoc, "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
}

bool gArrayMode::SpellingBefore(std::size_t i, std::size_t j) const {
  // Byte order, the shorter first when one is a prefix of the other —
  // gArraySort's symbol half, without the numeric one: a mode's order only
  // exists to sit equal spellings next to each other, so what the order *is*
  // could not matter less, only that equal means byte-equal.
  const std::string& a = store->elements[i];
  const std::string& b = store->elements[j];
  const std::size_t shared = (a.size() < b.size()) ? a.size() : b.size();
  const int comparison = (shared == 0) ? 0 : std::memcmp(a.data(), b.data(), shared);
  if (comparison != 0) return comparison < 0;
  return a.size() < b.size();
}

void gArrayMode::SortOrder(std::size_t count) {
  // Bottom-up merge sort through the fixed scratch — gZl's SortIndices, for
  // gZl's two reasons: **stable**, so within a run of equal spellings the
  // arrival order survives and the run's first entry is the element's first
  // occurrence, and **O(n log n) whatever the data**, on a path the audio
  // callback takes.
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
        merge[at++] = SpellingBefore(order[j], order[i]) ? order[j++] : order[i++];
      while (i < mid)
        merge[at++] = order[i++];
      while (j < right)
        merge[at++] = order[j++];
    }
    for (std::size_t i = 0; i < count; i++)
      order[i] = merge[i];
  }
}

bool gArrayMode::ReduceLocked() {
  const std::size_t size = store->count;
  if (size == 0) return false;

  SortOrder(size);

  // Equal spellings now sit adjacent, so one walk counts every run. The
  // best run is the longest; a tie keeps the run whose first occurrence is
  // earliest — and the stable order makes that the run's first entry, so
  // the comparison is one index against another.
  std::size_t bestIndex = 0;
  std::size_t bestLength = 0;
  std::size_t runStart = 0;
  for (std::size_t i = 1; i <= size; i++) {
    bool closes = (i == size);
    if (!closes) {
      const std::string& a = store->elements[order[runStart]];
      const std::string& b = store->elements[order[i]];
      closes = (a.size() != b.size()) ||
               (a.size() != 0 && std::memcmp(a.data(), b.data(), a.size()) != 0);
    }
    if (!closes) continue;
    const std::size_t length = i - runStart;
    if (length > bestLength || (length == bestLength && order[runStart] < bestIndex)) {
      bestLength = length;
      bestIndex = order[runStart];
    }
    runStart = i;
  }

  // The element itself, copied under the hold so the send after release
  // reports exactly what won.
  const std::string& element = store->elements[bestIndex];
  fetchedLength = element.size();
  std::memcpy(fetched, element.data(), fetchedLength);
  fetched[fetchedLength] = '\0';
  return true;
}

void gArrayMode::Report(YSE::THREAD thread) {
  // Through SendAtom, so the element leaves as the int, float or symbol it
  // spells — the patcher's transport rule.
  SendAtom(outputs[0], fetched, fetchedLength, emitScratch, thread);
}

// ─── .array.stddev ────────────────────────────────────────────────────────────

#undef className
#define className gArrayStdDev

gArrayStdDev::gArrayStdDev() : gArrayNumericStatsBase() {
  // Always a float — a spread is a statistic of the values, not one of them.
  // The empty outlet is always a bang.
  ADD_OUT_FLOAT;
  ADD_OUT_BANG;

  ADD_DESCRIPTION(
      "Outputs the standard deviation of the numbers in a named array, as a float. Words are "
      "skipped.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "standard deviation",
             "The population standard deviation (divided by the count, not the count minus "
             "one), as a float. A single number gives 0.",
             "0 or more");
  OUTLET_DOC(1, "empty", kEmptyNumericOutletDoc, "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
}

bool gArrayStdDev::ReduceLocked() {
  CollectNumbers();
  if (numericCount == 0) return false;

  // The two-pass form — mean first, then squared deviations — in double:
  // the one-pass sum-of-squares cancels catastrophically exactly when the
  // spread is small against the magnitude, which is the case a patch
  // watching a control value actually has.
  double sum = 0.0;
  for (std::size_t i = 0; i < numericCount; i++)
    sum += static_cast<double>(values[i]);
  const double mean = sum / static_cast<double>(numericCount);

  double squares = 0.0;
  for (std::size_t i = 0; i < numericCount; i++) {
    const double deviation = static_cast<double>(values[i]) - mean;
    squares += deviation * deviation;
  }
  result = static_cast<float>(std::sqrt(squares / static_cast<double>(numericCount)));
  return true;
}

void gArrayStdDev::Report(YSE::THREAD thread) {
  outputs[0].SendFloat(result, thread);
}
