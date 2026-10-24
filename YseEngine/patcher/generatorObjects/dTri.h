#pragma once

#include "../pObject.h"
#include "../../dsp/generators/bandLimitedOscillators.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~tri``: an antialiased triangle wave with a variable duty
     *         cycle, wrapping ``DSP::blTri`` (issue #1055).
     *
     *  Ported from Max's ``tri~``. The wrapper only maps pins and
     *  parameters onto the DSP class.
     *
     *  - Left inlet: frequency in Hz, as a signal or a number (``float`` /
     *    ``int``; the number is used while no signal is connected, see
     *    #1036). Also takes ``synctrig <x>``, the sync threshold (an
     *    addition: ``tri~`` documents the sync inlet but not the message).
     *  - Middle inlet: duty cycle, held within 0.05..0.95, as a signal or a
     *    number.
     *  - Right inlet: an optional sync signal; a rise across the threshold
     *    (0.5 by default) restarts the cycle.
     *  - Parameters: ``frequency`` (argument 1, default 0) and ``duty``
     *    (argument 2, default 0.5).
     *
     *  Control values reach the audio thread through atomics.
     */
    PATCHER_CLASS(dTri, YSE::OBJ::D_TRI)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFrequency)
    _INT_IN(SetFrequencyInt)
    _FLOAT_IN(SetDuty)
    _INT_IN(SetDutyInt)

  private:
    DSP::blTri tri;

    aFlt frequency;
    aFlt duty;
    aFlt threshold;
  };

} // namespace PATCHER
} // namespace YSE
