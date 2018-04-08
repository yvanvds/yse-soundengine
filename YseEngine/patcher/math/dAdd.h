#pragma once
#include "..\pObject.h"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(dAdd, YSE::OBJ::D_ADD)
      _NO_MESSAGES
      _DO_CALCULATE
      _DO_RESET

      _BUFFER_IN(SetLeftBuffer)
      _BUFFER_IN(SetRightBuffer)
      _FLOAT_IN(SetRightFloat)

    private:
      DSP::buffer * leftIn;
      DSP::buffer * rightIn;
      float rightFloatIn;

      DSP::buffer output;
    };
  }
}