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

  ADD_DESCRIPTION("Named send endpoint. Broadcasts incoming values to every gReceive in the patcher whose dataName matches.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "in", "Value inlet — accepts bang / int / float / list.", "");
  PARAM_DOC("dataName", "", "Name to broadcast on; matching gReceive nodes will emit the forwarded value.", "any identifier");
}

BANG_IN(SetBangValue) {
  if (parent != nullptr) {
    ((patcherImplementation*)parent)->PassBang(dataName, thread);
  }
}

INT_IN(SetIntValue) {
  if (parent != nullptr) {
    ((patcherImplementation*)parent)->PassData(value, dataName, thread);
  }
}

FLOAT_IN(SetFloatValue) {
  if (parent != nullptr) {
    ((patcherImplementation*)parent)->PassData(value, dataName, thread);
  }
}

LIST_IN(SetListValue) {
  if (parent != nullptr) {
    ((patcherImplementation*)parent)->PassData(value, dataName, thread);
  }
}