#include "dOnePole.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <cctype>
#include <cmath>
#include <string_view>

using namespace YSE::PATCHER;

namespace {
  // The first token of `message`, walked in place so nothing allocates on
  // whichever thread delivers the message. Empty when there is none.
  std::string_view FirstToken(const std::string& message) {
    const char* text = message.c_str();
    const std::size_t size = message.size();
    std::size_t begin = 0;
    while (begin < size && IsSelectorSeparator(text[begin]))
      begin++;
    std::size_t end = begin;
    while (end < size && !IsSelectorSeparator(text[end]))
      end++;
    return std::string_view(text + begin, end - begin);
  }

  bool EqualsIgnoreCase(std::string_view word, std::string_view lower) {
    if (word.size() != lower.size()) return false;
    for (std::size_t i = 0; i < word.size(); ++i)
      if (std::tolower(static_cast<unsigned char>(word[i])) != lower[i]) return false;
    return true;
  }

  // The frequency mode a word names, or -1.
  int ToMode(std::string_view word) {
    if (EqualsIgnoreCase(word, "hz")) return YSE::DSP::onePole::HZ;
    if (EqualsIgnoreCase(word, "linear")) return YSE::DSP::onePole::LINEAR;
    if (EqualsIgnoreCase(word, "radians")) return YSE::DSP::onePole::RADIANS;
    return -1;
  }
} // namespace

#define className dOnePole

CONSTRUCT_DSP() {
  // in 0: the signal to filter; messages
  ADD_IN_0;
  REG_SIGNAL_IN;

  // in 1: the frequency, a number or a signal; messages
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFrequency);
  REG_INT_IN(SetFrequencyInt);

  // out 0: the filtered signal
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("Single-pole lowpass filter, 6 dB per octave, with its cutoff in Hz or on a 0-1 "
                  "scale: cheap smoothing of a control signal or a gentle roll-off of harsh "
                  "highs.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "in",
            "The signal to filter (unconnected reads 0). 'hz', 'linear' and 'radians' set how "
            "the frequency is read; 'clear' sets the filter state to 0.",
            "");
  INLET_DOC(1, "frequency",
            "The cutoff, a number or a signal read once per block (unconnected, the last number "
            "sent here or the creation argument). In hz mode 0 to a quarter of the sample rate; "
            "in linear mode 0-1 across that range; in radians mode 0-1 as the filter "
            "coefficient. Takes the same words as the left inlet.",
            "0 to samplerate/4 Hz, or 0-1");
  OUTLET_DOC(0, "out", "The filtered signal.", "");
  PARAM_DOC("frequency", "0",
            "The cutoff, the first number, read in the mode a word among the arguments names: "
            "'hz' (the default), 'linear' or 'radians', e.g. '~onepole 0.5 linear'. Later "
            "numbers and messages do not change what is saved.",
            "a number, then a mode word");
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen. Clear runs first on every Set, parse only when
// there are arguments.
PARM_CLEAR() {
  arguments.clear();
  frequency = 0.f;
  mode = DSP::onePole::HZ;
}

PARM_PARSE() {
  bool haveFrequency = false;
  for (const std::string& token : arguments) {
    const int m = ToMode(token);
    if (m >= 0) {
      mode = m;
      continue;
    }
    // The first bare number is the frequency; later ones are ignored.
    float number = 0.f;
    if (!haveFrequency && ReadNumericToken(token, number) && std::isfinite(number)) {
      frequency = number;
      haveFrequency = true;
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

MESSAGES() {
  (void)value;
  const std::string_view word = FirstToken(message);
  if (word == "clear") {
    clearRequested = true;
    return;
  }
  const int m = ToMode(word);
  if (m >= 0) mode = m;
}

CALC() {
  fn.mode(static_cast<DSP::onePole::FREQUENCY_MODE>(mode.load()));
  if (clearRequested.exchange(false)) fn.clear();

  // Unconnected inlets (#1036): the input reads 0, the frequency its number.
  DSP::buffer& out = fn(inputs[0].Signal(0.f), inputs[1].Signal(frequency.load()));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
