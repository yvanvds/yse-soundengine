#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/generators/triggeredRamp.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~ramp``: a single ramp from a start to an end value, started
     *         by a trigger signal, wrapping ``DSP::triggeredRamp`` (issue
     *         #1072).
     *
     *  Ported from Max's ``ramp~``. The wrapper only maps pins, messages
     *  and parameters onto the DSP class.
     *
     *  - Inlet 0: the trigger signal; a ramp starts when it goes from 0 to
     *    non-zero. An unconnected inlet reads 0. A ``float`` / ``int`` sets
     *    the duration (as in Max). Also takes Max's attributes as messages:
     *    ``duration <ms>``, ``start <x>``, ``end <x>``, ``curve <-1..1>``,
     *    ``mode <0|1>``, ``reset <0|1>`` and ``retrigger <0|1>``.
     *    ``interval <n>`` is accepted and ignored: the DSP class computes
     *    the curve exactly rather than in segments.
     *  - Inlet 1: the duration in ms, a signal or a number (the number is
     *    used while no signal is connected, #1036).
     *  - Inlet 2: the start value, a signal or a number.
     *  - Inlet 3: the end value, a signal or a number.
     *  - Outlet: the ramp.
     *  - Parameters: ``duration`` (argument 1, Max's argument; default
     *    100 ms), then YSE's ``start`` (0), ``end`` (1), ``curve`` (0),
     *    ``mode`` (0), ``reset`` (0) and ``retrigger`` (0), so a patch saves
     *    what Max keeps as attributes.
     *
     *  Control values reach the audio thread through atomics. Messages and
     *  numbers are not saved; the arguments are.
     */
    PATCHER_CLASS(dRamp, YSE::OBJ::D_RAMP)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)

  private:
    void Set(int inlet, float value);

    DSP::triggeredRamp fn;

    aFlt duration;
    aFlt start;
    aFlt end;
    aFlt curve;
    std::atomic<int> mode{0};
    std::atomic<int> reset{0};
    std::atomic<int> retrigger{0};
  };

} // namespace PATCHER
} // namespace YSE
