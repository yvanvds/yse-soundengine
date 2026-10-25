#include "dFiltercoeff.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <cmath>
#include <string_view>

using namespace YSE::PATCHER;
using YSE::DSP::filterCoefficients;

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

  // The type words, in the order of filterCoefficients::TYPE.
  constexpr std::string_view TYPE_NAMES[filterCoefficients::TYPE_COUNT] = {
      "off",       "lowpass",   "highpass",     "bandpass", "bandstop",  "peaknotch",
      "lowshelf",  "highshelf", "resonant",     "allpass",  "gainlpass", "gainhpass",
      "gainbpass", "gainbstop", "gainresonant", "gainapass"};

  // The type `word` names, or -1.
  int ToType(std::string_view word) {
    for (int i = 0; i < filterCoefficients::TYPE_COUNT; ++i)
      if (word == TYPE_NAMES[i]) return i;
    return -1;
  }

  // Whether a type reads the gain, and so takes it among its arguments.
  bool HasGain(int type) {
    return type == filterCoefficients::PEAKNOTCH || type == filterCoefficients::LOWSHELF ||
           type == filterCoefficients::HIGHSHELF || type >= filterCoefficients::GAINLPASS;
  }

  bool ReadNumber(std::string_view token, float& out) {
    return ReadNumericToken(token.data(), token.size(), out) && std::isfinite(out);
  }

  // A resampling factor from `number`: whole and in the DSP class's range.
  unsigned int ToFactor(float number) {
    constexpr auto most = static_cast<float>(filterCoefficients::MAX_RESAMPLING);
    if (number < 1.f) return 1u;
    return number > most ? filterCoefficients::MAX_RESAMPLING : static_cast<unsigned int>(number);
  }

  // The settings a list of numbers fills, in order.
  struct targets {
    std::atomic<float>* slot[3];
  };
} // namespace

#define className dFiltercoeff

CONSTRUCT_DSP() {
  // in 0: the frequency, a number or a signal; messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFrequency);
  REG_INT_IN(SetFrequencyInt);

  // in 1: the gain, a number or a signal; messages
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetGain);
  REG_INT_IN(SetGainInt);

  // in 2: the Q, a number or a signal; messages
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetQ);
  REG_INT_IN(SetQInt);

  // out 0-4: a0 a1 a2 b1 b2
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("Biquad filter coefficients from a frequency, a gain and a Q, computed per "
                  "sample for the usual filter types: connect the five outlets to ~biquad for "
                  "filter sweeps at audio rate.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "frequency",
            "The cutoff or center frequency in Hz, a number or a signal read per sample "
            "(unconnected, the last number sent here; 1000 at first). A filter type word "
            "('lowpass', 'peaknotch', 'off' ...) sets the type, followed by the frequency and "
            "the Q, or the frequency, the gain and the Q for a type with a gain; 'resamp <n>' "
            "computes on one sample in n; a list of up to three numbers sets the frequency, the "
            "gain and the Q.",
            "0 to samplerate/2 Hz");
  INLET_DOC(1, "gain",
            "The linear gain, a number or a signal read per sample (unconnected, the last "
            "number sent here; 1 at first). Read by peaknotch, the shelves and the gain types "
            "only. Takes the same words as the left inlet.",
            "any number, usually 0-4");
  INLET_DOC(2, "q",
            "The Q, or the slope of a shelf, a number or a signal read per sample "
            "(unconnected, the last number sent here; 0.7071 at first). Clamped to 0.01-10000. "
            "Takes the same words as the left inlet.",
            "0.01-10000");
  OUTLET_DOC(0, "a0", "The a0 coefficient, for the a0 inlet of ~biquad.", "");
  OUTLET_DOC(1, "a1", "The a1 coefficient, for the a1 inlet of ~biquad.", "");
  OUTLET_DOC(2, "a2", "The a2 coefficient, for the a2 inlet of ~biquad.", "");
  OUTLET_DOC(3, "b1", "The b1 coefficient, for the b1 inlet of ~biquad.", "");
  OUTLET_DOC(4, "b2", "The b2 coefficient, for the b2 inlet of ~biquad.", "");
  PARAM_DOC("type", "lowpass",
            "The filter type word ('~filtercoeff peaknotch 4'; lowpass when left out), then a "
            "resampling factor, 1 to 4096 (1 when left out). Later messages do not change what "
            "is saved.",
            "a filter type, then a resampling factor");
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen. Clear runs first on every Set, parse only when
// there are arguments.
PARM_CLEAR() {
  arguments.clear();
  type = filterCoefficients::LOWPASS;
  resampling = 1;
  frequency = 1000.f;
  gain = 1.f;
  q = 0.70710678f;
}

PARM_PARSE() {
  bool typed = false;
  for (const std::string& token : arguments) {
    float number = 0.f;
    const int t = ToType(token);
    if (!typed && t >= 0) {
      type = t;
      typed = true;
    } else if (ReadNumber(token, number)) {
      resampling = ToFactor(number);
      return;
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
  float number = 0.f;
  if (word == "resamp") {
    if (ReadNumber(Token(message, next, next), number)) resampling = ToFactor(number);
    return;
  }
  // A list of numbers: the frequency, the gain, then the Q.
  targets order{{&frequency, &gain, &q}};
  std::string_view token = word;
  const int t = ToType(word);
  if (t >= 0) {
    type = t;
    // A type without a gain takes the frequency and the Q.
    if (!HasGain(t)) order = targets{{&frequency, &q, nullptr}};
    token = Token(message, next, next);
  } else if (!ReadNumber(word, number)) {
    return;
  }
  // A token that is not a finite number stops it.
  for (int i = 0; i < 3 && order.slot[i] != nullptr && ReadNumber(token, number); ++i) {
    *order.slot[i] = number;
    token = Token(message, next, next);
  }
}

CALC() {
  fn.type(static_cast<filterCoefficients::TYPE>(type.load()));
  if (fn.resampling() != resampling.load()) fn.resampling(resampling.load());

  // Unconnected inlets (#1036): the settings their numbers.
  fn(inputs[0].Signal(frequency.load()), inputs[1].Signal(gain.load()), inputs[2].Signal(q.load()));
  for (unsigned int i = 0; i < 5; ++i)
    outputs[i].SendBuffer(&fn.output(i), thread);
}

#undef className
