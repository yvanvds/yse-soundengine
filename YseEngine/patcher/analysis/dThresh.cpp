#include "dThresh.h"
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

  // Infinities are thresholds the DSP class takes; NaN is not.
  bool ReadNumber(std::string_view token, float& out) {
    return ReadNumericToken(token.data(), token.size(), out) && !std::isnan(out);
  }
} // namespace

#define className dThresh

CONSTRUCT_DSP() {
  // in 0: the input (signal, float or int); lists
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetIn);
  REG_INT_IN(SetInInt);

  // in 1: the low (reset) threshold, a number or a signal; lists
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetLow);
  REG_INT_IN(SetLowInt);

  // in 2: the high (set) threshold, a number or a signal; lists
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetHigh);
  REG_INT_IN(SetHighInt);

  // out 0: 1 or 0
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("A threshold with hysteresis: outputs 1 from the moment the signal reaches the "
                  "high threshold until it falls to the low one, and 0 otherwise. A gate that "
                  "does not chatter on a noisy signal.");
  ADD_CATEGORY(pCategory::ANALYSIS);
  INLET_DOC(0, "in",
            "The input: a signal, or a number used while no signal is connected. A list of two "
            "numbers (low, high) sets the thresholds the other inlets read.",
            "any float");
  INLET_DOC(1, "low",
            "The low (reset) threshold: at or below it the output turns 0. A number or a signal "
            "read per sample (unconnected, the last number sent here or the creation "
            "argument). Takes the same list as the left inlet.",
            "any float");
  INLET_DOC(2, "high",
            "The high (set) threshold: at or above it the output turns 1. A number or a signal "
            "read per sample (unconnected, the last number sent here or the creation "
            "argument). Takes the same list as the left inlet.",
            "any float");
  OUTLET_DOC(0, "out",
             "1 from the first sample at or above the high threshold until the first at or "
             "below the low one; with equal thresholds, 1 while at or above it.",
             "0 or 1");
  PARAM_DOC("thresh", "0 0",
            "The low and high thresholds, up to two numbers ('~thresh 0.1 0.5'; both default to "
            "0). A low threshold above the high one is used as the high one. Later numbers and "
            "lists do not change what is saved.",
            "a low threshold, then a high threshold");
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen. Clear runs first on every Set, parse only when
// there are arguments.
PARM_CLEAR() {
  arguments.clear();
  low = 0.f;
  high = 0.f;
}

PARM_PARSE() {
  // The first number is the low threshold, the second the high one.
  int count = 0;
  for (const std::string& token : arguments) {
    float number = 0.f;
    if (!ReadNumber(token, number)) continue;
    if (count == 0)
      low = number;
    else
      high = number;
    if (++count == 2) return;
  }
}

FLOAT_IN(SetIn) {
  (void)inlet;
  (void)thread;
  in = value;
}

INT_IN(SetInInt) {
  (void)inlet;
  (void)thread;
  in = static_cast<float>(value);
}

FLOAT_IN(SetLow) {
  (void)inlet;
  (void)thread;
  if (!std::isnan(value)) low = value;
}

INT_IN(SetLowInt) {
  (void)inlet;
  (void)thread;
  low = static_cast<float>(value);
}

FLOAT_IN(SetHigh) {
  (void)inlet;
  (void)thread;
  if (!std::isnan(value)) high = value;
}

INT_IN(SetHighInt) {
  (void)inlet;
  (void)thread;
  high = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  // A list of numbers: the low threshold, then the high one. A token that
  // is not a number stops it; a word-first message is not thresh~'s.
  std::size_t next = 0;
  float number = 0.f;
  if (!ReadNumber(Token(message, 0, next), number)) return;
  low = number;
  if (ReadNumber(Token(message, next, next), number)) high = number;
}

CALC() {
  // Unconnected inlets (#1036): the input and the thresholds read their
  // numbers.
  DSP::buffer& out =
      fn(inputs[0].Signal(in.load()), inputs[1].Signal(low.load()), inputs[2].Signal(high.load()));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
