#include "gPow.h"
#include "../pObjectList.hpp"
#include <cmath>

using namespace YSE::PATCHER;

#define className gPow

CONSTRUCT() {
  ADD_IN_0;
  REG_FLOAT_IN(SetLeftFloat);
  REG_INT_IN(SetLeftInt);

  ADD_IN_1;
  REG_FLOAT_IN(SetRightFloat);
  REG_INT_IN(SetRightInt);

  ADD_OUT_FLOAT;

  ADD_PARAM(rightIn);

  leftIn = rightIn = 0.f;

  ADD_DESCRIPTION("Control-rate power. Emits base ^ exponent as a float whenever inlet 0 fires. "
                  "Inputs with no real result — a negative base with a fractional exponent, or a "
                  "zero base with a negative exponent — emit 0 rather than a NaN or an infinity.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "base", "Base — fires the evaluation.", "any float");
  INLET_DOC(1, "exponent", "Exponent — stored until the next evaluation.", "any float");
  OUTLET_DOC(0, "out", "base raised to the power of exponent (or 0 when that has no finite value).",
             "any float");
  PARAM_DOC("exponent", "0", "Initial exponent.", "any float");
}

FLOAT_IN(SetLeftFloat) {
  leftIn = value;
}

FLOAT_IN(SetRightFloat) {
  rightIn = value;
}

INT_IN(SetLeftInt) {
  leftIn = (float)value;
}

INT_IN(SetRightInt) {
  rightIn = (float)value;
}

CALC() {
  // std::pow answers NaN for a negative base with a fractional exponent and an
  // infinity for 0 ^ negative; both are reachable from a patch and both would
  // then poison every downstream object. Emit 0 instead, the convention ./
  // already uses for division by zero. The check is a branch on a register:
  // no allocation, no lock, no I/O.
  const float result = std::pow(leftIn, rightIn);
  outputs[0].SendFloat(std::isfinite(result) ? result : 0.f, thread);
}
