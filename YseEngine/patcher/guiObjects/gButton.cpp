#include "gButton.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gButton

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(Bang);
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);

  ADD_OUT_BANG;

  on = false;

  ADD_DESCRIPTION("A button. Any bang or number sends a bang and lights the button until the host "
                  "next reads it.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "trigger", "A bang, int or float presses the button.", "");
  OUTLET_DOC(0, "out", "A bang for every press.", "");
}

BANG_IN(Bang) {
  on = true;
}
INT_IN(SetInt) {
  on = true;
}
FLOAT_IN(SetFloat) {
  on = true;
}

GUI_VALUE() {
  // Atomic read-and-clear. A plain `bool v = on; on = false;` is a load then a
  // separate store, so a press that lands between the two (writers run on the
  // audio thread during traversal and on the control thread) is silently
  // cleared and never reported — a lost press (issue #197). exchange() reads
  // and clears in one indivisible step.
  //
  // This destructive poll is also why `.b` stayed out when #846 opted the
  // other scalar controls into the settable protocol: the value is an *event*
  // report, not a state, so there is nothing a `.preset` could meaningfully
  // store, and a restore of "on" would replay a press. GuiValueIsSettable()
  // stays false, and `.preset` — which checks participation before reading —
  // never polls it. A momentary control with restorable intent is
  // `.led momentary` / `.textbutton momentary`, whose cell is "0"/"1" by
  // design.
  return on.exchange(false, std::memory_order_relaxed) ? "on" : "off";
}

CALC() {
  outputs[0].SendBang(thread);
}