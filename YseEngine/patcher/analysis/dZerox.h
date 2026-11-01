#pragma once

#include "../pObject.h"
#include "../../dsp/analysis/zeroCrossing.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~zerox``: zero crossings of a signal, wrapping
     *         ``DSP::zeroCrossing`` (issue #1124).
     *
     *  Ported from Max's ``zerox~``. The wrapper only maps the pins, the
     *  message and the parameter onto the DSP class.
     *
     *  - Inlet: the signal, or a number used while no signal is connected
     *    (#1036). ``set <volume>`` sets the click volume.
     *  - Outlet 0: the number of crossings in the block, as a signal.
     *  - Outlet 1: a click of the click volume on each crossing, 0 elsewhere.
     *  - Parameter: ``volume``, the creation argument, the click volume
     *    (default 1). It is saved with the patch; a later ``set`` is
     *    not, as for ``~slide``.
     *
     *  The numbers reach the audio thread through atomics, read at the start
     *  of each block.
     */
    PATCHER_CLASS(dZerox, YSE::OBJ::D_ZEROX)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetIn)
    _INT_IN(SetInInt)

  private:
    DSP::zeroCrossing fn;

    aFlt volume;
    aFlt in{0.f};
  };

} // namespace PATCHER
} // namespace YSE
