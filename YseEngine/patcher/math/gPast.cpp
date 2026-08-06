#include "gPast.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "gExprEval.h"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gPast

CONSTRUCT() {
  // One inlet, as in Max. See "No bang inlet, and one inlet rather than two" in
  // the header for why the threshold does not get a cold inlet of its own.
  ADD_IN_0;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);
  REG_LIST_IN(SetList);

  // A bang, and only a bang: the object reports that a line was crossed, not
  // what crossed it.
  ADD_OUT_BANG;

  ADD_PARAM(thresholdArgs);
  // The thresholds have to be read out of the argument tokens *after* they are
  // parsed, which is what the parse callback is for; the clear callback is what
  // makes `SetParams("")` return the object to a bare `.past` rather than
  // leaving the previous list in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // The state a bare `.past` starts in: one threshold of 0, nothing met yet.
  // ClearParams() is the same reset, and is not called for an object that is
  // never given parameters at all.
  count = 1;
  initialCount = 1;
  thresholds[0] = 0.f;
  initialThresholds[0] = 0.f;
  Rearm();

  ADD_DESCRIPTION(
      "Sends a bang the moment a value crosses a threshold, and then stays quiet until the value "
      "drops back below and crosses again — edge detection on continuous control data, where the "
      "interesting event is the crossing rather than the thousand messages that follow while the "
      "value stays up there. A .>= into a .sel 1 would fire on every one of them; this fires once. "
      "Max states the rule in two halves and only both together define the object: it bangs when "
      "every number is at or above its threshold having not been so before, and it re-arms only "
      "when a number goes back BELOW (strictly less than) its threshold. The two are exact "
      "complements, so the object is a plain latch on 'value >= threshold' rather than a "
      "hysteresis band, and two edges follow that a naive implementation gets wrong. A value "
      "exactly at the threshold has crossed it, so '.past 5' fed 5 bangs. And a value exactly at "
      "the threshold does not re-arm it, so 10, 5, 10 bangs once rather than twice: equality "
      "belongs to the 'past it' side of the line going up and coming down alike. A freshly created "
      "object is un-latched, so a value that is already above the threshold when the first message "
      "arrives bangs immediately — nothing has met the threshold yet, so the first thing that does "
      "is a crossing, which is also the state 'clear' restores. The creation argument is a list "
      "and "
      "so is the input: numbers are compared element-wise against the corresponding thresholds and "
      "the bang is the conjunction, so '.past 60 100' is an AND gate of two thresholds in one box. "
      "Each element keeps its own met/not-met flag, which is what Max's per-element re-arm wording "
      "describes, so a list shorter than the threshold list updates only the elements it supplies "
      "and a plain int or float is simply the one-element case; numbers past the last threshold "
      "are "
      "ignored, and at most 256 thresholds are held. 'clear' drops every flag so the next crossing "
      "bangs again, silently and without touching the thresholds. 'set <numbers>' replaces the "
      "thresholds and deliberately does NOT re-arm, because a patch that drives the threshold from "
      "elsewhere sends 'set' continuously and an object that re-armed on each one would bang for "
      "every message above the line, which is a .>= and not a .past; surviving elements keep their "
      "flags, elements the new list adds start un-met, and the latch is re-derived from the flags. "
      "'reset' is the patcher family's word rather than a Max message and means both halves at "
      "once — back to the creation-argument thresholds with a cleared latch — since after a 'set' "
      "there is otherwise no way back to the creation argument. All three are silent, and a "
      "message "
      "that is neither a word this object knows nor a list containing a number is ignored. A bang "
      "is not accepted: a message carrying no number cannot cross anything. Everything is a float, "
      "so Max's int-unless-the-argument-has-a-decimal-point duality is not reproduced. A "
      "non-finite "
      "number is read as 0 wherever it arrives, in the input and in the thresholds alike. On the "
      "threshold side that is load-bearing rather than tidy: a NaN compares false against "
      "everything, so a NaN threshold could never be met and would make the object permanently "
      "mute — and in a multi-threshold object it would mute the conjunction while every other "
      "element carried on working. A +infinity threshold does the same, and a -infinity one is met "
      "by every number there is, including the ones a patch sends to bring the value back down, so "
      "its element could never re-arm. On the input side the substitution buys consistency rather "
      "than safety: it is simply the answer the rest of the patcher gives a NaN it was handed.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "in",
            "Int, float or list to compare against the thresholds — a bang goes out the moment "
            "every element is at or above its own threshold, having not been so before. Also "
            "accepts the messages 'clear' (drop every met flag so the next crossing bangs again), "
            "'set <numbers>' (replace the thresholds, keeping the latch) and 'reset' (back to the "
            "creation-argument thresholds with the latch cleared). All three are silent.",
            "any list of floats");
  OUTLET_DOC(0, "cross",
             "Bang when every number is at or above its threshold, having not been so before. "
             "Silent until a number drops strictly below its threshold and the crossing happens "
             "again.",
             "bang");
  PARAM_DOC("thresholds", "0",
            "The thresholds to watch, as a list. A number must be at or above its threshold for "
            "the object to consider that element crossed, and all of them must be for the bang to "
            "go out. Defaults to a single threshold of 0; at most 256 are held.",
            "any list of floats");
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave a usable object
  // behind rather than an empty threshold list.
  thresholdArgs.clear();
  count = 1;
  initialCount = 1;
  thresholds[0] = 0.f;
  initialThresholds[0] = 0.f;
  Rearm();
}

