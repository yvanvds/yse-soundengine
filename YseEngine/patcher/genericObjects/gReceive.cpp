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

  ADD_DESCRIPTION("Named receive endpoint. Forwards values arriving from any matching gSend (same dataName) in the patcher.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "in", "Wired inlet (rarely used — receives typically pair with gSend by name).", "");
  OUTLET_DOC(0, "out", "Forwarded value from matching gSend nodes.", "");
  PARAM_DOC("dataName", "", "Name to listen for; must match the dataName of one or more gSend nodes.", "any identifier");
}

BANG_IN(SetBangValue) {
  outputs[0].SendBang(thread);
}

INT_IN(SetIntValue) {
  outputs[0].SendInt(value, thread);
}

FLOAT_IN(SetFloatValue) {
  outputs[0].SendFloat(value, thread);
}

LIST_IN(SetListValue) {
  outputs[0].SendList(value, thread);
}