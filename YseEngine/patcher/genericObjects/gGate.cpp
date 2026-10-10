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

  ADD_DESCRIPTION("Passes messages from its right inlet out one of its outlets, chosen by a number "
                  "in the left inlet. Outlets count from 1; 0 closes the gate.");
  ADD_CATEGORY(pCategory::ROUTING);
  INLET_DOC(0, "select",
            "The outlet to send to, counting from 1. 0 (the start value) or a number with no "
            "outlet closes the gate.",
            "0+");
  INLET_DOC(1, "in", "Bang, int, float or list to pass to the selected outlet.", "");
  OUTLET_DOC(0, "out0", "Sends the message when select is 1.", "");
  OUTLET_DOC(1, "out1", "Sends the message when select is 2.", "");
  PARAM_DOC("numOutlets", "2", "How many outlets to build. Fewer than 2 still builds 2.", "2+");
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

// Snapshot activeOutlet once per value delivery. Reading the atomic separately
// for the bounds check and the index is a TOCTOU: a concurrent SetActiveOutlet
// (control thread) could satisfy the check with a valid index and then have the
// index expression sample a larger value, driving `outputs[]` out of bounds
// (issue #197). `outputs` itself is never resized on a published object — a
// live SetParams that changes the outlet count goes through the graph-swap
// rebuild path (issue #234), not an in-place PARM_PARSE — so a single load of
// the selector is all that is needed here.
BANG_IN(SetBangValue) {
  const int active = activeOutlet.load(std::memory_order_relaxed);
  if (active > 0 && (unsigned int)active <= outputs.size()) {
    outputs[active - 1].SendBang(thread);
  }
}

INT_IN(SetIntValue) {
  const int active = activeOutlet.load(std::memory_order_relaxed);
  if (active > 0 && (unsigned int)active <= outputs.size()) {
    outputs[active - 1].SendInt(value, thread);
  }
}

FLOAT_IN(SetFloatValue) {
  const int active = activeOutlet.load(std::memory_order_relaxed);
  if (active > 0 && (unsigned int)active <= outputs.size()) {
    outputs[active - 1].SendFloat(value, thread);
  }
}

LIST_IN(SetListValue) {
  const int active = activeOutlet.load(std::memory_order_relaxed);
  if (active > 0 && (unsigned int)active <= outputs.size()) {
    outputs[active - 1].SendList(value, thread);
  }
}