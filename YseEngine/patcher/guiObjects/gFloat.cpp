#include "gFloat.h"
#include "../pObjectList.hpp"

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

  ADD_DESCRIPTION("Float number box. Stores a float; inlet 0 sets-and-fires (bang re-emits the current value), inlet 1 sets silently.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "set/bang", "Sets the value and emits it; bang re-emits the current value.", "any float");
  INLET_DOC(1, "silent set", "Silently updates the stored value without emitting.", "any float");
  OUTLET_DOC(0, "out", "Current float value.", "any float");
  PARAM_DOC("value", "0", "Initial value.", "any float");
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
