#include "gDivide.h"
#include "..\pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gDivide

CONSTRUCT() {
  ADD_IN_0;
  REG_FLOAT_IN(gDivide::SetLeftValue);

  ADD_IN_1;
  REG_FLOAT_IN(gDivide::SetRightValue);

  ADD_OUT_FLOAT;

  ADD_PARAM(rightIn);

  leftIn = rightIn = 0;
}

FLOAT_IN(gDivide::SetLeftValue) {
  leftIn = value;
}

FLOAT_IN(gDivide::SetRightValue) {
  rightIn = value;
}

CALC() {
  if (rightIn != 0.f) {
    result = leftIn / rightIn;
  }
  else result = 0.f;
  outputs[0].SendFloat(result);
}
