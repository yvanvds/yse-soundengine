#include "gExtremum.h"
#include "../pObjectList.hpp"
#include "gExprEval.h"
#include <string>

using namespace YSE::PATCHER;

#define className gExtremumBase

gExtremumBase::gExtremumBase(ExtremumOrder order)
  : pObject(false), beats(order), initial(0.f), comparand(0.f), lastOutput(0.f) {
  // Inlet 0 is hot: it compares and emits. Inlet 1 only stores.
  ADD_IN_0;
  REG_FLOAT_IN(SetLeftFloat);
  REG_INT_IN(SetLeftInt);
  REG_BANG_IN(SetLeftBang);
  REG_LIST_IN(SetLeftList);

  ADD_IN_1;
  REG_FLOAT_IN(SetRightFloat);
  REG_INT_IN(SetRightInt);

  ADD_OUT_FLOAT;

  ADD_PARAM(initial);
  // The comparand and the bang's stand-in have to be loaded from the argument
  // *after* it is parsed, or a saved `.maximum 5` would compare against 0 and
  // answer 0 to its first bang. That is what the parse callback is for.
  REG_PARM_PARSE;

  ADD_CATEGORY(pCategory::MATH);
}

void gExtremumBase::Document(const char* summary, const char* inletDoc, const char* comparandDoc,
                             const char* outletDoc, const char* paramDoc) {
  ADD_DESCRIPTION(summary);
  INLET_DOC(0, "in", inletDoc, "any float");
  INLET_DOC(1, "compare", comparandDoc, "any float");
  OUTLET_DOC(0, "out", outletDoc, "any float");
  PARAM_DOC("initial", "0", paramDoc, "any float");
}

PARM_PARSE() {
  // Runs on the control thread once the creation parameters have been read.
  // Both members move: the argument is the first comparand *and* the value a
  // bang reports before anything has been emitted.
  comparand = ExtremumSanitize(initial);
  lastOutput = comparand;
}

void gExtremumBase::Emit(float value, YSE::THREAD thread) {
  lastOutput = value;
  outputs[0].SendFloat(value, thread);
}

FLOAT_IN(SetLeftFloat) {
  if (inlet != 0) return;
  // Max: "If the number is greater than the value currently stored in maximum,
  // it is sent out the outlet. Otherwise, the stored value is sent out." The
  // comparand deliberately does not move — that is `peak`'s job (#463), not
  // this object's.
  Emit(ExtremumBest(ExtremumSanitize(value), comparand, beats), thread);
}

INT_IN(SetLeftInt) {
  // One numeric type throughout — see "One numeric type" in the header for why
  // Max's int variant is not reproduced.
  SetLeftFloat(static_cast<float>(value), inlet, thread);
}

BANG_IN(SetLeftBang) {
  if (inlet != 0) return;
  // Max: "Sends the most recent output out the outlet again." Before there has
  // been one, `lastOutput` holds the creation argument.
  Emit(lastOutput, thread);
}

LIST_IN(SetLeftList) {
  if (inlet != 0) return;

  // Bounded by MAX_LIST_ITEMS into a stack array: the shared reader neither
  // allocates nor touches locale state, so this is safe on whichever thread the
  // message arrived on. Non-finite items already read as 0 there, which is the
  // same substitution ExtremumSanitize makes on the scalar path.
  float items[MAX_LIST_ITEMS];
  const int read = ExprParseFloatList(value.c_str(), items, MAX_LIST_ITEMS);

  // A message with no numbers in it at all is not a list, it is a word this
  // object does not know. Ignored rather than treated as an empty list, which
  // would have to emit something.
  if (read == 0) return;

  // Max routes a one-element list to the int / float method, where it is
  // compared against the comparand and leaves it untouched.
  if (read == 1) {
    SetLeftFloat(items[0], 0, thread);
    return;
  }

  // Max: "The numbers in the list are all compared to each other, and the
  // greatest value is sent out the outlet. The value stored in maximum is
  // replaced by the next greatest value in the list." The comparand takes no
  // part in the comparison, and it is settled before the Send so nothing
  // reached from the outlet can observe the object half-updated.
  float best = 0.f;
  float runnerUp = 0.f;
  ExtremumScan(items, read, beats, best, runnerUp);
  comparand = runnerUp;
  Emit(best, thread);
}

FLOAT_IN(SetRightFloat) {
  if (inlet != 1) return;
  // Max: "The number is stored for comparison with subsequent numbers received
  // in the left inlet." Silent, and it does not touch what a bang replays —
  // a bang is a replay, and nothing has been sent.
  comparand = ExtremumSanitize(value);
}

