#pragma once

#include "../pObject.h"
#include "../../dsp/analysis/direction.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~change``: the direction of a signal, wrapping
     *         ``DSP::direction`` (issue #1120).
     *
     *  Ported from Max's ``change~``, whose only message is a float used as
     *  the input. Each output sample is 1 when the input sample is above the
     *  one before it, -1 when it is below and 0 when it is the same; the
     *  sample before the first is 0.
     *
     *  - Inlet: the signal, or a number used while no signal is connected
     *    (#1036). A new number shows as one sample of 1 or -1, then 0.
     *  - Outlet: the direction signal.
     *
     *  No parameters. The wrapper contains no sample math.
     */
    PATCHER_CLASS(dChange, YSE::OBJ::D_CHANGE)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)

  private:
    DSP::direction fn;
    aFlt floatIn;
  };

} // namespace PATCHER
} // namespace YSE
