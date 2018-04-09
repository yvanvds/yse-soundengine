#include "gCounter.h"
#include "..\pObjectList.hpp"

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

  startValue = 0;
  step = 1;
}

INT_IN(SetIntValue) {
  currentValue = value;
}

LIST_IN(SetListValue) {
  if (value.compare("reset") == 0) {
    currentValue.store(startValue);
  }
}

BANG_IN(Bang) {
  // linked to first inlet and will trigger output
}

CALC() {
  outputs[0].SendInt(currentValue++);
}

GUI_VALUE() {
  return std::to_string(currentValue);
}