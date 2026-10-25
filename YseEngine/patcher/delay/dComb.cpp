#include "dComb.h"
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

#define className dComb

CONSTRUCT_DSP() {
  // in 0: the signal to filter; messages
  ADD_IN_0;
  REG_SIGNAL_IN;

  // in 1: the delay in ms, a number or a signal; messages
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetDelay);
  REG_INT_IN(SetDelayInt);

  // in 2: the gain a, a number or a signal; messages
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetGain);
  REG_INT_IN(SetGainInt);

  // in 3: the feedforward gain b, a number or a signal; messages
  ADD_IN_3;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFeedforward);
  REG_INT_IN(SetFeedforwardInt);

  // in 4: the feedback gain c, a number or a signal; messages
  ADD_IN_4;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFeedback);
  REG_INT_IN(SetFeedbackInt);

  // out 0: the filtered signal
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("Comb filter, y(n) = a x(n) + b x(n-D) + c y(n-D), on an interpolated delay "
                  "line with the delay in ms and the three gains each a number or a signal read "
                  "per sample. For flanging, Karplus-Strong strings and resonators.");
  ADD_CATEGORY(pCategory::DELAY);
  INLET_DOC(0, "in",
            "The signal to filter (unconnected reads 0). 'clear' silences the delay lines; a "
            "list of up to four numbers (delay in ms, a, b, c) sets the numbers the other inlets "
            "read.",
            "");
  INLET_DOC(1, "delay",
            "The delay D in ms, a number or a signal read per sample (unconnected, the last "
            "number sent here or the creation argument). Fractional delays are interpolated. "
            "Clamped to one sample at the least and the maximum delay at the most. 'clear' and a "
            "list act as in the left inlet.",
            "1 sample to the maximum delay");
  INLET_DOC(2, "gain",
            "The gain a of the direct input, a number or a signal read per sample (unconnected, "
            "the last number sent here or the creation argument). 'clear' and a list act as in "
            "the left inlet.",
            "");
  INLET_DOC(3, "feedforward",
            "The gain b of the input D ms ago, a number or a signal read per sample "
            "(unconnected, the last number sent here or the creation argument). 'clear' and a "
            "list act as in the left inlet.",
            "");
  INLET_DOC(4, "feedback",
            "The gain c of the output D ms ago, a number or a signal read per sample "
            "(unconnected, the last number sent here or the creation argument): how much of each "
            "echo returns. Clamped to -1 to 1; at 1 or -1 it rings until 'clear'. 'clear' and a "
            "list act as in the left inlet.",
            "-1 to 1");
  OUTLET_DOC(0, "out", "The filtered signal.", "");
  PARAM_DOC("maxdelay", "10",
            "The maximum delay in ms, the initial delay in ms, then the gains a, b and c, up to "
            "five numbers ('~comb 50 5 1 0 0.9'; they default to 10, 0, 0, 0 and 0, which is "
            "silent). The maximum delay sizes the delay lines, for up to 192 kHz, and is clamped "
            "to 0-60000. Later numbers and messages do not change what is saved.",
            "a maximum delay, a delay, then the gains a, b and c");
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen, and the delay lines may be allocated here. Clear
// runs first on every Set, parse only when there are arguments.
PARM_CLEAR() {
  arguments.clear();
  delay = 0.f;
  gain = 0.f;
  feedforward = 0.f;
  feedback = 0.f;
  if (fn.maximumDelay() != DSP::comb::DEFAULT_MAXIMUM_DELAY) fn = DSP::comb();
}

PARM_PARSE() {
  float values[5] = {DSP::comb::DEFAULT_MAXIMUM_DELAY, 0.f, 0.f, 0.f, 0.f};
  int count = 0;
  for (const std::string& token : arguments) {
    if (count == 5) break;
    float number = 0.f;
    if (ReadNumber(token, number)) values[count++] = number;
  }
  if (values[0] != fn.maximumDelay()) fn = DSP::comb(values[0]);
  delay = values[1];
  gain = values[2];
  feedforward = values[3];
  feedback = values[4];
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

FLOAT_IN(SetFeedforward) {
  (void)inlet;
  (void)thread;
  if (std::isfinite(value)) feedforward = value;
}

INT_IN(SetFeedforwardInt) {
  (void)inlet;
  (void)thread;
  feedforward = static_cast<float>(value);
}

FLOAT_IN(SetFeedback) {
  (void)inlet;
  (void)thread;
  if (std::isfinite(value)) feedback = value;
}

INT_IN(SetFeedbackInt) {
  (void)inlet;
  (void)thread;
  feedback = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  std::size_t next = 0;
  std::string_view token = Token(message, 0, next);
  if (token == "clear") {
    clearRequested = true;
    return;
  }
  // A list of numbers: the delay, then a, b and c. A token that is not a
  // finite number stops it.
  std::atomic<float>* const targets[] = {&delay, &gain, &feedforward, &feedback};
  for (std::atomic<float>* target : targets) {
    float number = 0.f;
    if (!ReadNumber(token, number)) return;
    *target = number;
    token = Token(message, next, next);
  }
}

CALC() {
  if (clearRequested.exchange(false)) fn.clear();

  // Unconnected inlets (#1036): the input reads 0, the settings their
  // numbers.
  DSP::buffer& out =
      fn(inputs[0].Signal(0.f), inputs[1].Signal(delay.load()), inputs[2].Signal(gain.load()),
         inputs[3].Signal(feedforward.load()), inputs[4].Signal(feedback.load()));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
