#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/analysis/peakTracker.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~peakamp``: the peak amplitude of a signal since the last
     *         report, wrapping ``DSP::peakTracker`` (issue #1126).
     *
     *  Ported from Max's ``peakamp~``. The report is armed in CALC() and
     *  sent by the patcher after the block's DSP traversal (issue #1037).
     *
     *  - Inlet: the signal; unconnected it reads as silence. A bang reports
     *    the peak up to the end of the last block, at the start of the next
     *    one, and starts a new window. A number sets the reporting interval
     *    in milliseconds, 0 stops it, as in Max. Also takes
     *    ``interval <ms>`` and ``signed <0|1>``.
     *  - Outlet: the peak. Several reports in one block are one number, the
     *    last (the outlet slot keeps the latest float, see
     *    outlet::ArmFloat).
     *  - Parameters: ``interval`` in ms (creation argument 1, default 0:
     *    only a bang reports) and ``signed`` (argument 2, default 0). They
     *    are saved with the patch; a later number or message is not, as for
     *    ``~slide``.
     *
     *  The numbers and the bang reach the audio thread through atomics,
     *  read at the start of each block. The wrapper contains no sample math.
     */
    PATCHER_CLASS(dPeakamp, YSE::OBJ::D_PEAKAMP)
    _DO_MESSAGES
    _DO_CALCULATE

    _BANG_IN(Report)
    _FLOAT_IN(SetInterval)
    _INT_IN(SetIntervalInt)

  private:
    DSP::peakTracker fn;

    aFlt interval;
    aInt signedPeak;
    std::atomic<bool> report{false};
  };

} // namespace PATCHER
} // namespace YSE
