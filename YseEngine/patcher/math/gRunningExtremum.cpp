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
  OUTLET_DOC(1, "new", "1 when the number just received was a new extreme, 0 when it was not.",
             "0 or 1");
  OUTLET_DOC(2, "same", "The inverse of the 'new' outlet: 0 for a new extreme, 1 for a rejection.",
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
  Document(
      "Keeps the largest number it has seen and sends it out only when a new one arrives, which is "
      "what peak-hold metering, envelope maxima and 'only react when this gets worse' logic are "
      "made of. This is the running reading of the idea .maximum deliberately does not have: "
      ".maximum compares an input against a sticky comparand and forgets it, answering every "
      "message and never moving, while .peak stores what it emits and stays silent until the "
      "record is broken. Two inlets and three outlets, as in Max: a number on inlet 0 is kept and "
      "sent out outlet 0 if it is greater than the stored peak and produces no value at all if it "
      "is not; outlet 1 carries 1 when the number was a new peak and 0 when it was not, and outlet "
      "2 carries the inverse so that 'it was not a record' is one cord rather than a cord plus a "
      ".== 0. The three are sent right to left, so anything the value triggers already sees the "
      "matching flags. A number on inlet 1 reseeds the peak and sends it out — always a new peak, "
      "flags and all — which is the opposite of .maximum's silent cold inlet. A bang sends the "
      "stored peak out outlet 0 alone, and before any input that is the 'initial' creation "
      "argument (0 by default, as in Max) rather than silence, since a patch that bangs its "
      "objects once on load has to get an answer. Two silent messages on inlet 0 complete the "
      "object, because Max leaves a running extreme with no way back that does not announce "
      "itself: 'reset' returns to the initial argument and 'set <n>' stores n, both without "
      "emitting, so the next number received is the next peak event. 'clear' is deliberately "
      "absent, because on a '.peak 5' it could not be told whether it meant 0 or 5. A list is "
      "Max's two-element idiom — the second number becomes the new peak and is sent out, then the "
      "first is offered to it — with anything past the second ignored and a one-element list "
      "treated as a plain number. A message with no numbers in it is ignored. Everything is a "
      "float and outlet 0 is a float outlet: Max's int-unless-the-argument-has-a-decimal-point "
      "duality is not reproduced, because '.peak 5' and '.peak 5.0' are the same parameter string "
      "here and guessing integral would silently round a peak a patch is metering with — put a "
      ".round on the outlet instead. A non-finite number is read as 0 wherever it arrives, the "
      "convention ./ , .sqrt, .zmap, .clip, .slide and .mean already use, and it matters more here "
      "than anywhere else because this object keeps what it accepts: a stored NaN would lose every "
      "later comparison and be reported forever, and a stored -infinity would make every number a "
      "new peak while +infinity would silence the outlet for good.",
      "Int or float to offer to the stored peak, sent out only if it is greater / bang to send the "
      "stored peak out / list '<input> <peak>' to reseed with the second number and then offer the "
      "first / list 'set <n>' to store n silently / list 'reset' to return to the initial "
      "argument.",
      "The new peak value. Stored and sent out — a number received here is always a new peak.",
      "New peak values. Silent when the number received was not greater than the stored peak.", "0",
      "Starting peak, what a bang reports before any input, and the value 'reset' returns to. Max "
      "starts peak at 0.");
}

gTrough::gTrough() : gRunningExtremumBase(ExtremumLess, 128.f) {
  Document(
      "Keeps the smallest number it has seen and sends it out only when a new one arrives — the "
      "mirror of .peak, for tracking minima, best-case timings and 'only react when this improves' "
      "logic. This is the running reading of the idea .minimum deliberately does not have: "
      ".minimum compares an input against a sticky comparand and forgets it, answering every "
      "message and never moving, while .trough stores what it emits and stays silent until the "
      "record is broken. Two inlets and three outlets, as in Max: a number on inlet 0 is kept and "
      "sent out outlet 0 if it is less than the stored trough and produces no value at all if it "
      "is not; outlet 1 carries 1 when the number was a new minimum and 0 when it was not, and "
      "outlet 2 carries the inverse so that 'it was not a record' is one cord rather than a cord "
      "plus a .== 0. The three are sent right to left, so anything the value triggers already sees "
      "the matching flags. A number on inlet 1 reseeds the trough and sends it out — always a new "
      "minimum, flags and all — which is the opposite of .minimum's silent cold inlet. A bang "
      "sends the stored minimum out outlet 0 alone, and before any input that is the 'initial' "
      "creation argument rather than silence. That argument defaults to 128, Max's value and not "
      "an oversight next to .peak's 0: a running extreme is only useful if its starting value can "
      "lose, this family refuses to store an infinity because one that wins would pin the outlet "
      "for good, and a trough starting at 0 would reject every positive number it was ever sent. "
      "Two silent messages on inlet 0 complete the object, because Max leaves a running extreme "
      "with no way back that does not announce itself: 'reset' returns to the initial argument and "
      "'set <n>' stores n, both without emitting, so the next number received is the next trough "
      "event. 'clear' is deliberately absent, because on a '.trough 5' it could not be told "
      "whether it meant 0 or 5. A list is Max's two-element idiom — the second number becomes the "
      "new trough and is sent out, then the first is offered to it — with anything past the second "
      "ignored and a one-element list treated as a plain number. A message with no numbers in it "
      "is ignored. Everything is a float and outlet 0 is a float outlet: Max's "
      "int-unless-the-argument-has-a-decimal-point duality is not reproduced, because '.trough 5' "
      "and '.trough 5.0' are the same parameter string here. A non-finite number is read as 0 "
      "wherever it arrives, the convention ./ , .sqrt, .zmap, .clip, .slide and .mean already use, "
      "and it matters more here than anywhere else because this object keeps what it accepts: a "
      "stored NaN would lose every later comparison and be reported forever, and a stored "
      "+infinity would make every number a new minimum while -infinity would silence the outlet "
      "for good.",
      "Int or float to offer to the stored minimum, sent out only if it is less / bang to send the "
      "stored minimum out / list '<input> <trough>' to reseed with the second number and then "
      "offer the first / list 'set <n>' to store n silently / list 'reset' to return to the "
      "initial argument.",
      "The new minimum value. Stored and sent out — a number received here is always a new "
      "minimum.",
      "New minimum values. Silent when the number received was not less than the stored minimum.",
      "128",
      "Starting minimum, what a bang reports before any input, and the value 'reset' returns to. "
      "Max starts trough at 128, high enough that ordinary input can beat it — 0 could not be "
      "beaten by anything positive.");
}
