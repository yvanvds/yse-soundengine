#pragma once
#include "..\pObject.h"
#include "dsp\oscillators.hpp"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(pSine, YSE::OBJ::D_SINE)
      _HAS_PARAMS
      _NO_MESSAGES
      _HAS_CALCULATE
      _HAS_DSP_RESET

      FLOAT_IN(SetFrequency)
      BUFFER_IN(SetFrequencyBuffer)

    private:
      DSP::sine sine;

      aFlt frequency;
      DSP::buffer * freqBuffer;
    };
  }
}