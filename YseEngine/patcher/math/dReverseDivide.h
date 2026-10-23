#pragma once

#include "../pObject.h"
#include "../../dsp/operators/arithmetic.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~!/``: reversed signal division, ``right / left``, wrapping
     *         ``DSP::reverseDivide`` (issue #1046).
     *
     *  Ported from Max's ``rdiv~``. Both inlets take a signal or a number;
     *  the number is used while no signal is connected, and a connected
     *  signal wins (#1036). A left sample of exactly 0 gives 0.
     *
     *  - Left inlet: the divisor.
     *  - Right inlet: the value divided. Its number is the ``right``
     *    parameter (creation argument 1, default 1, so a bare ``~!/`` gives
     *    ``1 / x``).
     *
     *  The wrapper holds the numbers as atomics and contains no sample math.
     */
    PATCHER_CLASS(dReverseDivide, YSE::OBJ::D_REVERSEDIVIDE)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetLeftFloat)
    _INT_IN(SetLeftInt)
    _FLOAT_IN(SetRightFloat)
    _INT_IN(SetRightInt)

  private:
    aFlt leftFloatIn;
    aFlt rightFloatIn;

    DSP::reverseDivide op;
  };

} // namespace PATCHER
} // namespace YSE
