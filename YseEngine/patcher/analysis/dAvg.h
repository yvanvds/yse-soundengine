#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/analysis/movingAverage.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~avg``: the average absolute value of a signal since the
     *         last report, wrapping the accumulation of
     *         ``DSP::movingAverage`` (issue #1119).
     *
     *  Ported from Max's ``avg~``. The report is armed in CALC() and sent
     *  by the patcher after the block's DSP traversal (issue #1037).
     *
     *  - Inlet: the signal; unconnected it reads as silence. A bang reports
     *    the average up to the end of the last block, at the start of the
     *    next one, and starts over.
     *  - Outlet: the average, 0 when nothing was measured.
     *  - No parameters: Max's ``avg~`` takes no arguments.
     *
     *  The bang reaches the audio thread through an atomic, read at the
     *  start of each block. The wrapper contains no sample math.
     */
    PATCHER_CLASS(dAvg, YSE::OBJ::D_AVG)
    _NO_MESSAGES
    _DO_CALCULATE

    _BANG_IN(Report)

  private:
    DSP::movingAverage fn{1}; // only the accumulation is used: no history
    std::atomic<bool> report{false};
  };

} // namespace PATCHER
} // namespace YSE
