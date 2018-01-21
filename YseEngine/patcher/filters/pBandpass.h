#pragma once
#include "..\pObject.h"
#include "dsp\filters.hpp"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(pBandpass, YSE::OBJ::D_BANDPASS)
      _HAS_PARAMS
      _NO_MESSAGES
      _HAS_CALCULATE
      _HAS_DSP_RESET

      BUFFER_IN(SetBuffer)
      FLOAT_IN(SetFrequency)
      FLOAT_IN(SetQ)

    private:
      DSP::buffer * buffer;
      aFlt frequency, Q;
      YSE::DSP::bandPass filter;
    };
  }
}