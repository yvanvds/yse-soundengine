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

  ADD_DESCRIPTION("Sends the angle of the point (x, y) in radians, measured from the positive x "
                  "axis, from -pi to pi. The left inlet takes y and the right inlet x.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "y", "y coordinate; sends the result.", "any float");
  INLET_DOC(1, "x", "x coordinate, stored for the next result.", "any float");
  OUTLET_DOC(0, "out", "The angle of (x, y) in radians.", "-pi to pi");
  PARAM_DOC("x", "0", "Starting x coordinate.", "any float");
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
