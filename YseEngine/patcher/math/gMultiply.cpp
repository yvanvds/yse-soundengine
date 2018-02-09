#include "gMultiply.h"
#include "..\pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gMultiply

CONSTRUCT() {
  ADD_IN_0;
  REG_FLOAT_IN(gMultiply::SetLeftValue);

  ADD_IN_1;
  REG_FLOAT_IN(gMultiply::SetRightValue);

  ADD_OUT_FLOAT;

  ADD_PARAM(rightIn);

  leftIn = rightIn = 0;
}

FLOAT_IN(gMultiply::SetLeftValue) {
  leftIn = value;
}

FLOAT_IN(gMultiply::SetRightValue) {
  rightIn = value;
}

CALC() {
  result = leftIn * rightIn;
  outputs[0].SendFloat(result);
}
