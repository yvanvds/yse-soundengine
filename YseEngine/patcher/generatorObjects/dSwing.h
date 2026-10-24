#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/generators/phasorSwing.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~swing``: splits each cycle of a 0..1 ramp into two unequal
     *         ramps, wrapping ``DSP::phasorSwing`` (issue #1076).
     *
     *  Ported from Max's ``swing~``. The wrapper only maps pins, messages and
     *  parameters onto the DSP class.
     *
     *  - Inlet: the ramp to swing, usually a ``~phasor`` (unconnected reads
     *    0). A ``float`` / ``int`` sets the swing. Messages: ``swing <value>``
     *    and ``syncupdate <0|1>``.
     *  - Outlet: the two ramps per input cycle.
     *  - Parameters: ``swing`` (argument 1, default 0.5, clamped to
     *    0.15 .. 0.85 by the DSP class) and ``syncupdate`` (argument 2,
     *    default 0).
     *
     *  Control values reach the audio thread through atomics and are handed
     *  to the DSP class at the top of every block.
     */
    PATCHER_CLASS(dSwing, YSE::OBJ::D_SWING)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetSwing)
    _INT_IN(SetSwingInt)

  private:
    DSP::phasorSwing fn;

    aFlt swing;
    std::atomic<int> syncupdate{0};
  };

} // namespace PATCHER
} // namespace YSE
