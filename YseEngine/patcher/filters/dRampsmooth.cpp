#include "dRampsmooth.h"
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

#define className dRampsmooth

CONSTRUCT_DSP() {
  // in 0: the signal to smooth; messages
  ADD_IN_0;
  REG_SIGNAL_IN;

  // in 1: the ramp-up length, a number or a signal; messages
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetUp);
  REG_INT_IN(SetUpInt);

  // in 2: the ramp-down length, a number or a signal; messages
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetDown);
  REG_INT_IN(SetDownInt);

  // out 0: the smoothed signal
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("Smooths a signal linearly: each time the input changes, the output ramps in "
                  "a straight line to the new value over rampUp samples when rising and "
                  "rampDown samples when falling. For declicking and parameter smoothing. A "
                  "ramp of 1 passes the signal through.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "in",
            "The signal to smooth (unconnected reads 0). 'ramp <n>' sets both lengths, "
            "'rampup <n>' and 'rampdown <n>' one each; a number, or a list of up to two numbers "
            "(ramp up, ramp down), sets the numbers the other inlets read.",
            "");
  INLET_DOC(1, "rampUp",
            "Ramp-up length in whole samples, used when the input moves above the output: a "
            "number or a signal read per sample (unconnected, the last number sent here or the "
            "creation argument). Below 1 counts as 1. A ramp keeps the length it started with. "
            "Takes the same words as the left inlet.",
            "1+");
  INLET_DOC(2, "rampDown",
            "Ramp-down length in whole samples, used when the input moves to or below the "
            "output: a number or a signal read per sample (unconnected, the last number sent "
            "here or the creation argument). Below 1 counts as 1. A ramp keeps the length it "
            "started with. Takes the same words as the left inlet.",
            "1+");
  OUTLET_DOC(0, "out", "The smoothed signal.", "");
  PARAM_DOC("ramp", "1 1",
            "The ramp-up and ramp-down lengths in samples, up to two numbers ('~rampsmooth 100 "
            "1000'; they default to 1 and 1). Later numbers and messages do not change what is "
            "saved.",
            "a ramp-up length, then a ramp-down length");
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen. Clear runs first on every Set, parse only when
// there are arguments.
PARM_CLEAR() {
  arguments.clear();
  up = DSP::rampSmooth::DEFAULT_RAMP;
  down = DSP::rampSmooth::DEFAULT_RAMP;
}

PARM_PARSE() {
  // The first number is the ramp-up length, the second the ramp-down.
  int count = 0;
  for (const std::string& token : arguments) {
    float number = 0.f;
    if (!ReadNumber(token, number)) continue;
    if (count == 0)
      up = number;
    else
      down = number;
    if (++count == 2) return;
  }
}

FLOAT_IN(SetUp) {
  (void)inlet;
  (void)thread;
  if (std::isfinite(value)) up = value;
}

INT_IN(SetUpInt) {
  (void)inlet;
  (void)thread;
  up = static_cast<float>(value);
}

FLOAT_IN(SetDown) {
  (void)inlet;
  (void)thread;
  if (std::isfinite(value)) down = value;
}

INT_IN(SetDownInt) {
  (void)inlet;
  (void)thread;
  down = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  std::size_t next = 0;
  const std::string_view word = Token(message, 0, next);
  float number = 0.f;
  // Max's ramp message and attributes.
  if (word == "ramp") {
    if (ReadNumber(Token(message, next, next), number)) {
      up = number;
      down = number;
    }
    return;
  }
  if (word == "rampup") {
    if (ReadNumber(Token(message, next, next), number)) up = number;
    return;
  }
  if (word == "rampdown") {
    if (ReadNumber(Token(message, next, next), number)) down = number;
    return;
  }
  // A list of numbers: ramp up, then ramp down. A token that is not a finite
  // number stops it.
  if (!ReadNumber(word, number)) return;
  up = number;
  if (ReadNumber(Token(message, next, next), number)) down = number;
}

CALC() {
  // Unconnected inlets (#1036): the input reads 0, the lengths their
  // numbers.
  DSP::buffer& out =
      fn(inputs[0].Signal(0.f), inputs[1].Signal(up.load()), inputs[2].Signal(down.load()));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
