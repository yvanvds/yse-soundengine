#pragma once

#include "../pObject.h"
#include "../../dsp/operators/downsample.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~downsamp``: takes an input sample every N samples and holds
     *         it in between, wrapping ``DSP::downsample`` (issue #1109).
     *
     *  Ported from MSP's ``downsamp~``. The wrapper only maps the pins and
     *  the parameter onto the DSP class.
     *
     *  - Inlet 0: the signal (unconnected reads 0).
     *  - Inlet 1: the period, 1 or more: the number of samples each take is
     *    held. A number (float or int) or a signal read per sample;
     *    unconnected reads the last number sent to it, else the creation
     *    argument (#1036).
     *  - Outlet: the downsampled signal.
     *  - Parameter: ``period`` (argument 1, default 1). It is saved with the
     *    patch; a later number is not, as for ``~slide``.
     *
     *  The number reaches the audio thread through an atomic and is read as
     *  a constant signal at the start of each block.
     */
    PATCHER_CLASS(dDownsample, YSE::OBJ::D_DOWNSAMPLE)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetPeriod)
    _INT_IN(SetPeriodInt)

  private:
    DSP::downsample fn;

    aFlt period;
  };

} // namespace PATCHER
} // namespace YSE
