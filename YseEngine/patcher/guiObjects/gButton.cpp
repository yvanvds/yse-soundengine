#include "gButton.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gButton

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(Bang);
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);

  ADD_OUT_BANG;

  on = false;

  ADD_DESCRIPTION("Momentary button. Any input (bang/int/float) lights the button until the next GUI poll; emits a bang on every calculate tick.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "trigger", "Press — bang/int/float all light the button.", "");
  OUTLET_DOC(0, "out", "Bang emitted on every calculate tick.", "");
}

BANG_IN(Bang) { on = true; }
INT_IN(SetInt) { on = true; }
FLOAT_IN(SetFloat) { on = true; }

GUI_VALUE() {
  bool value = on;
  on = false;
  return value ? "on" : "off";
}

CALC() {
  outputs[0].SendBang(thread);
}