#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/operators/accumulator.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~+=``: running sum of a signal, wrapping
     *         ``DSP::accumulator`` (issue #1047).
     *
     *  Ported from Max's ``plusequals~``. Each output sample is the sum of
     *  every input sample so far, the current one included.
     *
     *  - Left inlet: the signal summed, or a number used while no signal is
     *    connected (#1036). A ``bang`` sets the sum to 0; ``set <x>`` sets it
     *    to ``x`` without output of its own.
     *  - Right inlet: a reset signal. At every sample where it is not 0 the
     *    sum restarts from 0. Unconnected, it never resets.
     *  - Parameter: ``initial`` (creation argument 1, default 0), the sum the
     *    object starts from.
     *
     *  Control-thread requests reach the audio thread through atomics: a
     *  bang or a ``set`` is posted and applied at the top of the next block.
     *  The wrapper contains no sample math.
     */
    PATCHER_CLASS(dPlusEquals, YSE::OBJ::D_PLUSEQUALS)
    _DO_MESSAGES
    _DO_CALCULATE

    _BANG_IN(Reset)
    _FLOAT_IN(SetInputFloat)
    _INT_IN(SetInputInt)

  private:
    DSP::accumulator accumulator;

    aFlt inputFloat;
    aFlt initial;

    // A `set` value and a bang waiting for the audio thread. The flags are
    // exchanged in CALC(), so each request is applied once.
    aFlt pendingSet;
    std::atomic<bool> setPending{false};
    std::atomic<bool> resetPending{false};
    // Audio thread only: whether the first block has run, so the sum starts
    // at `initial`.
    bool started = false;
  };

} // namespace PATCHER
} // namespace YSE
