#include "gGate.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;
#define className gGate

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(SetActiveOutlet);

  ADD_IN_1;
  REG_BANG_IN(SetBangValue);
  REG_INT_IN(SetIntValue);
  REG_FLOAT_IN(SetFloatValue);
  REG_LIST_IN(SetListValue);

  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_OUT_ANY;
  ADD_OUT_ANY;

  ADD_PARAM(numOutlets);

  activeOutlet = 0;
  numOutlets = 2;

  ADD_DESCRIPTION("Demultiplexer. Routes a value inlet to one of N outlets, selected by inlet 0. activeOutlet=0 silences output; 1-based otherwise.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "select", "1-based outlet index to forward to (0 = mute).", "0+");
  INLET_DOC(1, "in", "Value inlet — accepts bang / int / float / list.", "");
  OUTLET_DOC(0, "out0", "Outlet 0 — emits the routed value when select == 1.", "");
  OUTLET_DOC(1, "out1", "Outlet 1 — emits the routed value when select == 2.", "");
  PARAM_DOC("numOutlets", "2", "Number of value outlets (additional outlets are created by SetParams).", "1+");
}

INT_IN(SetActiveOutlet) {
  activeOutlet = value;
}

PARM_CLEAR() {
  while (outputs.size() > 2) {
    outputs.pop_back();
  }
}

PARM_PARSE() {
  while (outputs.size() < (unsigned int)numOutlets) {
    ADD_OUT_ANY;
  }
}

BANG_IN(SetBangValue) {
  if (activeOutlet > 0 && (unsigned int)activeOutlet <= outputs.size()) {
    outputs[activeOutlet - 1].SendBang(thread);
  }
}

INT_IN(SetIntValue) {
  if (activeOutlet > 0 && (unsigned int)activeOutlet <= outputs.size()) {
    outputs[activeOutlet - 1].SendInt(value, thread);
  }
}

FLOAT_IN(SetFloatValue) {
  if (activeOutlet > 0 && (unsigned int)activeOutlet <= outputs.size()) {
    outputs[activeOutlet - 1].SendFloat(value, thread);
  }
}

LIST_IN(SetListValue) {
  if (activeOutlet > 0 && (unsigned int)activeOutlet <= outputs.size()) {
    outputs[activeOutlet - 1].SendList(value, thread);
  }
}