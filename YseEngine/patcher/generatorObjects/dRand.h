#pragma once

#include "../pObject.h"
#include "../../dsp/generators/bandLimitedRandom.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~rand``: a band-limited random signal, wrapping
     *         ``DSP::bandLimitedRandom`` (issue #1057).
     *
     *  Ported from Max's ``rand~``. The wrapper only maps the pins and the
     *  parameter onto the DSP class; the signal is ``DSP::bandLimitedRandom``.
     *
     *  - Inlet: the frequency at which new random values are picked, in Hz,
     *    as a signal or a number (``float`` / ``int``; the number is used
     *    while no signal is connected, see #1036).
     *  - Parameter: ``frequency`` (creation argument 1, Max's
     *    ``initial-frequency``).
     *
     *  The number reaches the audio thread through an atomic.
     */
    PATCHER_CLASS(dRand, YSE::OBJ::D_RAND)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFrequency)
    _INT_IN(SetFrequencyInt)

  private:
    DSP::bandLimitedRandom random;
    aFlt frequency;
  };

} // namespace PATCHER
} // namespace YSE
