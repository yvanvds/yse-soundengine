#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/generators/phasorTwist.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~twist``: bends a linear 0..1 ramp into an exponential
     *         curve, wrapping ``DSP::phasorTwist`` (issue #1077).
     *
     *  Ported from Max's ``twist~``. The wrapper only maps pins, messages and
     *  parameters onto the DSP class.
     *
     *  - Inlet: the ramp to bend, usually a ``~phasor`` (unconnected reads
     *    0). A ``float`` / ``int`` sets the curve. Messages: ``curve
     *    <value>``, ``shapemode <0|1>`` and ``syncupdate <0|1>``. Max's
     *    undocumented ``test`` message is not ported.
     *  - Outlet: the curved ramp.
     *  - Parameters: ``curve`` (argument 1, default 0, clamped to -1 .. 1 by
     *    the DSP class), ``shapemode`` (argument 2, default 1) and
     *    ``syncupdate`` (argument 3, default 0).
     *
     *  Control values reach the audio thread through atomics and are handed
     *  to the DSP class at the top of every block.
     */
    PATCHER_CLASS(dTwist, YSE::OBJ::D_TWIST)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetCurve)
    _INT_IN(SetCurveInt)

  private:
    DSP::phasorTwist fn;

    aFlt curve;
    std::atomic<int> shapemode{1};
    std::atomic<int> syncupdate{0};
  };

} // namespace PATCHER
} // namespace YSE
