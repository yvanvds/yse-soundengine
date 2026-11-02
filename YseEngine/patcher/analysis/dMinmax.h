#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/analysis/minMax.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~minmax``: the running minimum and maximum of a signal,
     *         wrapping ``DSP::minMax`` (issue #1127).
     *
     *  Ported from Max's ``minmax~``. The bang's report is armed in CALC()
     *  and sent by the patcher after the block's DSP traversal (issue
     *  #1037).
     *
     *  - Inlet 0: the signal, or a number used while no signal is connected
     *    (#1036). A bang reports the minimum and the maximum up to the end
     *    of the last block, at the start of the next one. ``reset`` starts
     *    over at the start of the next block, after a bang that came with
     *    it.
     *  - Inlet 1: the reset signal, or a number used while none is
     *    connected: every sample where it is not 0 starts over at that
     *    sample.
     *  - Outlet 0 and 1: the running minimum and maximum, as signals.
     *  - Outlet 2 and 3: the minimum and the maximum, on a bang.
     *  - No parameters: Max's ``minmax~`` takes no arguments.
     *
     *  The numbers, the bang and the reset reach the audio thread through
     *  atomics, read at the start of each block. The wrapper contains no
     *  sample math.
     */
    PATCHER_CLASS(dMinmax, YSE::OBJ::D_MINMAX)
    _DO_MESSAGES
    _DO_CALCULATE

    _BANG_IN(Report)
    _FLOAT_IN(SetIn)
    _INT_IN(SetInInt)
    _FLOAT_IN(SetReset)
    _INT_IN(SetResetInt)

  private:
    DSP::minMax fn;

    aFlt in{0.f};
    aFlt resetIn{0.f};
    std::atomic<bool> report{false};
    std::atomic<bool> resetRequested{false};
  };

} // namespace PATCHER
} // namespace YSE
