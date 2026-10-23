#pragma once

#include "../pObject.h"
#include "../../dsp/operators/arithmetic.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~%``: signal remainder, wrapping ``DSP::modulo`` (issue
     *         #1046).
     *
     *  Ported from Max's ``modulo~``. Both inlets take a signal or a number;
     *  the number is used while no signal is connected, and a connected
     *  signal wins (#1036). The remainder has the sign of the left input, as
     *  C's ``fmod`` (``-5.5 % 2`` is -1.5); a divisor of 0 gives 0.
     *
     *  - Left inlet: the value divided.
     *  - Right inlet: the divisor. Its number is the ``right`` parameter
     *    (creation argument 1, default 1).
     *
     *  The wrapper holds the numbers as atomics and contains no sample math.
     */
    PATCHER_CLASS(dModulo, YSE::OBJ::D_MODULO)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetLeftFloat)
    _INT_IN(SetLeftInt)
    _FLOAT_IN(SetRightFloat)
    _INT_IN(SetRightInt)

  private:
    aFlt leftFloatIn;
    aFlt rightFloatIn;

    DSP::modulo op;
  };

} // namespace PATCHER
} // namespace YSE
