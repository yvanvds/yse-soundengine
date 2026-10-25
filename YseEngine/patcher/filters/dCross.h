#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/filters/crossover.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~cross``: third-order crossover, wrapping ``DSP::crossover``
     *         (issue #1095).
     *
     *  Ported from Max's ``cross~``. The wrapper only maps the pins,
     *  messages and parameter onto the DSP class.
     *
     *  - Inlet 0: the signal to split (unconnected reads 0).
     *  - Inlet 1: the cutoff in Hz, a number or a signal (read per sample);
     *    unconnected reads the last number sent to it, else the creation
     *    argument (#1036).
     *  - Any inlet takes ``clear``, which sets the filter state to 0, and a
     *    number as text (a list through a receive), which sets the cutoff.
     *  - Outlet 0: the lowpass band; outlet 1: the highpass band.
     *  - Parameter: ``cutoff``, the creation argument (``~cross 800``,
     *    default 1000 Hz). It is saved with the patch; a later number or
     *    message is not, as for ``~lores``.
     *
     *  Everything reaches the audio thread through atomics, applied to the
     *  DSP class at the start of each block.
     */
    PATCHER_CLASS(dCross, YSE::OBJ::D_CROSS)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetCutoff)
    _INT_IN(SetCutoffInt)

  private:
    DSP::crossover fn;

    std::vector<std::string> arguments;

    std::atomic<float> cutoff{DSP::crossover::DEFAULT_CUTOFF};
    std::atomic<bool> clearRequested{false};
  };

} // namespace PATCHER
} // namespace YSE
