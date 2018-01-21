#pragma once
#include "..\pObject.h"
#include "dsp/ramp.hpp"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(pLine, YSE::OBJ::D_LINE)
      _HAS_PARAMS
      _HAS_MESSAGES
      _HAS_CALCULATE

      FLOAT_IN(SetTarget)
      FLOAT_IN(SetTime)

    private:
      aFlt target;
      aFlt time;
      bool stop;
      DSP::ramp ramp;
    };
  }
}