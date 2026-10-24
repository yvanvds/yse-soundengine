#pragma once

#include "../pObject.h"
#include "../../dsp/generators/bandLimitedOscillators.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~rect``: an antialiased rectangle (pulse) wave, wrapping
     *         ``DSP::blRect`` (issue #1055).
     *
     *  Ported from Max's ``rect~``. The wrapper only maps pins and
     *  parameters onto the DSP class.
     *
     *  - Left inlet: frequency in Hz, as a signal or a number (``float`` /
     *    ``int``; the number is used while no signal is connected, see
     *    #1036). Also takes ``synctrig <x>``, the sync threshold.
     *  - Middle inlet: pulse width, wrapped into 0..1, as a signal or a number.
     *  - Right inlet: an optional sync signal; a rise across the threshold
     *    (0.5 by default) restarts the cycle.
     *  - Parameters: ``frequency`` (argument 1, default 0) and ``width``
     *    (argument 2, default 0.5).
     *
     *  Control values reach the audio thread through atomics.
     */
    PATCHER_CLASS(dRect, YSE::OBJ::D_RECT)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFrequency)
    _INT_IN(SetFrequencyInt)
    _FLOAT_IN(SetWidth)
    _INT_IN(SetWidthInt)

  private:
    DSP::blRect rect;

    aFlt frequency;
    aFlt width;
    aFlt threshold;
  };

} // namespace PATCHER
} // namespace YSE
