#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/filters/lores.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~lores``: two-pole resonant lowpass, wrapping ``DSP::lores``
     *         (issue #1087).
     *
     *  Ported from Max's ``lores~``. The wrapper only maps the pins,
     *  messages and parameter onto the DSP class.
     *
     *  - Inlet 0: the signal to filter (unconnected reads 0).
     *  - Inlet 1: the cutoff in Hz, a number or a signal (read per sample);
     *    unconnected reads the last number sent to it, else the creation
     *    argument (#1036).
     *  - Inlet 2: the resonance, 0-1, a number or a signal, likewise.
     *  - Any inlet takes ``clear``, which sets the filter state to 0, and
     *    ``cutoff <x>`` / ``resonance <x>``, which set the numbers the
     *    unconnected inlets read. A command that arrives as a list (text
     *    through a receive) is read the same way.
     *  - Outlet: the filtered signal.
     *  - Parameter: ``cutoff``, the creation arguments: the cutoff and the
     *    resonance as two numbers (``~lores 800 0.7``), or as
     *    ``cutoff <x>`` / ``resonance <x>``. They are saved with the patch; a
     *    later number or message is not, as for ``~onepole``.
     *
     *  Everything reaches the audio thread through atomics, applied to the
     *  DSP class at the start of each block.
     */
    PATCHER_CLASS(dLores, YSE::OBJ::D_LORES)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetCutoff)
    _INT_IN(SetCutoffInt)
    _FLOAT_IN(SetResonance)
    _INT_IN(SetResonanceInt)

  private:
    DSP::lores fn;

    std::vector<std::string> arguments;

    std::atomic<float> cutoff{0.f};
    std::atomic<float> resonance{0.f};
    std::atomic<bool> clearRequested{false};
  };

} // namespace PATCHER
} // namespace YSE
