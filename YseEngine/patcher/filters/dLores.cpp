#include "dLores.h"
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

  enum class keyword { none, cutoff, resonance };

  keyword ToKeyword(std::string_view word) {
    if (word == "cutoff") return keyword::cutoff;
    if (word == "resonance") return keyword::resonance;
    return keyword::none;
  }

  bool ReadNumber(std::string_view token, float& out) {
    return ReadNumericToken(token.data(), token.size(), out) && std::isfinite(out);
  }
} // namespace

#define className dLores

CONSTRUCT_DSP() {
  // in 0: the signal to filter; messages
  ADD_IN_0;
  REG_SIGNAL_IN;

  // in 1: the cutoff, a number or a signal; messages
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetCutoff);
  REG_INT_IN(SetCutoffInt);

  // in 2: the resonance, a number or a signal; messages
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetResonance);
  REG_INT_IN(SetResonanceInt);

  // out 0: the filtered signal
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("Two-pole resonant lowpass filter, 12 dB per octave, with a cutoff in Hz and a "
                  "resonance from 0 to 1: the classic synth filter, from a flat roll-off to a "
                  "ringing peak at the cutoff.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "in",
            "The signal to filter (unconnected reads 0). 'clear' sets the filter state to 0; "
            "'cutoff <x>' and 'resonance <x>' set the numbers the other inlets read.",
            "");
  INLET_DOC(1, "cutoff",
            "The cutoff in Hz, a number or a signal read per sample (unconnected, the last "
            "number sent here or the creation argument). Clamped to half the sample rate; 0 "
            "passes nothing. Takes the same words as the left inlet.",
            "0 to samplerate/2 Hz");
  INLET_DOC(2, "resonance",
            "The resonance, a number or a signal read per sample (unconnected, the last number "
            "sent here or the creation argument): 0 is a flat roll-off, values toward 1 a "
            "sharper peak at the cutoff, clamped to 0.999. Takes the same words as the left "
            "inlet.",
            "0-1");
  OUTLET_DOC(0, "out", "The filtered signal.", "");
  PARAM_DOC("cutoff", "0",
            "The cutoff in Hz and the resonance, two numbers ('~lores 800 0.7'; the resonance "
            "defaults to 0), or 'cutoff <x>' and 'resonance <x>'. Later numbers and messages "
            "do not change what is saved.",
            "a cutoff, then a resonance");
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen. Clear runs first on every Set, parse only when
// there are arguments.
PARM_CLEAR() {
  arguments.clear();
  cutoff = 0.f;
  resonance = 0.f;
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
        if (word == keyword::cutoff)
          cutoff = number;
        else
          resonance = number;
      }
      continue;
    }
    // The first bare number is the cutoff, the second the resonance.
    if (bare < 2 && ReadNumber(token, number)) {
      if (bare == 0)
        cutoff = number;
      else
        resonance = number;
      bare++;
    }
  }
}

FLOAT_IN(SetCutoff) {
  (void)inlet;
  (void)thread;
  if (std::isfinite(value)) cutoff = value;
}

INT_IN(SetCutoffInt) {
  (void)inlet;
  (void)thread;
  cutoff = static_cast<float>(value);
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
  const keyword k = ToKeyword(word);
  if (k == keyword::none) return;
  float number = 0.f;
  if (!ReadNumber(Token(message, next, next), number)) return;
  if (k == keyword::cutoff)
    cutoff = number;
  else
    resonance = number;
}

CALC() {
  if (clearRequested.exchange(false)) fn.clear();

  // Unconnected inlets (#1036): the input reads 0, cutoff and resonance their
  // numbers.
  DSP::buffer& out = fn(inputs[0].Signal(0.f), inputs[1].Signal(cutoff.load()),
                        inputs[2].Signal(resonance.load()));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
