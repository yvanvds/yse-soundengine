#pragma once

#include "../pObject.h"
#include "../../dsp/operators/degrade.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~degrade``: a bitcrusher, reducing the effective sample rate
     *         and the bit depth of a signal, wrapping ``DSP::degrade``
     *         (issue #1106).
     *
     *  Ported from MSP's ``degrade~``. The wrapper only maps the pins and
     *  parameters onto the DSP class.
     *
     *  - Inlet 0: the signal (unconnected reads 0).
     *  - Inlet 1: the ratio, 0 to 1: the fraction of the sample rate at which
     *    a new input sample is taken. A number (float or int) or a signal read
     *    per sample; unconnected reads the last number sent to it, else the
     *    creation argument (#1036).
     *  - Inlet 2: the bits, 1 to 24, likewise.
     *  - Outlet: the degraded signal.
     *  - Parameters: ``ratio`` (argument 1, default 1) and ``bits`` (argument
     *    2, default 24). They are saved with the patch; a later number is not,
     *    as for ``~slide``.
     *
     *  The numbers reach the audio thread through atomics and are read as
     *  constant signals at the start of each block.
     */
    PATCHER_CLASS(dDegrade, YSE::OBJ::D_DEGRADE)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetRatio)
    _INT_IN(SetRatioInt)
    _FLOAT_IN(SetBits)
    _INT_IN(SetBitsInt)

  private:
    DSP::degrade fn;

    aFlt ratio;
    aFlt bits;
  };

} // namespace PATCHER
} // namespace YSE
