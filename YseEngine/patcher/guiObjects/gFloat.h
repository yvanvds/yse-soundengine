#pragma once
#include "../pObject.h"
#include <atomic>

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(gFloat, YSE::OBJ::G_FLOAT)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)
    _BANG_IN(Bang)
    _LIST_IN(SetList)

    // Settable since #846: inlet 0 takes the exact string GetGuiValue()
    // produces back as a list, plus "set 0 <value>" for the one cell, which
    // is what lets `.preset` capture and restore the number box.
    _HAS_GUI_SETTABLE

  private:
    std::atomic<float> value;
  };
}
}
