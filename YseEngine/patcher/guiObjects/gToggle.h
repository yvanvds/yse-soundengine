#pragma once
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {
    PATCHER_CLASS(gToggle, YSE::OBJ::G_TOGGLE)
    _NO_MESSAGES
    _DO_CALCULATE

    _INT_IN(SetValue)
    _BANG_IN(Bang)
    _LIST_IN(SetList)

    // Settable since #846: inlet 0 takes the exact string GetGuiValue()
    // produces — the words "on" / "off" — back as a list, plus a numeric
    // whole state and "set 0 <value>" for the one cell, which is what lets
    // `.preset` capture and restore the toggle. The read stays "on" / "off"
    // and stays a pure state load, unlike `.b`'s consume-on-read press.
    _HAS_GUI_SETTABLE

  private:
    std::atomic<bool> value;
  };
}
}
