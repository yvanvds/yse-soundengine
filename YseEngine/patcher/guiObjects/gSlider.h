#pragma once
#include "..\pObject.h"
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

      _HAS_GUI

private:
      std::atomic<float> value;
    };
  }
}