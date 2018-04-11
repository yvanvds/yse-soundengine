#include "gReceive.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;
#define className gReceive

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(SetBangValue);
  REG_INT_IN(SetIntValue);
  REG_FLOAT_IN(SetFloatValue);
  REG_LIST_IN(SetListValue);

  ADD_PARAM(dataName);

  ADD_OUT_ANY;
}

BANG_IN(SetBangValue) {
  outputs[0].SendBang();
}

INT_IN(SetIntValue) {
  outputs[0].SendInt(value);
}

FLOAT_IN(SetFloatValue) {
  outputs[0].SendFloat(value);
}

LIST_IN(SetListValue) {
  outputs[0].SendList(value);
}