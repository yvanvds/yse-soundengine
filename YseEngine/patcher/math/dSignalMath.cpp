#include "dSignalMath.h"
#include "../pObjectList.hpp"
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

namespace {
  constexpr const char* kSignalIn =
      "The input: a signal, or a number used while no signal is connected.";
} // namespace

// ─── the one-inlet objects ────────────────────────────────────────────────────

dAbs::dAbs() {
  Document("Outputs the absolute value of the input: negative samples are made positive. A "
           "full-wave rectifier.",
           kSignalIn, "any float", "The absolute value of the input.", ">= 0");
}

dSqrt::dSqrt() {
  Document("Outputs the square root of the input. Negative input gives 0, so no NaN reaches the "
           "graph.",
           kSignalIn, ">= 0 (below gives 0)", "The square root of the input.", ">= 0");
}

dTrunc::dTrunc() {
  Document("Outputs the input without its fractional part, rounding toward zero: 1.7 gives 1 "
           "and -1.7 gives -1.",
           kSignalIn, "any float", "The input truncated toward zero.", "any whole number");
}

// ─── ~pow ─────────────────────────────────────────────────────────────────────

#define className dPow

CONSTRUCT_DSP() {
  exponentIn = 0.f;
  baseIn = 0.f;

  // in 0: the exponent (signal, float or int)
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetExponentFloat);
  REG_INT_IN(SetExponentInt);

  // in 1: the base (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetBaseFloat);
  REG_INT_IN(SetBaseInt);

  // out 0: base to the power of the exponent
  ADD_OUT_BUFFER;

  ADD_PARAM(baseIn);

  ADD_DESCRIPTION("Raises the base (right inlet) to the power of the exponent (left inlet): the "
                  "order is the reverse of .pow. A base of 0 or below, or a result too large for "
                  "a float, gives 0.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "exponent", "The exponent: a signal, or a number used while no signal is connected.",
            "any float");
  INLET_DOC(1, "base", "The base: a signal, or a number used while no signal is connected.",
            "> 0 (0 and below give 0)");
  OUTLET_DOC(0, "out", "The base to the power of the exponent.", ">= 0");
  PARAM_DOC("base", "0", "Base used while no signal is connected to the right inlet.",
            "> 0 (0 and below give 0)");
}

FLOAT_IN(SetExponentFloat) {
  exponentIn = value;
}

INT_IN(SetExponentInt) {
  exponentIn = static_cast<float>(value);
}

FLOAT_IN(SetBaseFloat) {
  baseIn = value;
}

INT_IN(SetBaseInt) {
  baseIn = static_cast<float>(value);
}

CALC() {
  // Unconnected inlets read as their numbers; a connected signal wins (#1036).
  DSP::buffer& exponent = inputs[0].Signal(exponentIn);
  DSP::buffer& base = inputs[1].Signal(baseIn);
  outputs[0].SendBuffer(&fn(base, exponent), thread);
}

#undef className

// ─── ~log ─────────────────────────────────────────────────────────────────────

#define className dLog

CONSTRUCT_DSP() {
  inputFloat = 0.f;
  // e by default, so ~log with no argument is the natural logarithm. Set
  // before Register() so a creation argument overwrites it.
  baseIn = 2.7182818f;

  // in 0: the input (signal, float or int)
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetInputFloat);
  REG_INT_IN(SetInputInt);

  // in 1: the base (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetBaseFloat);
  REG_INT_IN(SetBaseInt);

  // out 0: the logarithm
  ADD_OUT_BUFFER;

  ADD_PARAM(baseIn);

  ADD_DESCRIPTION("Outputs the logarithm of the input in the given base (e by default). An input "
                  "of 0 or below gives 0, so no NaN or infinity reaches the graph; a base of 0 or "
                  "below, or of 1, falls back to the natural logarithm.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "in", kSignalIn, "> 0 (0 and below give 0)");
  INLET_DOC(1, "base", "The base: a signal, or a number used while no signal is connected.",
            "> 0, not 1");
  OUTLET_DOC(0, "out", "The logarithm of the input.", "any float");
  PARAM_DOC("base", "2.718282", "Base used while no signal is connected to the right inlet.",
            "> 0, not 1");
}

