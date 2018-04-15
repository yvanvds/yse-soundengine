#include "gAdd.h"
#include "..\pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gAdd

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
  result = leftIn + rightIn;
  outputs[0].SendFloat(result, thread);
}
