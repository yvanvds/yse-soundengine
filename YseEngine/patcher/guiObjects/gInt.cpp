#include "gInt.h"
#include "..\pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gInt

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(gInt::SetValue);
  REG_BANG_IN(gInt::Bang);

  ADD_IN_1;
  REG_INT_IN(gInt::SetValue);
  
  ADD_OUT_INT;

  ADD_PARAM(value);
  PASS_GUI_INT(value);

  value = 0;
}

INT_IN(gInt::SetValue) {
  this->value = value;
}

BANG_IN(gInt::Bang) {
  // nothing to do here, but needed to trigger output of value
}

CALC() {
  outputs[0].SendInt(value);
}