#include "gToggle.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gToggle

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(SetValue);
  REG_BANG_IN(Bang);

  ADD_OUT_INT;

  value = false;
}

INT_IN(SetValue) {
  if (value == 0) this->value = false;
  else this->value = true;
}

BANG_IN(Bang) {
  value = !value;
}

GUI_VALUE() {
  return value == 0 ? "off" : "on";
}

CALC() {
  outputs[0].SendInt(value == false ? 0 : 1, thread);
}