FLOAT_IN(SetInputFloat) {
  inputFloat = value;
}

INT_IN(SetInputInt) {
  inputFloat = static_cast<float>(value);
}

FLOAT_IN(SetBaseFloat) {
  baseIn = value;
}

INT_IN(SetBaseInt) {
  baseIn = static_cast<float>(value);
}

CALC() {
  // Unconnected inlets read as their numbers; a connected signal wins (#1036).
  DSP::buffer& in = inputs[0].Signal(inputFloat);
  DSP::buffer& base = inputs[1].Signal(baseIn);
  outputs[0].SendBuffer(&fn(in, base), thread);
}

#undef className

// ─── ~round ───────────────────────────────────────────────────────────────────

#define className dRound

CONSTRUCT_DSP() {
  inputFloat = 0.f;
  // Set before Register() so creation arguments overwrite them: a step of 1
  // (whole numbers, as .round) in nearest mode.
  stepIn = 1.f;
  nearest = 1;

  // in 0: the input (signal, float or int) and `nearest <0|1>`
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetInputFloat);
  REG_INT_IN(SetInputInt);

  // in 1: the step (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetStepFloat);
  REG_INT_IN(SetStepInt);

  // out 0: the rounded input
  ADD_OUT_BUFFER;

  ADD_PARAM(stepIn);
  ADD_PARAM(nearest);

  ADD_DESCRIPTION("Rounds the input to a multiple of the step: a step of 1, the default, rounds "
                  "to whole numbers. Rounds to the nearest multiple (halfway values away from "
                  "zero), or down with 'nearest 0'. A step of 0 passes the input through.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "in",
            "The input: a signal, or a number used while no signal is connected. 'nearest 1' "
            "rounds to the nearest multiple, 'nearest 0' rounds down.",
            "any float");
  INLET_DOC(1, "step", "The step: a signal, or a number used while no signal is connected.",
            "any float, 0 = off");
  OUTLET_DOC(0, "out", "The input rounded to a multiple of the step.", "any float");
  PARAM_DOC("step", "1",
            "Step used while no signal is connected to the right inlet. 0 passes "
            "the input through.",
            "any float");
  PARAM_DOC("nearest", "1", "1 rounds to the nearest multiple, 0 rounds down.", "0 or 1");
}

FLOAT_IN(SetInputFloat) {
  inputFloat = value;
}

INT_IN(SetInputInt) {
  inputFloat = static_cast<float>(value);
}

FLOAT_IN(SetStepFloat) {
  stepIn = value;
}

INT_IN(SetStepInt) {
  stepIn = static_cast<float>(value);
}

// `nearest <0|1>`. The text is walked in place, as dPlusEquals does, so this
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
  if (end - begin != 7 || message.compare(begin, 7, "nearest") != 0) return;

  std::size_t arg = end;
  while (arg < length && IsSelectorSeparator(text[arg]))
    arg++;
  std::size_t argEnd = arg;
  while (argEnd < length && !IsSelectorSeparator(text[argEnd]))
    argEnd++;
  float number = 0.f;
  if (ReadNumericToken(text + arg, argEnd - arg, number)) nearest = number != 0.f ? 1 : 0;
}

CALC() {
  fn.setNearest(nearest != 0);
  // Unconnected inlets read as their numbers; a connected signal wins (#1036).
  DSP::buffer& in = inputs[0].Signal(inputFloat);
  DSP::buffer& out =
      inputs[1].IsConstant() ? fn.setStep(stepIn)(in) : fn(in, inputs[1].Signal(stepIn));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
