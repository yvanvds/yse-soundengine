#pragma once
#include "..\pObject.h"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(dSubstract, YSE::OBJ::D_SUBSTRACT)
      _NO_MESSAGES
      _DO_CALCULATE
      _DO_RESET

      _BUFFER_IN(SetLeftBuffer)
      _BUFFER_IN(SetRightBuffer)
      _FLOAT_IN(SetRightFloat)

    private:
      DSP::buffer * leftIn;
      DSP::buffer * rightIn;
      aFlt rightFloatIn;

      DSP::buffer output;
    };
  }
}