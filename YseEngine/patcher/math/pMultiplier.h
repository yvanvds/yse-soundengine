#pragma once
#include "..\pObject.h"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(pMultiply, YSE::OBJ::D_MULTIPLY)
      _HAS_PARAMS
      _NO_MESSAGES
      _HAS_CALCULATE
      _HAS_DSP_RESET

      BUFFER_IN(SetLeftBuffer)
      BUFFER_IN(SetRightBuffer)
      FLOAT_IN(SetRightFloat)

    private:
      DSP::buffer * leftIn;
      DSP::buffer * rightIn;
      aFlt rightFloatIn;

      DSP::buffer output;
    };
  }
}