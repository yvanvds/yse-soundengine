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