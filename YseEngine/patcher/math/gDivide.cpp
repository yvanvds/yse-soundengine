#include "gDivide.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gDivide

CONSTRUCT() {
  ADD_IN_0;
  REG_FLOAT_IN(SetLeftFloat);
  REG_INT_IN(SetLeftInt);

  ADD_IN_1;
  REG_FLOAT_IN(SetRightFloat);
  REG_INT_IN(SetRightInt);

  ADD_OUT_FLOAT;

  ADD_PARAM(rightIn);

  leftIn = rightIn = 0;

  ADD_DESCRIPTION("Divides the left operand by the right one and sends the quotient as a float. "
                  "Dividing by 0 sends 0.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "left", "Dividend; sends the result.", "any float");
  INLET_DOC(1, "right", "Divisor, stored for the next result. 0 makes the result 0.", "any float");
  OUTLET_DOC(0, "out", "left / right, or 0 when right is 0.", "any float");
  PARAM_DOC("right", "0", "Starting divisor.", "any float");
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
  if (rightIn != 0.f) {
    result = leftIn / rightIn;
  } else
    result = 0.f;
  outputs[0].SendFloat(result, thread);
}
