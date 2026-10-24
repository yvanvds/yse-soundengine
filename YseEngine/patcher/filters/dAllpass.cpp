#include "dAllpass.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <cmath>
#include <string_view>

using namespace YSE::PATCHER;

namespace {
  // The token starting at or after `from` in `text`, walked in place so
  // nothing allocates on whichever thread delivers the message. `next` is
  // set to where the token ends. Empty when there is none.
  std::string_view Token(const std::string& text, std::size_t from, std::size_t& next) {
    const char* chars = text.c_str();
    const std::size_t size = text.size();
    std::size_t begin = from;
    while (begin < size && IsSelectorSeparator(chars[begin]))
      begin++;
    std::size_t end = begin;
    while (end < size && !IsSelectorSeparator(chars[end]))
      end++;
    next = end;
    return std::string_view(chars + begin, end - begin);
  }

  bool ReadNumber(std::string_view token, float& out) {
    return ReadNumericToken(token.data(), token.size(), out) && std::isfinite(out);
  }
} // namespace

#define className dAllpass

CONSTRUCT_DSP() {
  // in 0: the signal to filter; messages
  ADD_IN_0;
  REG_SIGNAL_IN;

  // in 1: the delay in ms, a number or a signal; messages
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetDelay);
  REG_INT_IN(SetDelayInt);

  // in 2: the gain, a number or a signal; messages
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetGain);
  REG_INT_IN(SetGainInt);

  // out 0: the filtered signal
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("Schroeder allpass filter, y(n) = -g x(n) + x(n-D) + g y(n-D), on an "
                  "interpolated delay line with the delay in ms and the gain each a number or a "
                  "signal read per sample: every frequency passes, only the phase changes. For "
                  "reverb diffusers and phase smearing.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "in",
            "The signal to filter (unconnected reads 0). 'clear' silences the delay lines; a "
            "list of up to two numbers (delay in ms, gain) sets the numbers the other inlets "
            "read.",
            "");
  INLET_DOC(1, "delay",
            "The delay in ms, a number or a signal read per sample (unconnected, the last number "
            "sent here or the creation argument). Fractional delays are interpolated. Clamped to "
            "one sample at the least and the maximum delay at the most. 'clear' and a list of "
            "two numbers act as in the left inlet.",
            "1 sample to the maximum delay");
  INLET_DOC(2, "gain",
            "The gain, a number or a signal read per sample (unconnected, the last number sent "
            "here or the creation argument): how much of each echo feeds back. 0 is a plain "
            "delay; at 1 or -1 the filter passes the signal times -gain. Clamped to -1 to 1. "
            "'clear' and a list of two numbers act as in the left inlet.",
            "-1 to 1");
  OUTLET_DOC(0, "out", "The filtered signal.", "");
  PARAM_DOC("maxdelay", "10",
            "The maximum delay in ms, the initial delay in ms and the gain, up to three numbers "
            "('~allpass 50 12.5 0.7'; they default to 10, 0 and 0). The maximum delay sizes the "
            "delay lines, for up to 192 kHz, and is clamped to 0-60000. Later numbers and "
            "messages do not change what is saved.",
            "a maximum delay, a delay, then a gain");
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen, and the delay lines may be allocated here. Clear
// runs first on every Set, parse only when there are arguments.
PARM_CLEAR() {
  arguments.clear();
  delay = 0.f;
  gain = 0.f;
  if (fn.maximumDelay() != DSP::allpass::DEFAULT_MAXIMUM_DELAY) fn = DSP::allpass();
}

PARM_PARSE() {
  float values[3] = {DSP::allpass::DEFAULT_MAXIMUM_DELAY, 0.f, 0.f};
  int count = 0;
  for (const std::string& token : arguments) {
    if (count == 3) break;
    float number = 0.f;
    if (ReadNumber(token, number)) values[count++] = number;
  }
  if (values[0] != fn.maximumDelay()) fn = DSP::allpass(values[0]);
  delay = values[1];
  gain = values[2];
}

FLOAT_IN(SetDelay) {
  (void)inlet;
  (void)thread;
  if (std::isfinite(value)) delay = value;
}

INT_IN(SetDelayInt) {
  (void)inlet;
  (void)thread;
  delay = static_cast<float>(value);
}

FLOAT_IN(SetGain) {
  (void)inlet;
  (void)thread;
  if (std::isfinite(value)) gain = value;
}

INT_IN(SetGainInt) {
  (void)inlet;
  (void)thread;
  gain = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  std::size_t next = 0;
  std::string_view token = Token(message, 0, next);
  if (token == "clear") {
    clearRequested = true;
    return;
  }
  // A list of numbers: the delay, then the gain. A token that is not a
  // finite number stops it.
  float number = 0.f;
  if (!ReadNumber(token, number)) return;
  delay = number;
  token = Token(message, next, next);
  if (ReadNumber(token, number)) gain = number;
}

CALC() {
  if (clearRequested.exchange(false)) fn.clear();

  // Unconnected inlets (#1036): the input reads 0, the settings their
  // numbers.
  DSP::buffer& out =
      fn(inputs[0].Signal(0.f), inputs[1].Signal(delay.load()), inputs[2].Signal(gain.load()));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
