#include "gFloat.h"
#include "..\pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gFloat

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);
  REG_BANG_IN(Bang);

  ADD_IN_1;
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);

  ADD_OUT_FLOAT;

  ADD_PARAM(value);

  value = 0;
}

FLOAT_IN(SetFloat) {
  this->value = value;
}

INT_IN(SetInt) {
  this->value = (float)value;
}

BANG_IN(Bang) {
  // nothing to do here, but needed to trigger output of value
}

GUI_VALUE() {
  return std::to_string(value.load());
}

CALC() {
  outputs[0].SendFloat(value, thread);
}