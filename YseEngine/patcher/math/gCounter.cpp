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