#include "gMean.h"
#include "../pObjectList.hpp"
#include "gExprEval.h"
#include <cmath>
#include <string>

using namespace YSE::PATCHER;

#define className gMean

namespace {

  // Largest value the int outlet can carry. The internal count is 64-bit and
  // keeps climbing past this, so only the *report* saturates.
  constexpr I64 COUNT_CEILING = 2147483647LL;

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);
  REG_BANG_IN(Bang);
  REG_LIST_IN(SetList);

  ADD_OUT_FLOAT;
  ADD_OUT_INT;

  // Max's `mean` takes no creation arguments, so the object registers no
  // parameters. An empty parameter string round-trips through DumpJSON /
  // ParseJSON unchanged.

  sum = 0.0;
  count = 0;

  ADD_DESCRIPTION(
      "Running average of every number received. Each int or float is added to a running sum and "
      "the new mean comes out of outlet 0, with the number of values it rests on out outlet 1; the "
      "count is sent first, so a patch triggering off the mean already holds the matching count. "
      "Unlike .slide, which forgets the past geometrically, this never forgets: the thousandth "
      "value counts exactly as much as the first. A bang re-sends the stored mean and count "
      "without adding anything, and before any number has arrived — or straight after a clear — "
      "that is 0 with a count of 0, since the mean of nothing has to be chosen rather than "
      "computed and 0/0 would put a NaN on the outlet. A list is Max's one-shot mean: the "
      "previously received numbers are cleared and the mean of the list items is sent, up to 256 "
      "items. 'clear' (or 'reset', accepted as a synonym) zeroes the sum and the count and emits "
      "nothing. The sum is kept in double and the count as an exact 64-bit integer, which is the "
      "point of the object: a float sum drifts, and worse, stagnates once it exceeds 2^24 times a "
      "new value — about seventeen million messages — after which further input contributes "
      "nothing at all while the count keeps climbing. In double the error stays within "
      "(n-1)*1.11e-16 relative to the average input magnitude, which is under one ulp of the float "
      "on the outlet even after a billion values, and the stagnation point moves to 9e15 messages. "
      "A non-finite input is folded in as 0 and still counted, the convention ./ , .sqrt, .zmap "
      "and .clip already use, which is also what keeps an infinity from pinning the mean forever.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "in",
            "Int or float to fold into the running average / bang to re-send the stored mean and "
            "count without adding anything / a list of numbers to report their mean and forget "
            "everything before it / list 'clear' (or 'reset') to zero the sum and the count.",
            "any float");
  OUTLET_DOC(0, "mean", "The average of every number received so far; 0 when none have been.",
             "between the smallest and largest input");
  OUTLET_DOC(1, "count",
             "How many numbers the mean rests on, sent before the mean. Saturates at 2147483647; "
             "the mean itself stays correct past that.",
             "0 and up");
}

int gMean::ReportableCount(I64 value) {
  if (value > COUNT_CEILING) return static_cast<int>(COUNT_CEILING);
  if (value < 0) return 0;
  return static_cast<int>(value);
}

double gMean::Mean() const {
  // The mean of nothing is chosen, not computed: 0/0 would be a NaN and every
  // object downstream would then carry it. See "Bang before any input".
  if (count <= 0) return 0.0;
  return sum / static_cast<double>(count);
}

void gMean::Add(float value) {
  // A non-finite input has to be caught before it reaches the sum: an infinity
  // would pin the mean at infinity, and a following -inf would make the sum a
  // NaN that no later input could ever clear. Substituting 0 is the convention
  // the rest of the math family uses; the value is still counted, so the count
  // outlet keeps meaning "how many messages the inlet accepted".
  sum += std::isfinite(value) ? static_cast<double>(value) : 0.0;
  count++;
}

void gMean::Report(YSE::THREAD thread) {
  // Right to left, as in Max and as .cartopol already does here: the count
  // lands first, so an object triggered by the mean cannot read the pair
  // half-updated.
  outputs[1].SendInt(ReportableCount(count), thread);
  outputs[0].SendFloat(static_cast<float>(Mean()), thread);
}

void gMean::Clear() {
  sum = 0.0;
  count = 0;
}

FLOAT_IN(SetFloat) {
  (void)inlet; // only inlet 0 exists
  Add(value);
  Report(thread);
}

INT_IN(SetInt) {
  // Max's `mean` averages ints and floats into the same sum; an int is just a
  // float that happens to be whole.
  SetFloat(static_cast<float>(value), inlet, thread);
}

BANG_IN(Bang) {
  (void)inlet;
  // Max: "Sends out the previous output (the stored average value)." Nothing
  // is added, so the count does not move either.
  Report(thread);
}

LIST_IN(SetList) {
  (void)inlet;

  // `clear` is Max's word for this object; `reset` is Max's word for .slide's
  // equivalent. Both are accepted so a patch does not have to remember which
  // reference page an object came from.
  if (value == "clear" || value == "reset") {
    // Max: "Resets the stored and calculated contents of the object to zero."
    // Silent, like .histo's clear and .slide's reset.
    Clear();
    return;
  }

  // Max: "The numbers in the list are added together, the sum is divided by
  // the number of items in the list, and the mean is sent out. All previously
  // received numbers are cleared from memory." So a list is a one-shot mean
  // and an erasure, not an append.
  //
  // Bounded by MAX_LIST_ITEMS into a stack array: the shared reader neither
  // allocates nor touches locale state, so this is safe on whichever thread
  // the message arrived on. Non-finite items already read as 0 there, which is
  // the same answer Add() gives them.
  float items[MAX_LIST_ITEMS];
  const int read = ExprParseFloatList(value.c_str(), items, MAX_LIST_ITEMS);

  // A message with no numbers in it at all is not a list, it is a word this
  // object does not know. Ignored rather than treated as an empty list, which
  // would make every unknown message a silent `clear`.
  if (read == 0) return;

  Clear();
  for (int i = 0; i < read; i++)
    Add(items[i]);
  Report(thread);
}

GUI_VALUE() {
  // The mean is what the object is for; the count is already on an outlet.
  return std::to_string(Mean());
}
