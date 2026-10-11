#pragma once
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(dMultiply, YSE::OBJ::D_MULTIPLY)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetRightFloat)
    _INT_IN(SetRightInt)

  private:
    aFlt rightFloatIn;

    DSP::buffer output;
  };
}
}
