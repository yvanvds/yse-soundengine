
#include "gMultiply.h"
#include "../pObjectList.hpp"
#include "../../implementations/logImplementation.h"

using namespace YSE::PATCHER;

#define className gMultiply

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

  ADD_DESCRIPTION("Control-rate multiply. Emits left * right as a float whenever inlet 0 fires.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "left", "Left operand — fires the multiplication.", "any float");
  INLET_DOC(1, "right", "Right operand — stored until next multiply.", "any float");
  OUTLET_DOC(0, "out", "left * right.", "any float");
  PARAM_DOC("right", "0", "Initial right-operand value.", "any float");
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
  result = leftIn * rightIn;
  outputs[0].SendFloat(result, thread);
}
