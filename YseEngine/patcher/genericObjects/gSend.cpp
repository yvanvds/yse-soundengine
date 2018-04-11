#include "gSend.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

using namespace YSE::PATCHER;
#define className gSend

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(SetBangValue);
  REG_INT_IN(SetIntValue);
  REG_FLOAT_IN(SetFloatValue);
  REG_LIST_IN(SetListValue);

  ADD_PARAM(dataName);
}

BANG_IN(SetBangValue) {
  if (parent != nullptr) {
    ((patcherImplementation*)parent)->PassBang(dataName);
  }
}

INT_IN(SetIntValue) {
  if (parent != nullptr) {
    ((patcherImplementation*)parent)->PassData(value, dataName);
  }
}

FLOAT_IN(SetFloatValue) {
  if (parent != nullptr) {
    ((patcherImplementation*)parent)->PassData(value, dataName);
  }
}

LIST_IN(SetListValue) {
  if (parent != nullptr) {
    ((patcherImplementation*)parent)->PassData(value, dataName);
  }
}