#pragma once

#include "../pObject.h"
#include "../../dsp/operators/arithmetic.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~!-``: reversed signal subtraction, ``right - left``, wrapping
     *         ``DSP::reverseSubtract`` (issue #1046).
     *
     *  Ported from Max's ``rminus~``. Both inlets take a signal or a number;
     *  the number is used while no signal is connected, and a connected
     *  signal wins (#1036). ``~!- 1`` gives ``1 - x``.
     *
     *  - Left inlet: the value subtracted.
     *  - Right inlet: the value subtracted from. Its number is the ``right``
     *    parameter (creation argument 1, default 0).
     *
     *  The wrapper holds the numbers as atomics and contains no sample math.
     */
    PATCHER_CLASS(dReverseSubstract, YSE::OBJ::D_REVERSESUBSTRACT)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetLeftFloat)
    _INT_IN(SetLeftInt)
    _FLOAT_IN(SetRightFloat)
    _INT_IN(SetRightInt)

  private:
    aFlt leftFloatIn;
    aFlt rightFloatIn;

    DSP::reverseSubtract op;
  };

} // namespace PATCHER
} // namespace YSE
