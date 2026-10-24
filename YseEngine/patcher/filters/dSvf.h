#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/filters/stateVariableFilter.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~svf``: state-variable filter with simultaneous lowpass,
     *         highpass, bandpass and notch outputs, wrapping
     *         ``DSP::stateVariableFilter`` (issue #1089).
     *
     *  Ported from Max's ``svf~``. The wrapper only maps the pins, messages
     *  and parameter onto the DSP class.
     *
     *  - Inlet 0: the signal to filter (unconnected reads 0).
     *  - Inlet 1: the center frequency, a number or a signal (read per
     *    sample), read in the current mode; unconnected reads the last number
     *    sent to it, else the creation argument (#1036).
     *  - Inlet 2: the resonance, 0-1, a number or a signal, likewise.
     *  - Any inlet takes ``hz``, ``linear`` and ``radians`` (the frequency
     *    mode, any letter case), ``clear``, which sets the filter state to 0,
     *    ``freq <x>`` (or ``cf <x>``) and ``q <x>`` (or ``resonance <x>``),
     *    and a list of up to two numbers: the frequency and the resonance.
     *    Each sets the numbers the unconnected inlets read. A command that
     *    arrives as a list (text through a receive) is read the same way.
     *  - Outlets: the lowpass, the highpass, the bandpass and the notch.
     *  - Parameter: ``frequency``, the creation arguments: the frequency and
     *    the resonance as up to two numbers and a mode word
     *    (``~svf 0.25 0.8 linear``), or the keywords above. They are saved
     *    with the patch; a later number or message is not, as for ``~lores``.
     *
     *  Everything reaches the audio thread through atomics, applied to the
     *  DSP class at the start of each block.
     */
    PATCHER_CLASS(dSvf, YSE::OBJ::D_SVF)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetFrequency)
    _INT_IN(SetFrequencyInt)
    _FLOAT_IN(SetResonance)
    _INT_IN(SetResonanceInt)

  private:
    DSP::stateVariableFilter fn;

    std::vector<std::string> arguments;

    std::atomic<float> frequency{0.f};
    std::atomic<float> resonance{0.01f};
    std::atomic<int> mode{DSP::stateVariableFilter::HZ};
    std::atomic<bool> clearRequested{false};
  };

} // namespace PATCHER
} // namespace YSE
