#include "gSwitch.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;
#define className gSwitch

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(SetActiveInlet);

  // a minimum of 2 outputs is required for a switch

  ADD_IN_1;
  REG_BANG_IN(SetBangValue);
  REG_INT_IN(SetIntValue);
  REG_FLOAT_IN(SetFloatValue);
  REG_LIST_IN(SetListValue);

  ADD_IN_2;
  REG_BANG_IN(SetBangValue);
  REG_INT_IN(SetIntValue);
  REG_FLOAT_IN(SetFloatValue);
  REG_LIST_IN(SetListValue);

  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_OUT_ANY;

  ADD_PARAM(numInlets);

  activeInlet = 0;
  numInlets = 2;

  ADD_DESCRIPTION(
      "Passes messages from one of its right-hand inlets to the outlet, chosen by a number in the "
      "left inlet. Inlets count from 1; 0 closes the switch.");
  ADD_CATEGORY(pCategory::ROUTING);
  INLET_DOC(0, "select",
            "Which inlet passes, counting from 1 for the inlet next to this one. 0 (the start "
            "value) or a number with no inlet passes nothing.",
            "0+");
  INLET_DOC(1, "in0", "Bang, int, float or list, passed on when select is 1.", "");
  INLET_DOC(2, "in1", "Bang, int, float or list, passed on when select is 2.", "");
  OUTLET_DOC(0, "out", "Messages from the selected inlet.", "");
  PARAM_DOC("numInlets", "2", "How many inlets to pass from. Fewer than 2 still builds 2.", "2+");
}

INT_IN(SetActiveInlet) {
  activeInlet = value;
}

PARM_CLEAR() {
  while (inputs.size() > 3) {
    inputs.pop_back();
  }
}

PARM_PARSE() {
  while (inputs.size() < (unsigned int)numInlets + 1) {
    inputs.emplace_back(this, false, inputs.size());
    REG_BANG_IN(SetBangValue);
    REG_INT_IN(SetIntValue);
    REG_FLOAT_IN(SetFloatValue);
    REG_LIST_IN(SetListValue);
  }
}

BANG_IN(SetBangValue) {
  if (inlet == activeInlet) {
    outputs[0].SendBang(thread);
  }
}

INT_IN(SetIntValue) {
  if (inlet == activeInlet) {
    outputs[0].SendInt(value, thread);
  }
}

FLOAT_IN(SetFloatValue) {
  if (inlet == activeInlet) {
    outputs[0].SendFloat(value, thread);
  }
}

LIST_IN(SetListValue) {
  if (inlet == activeInlet) {
    outputs[0].SendList(value, thread);
  }
}