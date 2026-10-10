#include "gAtan2.h"
#include "../pObjectList.hpp"
#include <cmath>

using namespace YSE::PATCHER;

#define className gAtan2

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

  ADD_DESCRIPTION("Control-rate two-argument arc-tangent. Emits the angle in radians of the point "
                  "(x, y) measured from the positive x axis, whenever inlet 0 fires. Unlike .atan "
                  "it sees the signs of both operands, so it resolves the full circle rather than "
                  "only the half plane — this is the angle half of a cartesian-to-polar "
                  "conversion.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "y", "y coordinate — fires the evaluation.", "any float");
  INLET_DOC(1, "x", "x coordinate — stored until the next evaluation.", "any float");
  OUTLET_DOC(0, "out", "The angle of (x, y) in radians.", "-pi to pi");
  PARAM_DOC("x", "0", "Initial x coordinate.", "any float");
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
  // std::atan2 is finite for every finite pair, including (0, 0), so the only
  // way out is a non-finite operand arriving from elsewhere in the patch. Guard
  // it anyway so no NaN escapes to poison the objects downstream, the same
  // convention ./ and .sqrt use. One branch on a register: no allocation, no
  // lock, no I/O.
  const float result = std::atan2(leftIn, rightIn);
  outputs[0].SendFloat(std::isfinite(result) ? result : 0.f, thread);
}