PARM_PARSE() {
  // Runs on the control thread once the creation parameters have been read.
  // Reading the tokens here rather than in the constructor is what lets a saved
  // `.past 60 100` come back watching two thresholds.
  count = 0;
  for (std::size_t i = 0; i < thresholdArgs.size() && count < MAX_THRESHOLDS; i++) {
    float parsed = 0.f;
    // A token that is not a number is skipped rather than stored as a zero
    // nobody asked for — a stray word in the argument list would otherwise
    // insert a threshold of 0 that every positive number meets.
    if (ExprParseFloatList(thresholdArgs[i].c_str(), &parsed, 1) != 1) continue;
    thresholds[count++] = ExtremumSanitize(parsed);
  }

  // A `.past` with no numeric argument watches a single threshold of 0, which
  // is the object Max's optional argument leaves behind.
  if (count == 0) {
    count = 1;
    thresholds[0] = 0.f;
  }

  // What `reset` returns to.
  initialCount = count;
  for (int i = 0; i < count; i++)
    initialThresholds[i] = thresholds[i];

  Rearm();
}

bool gPast::AllMet() const {
  for (int i = 0; i < count; i++) {
    if (!met[i]) return false;
  }
  return true;
}

void gPast::Rearm() {
  // Every flag, not just the ones below `count`: a later `set` that grows the
  // list must not find a stale true left over from a longer list before it.
  for (int i = 0; i < MAX_THRESHOLDS; i++)
    met[i] = false;
  latched = false;
}

void gPast::Install(const float* values, int newCount) {
  if (newCount > MAX_THRESHOLDS) newCount = MAX_THRESHOLDS;
  if (newCount < 1) return;

  // Elements the new list adds have had nothing compared against them yet.
  // Elements that survive keep their flags, which is what makes a threshold
  // change carry the latch across — see "clear, set and reset" in the header
  // for why re-arming here would turn the object into a .>=.
  for (int i = count; i < newCount; i++)
    met[i] = false;

  for (int i = 0; i < newCount; i++)
    thresholds[i] = ExtremumSanitize(values[i]);
  count = newCount;

  // Re-derived rather than carried: the latch means "the conjunction held last
  // time it was evaluated", and the conjunction is over a different set of
  // elements now. Growing the list can therefore legitimately un-latch it.
  latched = AllMet();
}

void gPast::Compare(const float* values, int valueCount, YSE::THREAD thread) {
  // Numbers past the last threshold have nothing to be compared against, and a
  // short list updates only the elements it supplies — Max's flags are per
  // element, so the ones it does not mention keep what they had.
  const int n = valueCount < count ? valueCount : count;
  for (int i = 0; i < n; i++) {
    // At or above, not above: Max's "equaled or exceeded". A non-finite number
    // reads as 0, so a NaN cannot masquerade as "below the threshold" and
    // re-arm the object behind the patch's back.
    met[i] = ExtremumSanitize(values[i]) >= thresholds[i];
  }

  const bool all = AllMet();
  // The rising edge, and only the rising edge.
  const bool crossing = all && !latched;
  // Settled before anything is sent, so nothing reached from the outlet can
  // observe the object half-updated.
  latched = all;
  if (crossing) outputs[0].SendBang(thread);
}

FLOAT_IN(SetFloat) {
  if (inlet != 0) return;
  // A number is the one-element case, which is also how Max routes it.
  Compare(&value, 1, thread);
}

INT_IN(SetInt) {
  // One numeric type throughout — see "One numeric type" in the header.
  SetFloat(static_cast<float>(value), inlet, thread);
}

LIST_IN(SetList) {
  if (inlet != 0) return;

  // Max: "Causes past to forget previously received input, readying it to send
  // a bang message again." The thresholds are not touched, and nothing is sent.
  if (value == "clear") {
    Rearm();
    return;
  }

  // The family's word, with .counter's / .accum's / .peak's meaning: back to
  // how the object was created. Both halves, because the whole state is the
  // only unambiguous thing the word can mean here.
  if (value == "reset") {
    count = initialCount;
    for (int i = 0; i < count; i++)
      thresholds[i] = initialThresholds[i];
    Rearm();
    return;
  }

  // Bounded by MAX_THRESHOLDS into a stack array. ExprParseFloatList neither
  // allocates nor touches locale state, so this is safe on whichever thread the
  // message arrived on.
  float items[MAX_THRESHOLDS];

  // Max: "The word set, followed by one or more numbers, sets the numbers which
  // must be equaled or exceeded." MatchWord requires the separator, so
  // `settle 3` is not read as `set tle 3`.
  std::size_t argument = 0;
  if (MatchWord(value, "set", 3, argument)) {
    const int read = ExprParseFloatList(value.c_str() + argument, items, MAX_THRESHOLDS);
    // `set wobble` keeps the thresholds it had rather than installing nothing.
    if (read > 0) Install(items, read);
    return;
  }

  const int read = ExprParseFloatList(value.c_str(), items, MAX_THRESHOLDS);

  // A message with no numbers in it and no word this object knows. Ignored
  // rather than guessed at, as in .slide, .mean, .accum, .maximum and .peak.
  if (read == 0) return;

  Compare(items, read, thread);
}

GUI_VALUE() {
  // The latch, which is the object's whole state: 1 while it is waiting for a
  // value to drop back below a threshold, 0 while it is armed.
  return latched ? "1" : "0";
}
