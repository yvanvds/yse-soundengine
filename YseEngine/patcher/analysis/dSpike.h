#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/analysis/spikeDetector.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~spike``: the time between zero to non-zero transitions of a
     *         signal, wrapping ``DSP::spikeDetector`` (issue #1123).
     *
     *  Ported from Max's ``spike~``. The interval is armed in CALC() and
     *  sent by the patcher after the block's DSP traversal (issue #1037).
     *
     *  - Inlet 0: the signal, or a number used while no signal is connected
     *    (#1036). A bang restarts the refractory period from the start of
     *    the next block.
     *  - Inlet 1: the refractory period in milliseconds, a number.
     *  - Outlet: the interval in milliseconds since the previous reported
     *    transition. Several in one block are one number, the last (the
     *    outlet slot keeps the latest float, see outlet::ArmFloat).
     *  - Parameter: ``refractory``, the creation argument, in ms (default
     *    0). A number in the right inlet changes it and is saved with it.
     *
     *  The number and the bang reach the audio thread through atomics, read
     *  at the start of each block. The wrapper contains no sample math.
     */
    PATCHER_CLASS(dSpike, YSE::OBJ::D_SPIKE)
    _NO_MESSAGES
    _DO_CALCULATE

    _BANG_IN(Restart)
    _FLOAT_IN(SetIn)
    _INT_IN(SetInInt)
    _FLOAT_IN(SetRefractory)
    _INT_IN(SetRefractoryInt)

  private:
    DSP::spikeDetector fn;

    aFlt refractory;
    aFlt in{0.f};
    std::atomic<bool> restart{false};
  };

} // namespace PATCHER
} // namespace YSE
