#pragma once
#include "../pObject.h"
#include <atomic>
#include <string>

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(gSlider, YSE::OBJ::G_SLIDER)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)
    _BANG_IN(SetBang)
    _LIST_IN(SetList)

    // Settable since #846: inlet 0 takes the exact string GetGuiValue()
    // produces back as a list, plus "set 0 <value>" for the one cell, which
    // is what lets `.preset` capture and restore the slider.
    _HAS_GUI_SETTABLE

  private:
    // Shared by every write path: store clamped to [0, 1].
    void Store(float value);

    std::atomic<float> value;
  };
}
}
