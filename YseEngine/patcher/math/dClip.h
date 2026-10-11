
#pragma once

#include "../pObject.h"
#include "../../dsp/math.hpp"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(dClip, YSE::OBJ::D_CLIP)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetLow)
    _FLOAT_IN(SetHigh)
    _INT_IN(SetIntLow)
    _INT_IN(SetIntHigh)

  private:
    aFlt low;
    aFlt high;

    DSP::clip clip;
  };
}
}
