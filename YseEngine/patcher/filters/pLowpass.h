#pragma once
#include "..\pObject.h"
#include "dsp/filters.hpp"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(pLowpass, YSE::OBJ::D_LOWPASS)
      _NO_MESSAGES
      _DO_CALCULATE
      _DO_RESET

      _BUFFER_IN(SetBuffer)
      _FLOAT_IN(SetFrequency)

    private:
      DSP::buffer * buffer;
      aFlt frequency;
      DSP::lowPass filter;
    };
  }
}