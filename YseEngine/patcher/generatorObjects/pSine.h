#pragma once
#include "..\pObject.h"
#include "dsp\oscillators.hpp"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(pSine, YSE::OBJ::D_SINE)
      _NO_MESSAGES
      _DO_CALCULATE
      _DO_RESET

      _FLOAT_IN(SetFrequency)
      _BUFFER_IN(SetFrequencyBuffer)

    private:
      DSP::sine sine;

      aFlt frequency;
      DSP::buffer * freqBuffer;
    };
  }
}