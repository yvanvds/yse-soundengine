#pragma once
#include "..\pObject.h"
#include "dsp/ramp.hpp"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(pLine, YSE::OBJ::D_LINE)
      _DO_MESSAGES
      _DO_CALCULATE

      _FLOAT_IN(SetTarget)
      _FLOAT_IN(SetTime)

    private:
      aFlt target;
      aFlt time;
      bool stop;
      DSP::ramp ramp;
    };
  }
}