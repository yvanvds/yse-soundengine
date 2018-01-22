#include "gAdd.h"
#include "..\pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gAdd

CONSTRUCT() {
  ADD_IN_0;
  REG_FLOAT_IN(gAdd::SetLeftValue);

  ADD_IN_1;
  REG_FLOAT_IN(gAdd::SetRightValue);

  ADD_OUT_FLOAT;

  ADD_PARAM(rightIn);

  leftIn = rightIn = 0;
}

FLOAT_IN(gAdd::SetLeftValue) {
  leftIn = value;
}

FLOAT_IN(gAdd::SetRightValue) {
  rightIn = value;
}

CALC() {
  result = leftIn + rightIn;
  outputs[0].SendFloat(result);
}
