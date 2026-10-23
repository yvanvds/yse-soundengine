#include "dPlusEquals.h"
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

#define className dPlusEquals

CONSTRUCT_DSP() {
  inputFloat = 0.f;
  initial = 0.f;
  pendingSet = 0.f;

  // in 0: the signal summed (signal, float or int), bang and `set <x>`
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetInputFloat);
  REG_INT_IN(SetInputInt);
  REG_BANG_IN(Reset);

  // in 1: reset signal
  ADD_IN_1;
  REG_SIGNAL_IN;

  // out 0: the running sum
  ADD_OUT_BUFFER;

  ADD_PARAM(initial);

  ADD_DESCRIPTION("Adds up its input sample by sample and outputs the running sum: turns a "
                  "constant into a ramp, counts samples, or integrates a rate.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "in",
            "The signal summed: a signal, or a number used while no signal is connected. A "
            "bang sets the sum to 0; 'set <x>' sets it to x.",
            "any float");
  INLET_DOC(1, "reset", "Reset signal: the sum restarts from 0 at every sample where it is not 0.",
            "any float");
  OUTLET_DOC(0, "out", "The sum of every input sample so far.", "any float");
  PARAM_DOC("initial", "0", "The sum the object starts from.", "any float");
}

FLOAT_IN(SetInputFloat) {
  inputFloat = value;
}

INT_IN(SetInputInt) {
  inputFloat = static_cast<float>(value);
}

BANG_IN(Reset) {
  resetPending = true;
}

// `set <x>`. The text is walked in place, as inlet.cpp does, so this
// allocates nothing on whichever thread delivers it.
MESSAGES() {
  (void)value;
  const char* text = message.c_str();
  const std::size_t length = message.size();
  std::size_t begin = 0;
  while (begin < length && IsSelectorSeparator(text[begin]))
    begin++;
  std::size_t end = begin;
  while (end < length && !IsSelectorSeparator(text[end]))
    end++;
  if (end - begin != 3 || message.compare(begin, 3, "set") != 0) return;

  std::size_t arg = end;
  while (arg < length && IsSelectorSeparator(text[arg]))
    arg++;
  std::size_t argEnd = arg;
  while (argEnd < length && !IsSelectorSeparator(text[argEnd]))
    argEnd++;
  float number = 0.f;
  if (ReadNumericToken(text + arg, argEnd - arg, number)) {
    pendingSet = number;
    setPending = true;
  }
}

CALC() {
  if (!started) {
    accumulator.set(initial);
    started = true;
  }
  // A bang and a `set` in the same block: which came last cannot be told
  // from the flags, so the explicit value wins.
  if (resetPending.exchange(false)) accumulator.reset();
  if (setPending.exchange(false)) accumulator.set(pendingSet);

  // An unconnected input reads as its number; a connected signal wins (#1036).
  DSP::buffer& in = inputs[0].Signal(inputFloat);
  DSP::buffer& out =
      inputs[1].IsConstant() ? accumulator(in) : accumulator(in, inputs[1].Signal(0.f));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
