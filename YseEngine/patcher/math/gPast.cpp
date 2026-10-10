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

  ADD_DESCRIPTION("Sends a bang when a number rises to or above a threshold, then stays silent "
                  "until the number drops below it again. With several thresholds, each item of a "
                  "list must reach its own.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "in",
            "Number or list to compare with the thresholds. 'clear': let the next crossing bang "
            "again. 'set <thresholds>': change the thresholds. 'reset': go back to the creation "
            "thresholds.",
            "any list of floats");
  OUTLET_DOC(0, "cross",
             "Bang when every item has reached its threshold, having not done so before.", "bang");
  PARAM_DOC("thresholds", "0",
            "The thresholds, one per list position. A single 0 by default; at most 256.",
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
