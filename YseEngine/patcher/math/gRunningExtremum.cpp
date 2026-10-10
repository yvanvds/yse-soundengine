#include "gRunningExtremum.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "gExprEval.h"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gRunningExtremumBase

gRunningExtremumBase::gRunningExtremumBase(ExtremumOrder order, float defaultInitial)
  : pObject(false), beats(order), initial(defaultInitial), extreme(defaultInitial) {
  // Inlet 0 is hot: it offers, reports and replays. Inlet 1 reseeds — and,
  // unlike .maximum's cold inlet, that is itself an event worth announcing.
  ADD_IN_0;
  REG_FLOAT_IN(SetLeftFloat);
  REG_INT_IN(SetLeftInt);
  REG_BANG_IN(SetLeftBang);
  REG_LIST_IN(SetLeftList);

  ADD_IN_1;
  REG_FLOAT_IN(SetRightFloat);
  REG_INT_IN(SetRightInt);

  // The value, then Max's two flag outlets. Ints, because 0 and 1 are the only
  // values they can carry.
  ADD_OUT_FLOAT;
  ADD_OUT_INT;
  ADD_OUT_INT;

  ADD_PARAM(initial);
  // The extreme has to be loaded from the argument *after* it is parsed, or a
  // saved `.peak 5` would start at the constructor's default and accept a 3
  // that the original object had already rejected. That is what the parse
  // callback is for.
  REG_PARM_PARSE;

  ADD_CATEGORY(pCategory::MATH);
}

void gRunningExtremumBase::Document(const char* summary, const char* inletDoc, const char* seedDoc,
                                    const char* outletDoc, const char* defaultValue,
                                    const char* paramDoc) {
  ADD_DESCRIPTION(summary);
  INLET_DOC(0, "in", inletDoc, "any float");
  INLET_DOC(1, "seed", seedDoc, "any float");
  OUTLET_DOC(0, "out", outletDoc, "any float");
  OUTLET_DOC(1, "new", "1 when the number just received set a new record, otherwise 0.", "0 or 1");
  OUTLET_DOC(2, "same", "The opposite of the 'new' outlet: 0 for a new record, otherwise 1.",
             "0 or 1");
  PARAM_DOC("initial", defaultValue, paramDoc, "any float");
}

PARM_PARSE() {
  // Runs on the control thread once the creation parameters have been read.
  initial = ExtremumSanitize(initial);
  extreme = initial;
}

void gRunningExtremumBase::Report(bool isNew, YSE::THREAD thread) {
  // Right to left, as .mean and .cartopol already send: whatever the value
  // outlet triggers downstream already sees the matching flags.
  outputs[2].SendInt(isNew ? 0 : 1, thread);
  outputs[1].SendInt(isNew ? 1 : 0, thread);
}

void gRunningExtremumBase::Store(float value) {
  extreme = ExtremumSanitize(value);
}

void gRunningExtremumBase::Offer(float value, YSE::THREAD thread) {
  const float candidate = ExtremumSanitize(value);
  // Strict, so a tie is not a new extreme: the outlet would carry the number it
  // just carried, and the flag outlets would call an unchanged record a new
  // one.
  const bool isNew = beats(candidate, extreme);

  // Settled before anything is sent, so nothing reached from an outlet can
  // observe the object half-updated.
  if (isNew) extreme = candidate;

  Report(isNew, thread);
  // Max: "Compares a number to a previous peak-value and, if larger, it is sent
  // out the output while the new peak-value is set to that number." Silence
  // when it is not — that is what makes this object a running extreme rather
  // than a two-operand max.
  if (isNew) outputs[0].SendFloat(extreme, thread);
}

void gRunningExtremumBase::Reseed(float value, YSE::THREAD thread) {
  Store(value);
  // Max: "The number is stored in peak as the new peak value, and is sent out",
  // and the left outlet's "(A number received in the right inlet is always the
  // new peak value.)" is what settles the flags for this path.
  Report(true, thread);
  outputs[0].SendFloat(extreme, thread);
}

FLOAT_IN(SetLeftFloat) {
  if (inlet != 0) return;
  Offer(value, thread);
}

