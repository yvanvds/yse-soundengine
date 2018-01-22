#pragma once
#include "..\pObject.h"
#include "dsp\filters.hpp"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(pBandpass, YSE::OBJ::D_BANDPASS)
      _NO_MESSAGES
      _DO_CALCULATE
      _DO_RESET

      _BUFFER_IN(SetBuffer)
      _FLOAT_IN(SetFrequency)
      _FLOAT_IN(SetQ)

    private:
      DSP::buffer * buffer;
      aFlt frequency, Q;
      YSE::DSP::bandPass filter;
    };
  }
}