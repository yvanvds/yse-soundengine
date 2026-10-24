#include "dBiquad.h"
#include "../pListArgs.h"
#include "../pSelector.h"

#include <cmath>
#include <limits>
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

#define className dBiquad

CONSTRUCT_DSP() {
  // in 0: the signal to filter; messages
  ADD_IN_0;
  REG_SIGNAL_IN;

  // in 1-5: the coefficients a0 a1 a2 b1 b2, each a number or a signal;
  // messages
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetCoefficient);
  REG_INT_IN(SetCoefficientInt);
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetCoefficient);
  REG_INT_IN(SetCoefficientInt);
  ADD_IN_3;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetCoefficient);
  REG_INT_IN(SetCoefficientInt);
  ADD_IN_4;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetCoefficient);
  REG_INT_IN(SetCoefficientInt);
  ADD_IN_5;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetCoefficient);
  REG_INT_IN(SetCoefficientInt);

  // out 0: the filtered signal
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("Two-pole, two-zero filter driven directly by five coefficients, y(n) = a0 x(n) "
                  "+ a1 x(n-1) + a2 x(n-2) - b1 y(n-1) - b2 y(n-2), each a number or a signal "
                  "read per sample: any biquad design, or a ringing oscillator.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "in",
            "The signal to filter (unconnected reads 0). 'clear' sets the previous inputs and "
            "outputs to 0 (it recovers a filter made to explode); 'smooth 0' or 'smooth 1' turns "
            "the smoothing of number changes off or on; 'stoke <y1> <y2> <x1> <x2>' preloads "
            "the previous outputs and inputs (missing ones 0); a list of up to five numbers (a0, "
            "a1, a2, b1, b2) sets the numbers the coefficient inlets read.",
            "");
  INLET_DOC(1, "a0",
            "The coefficient a0 of the current input, a number or a signal read per sample "
            "(unconnected, the last number sent here or the creation argument). Takes the same "
            "words as the left inlet.",
            "any number");
  INLET_DOC(2, "a1",
            "The coefficient a1 of the previous input, a number or a signal read per sample "
            "(unconnected, the last number sent here or the creation argument). Takes the same "
            "words as the left inlet.",
            "any number");
  INLET_DOC(3, "a2",
            "The coefficient a2 of the input two samples back, a number or a signal read per "
            "sample (unconnected, the last number sent here or the creation argument). Takes "
            "the same words as the left inlet.",
            "any number");
  INLET_DOC(4, "b1",
            "The coefficient b1 of the previous output, subtracted, a number or a signal read "
            "per sample (unconnected, the last number sent here or the creation argument). "
            "Takes the same words as the left inlet.",
            "any number; stable inside the triangle |b2| < 1, |b1| < 1 + b2");
  INLET_DOC(5, "b2",
            "The coefficient b2 of the output two samples back, subtracted, a number or a "
            "signal read per sample (unconnected, the last number sent here or the creation "
            "argument). Takes the same words as the left inlet.",
            "any number; stable inside the triangle |b2| < 1, |b1| < 1 + b2");
  OUTLET_DOC(0, "out", "The filtered signal.", "");
  PARAM_DOC("a0", "0",
            "Up to five coefficients, a0 a1 a2 b1 b2 ('~biquad 0.5 0 0 -0.5 0'; they default to "
            "0), and 'smooth 0' to turn the smoothing of number changes off (it is on by "
            "default). Later numbers and messages do not change what is saved.",
            "the coefficients a0 a1 a2 b1 b2, then smooth 0 or 1");
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen. Clear runs first on every Set, parse only when
// there are arguments.
PARM_CLEAR() {
  arguments.clear();
  for (auto& c : coefficient)
    c = 0.f;
  smoothing = true;
}

PARM_PARSE() {
  int bare = 0;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const std::string& token = arguments[i];
    float number = 0.f;
    // smooth takes the number after it; without one it is skipped.
    if (token == "smooth") {
      if (i + 1 < arguments.size() && ReadNumber(arguments[i + 1], number)) {
        ++i;
        smoothing = number != 0.f;
      }
      continue;
    }
    // Bare numbers: a0, a1, a2, b1, then b2.
    if (bare < 5 && ReadNumber(token, number)) coefficient[bare++] = number;
  }
}

FLOAT_IN(SetCoefficient) {
  (void)thread;
  if (inlet >= 1 && inlet <= 5 && std::isfinite(value)) coefficient[inlet - 1] = value;
}

INT_IN(SetCoefficientInt) {
  (void)thread;
  if (inlet >= 1 && inlet <= 5) coefficient[inlet - 1] = static_cast<float>(value);
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
  if (word == "smooth") {
    if (ReadNumber(Token(message, next, next), number)) smoothing = number != 0.f;
    return;
  }
  if (word == "stoke") {
    // y1 y2 x1 x2; a missing or malformed value is 0.
    for (auto& s : stokeValue) {
      s = ReadNumber(Token(message, next, next), number) ? number : 0.f;
    }
    stokeRequested = true;
    return;
  }
  // A list of numbers: a0, a1, a2, b1, b2. A token that is not a finite
  // number stops it.
  std::string_view token = word;
  for (int i = 0; i < 5 && ReadNumber(token, number); ++i) {
    coefficient[i] = number;
    token = Token(message, next, next);
  }
}

CALC() {
  if (clearRequested.exchange(false)) fn.clear();
  if (stokeRequested.exchange(false))
    fn.stoke(stokeValue[0].load(), stokeValue[1].load(), stokeValue[2].load(),
             stokeValue[3].load());
  fn.smooth(smoothing.load());

  // A connected coefficient inlet is read per sample; an unconnected one
  // (#1036) reads its number, which the DSP class smooths. NaN leaves a
  // connected coefficient's scalar as the signal last set it.
  constexpr float none = std::numeric_limits<float>::quiet_NaN();
  DSP::buffer* signals[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
  float scalars[5] = {none, none, none, none, none};
  for (int i = 0; i < 5; ++i) {
    if (inputs[i + 1].IsConstant())
      scalars[i] = coefficient[i].load();
    else
      signals[i] = &inputs[i + 1].Signal(0.f);
  }
  fn.coefficients(scalars[0], scalars[1], scalars[2], scalars[3], scalars[4]);

  DSP::buffer& out =
      fn(inputs[0].Signal(0.f), signals[0], signals[1], signals[2], signals[3], signals[4]);
  outputs[0].SendBuffer(&out, thread);
}

#undef className
