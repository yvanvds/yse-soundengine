
#pragma once

#include "../pObject.h"
#include "../../dsp/oscillators.hpp"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(dSaw, YSE::OBJ::D_SAW)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFrequency)

  private:
    DSP::saw saw;

    aFlt frequency;
  };
}
}
