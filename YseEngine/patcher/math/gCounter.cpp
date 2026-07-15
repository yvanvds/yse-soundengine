#include "gCounter.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;
#define className gCounter

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(SetIntValue);
  REG_LIST_IN(SetListValue);
  REG_BANG_IN(Bang);

  ADD_IN_1;
  REG_INT_IN(SetIntValue);

  ADD_OUT_INT;

  ADD_PARAM(startValue);
  ADD_PARAM(step);

  ADD_DESCRIPTION("Step counter. Bang increments the current value by 'step' and emits it. Send "
                  "'reset' as a list to return to startValue.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "control",
            "Bang to step / int to set the value / list 'reset' to return to startValue.",
            "any int");
  INLET_DOC(1, "step", "Sets the increment used on each bang.", "any int");
  OUTLET_DOC(0, "out", "Current counter value.", "any int");
  PARAM_DOC("startValue", "0", "Initial counter value (and the value 'reset' returns to).",
            "any int");
  PARAM_DOC("step", "1", "Increment applied on each bang.", "any int");
}

INT_IN(SetIntValue) {
  startValue = currentValue = value;
  if (inlet == 0) outputs[0].SendInt(currentValue, thread);
}

LIST_IN(SetListValue) {
  if (value.compare("reset") == 0) {
    currentValue.store(startValue);
  }
}

BANG_IN(Bang) {
  currentValue += step;
  outputs[0].SendInt(currentValue, thread);
}

GUI_VALUE() {
  return std::to_string(currentValue);
}
