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

  ADD_DESCRIPTION("Raises the left operand to the power of the right one and sends the result as "
                  "a float. Combinations with no finite real result, such as a negative base with "
                  "a fractional exponent, send 0.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "base", "Base; sends the result.", "any float");
  INLET_DOC(1, "exponent", "Exponent, stored for the next result.", "any float");
  OUTLET_DOC(0, "out", "base to the power of exponent, or 0 when that has no finite value.",
             "any float");
  PARAM_DOC("exponent", "0", "Starting exponent.", "any float");
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
