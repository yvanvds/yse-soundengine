#include "dReson.h"
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

  // The settings in list order: a bare number fills the next one.
  enum class keyword { gain, frequency, q, none };

  keyword ToKeyword(std::string_view word) {
    if (word == "gain") return keyword::gain;
    if (word == "cf" || word == "freq") return keyword::frequency;
    if (word == "q") return keyword::q;
    return keyword::none;
  }

  bool ReadNumber(std::string_view token, float& out) {
    return ReadNumericToken(token.data(), token.size(), out) && std::isfinite(out);
  }

  // Stores `number` in the setting `k` names.
  void Assign(keyword k, float number, std::atomic<float>& gain, std::atomic<float>& frequency,
              std::atomic<float>& q) {
    if (k == keyword::gain)
      gain = number;
    else if (k == keyword::frequency)
      frequency = number;
    else if (k == keyword::q)
      q = number;
  }
} // namespace

#define className dReson

CONSTRUCT_DSP() {
  // in 0: the signal to filter; messages
  ADD_IN_0;
  REG_SIGNAL_IN;

  // in 1: the gain, a number or a signal; messages
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetGain);
  REG_INT_IN(SetGainInt);

  // in 2: the center frequency, a number or a signal; messages
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFrequency);
  REG_INT_IN(SetFrequencyInt);

  // in 3: the Q, a number or a signal; messages
  ADD_IN_3;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetQ);
  REG_INT_IN(SetQInt);

  // out 0: the filtered signal
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("Resonant bandpass filter with a gain, a center frequency in Hz and a Q, all "
                  "settable per sample: formants, modal synthesis, resonators. The gain at the "
                  "center frequency is the gain set, whatever the frequency and the Q.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "in",
            "The signal to filter (unconnected reads 0). 'clear' sets the filter state to 0; "
            "'gain <x>', 'cf <x>' (or 'freq <x>') and 'q <x>', or a list of up to three "
            "numbers (gain, center frequency, Q), set the numbers the other inlets read.",
            "");
  INLET_DOC(1, "gain",
            "The gain at the center frequency, a number or a signal read per sample "
            "(unconnected, the last number sent here or the creation argument). Takes the "
            "same words as the left inlet.",
            "any number, usually 0-1");
  INLET_DOC(2, "cf",
            "The center frequency in Hz, a number or a signal read per sample (unconnected, "
            "the last number sent here or the creation argument). Clamped to half the sample "
            "rate; 0 passes nothing. Takes the same words as the left inlet.",
            "0 to samplerate/2 Hz");
  INLET_DOC(3, "q",
            "The Q, the center frequency over the bandwidth, a number or a signal read per "
            "sample (unconnected, the last number sent here or the creation argument): higher "
            "is narrower and rings longer. Clamped to 0.01-10000. Takes the same words as the "
            "left inlet.",
            "0.01-10000");
  OUTLET_DOC(0, "out", "The filtered signal.", "");
  PARAM_DOC("gain", "0",
            "The gain, the center frequency in Hz and the Q, up to three numbers ('~reson 1 "
            "800 20'; they default to 0, 0 and 0.01), or 'gain <x>', 'cf <x>' and 'q <x>'. "
            "Later numbers and messages do not change what is saved.",
            "a gain, a center frequency, then a Q");
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen. Clear runs first on every Set, parse only when
// there are arguments.
PARM_CLEAR() {
  arguments.clear();
  gain = 0.f;
  frequency = 0.f;
  q = DSP::reson::MIN_Q;
}

PARM_PARSE() {
  int bare = 0;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const std::string& token = arguments[i];
    float number = 0.f;
    const keyword word = ToKeyword(token);
    // A keyword takes the number after it; a keyword with none is skipped.
    if (word != keyword::none) {
      if (i + 1 < arguments.size() && ReadNumber(arguments[i + 1], number)) {
        ++i;
        Assign(word, number, gain, frequency, q);
      }
      continue;
    }
    // Bare numbers: the gain, the center frequency, then the Q.
    if (bare < 3 && ReadNumber(token, number)) {
      Assign(static_cast<keyword>(bare), number, gain, frequency, q);
      bare++;
    }
  }
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
  float number = 0.f;
  const keyword k = ToKeyword(word);
  if (k != keyword::none) {
    if (ReadNumber(Token(message, next, next), number)) Assign(k, number, gain, frequency, q);
    return;
  }
  // A list of numbers: the gain, the center frequency, then the Q. A token
  // that is not a finite number stops it.
  std::string_view token = word;
  for (int i = 0; i < 3 && ReadNumber(token, number); ++i) {
    Assign(static_cast<keyword>(i), number, gain, frequency, q);
    token = Token(message, next, next);
  }
}

CALC() {
  if (clearRequested.exchange(false)) fn.clear();

  // Unconnected inlets (#1036): the input reads 0, the settings their
  // numbers.
  DSP::buffer& out = fn(inputs[0].Signal(0.f), inputs[1].Signal(gain.load()),
                        inputs[2].Signal(frequency.load()), inputs[3].Signal(q.load()));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