INT_IN(SetRightInt) {
  SetRightFloat(static_cast<float>(value), inlet, thread);
}

GUI_VALUE() {
  // What the outlet last carried, which is what a bang would send.
  return std::to_string(lastOutput);
}

#undef className

// ─── the two directions ───────────────────────────────────────────────────────
// The only thing that differs is the ordering and the words describing it.

gMaximum::gMaximum() : gExtremumBase(ExtremumGreater) {
  Document(
      "Sends out the larger of the number received on inlet 0 and a stored comparand, which makes "
      "it a .clip whose bound is itself a signal and the natural way to combine two control "
      "sources. Inlet 1 sets the comparand without emitting anything; a number on inlet 0 is "
      "compared and then forgotten, so the comparand is never moved by the input — that running "
      "reading of the idea is .peak, a different object. A bang re-sends the most recent output, "
      "and before there has been one that is the 'initial' creation argument (0 by default) rather "
      "than silence, since a patch that bangs its objects once on load has to get an answer. A "
      "list of two or more numbers is Max's reduction: the numbers are compared with each other "
      "and the greatest is sent out, the comparand taking no part, and afterwards the comparand is "
      "replaced by the next greatest value in the list — not by the greatest, which would make "
      "every later comparison a tie with the value just emitted. Up to 256 items are read; a "
      "one-element list is a plain number and compares against the comparand as one. A message "
      "with no numbers in it is ignored. Everything is a float and the outlet is a float outlet: "
      "Max's int-unless-the-argument-has-a-decimal-point duality is deliberately not reproduced, "
      "because '.maximum 5' and '.maximum 5.0' are the same parameter string here and guessing "
      "integral would silently round a comparison a patch is using to clamp a gain — put a .round "
      "on the outlet instead. A non-finite number is read as 0 wherever it arrives, the convention "
      "./ , .sqrt, .zmap, .clip, .slide and .mean already use, and here it also keeps a stray NaN "
      "from losing every comparison while still being the value sent out, and a stray infinity "
      "from pinning the outlet for good.",
      "Int or float to compare against the stored comparand, sending the larger of the two out / "
      "bang to re-send the most recent output / a list of numbers to send out the greatest of "
      "them and store the next greatest as the new comparand.",
      "The value inlet-0 numbers are compared against. Stored without emitting anything.",
      "The greater of the input and the stored comparand.",
      "Initial comparand, and what a bang reports before anything has been sent.");
}

gMinimum::gMinimum() : gExtremumBase(ExtremumLess) {
  Document(
      "Sends out the smaller of the number received on inlet 0 and a stored comparand, which makes "
      "it a .clip whose bound is itself a signal and the natural way to combine two control "
      "sources. Inlet 1 sets the comparand without emitting anything; a number on inlet 0 is "
      "compared and then forgotten, so the comparand is never moved by the input — that running "
      "reading of the idea is .trough, a different object. A bang re-sends the most recent output, "
      "and before there has been one that is the 'initial' creation argument (0 by default) rather "
      "than silence, since a patch that bangs its objects once on load has to get an answer. A "
      "list of two or more numbers is Max's reduction: the numbers are compared with each other "
      "and the smallest is sent out, the comparand taking no part, and afterwards the comparand is "
      "replaced by the next smallest value in the list — not by the smallest, which would make "
      "every later comparison a tie with the value just emitted. Up to 256 items are read; a "
      "one-element list is a plain number and compares against the comparand as one. A message "
      "with no numbers in it is ignored. Everything is a float and the outlet is a float outlet: "
      "Max's int-unless-the-argument-has-a-decimal-point duality is deliberately not reproduced, "
      "because '.minimum 5' and '.minimum 5.0' are the same parameter string here and guessing "
      "integral would silently round a comparison a patch is using to clamp a gain — put a .round "
      "on the outlet instead. Note that the default comparand of 0 makes a bare .minimum report 0 "
      "for every positive input, so this object almost always wants an argument or a cord on inlet "
      "1. A non-finite number is read as 0 wherever it arrives, the convention ./ , .sqrt, .zmap, "
      ".clip, .slide and .mean already use, and here it also keeps a stray NaN from losing every "
      "comparison while still being the value sent out, and a stray infinity from pinning the "
      "outlet for good.",
      "Int or float to compare against the stored comparand, sending the smaller of the two out / "
      "bang to re-send the most recent output / a list of numbers to send out the smallest of "
      "them and store the next smallest as the new comparand.",
      "The value inlet-0 numbers are compared against. Stored without emitting anything.",
      "The smaller of the input and the stored comparand.",
      "Initial comparand, and what a bang reports before anything has been sent.");
}
