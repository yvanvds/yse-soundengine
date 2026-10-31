#include "dDeltaclip.h"
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

#define className dDeltaclip

CONSTRUCT_DSP() {
  // in 0: the signal to limit; messages
  ADD_IN_0;
  REG_SIGNAL_IN;

  // in 1: the minimum change, a number or a signal; messages
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetMinimum);
  REG_INT_IN(SetMinimumInt);

  // in 2: the maximum change, a number or a signal; messages
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetMaximum);
  REG_INT_IN(SetMaximumInt);

  // out 0: the limited signal
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("Limits how far a signal may move from one sample to the next: each change is "
                  "clipped between a minimum (the steepest fall) and a maximum (the steepest "
                  "rise) per sample. A slew limiter, for declicking and turning steps into "
                  "ramps. Both limits default to 0, which holds the output still.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "in",
            "The signal to limit (unconnected reads 0). 'reset' sets both limit numbers to 0; a "
            "number, or a list of up to two numbers (minimum, maximum), sets the numbers the "
            "other inlets read.",
            "");
  INLET_DOC(1, "minimum",
            "The smallest change per sample, normally negative: the steepest fall. A number or "
            "a signal read per sample (unconnected, the last number sent here or the creation "
            "argument). Takes the same words as the left inlet.",
            "any float");
  INLET_DOC(2, "maximum",
            "The largest change per sample, normally positive: the steepest rise. A number or a "
            "signal read per sample (unconnected, the last number sent here or the creation "
            "argument). Above the minimum, as a rule; below it the output always moves by the "
            "maximum. Takes the same words as the left inlet.",
            "any float");
  OUTLET_DOC(0, "out", "The limited signal.", "");
  PARAM_DOC("limits", "0 0",
            "The minimum and maximum change per sample, up to two numbers ('~deltaclip -0.01 "
            "0.01'; they default to 0 and 0). Later numbers and messages do not change what is "
            "saved.",
            "a minimum, then a maximum");
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen. Clear runs first on every Set, parse only when
// there are arguments.
PARM_CLEAR() {
  arguments.clear();
  minimum = DSP::slewLimiter::DEFAULT_LIMIT;
  maximum = DSP::slewLimiter::DEFAULT_LIMIT;
}

PARM_PARSE() {
  // The first number is the minimum, the second the maximum.
  int count = 0;
  for (const std::string& token : arguments) {
    float number = 0.f;
    if (!ReadNumber(token, number)) continue;
    if (count == 0)
      minimum = number;
    else
      maximum = number;
    if (++count == 2) return;
  }
}

FLOAT_IN(SetMinimum) {
  (void)inlet;
  (void)thread;
  if (std::isfinite(value)) minimum = value;
}

INT_IN(SetMinimumInt) {
  (void)inlet;
  (void)thread;
  minimum = static_cast<float>(value);
}

FLOAT_IN(SetMaximum) {
  (void)inlet;
  (void)thread;
  if (std::isfinite(value)) maximum = value;
}

INT_IN(SetMaximumInt) {
  (void)inlet;
  (void)thread;
  maximum = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  // MSP's reset: both limits back to their default of 0.
  if (word == "reset") {
    minimum = DSP::slewLimiter::DEFAULT_LIMIT;
    maximum = DSP::slewLimiter::DEFAULT_LIMIT;
    return;
  }
  // A list of numbers: the minimum, then the maximum. A token that is not a
  // finite number stops it.
  float number = 0.f;
  if (!ReadNumber(word, number)) return;
  minimum = number;
  if (ReadNumber(Token(message, next, next), number)) maximum = number;
}

CALC() {
  // Unconnected inlets (#1036): the input reads 0, the limits their
  // numbers.
  DSP::buffer& out =
      fn(inputs[0].Signal(0.f), inputs[1].Signal(minimum.load()), inputs[2].Signal(maximum.load()));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
