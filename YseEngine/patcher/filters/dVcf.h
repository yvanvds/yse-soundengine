
#pragma once

#include "../pObject.h"
#include "../../dsp/oscillators.hpp"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(dVcf, YSE::OBJ::D_VCF)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetCenter)
    _FLOAT_IN(SetSharpness)

  private:
    DSP::vcf vcf;

    aFlt sharpness;

    aFlt center;
  };
}
}
