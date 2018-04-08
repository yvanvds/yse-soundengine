#pragma once
#include "..\pObject.h"
#include "dsp/math.hpp"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(dClip, YSE::OBJ::D_CLIP)
      _NO_MESSAGES
      _DO_CALCULATE
      _DO_RESET

      _BUFFER_IN(SetBuffer)
      _FLOAT_IN(SetLow)
      _FLOAT_IN(SetHigh)

  private:
    DSP::buffer * buffer;
    aFlt low;
    aFlt high;

    DSP::clip clip;
    };
  }
}