#pragma once
#include "..\pObject.h"
#include "dsp\oscillators.hpp"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(dSaw, YSE::OBJ::D_SAW)
      _NO_MESSAGES
      _DO_CALCULATE
      _DO_RESET

      _FLOAT_IN(SetFrequency)
      _BUFFER_IN(SetFrequencyBuffer)

private:
  DSP::saw saw;

  aFlt frequency;
  DSP::buffer * freqBuffer;
  };
}
}