
#pragma once

#include "../pObject.h"
#include "../../dsp/generators/bandLimitedOscillators.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~saw``: an antialiased sawtooth, wrapping ``DSP::blSaw``
     *         (issue #1055).
     *
     *  Ported from Max's ``saw~``. Before #1055 it was a naive phase ramp
     *  rescaled inside the wrapper, which aliased at high pitches; it is now
     *  band-limited (polyBLEP) and the wrapper only maps pins and parameters.
     *
     *  - Left inlet: frequency in Hz, as a signal or a number (``float`` /
     *    ``int``; the number is used while no signal is connected, see
     *    #1036). Also takes ``synctrig <x>``, the sync threshold.
     *  - Right inlet: an optional sync signal; a rise across the threshold
     *    (0.5 by default) restarts the cycle.
     *  - Parameter: ``frequency`` (creation argument 1). Its default stays
     *    440 Hz, as before the port, where ``saw~`` defaults to 0.
     *
     *  Control values reach the audio thread through atomics.
     */
    PATCHER_CLASS(dSaw, YSE::OBJ::D_SAW)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFrequency)
    _INT_IN(SetFrequencyInt)

  private:
    DSP::blSaw saw;

    aFlt frequency;
    aFlt threshold;
  };
}
}
