#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/modules/normalize.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~normalize``: scales a signal so its running peak meets a
     *         target amplitude, wrapping ``DSP::normalize`` (issue #1111).
     *
     *  Ported from Max's ``normalize~``. The wrapper only maps the pins,
     *  messages and parameter onto the DSP class.
     *
     *  - Inlet 0: the signal to normalize (unconnected reads 0). A number
     *    (float or int) sets the running peak.
     *  - Inlet 1: the target amplitude, a number (float or int) or a signal
     *    read per sample; unconnected reads the last number sent to it, else
     *    the creation argument (#1036).
     *  - Either inlet takes ``reset``, which sets the peak to its floor, or
     *    ``reset <peak>``, which sets it to that value, also as text through
     *    a receive.
     *  - Outlet: the normalized signal.
     *  - Parameter: ``amplitude`` (argument 1, default 1). It is saved with
     *    the patch; a later number is not, as for ``~slide``.
     *
     *  Everything reaches the audio thread through atomics, applied to the
     *  DSP class at the start of each block.
     */
    PATCHER_CLASS(dNormalize, YSE::OBJ::D_NORMALIZE)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)

  private:
    DSP::normalize fn;

    aFlt amplitude;
    aFlt peakValue;
    std::atomic<bool> peakRequested{false};
  };

} // namespace PATCHER
} // namespace YSE
