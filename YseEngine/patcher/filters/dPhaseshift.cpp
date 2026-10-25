#include "dPhaseshift.h"
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

#define className dPhaseshift

CONSTRUCT_DSP() {
  // in 0: the signal to filter; messages
  ADD_IN_0;
  REG_SIGNAL_IN;

  // in 1: the center frequency, a number or a signal; messages
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFrequency);
  REG_INT_IN(SetFrequencyInt);

  // in 2: the Q, a number or a signal; messages
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetQ);
  REG_INT_IN(SetQInt);

  // out 0: the phase-shifted signal
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("Second-order allpass with a center frequency in Hz and a Q, both settable "
                  "per sample: the level is unchanged, the phase turns from 0 to -360 degrees "
                  "and is -180 at the center frequency. For phaser stages and phase "
                  "distortion.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "in",
            "The signal to filter (unconnected reads 0). 'clear' sets the filter state to 0; a "
            "number, or a list of up to two numbers (center frequency, Q), sets the numbers the "
            "other inlets read.",
            "");
  INLET_DOC(1, "frequency",
            "The center frequency in Hz, where the phase is -180 degrees, a number or a signal "
            "read per sample (unconnected, the last number sent here or the creation argument). "
            "Clamped to just inside 0 to half the sample rate. Takes the same words as the left "
            "inlet.",
            "0 to samplerate/2 Hz");
  INLET_DOC(2, "q",
            "The Q, a number or a signal read per sample (unconnected, the last number sent "
            "here or the creation argument): higher turns the phase over a narrower band. "
            "Clamped to 0.01-10000. Takes the same words as the left inlet.",
            "0.01-10000");
  OUTLET_DOC(0, "out", "The phase-shifted signal, at the level of the input.", "");
  PARAM_DOC("frequency", "1000",
            "The center frequency in Hz and the Q, up to two numbers ('~phaseshift 800 2'; "
            "they default to 1000 and 1). Later numbers and messages do not change what is "
            "saved.",
            "a center frequency, then a Q");
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen. Clear runs first on every Set, parse only when
// there are arguments.
PARM_CLEAR() {
  arguments.clear();
  frequency = DSP::phaseShift::DEFAULT_FREQUENCY;
  q = DSP::phaseShift::DEFAULT_Q;
}

PARM_PARSE() {
  // The first number is the center frequency, the second the Q.
  int count = 0;
  for (const std::string& token : arguments) {
    float number = 0.f;
    if (!ReadNumber(token, number)) continue;
    if (count == 0)
      frequency = number;
    else
      q = number;
    if (++count == 2) return;
  }
}

FLOAT_IN(SetFrequency) {
  (void)inlet;
  (void)thread;
  if (std::isfinite(value)) frequency = value;
}

INT_IN(SetFrequencyInt) {
  (void)inlet;
  (void)thread;
  frequency = static_cast<float>(value);
}

FLOAT_IN(SetQ) {
  (void)inlet;
  (void)thread;
  if (std::isfinite(value)) q = value;
}

INT_IN(SetQInt) {
  (void)inlet;
  (void)thread;
  q = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  if (word == "clear") {
    clearRequested = true;
    return;
  }
  // A list of numbers: the center frequency, then the Q. A token that is not
  // a finite number stops it.
  float number = 0.f;
  if (!ReadNumber(word, number)) return;
  frequency = number;
  if (ReadNumber(Token(message, next, next), number)) q = number;
}

CALC() {
  if (clearRequested.exchange(false)) fn.clear();

  // Unconnected inlets (#1036): the input reads 0, the settings their
  // numbers.
  DSP::buffer& out =
      fn(inputs[0].Signal(0.f), inputs[1].Signal(frequency.load()), inputs[2].Signal(q.load()));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
