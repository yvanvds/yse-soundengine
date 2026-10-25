#include "dCross.h"
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

#define className dCross

CONSTRUCT_DSP() {
  // in 0: the signal to split; messages
  ADD_IN_0;
  REG_SIGNAL_IN;

  // in 1: the cutoff, a number or a signal; messages
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetCutoff);
  REG_INT_IN(SetCutoffInt);

  // out 0-1: lowpass, highpass
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("Third-order crossover: splits a signal at one cutoff into a lowpass and a "
                  "highpass band, 18 dB per octave each, that add back up to a flat response. "
                  "For multiband processing.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "in",
            "The signal to split (unconnected reads 0). 'clear' sets the filter state to 0; a "
            "number sets the cutoff the right inlet reads.",
            "");
  INLET_DOC(1, "cutoff",
            "The cutoff in Hz for both bands, a number or a signal read per sample (unconnected, "
            "the last number sent here or the creation argument). Clamped to 0.49 times the "
            "sample rate; at 0 the lowpass is silent and the highpass passes the input. Takes "
            "the same words as the left inlet.",
            "0 to 0.49 * samplerate Hz");
  OUTLET_DOC(0, "lowpass", "The band below the cutoff, 3 dB down at the cutoff.", "");
  OUTLET_DOC(1, "highpass", "The band above the cutoff, 3 dB down at the cutoff.", "");
  PARAM_DOC("cutoff", "1000",
            "The cutoff in Hz ('~cross 800'). Later numbers and messages do not change what is "
            "saved.",
            "0 to 0.49 * samplerate Hz");
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen. Clear runs first on every Set, parse only when
// there are arguments.
PARM_CLEAR() {
  arguments.clear();
  cutoff = DSP::crossover::DEFAULT_CUTOFF;
}

PARM_PARSE() {
  // The first number is the cutoff.
  for (const std::string& token : arguments) {
    float number = 0.f;
    if (ReadNumber(token, number)) {
      cutoff = number;
      return;
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

MESSAGES() {
  (void)value;
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  if (word == "clear") {
    clearRequested = true;
    return;
  }
  float number = 0.f;
  if (ReadNumber(word, number)) cutoff = number;
}

CALC() {
  if (clearRequested.exchange(false)) fn.clear();

  // Unconnected inlets (#1036): the input reads 0, the cutoff its number.
  fn(inputs[0].Signal(0.f), inputs[1].Signal(cutoff.load()));
  outputs[0].SendBuffer(&fn.lowpass(), thread);
  outputs[1].SendBuffer(&fn.highpass(), thread);
}

#undef className
