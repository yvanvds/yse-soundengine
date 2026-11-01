#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/modules/frequencyShifter.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~freqshift``: single-sideband frequency shifter, wrapping
     *         ``DSP::frequencyShifter`` (issue #1110).
     *
     *  Ported from Max's ``freqshift~``. The wrapper only maps the pins,
     *  messages and parameter onto the DSP class.
     *
     *  - Inlet 0: the signal to shift (unconnected reads 0).
     *  - Inlet 1: the shift in Hz, a number (float or int) or a signal read
     *    per sample; unconnected reads the last number sent to it, else the
     *    creation argument (#1036).
     *  - Either inlet takes ``clear``, which sets the filter state to 0, and
     *    ``resetphase``, which sets the oscillator phase to 0, also as text
     *    through a receive.
     *  - Outlet 0: the signal shifted up; outlet 1: shifted down.
     *  - Parameter: ``shift`` (argument 1, default 0 Hz). It is saved with
     *    the patch; a later number is not, as for ``~slide``.
     *
     *  Everything reaches the audio thread through atomics, applied to the
     *  DSP class at the start of each block.
     */
    PATCHER_CLASS(dFreqshift, YSE::OBJ::D_FREQSHIFT)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetShift)
    _INT_IN(SetShiftInt)

  private:
    DSP::frequencyShifter fn;

    aFlt shift;
    std::atomic<bool> clearRequested{false};
    std::atomic<bool> resetRequested{false};
  };

} // namespace PATCHER
} // namespace YSE
