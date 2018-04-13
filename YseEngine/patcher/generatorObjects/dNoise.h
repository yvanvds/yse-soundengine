#pragma once
#include "..\pObject.h"
#include "dsp\oscillators.hpp"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(dNoise, YSE::OBJ::D_NOISE)
      _NO_MESSAGES
      _DO_CALCULATE

private:
  DSP::noise noise;
  };
}
}