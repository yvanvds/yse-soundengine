#include "gAdd.h"
#include "..\pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gAdd

CONSTRUCT() {
  ADD_INLET_0;
  REG_FLOAT_FUNC(gAdd::SetLeftValue);

  ADD_INLET_1;
  REG_FLOAT_FUNC(gAdd::SetRightValue);

  ADD_OUTLET_FLOAT;

  leftIn = rightIn = 0;
}

FLOAT_IN_FUNC(gAdd::SetLeftValue) {
  leftIn = value;
}

FLOAT_IN_FUNC(gAdd::SetRightValue) {
  rightIn = value;
}

PARAMS_FUNC() {
  if (pos == 0) rightIn = value;
}

CALC_FUNC() {
  result = leftIn + rightIn;
  outputs[0].SendFloat(result);
}