INT_IN(SetLeftInt) {
  // One numeric type throughout — see "One numeric type" in the header for why
  // Max's int variant is not reproduced.
  SetLeftFloat(static_cast<float>(value), inlet, thread);
}

BANG_IN(SetLeftBang) {
  if (inlet != 0) return;
  // Max: "Sends the currently stored peak value out the left outlet." Only the
  // left one: a bang receives no number, so the flag outlets have nothing to
  // answer. Before any input the stored value is the creation argument.
  outputs[0].SendFloat(extreme, thread);
}

LIST_IN(SetLeftList) {
  if (inlet != 0) return;

  // Back to the creation argument, as .counter's and .accum's reset do, and
  // silently — the cold inlet already covers "reseed and announce", so the
  // useful message here is the one that stays quiet and lets the *next* number
  // be the next peak event. `clear` is deliberately not accepted: on a
  // `.peak 5` it could mean either 0 or 5.
  if (value == "reset") {
    extreme = initial;
    return;
  }

  // The silent twin of the cold inlet, spelled as .accum spells it. MatchWord
  // requires the separator so `settle 3` is not read as `set 3`.
  std::size_t argument = 0;
  if (MatchWord(value, "set", 3, argument)) {
    float parsed = 0.f;
    // Returns 0 when no number follows, which is what keeps a malformed
    // `set wobble` an ignored message rather than a store of a zero nobody
    // sent. ExprParseFloatList neither allocates nor touches locale state, so
    // this is safe on whichever thread the message arrived on.
    if (ExprParseFloatList(value.c_str() + argument, &parsed, 1) == 1) Store(parsed);
    return;
  }

  // Bounded by MAX_LIST_ITEMS into a stack array. Non-finite items already read
  // as 0 there, the same substitution ExtremumSanitize makes on every other
  // path.
  float items[MAX_LIST_ITEMS];
  const int read = ExprParseFloatList(value.c_str(), items, MAX_LIST_ITEMS);

  // A message with no numbers in it and no word this object knows. Ignored
  // rather than guessed at, as in .slide, .mean, .accum and .maximum.
  if (read == 0) return;

  // Max routes a one-element list to the int / float method.
  if (read == 1) {
    Offer(items[0], thread);
    return;
  }

  // Max: "The second number is stored as the new peak value and is sent out,
  // then the first number is received in the left inlet." Two events from one
  // message, in that order; numbers past the second are ignored, since Max
  // documents exactly two.
  Reseed(items[1], thread);
  Offer(items[0], thread);
}

FLOAT_IN(SetRightFloat) {
  if (inlet != 1) return;
  Reseed(value, thread);
}

INT_IN(SetRightInt) {
  SetRightFloat(static_cast<float>(value), inlet, thread);
}

GUI_VALUE() {
  // The running extreme, which is what a bang would send.
  return std::to_string(extreme);
}

#undef className

// ─── the two directions ───────────────────────────────────────────────────────
// The ordering, where the extreme starts, and the words describing them.

gPeak::gPeak() : gRunningExtremumBase(ExtremumGreater, 0.f) {
  Document("Keeps the largest number received so far and sends it only when a new number beats "
           "it. Two more outlets report whether each number set a new peak.",
           "Number: send it if it beats the peak. Bang: send the peak. List '<number> <peak>': "
           "set the peak, then offer the number. 'set <n>': set the peak without sending. "
           "'reset': go back to the starting peak without sending.",
           "Number that becomes the new peak and is sent, always reported as a new record.",
           "The new peak. Silent when a number does not beat it.", "0",
           "Starting peak, and the peak 'reset' returns to.");
}

gTrough::gTrough() : gRunningExtremumBase(ExtremumLess, 128.f) {
  Document("Keeps the smallest number received so far and sends it only when a new number goes "
           "below it. Two more outlets report whether each number set a new minimum.",
           "Number: send it if it is below the minimum. Bang: send the minimum. List '<number> "
           "<minimum>': set the minimum, then offer the number. 'set <n>': set the minimum "
           "without sending. 'reset': go back to the starting minimum without sending.",
           "Number that becomes the new minimum and is sent, always reported as a new record.",
           "The new minimum. Silent when a number does not go below it.", "128",
           "Starting minimum, and the minimum 'reset' returns to. 128 by default, so that "
           "ordinary numbers can go below it.");
}
