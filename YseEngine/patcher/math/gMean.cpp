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

  ADD_DESCRIPTION("Sends the average of every number received so far, and how many numbers that "
                  "is. A list sends the average of its own items and starts the average over.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "in",
            "Number: add it to the average and send. Bang: send the average again. List: average "
            "its items, forgetting earlier numbers. 'clear' or 'reset': start over without "
            "sending.",
            "any float");
  OUTLET_DOC(0, "mean", "The average; 0 before any number has arrived.",
             "between the smallest and largest input");
  OUTLET_DOC(1, "count", "How many numbers the average includes. Sent before the average.",
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
