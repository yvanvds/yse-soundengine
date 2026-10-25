#include "dSlide.h"
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

#define className dSlide

CONSTRUCT_DSP() {
  // in 0: the signal to smooth; messages
  ADD_IN_0;
  REG_SIGNAL_IN;

  // in 1: the slide-up amount, a number or a signal; messages
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetUp);
  REG_INT_IN(SetUpInt);

  // in 2: the slide-down amount, a number or a signal; messages
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetDown);
  REG_INT_IN(SetDownInt);

  // out 0: the smoothed signal
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("Smooths a signal logarithmically: each sample moves the output 1/slideUp of "
                  "the way towards the input while rising and 1/slideDown while falling. For "
                  "envelope followers and portamento. A slide of 1 passes the signal through.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "in",
            "The signal to smooth (unconnected reads 0). 'reset' sets the output to 0; "
            "'slideup <n>' and 'slidedown <n>' set the amounts; a number, or a list of up to two "
            "numbers (slide up, slide down), sets the numbers the other inlets read.",
            "");
  INLET_DOC(1, "slideUp",
            "Slide-up amount in samples, used while the input is above the output: a number or "
            "a signal read per sample (unconnected, the last number sent here or the creation "
            "argument). Below 1 counts as 1. Takes the same words as the left inlet.",
            "1+");
  INLET_DOC(2, "slideDown",
            "Slide-down amount in samples, used while the input is at or below the output: a "
            "number or a signal read per sample (unconnected, the last number sent here or the "
            "creation argument). Below 1 counts as 1. Takes the same words as the left inlet.",
            "1+");
  OUTLET_DOC(0, "out", "The smoothed signal.", "");
  PARAM_DOC("slide", "1 1",
            "The slide-up and slide-down amounts in samples, up to two numbers ('~slide 10 "
            "1000'; they default to 1 and 1). Later numbers and messages do not change what is "
            "saved.",
            "a slide-up amount, then a slide-down amount");
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen. Clear runs first on every Set, parse only when
// there are arguments.
PARM_CLEAR() {
  arguments.clear();
  up = DSP::slide::DEFAULT_SLIDE;
  down = DSP::slide::DEFAULT_SLIDE;
}

PARM_PARSE() {
  // The first number is the slide-up amount, the second the slide-down.
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
  if (word == "reset") {
    resetRequested = true;
    return;
  }
  float number = 0.f;
  // Max's attributes, as messages.
  if (word == "slideup") {
    if (ReadNumber(Token(message, next, next), number)) up = number;
    return;
  }
  if (word == "slidedown") {
    if (ReadNumber(Token(message, next, next), number)) down = number;
    return;
  }
  // A list of numbers: slide up, then slide down. A token that is not a
  // finite number stops it.
  if (!ReadNumber(word, number)) return;
  up = number;
  if (ReadNumber(Token(message, next, next), number)) down = number;
}

CALC() {
  if (resetRequested.exchange(false)) fn.reset();

  // Unconnected inlets (#1036): the input reads 0, the amounts their
  // numbers.
  DSP::buffer& out =
      fn(inputs[0].Signal(0.f), inputs[1].Signal(up.load()), inputs[2].Signal(down.load()));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
