#pragma once

#include "../pObject.h"
#include "../../dsp/operators/delta.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~delta``: the difference between successive samples,
     *         wrapping ``DSP::delta`` (issue #1108).
     *
     *  Ported from Max's ``delta~``, which takes no arguments, messages or
     *  attributes. Each output sample is the input sample minus the one
     *  before it; the sample before the first is 0.
     *
     *  - Inlet: the signal, or a number used while no signal is connected
     *    (#1036). A new number shows as one sample of difference, then 0.
     *  - Outlet: the difference signal.
     *
     *  No parameters. The wrapper contains no sample math.
     */
    PATCHER_CLASS(dDelta, YSE::OBJ::D_DELTA)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)

  private:
    DSP::delta fn;
    aFlt floatIn;
  };

} // namespace PATCHER
} // namespace YSE
