#pragma once
#include "..\pObject.h"
#include "dsp/filters.hpp"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(pHighpass, YSE::OBJ::D_HIGHPASS)
      _HAS_PARAMS
      _NO_MESSAGES
      _HAS_CALCULATE
      _HAS_DSP_RESET

      BUFFER_IN(SetBuffer)
      FLOAT_IN(SetFrequency)

    private:
      DSP::buffer * buffer;
      aFlt frequency;
      DSP::highPass filter;
    };
  }
}