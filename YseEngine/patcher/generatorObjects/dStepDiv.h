#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/generators/phasorStepDiv.hpp"
#include "dSubdiv.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~stepdiv``: a fresh 0..1 ramp for each step of a stepped
     *         function, wrapping ``DSP::phasorStepDiv`` (issue #1075).
     *
     *  Ported from Max's ``stepdiv~``. The wrapper only maps pins, messages
     *  and parameters onto the DSP class.
     *
     *  - Inlet 0: a ramp to divide, usually a ``~phasor``. While it is
     *    connected the steps share each of its cycles; while it is not, they
     *    run free over their times in milliseconds. A ``bang`` starts the
     *    free-running sequence. A ``float`` / ``int`` sets a pattern of one
     *    step of that time. A list is a breakpoint function in line format
     *    (what ``.function`` sends on a bang), read as ``value time`` pairs or,
     *    with ``curvemode 1``, ``value time curve`` triples; its times are
     *    the steps. Messages: ``pattern <times...>`` (none clears it),
     *    ``what <onsets...>`` (the moments the steps end, from the start),
     *    ``curvemode <0|1|2>``, ``loop <0|1>``, ``syncupdate <0|1>``.
     *  - Inlet 1: an ``int`` (a ``float`` is truncated) jumps the
     *    free-running sequence to the start of that step.
     *  - Outlet 0: the step ramps. Outlet 1: the index of the step playing,
     *    a signal.
     *  - Parameters: ``curvemode`` (argument 1, default 2), ``loop``
     *    (argument 2, default 0) and ``syncupdate`` (argument 3, default 0).
     *    The pattern is set by message only and is not saved with the
     *    patch.
     *
     *  ``curvemode 2`` (auto) follows the sending function in Max. The only
     *  function object here, ``.function``, sends pairs, so auto reads pairs.
     *
     *  Scalars and triggers reach the audio thread through atomics. A
     *  pattern travels in ``dSubdiv``'s mailbox (a fixed table of atomics
     *  under a sequence counter): slot 0 says whether the rest are times (0)
     *  or onsets (1), so a message carries up to 63 numbers. One message
     *  thread at a time is assumed, as for every object's messages.
     */
    PATCHER_CLASS(dStepDiv, YSE::OBJ::D_STEPDIV)
    _DO_MESSAGES
    _DO_CALCULATE

    _BANG_IN(Start)
    _FLOAT_IN(SetTime)
    _INT_IN(SetTimeInt)
    _LIST_IN(SetList)
    _FLOAT_IN(SetStep)
    _INT_IN(SetStepInt)

  private:
    static_assert(DSP::phasorStepDiv::MAX_STEPS == DSP::phasorSubdiv::MAX_PATTERN,
                  "the pattern travels in dSubdiv's mailbox");

    // Slot 0 of a pattern message: what the numbers after it are.
    static constexpr float TIMES = 0.f;
    static constexpr float ONSETS = 1.f;

    void PostTime(float time);

    DSP::phasorStepDiv fn;

    std::atomic<int> curvemode{2};
    std::atomic<int> loop{0};
    std::atomic<int> syncupdate{0};

    std::atomic<bool> startPending{false};
    std::atomic<bool> stepPending{false};
    std::atomic<int> stepTarget{0};

    dSubdiv::mailbox<float> patterns;
  };

} // namespace PATCHER
} // namespace YSE
