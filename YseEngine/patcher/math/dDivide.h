#pragma once
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(dDivide, YSE::OBJ::D_DIVIDE)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetRightFloat)
    _INT_IN(SetRightInt)

  private:
    float rightFloatIn;

    DSP::buffer output;
  };
}
}
