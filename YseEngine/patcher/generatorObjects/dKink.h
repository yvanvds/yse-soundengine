#pragma once

#include "../pObject.h"
#include "../../dsp/generators/kink.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~kink``: phase distortion of a 0..1 ramp, wrapping
     *         ``DSP::kink`` (issue #1060).
     *
     *  Ported from Max's ``kink~``. The wrapper only maps pins and
     *  parameters onto the DSP class.
     *
     *  - Left inlet: the phase, usually a ``~phasor``, as a signal or a
     *    number (``float`` / ``int``; the number is used while no signal is
     *    connected, see #1036).
     *  - Right inlet: the slope multiplier, as a signal or a number.
     *  - Parameter: ``slope`` (argument 1, default 1).
     *
     *  Control values reach the audio thread through atomics.
     */
    PATCHER_CLASS(dKink, YSE::OBJ::D_KINK)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetPhase)
    _INT_IN(SetPhaseInt)
    _FLOAT_IN(SetSlope)
    _INT_IN(SetSlopeInt)

  private:
    DSP::kink fn;

    aFlt phase;
    aFlt slope;
  };

} // namespace PATCHER
} // namespace YSE
