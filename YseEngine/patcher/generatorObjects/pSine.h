
#pragma once

#include "../pObject.h"
#include "../../dsp/oscillators.hpp"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(pSine, YSE::OBJ::D_SINE)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFrequency)

  private:
    DSP::sine sine;

    aFlt frequency;
  };
}
}
