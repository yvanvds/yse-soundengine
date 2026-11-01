#pragma once

#include "../pObject.h"
#include "../../dsp/analysis/edgeDetector.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~edge``: bangs on the zero / non-zero transitions of a
     *         signal, wrapping ``DSP::edgeDetector`` (issue #1121).
     *
     *  Ported from Max's ``edge~``, which has no arguments, messages or
     *  attributes. The bangs are armed in CALC() and sent by the patcher
     *  after the block's DSP traversal (issue #1037), right outlet first.
     *
     *  - Inlet: the signal, or a number used while no signal is connected
     *    (#1036). A number that turns zero or non-zero is a transition at
     *    the start of the next block.
     *  - Left outlet: a bang when the signal goes from zero to non-zero.
     *  - Right outlet: a bang when it goes from non-zero to zero.
     *
     *  Several transitions of one kind in a block are one bang: the outlet
     *  slot coalesces bangs (see outlet::ArmBang). No parameters. The
     *  wrapper contains no sample math.
     */
    PATCHER_CLASS(dEdge, YSE::OBJ::D_EDGE)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)

  private:
    DSP::edgeDetector fn;
    aFlt floatIn;
  };

} // namespace PATCHER
} // namespace YSE
