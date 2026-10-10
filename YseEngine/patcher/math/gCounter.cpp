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
  // Load the count from startValue once the arguments are parsed, so
  // `.counter -1` sends 0 on its first bang rather than only after a reset
  // (#956).
  REG_PARM_PARSE;

  ADD_DESCRIPTION("Counts bangs. Each bang adds the step to the count and sends the new count as "
                  "an int; an int on the left inlet sets the count and sends it.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "control",
            "Bang: add the step and send the count. Int: set the count and send it. 'reset': go "
            "back to the start value without sending. Floats are ignored.",
            "any int");
  INLET_DOC(1, "step", "Step added on each bang. Floats are ignored.", "any int");
  OUTLET_DOC(0, "out", "The count.", "any int");
  PARAM_DOC("startValue", "0",
            "The count before the first bang, and the count 'reset' returns to. The first bang "
            "sends it plus the step.",
            "any int");
  PARAM_DOC("step", "1", "Starting step; may be negative to count down.", "any int");
}

INT_IN(SetIntValue) {
  // Inlet 1 is the step; inlet 0 sets the count and sends it. startValue is
  // left alone, so 'reset' still returns to the creation argument (#956).
  if (inlet == 1) {
    step = value;
    return;
  }
  currentValue = value;
  outputs[0].SendInt(currentValue, thread);
}

PARM_PARSE() {
  currentValue.store(startValue);
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
