#include "dSvf.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <cctype>
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

  bool EqualsIgnoreCase(std::string_view word, std::string_view lower) {
    if (word.size() != lower.size()) return false;
    for (std::size_t i = 0; i < word.size(); ++i)
      if (std::tolower(static_cast<unsigned char>(word[i])) != lower[i]) return false;
    return true;
  }

  // The frequency mode a word names, or -1.
  int ToMode(std::string_view word) {
    if (EqualsIgnoreCase(word, "hz")) return YSE::DSP::stateVariableFilter::HZ;
    if (EqualsIgnoreCase(word, "linear")) return YSE::DSP::stateVariableFilter::LINEAR;
    if (EqualsIgnoreCase(word, "radians")) return YSE::DSP::stateVariableFilter::RADIANS;
    return -1;
  }

  // The settings in list order: a bare number fills the next one.
  enum class keyword { frequency, resonance, none };

  keyword ToKeyword(std::string_view word) {
    if (word == "freq" || word == "cf") return keyword::frequency;
    if (word == "q" || word == "resonance") return keyword::resonance;
    return keyword::none;
  }

  bool ReadNumber(std::string_view token, float& out) {
    return ReadNumericToken(token.data(), token.size(), out) && std::isfinite(out);
  }

  // Stores `number` in the setting `k` names.
  void Assign(keyword k, float number, std::atomic<float>& frequency,
              std::atomic<float>& resonance) {
    if (k == keyword::frequency)
      frequency = number;
    else if (k == keyword::resonance)
      resonance = number;
  }
} // namespace

#define className dSvf

CONSTRUCT_DSP() {
  // in 0: the signal to filter; messages
  ADD_IN_0;
  REG_SIGNAL_IN;

  // in 1: the center frequency, a number or a signal; messages
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFrequency);
  REG_INT_IN(SetFrequencyInt);

  // in 2: the resonance, a number or a signal; messages
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetResonance);
  REG_INT_IN(SetResonanceInt);

  // out 0-3: lowpass, highpass, bandpass, notch
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("State-variable filter giving a lowpass, a highpass, a bandpass and a notch "
                  "output at once from one center frequency and one resonance, with the "
                  "frequency in Hz or on a 0-1 scale. Stable at every setting.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "in",
            "The signal to filter (unconnected reads 0). 'hz', 'linear' and 'radians' set how "
            "the frequency is read; 'clear' sets the filter state to 0; 'freq <x>' (or 'cf "
            "<x>') and 'q <x>' (or 'resonance <x>'), or a list of up to two numbers "
            "(frequency, resonance), set the numbers the other inlets read.",
            "");
  INLET_DOC(1, "frequency",
            "The center frequency, a number or a signal read per sample (unconnected, the last "
            "number sent here or the creation argument). In hz mode 0 to a quarter of the "
            "sample rate; in linear mode 0-1 across that range; in radians mode 0-1 as the "
            "sine of the frequency in radians per sample. Takes the same words as the left "
            "inlet.",
            "0 to samplerate/4 Hz, or 0-1");
  INLET_DOC(2, "resonance",
            "The resonance, a number or a signal read per sample (unconnected, the last number "
            "sent here or the creation argument): 0 is no peak, 0.5 a Q of 1, 1 a Q of 1000. "
            "Clamped to 0-1. Takes the same words as the left inlet.",
            "0-1");
  OUTLET_DOC(0, "lowpass", "The lowpass output.", "");
  OUTLET_DOC(1, "highpass", "The highpass output.", "");
  OUTLET_DOC(2, "bandpass", "The bandpass output, with a gain of the Q at the center.", "");
  OUTLET_DOC(3, "notch", "The notch (band-reject) output.", "");
  PARAM_DOC("frequency", "0",
            "The center frequency and the resonance, up to two numbers (they default to 0 and "
            "0.01), and a mode word: 'hz' (the default), 'linear' or 'radians', e.g. '~svf "
            "0.25 0.8 linear'; or 'freq <x>' and 'q <x>'. Later numbers and messages do not "
            "change what is saved.",
            "a frequency, a resonance, then a mode word");
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen. Clear runs first on every Set, parse only when
// there are arguments.
PARM_CLEAR() {
  arguments.clear();
  frequency = 0.f;
  resonance = 0.01f;
  mode = DSP::stateVariableFilter::HZ;
}

PARM_PARSE() {
  int bare = 0;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const std::string& token = arguments[i];
    const int m = ToMode(token);
    if (m >= 0) {
      mode = m;
      continue;
    }
    float number = 0.f;
    const keyword word = ToKeyword(token);
    // A keyword takes the number after it; a keyword with none is skipped.
    if (word != keyword::none) {
      if (i + 1 < arguments.size() && ReadNumber(arguments[i + 1], number)) {
        ++i;
        Assign(word, number, frequency, resonance);
      }
      continue;
    }
    // Bare numbers: the frequency, then the resonance.
    if (bare < 2 && ReadNumber(token, number)) {
      Assign(static_cast<keyword>(bare), number, frequency, resonance);
      bare++;
    }
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

FLOAT_IN(SetResonance) {
  (void)inlet;
  (void)thread;
  if (std::isfinite(value)) resonance = value;
}

INT_IN(SetResonanceInt) {
  (void)inlet;
  (void)thread;
  resonance = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  if (word == "clear") {
    clearRequested = true;
    return;
  }
  const int m = ToMode(word);
  if (m >= 0) {
    mode = m;
    return;
  }
  float number = 0.f;
  const keyword k = ToKeyword(word);
  if (k != keyword::none) {
    if (ReadNumber(Token(message, next, next), number)) Assign(k, number, frequency, resonance);
    return;
  }
  // A list of numbers: the frequency, then the resonance. A token that is
  // not a finite number stops it.
  std::string_view token = word;
  for (int i = 0; i < 2 && ReadNumber(token, number); ++i) {
    Assign(static_cast<keyword>(i), number, frequency, resonance);
    token = Token(message, next, next);
  }
}

CALC() {
  fn.mode(static_cast<DSP::stateVariableFilter::FREQUENCY_MODE>(mode.load()));
  if (clearRequested.exchange(false)) fn.clear();

  // Unconnected inlets (#1036): the input reads 0, the settings their
  // numbers.
  fn(inputs[0].Signal(0.f), inputs[1].Signal(frequency.load()), inputs[2].Signal(resonance.load()));
  outputs[0].SendBuffer(&fn.lowpass(), thread);
  outputs[1].SendBuffer(&fn.highpass(), thread);
  outputs[2].SendBuffer(&fn.bandpass(), thread);
  outputs[3].SendBuffer(&fn.notch(), thread);
}

#undef className
