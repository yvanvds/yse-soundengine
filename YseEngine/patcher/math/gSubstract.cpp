#include "gSubstract.h"
#include "..\pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gSubstract

CONSTRUCT() {
  ADD_IN_0;
  REG_FLOAT_IN(gSubstract::SetLeftValue);

  ADD_IN_1;
  REG_FLOAT_IN(gSubstract::SetRightValue);

  ADD_OUT_FLOAT;

  ADD_PARAM(rightIn);

  leftIn = rightIn = 0;
}

FLOAT_IN(gSubstract::SetLeftValue) {
  leftIn = value;
}

FLOAT_IN(gSubstract::SetRightValue) {
  rightIn = value;
}

CALC() {
  result = leftIn - rightIn;
  outputs[0].SendFloat(result);
}
