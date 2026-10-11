#include "dTapin.h"
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

#define className dTapin

namespace {
  constexpr float DEFAULT_MAXDELAY = 100.f;
} // namespace

CONSTRUCT_DSP(), line(DEFAULT_MAXDELAY) {
  maxdelay = DEFAULT_MAXDELAY;
  pendingMaximum = DEFAULT_MAXDELAY;

  // in 0: the signal to record, a new maximum delay, and clear / freeze
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetMaximum);
  REG_INT_IN(SetMaximumInt);

  // out 0: the link to ~tapout
  ADD_OUT_LINK;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(maxdelay);

  ADD_DESCRIPTION("The input of a delay line. Connect its outlet to the left inlet of ~tapout, "
                  "which reads the line at one or more delay times. Feedback from ~tapout back "
                  "into ~tapin is allowed.");
  ADD_CATEGORY(pCategory::DELAY);
  INLET_DOC(0, "in",
            "The signal to record. A number sets the maximum delay in ms (up to the maxdelay "
            "argument) and clears the line. Also takes 'clear' and 'freeze <0/1>'.",
            "any signal");
  OUTLET_DOC(0, "line",
             "The delay line, shared with ~tapout. Connects only to the left inlet of ~tapout; "
             "carries no samples itself.",
             "");
  PARAM_DOC("maxdelay", "100",
            "The longest delay in ms. The line's memory is allocated for this, at up to 192 kHz.",
            "0 or more");
}

PARM_CLEAR() {
  maxdelay = DEFAULT_MAXDELAY;
  line = DSP::delayLine(DEFAULT_MAXDELAY);
}

// Control thread, before the object is published: a live change of maxdelay
// rebuilds the object (the callbacks make ParamsNeedRebuild() true), so the
// allocation never happens on a line the audio thread is reading.
PARM_PARSE() {
  if (maxdelay < 0.f) maxdelay = 0.f;
  line = DSP::delayLine(maxdelay);
}

FLOAT_IN(SetMaximum) {
  pendingMaximum = value;
  maximumPending = true;
}

INT_IN(SetMaximumInt) {
  pendingMaximum = static_cast<float>(value);
  maximumPending = true;
}

// `clear` and `freeze <0/1>`. Walked in place, as dPhasor does, so this
// allocates nothing on whichever thread delivers it.
MESSAGES() {
  (void)value;
  const char* text = message.c_str();
  const std::size_t length = message.size();
  std::size_t begin = 0;
  while (begin < length && IsSelectorSeparator(text[begin]))
    begin++;
  std::size_t end = begin;
  while (end < length && !IsSelectorSeparator(text[end]))
    end++;
  const std::size_t word = end - begin;

  if (word == 5 && message.compare(begin, 5, "clear") == 0) {
    clearPending = true;
    return;
  }
  if (word == 6 && message.compare(begin, 6, "freeze") == 0) {
    std::size_t arg = end;
    while (arg < length && IsSelectorSeparator(text[arg]))
      arg++;
    std::size_t argEnd = arg;
    while (argEnd < length && !IsSelectorSeparator(text[argEnd]))
      argEnd++;
    float number = 1.f;
    if (argEnd > arg && !ReadNumericToken(text + arg, argEnd - arg, number)) return;
    frozen = number != 0.f;
  }
}

RESET() // {
// The top of the block, before any ~tapout reads: requests land here so every
// tap hears them in the same block, whichever runs first.
// A new maximum clears the line too, so a clear in the same block is done.
const bool clear = clearPending.exchange(false);
if (maximumPending.exchange(false)) {
  line.maximumDelay(pendingMaximum);
} else if (clear) {
  line.clear();
}
line.freeze(frozen);
line.beginBlock();
}

CALC() {
  (void)thread;
  line.write(inputs[0].Signal(0.f));
}

#undef className
