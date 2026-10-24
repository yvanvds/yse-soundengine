#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/generators/phasorRate.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~rate``: time-scales a 0..1 ramp, wrapping
     *         ``DSP::phasorRate`` (issue #1073).
     *
     *  Ported from Max's ``rate~``. The wrapper only maps pins, messages and
     *  parameters onto the DSP class.
     *
     *  - Left inlet: the ramp to follow, usually a ``~phasor`` (unconnected
     *    reads 0). A ``float`` / ``int`` jumps the output to that value now
     *    (as ``goto``). Messages: ``goto <value> [<time>]``, ``reset``,
     *    ``oneshot <0|1>`` and ``sync <0|1|2>`` or
     *    ``sync <cycle|lock|off>``.
     *  - Right inlet: the multiplier, as a signal or a number (the number is
     *    used while no signal is connected, #1036).
     *  - Outlet: the time-scaled ramp.
     *  - Parameters: ``multiplier`` (argument 1, default 1), ``sync``
     *    (argument 2, default 2 = off) and ``oneshot`` (argument 3, default
     *    0).
     *
     *  Control values reach the audio thread through atomics; a jump or a
     *  reset is posted as a request and handed to the DSP class at the top
     *  of the next block.
     */
    PATCHER_CLASS(dRate, YSE::OBJ::D_RATE)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetPhase)
    _INT_IN(SetPhaseInt)
    _FLOAT_IN(SetMultiplier)
    _INT_IN(SetMultiplierInt)

  private:
    void PostJump(float value, float at, int kind);

    DSP::phasorRate fn;

    aFlt multiplier;
    std::atomic<int> sync{static_cast<int>(DSP::phasorRate::syncMode::off)};
    std::atomic<int> oneshot{0};

    // A jump waiting for the audio thread: 0 none, 1 now, 2 at `jumpAt`.
    // The kind is exchanged in CALC(), so each request is applied once.
    aFlt jumpValue;
    aFlt jumpAt;
    std::atomic<int> jumpPending{0};
    std::atomic<bool> resetPending{false};
  };

} // namespace PATCHER
} // namespace YSE
