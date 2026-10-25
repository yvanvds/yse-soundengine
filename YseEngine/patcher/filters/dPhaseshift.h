#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/filters/phaseShift.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~phaseshift``: second-order allpass, wrapping
     *         ``DSP::phaseShift`` (issue #1098).
     *
     *  Ported from Max's ``phaseshift~``. The wrapper only maps the pins,
     *  messages and parameter onto the DSP class.
     *
     *  - Inlet 0: the signal to filter (unconnected reads 0).
     *  - Inlet 1: the center frequency in Hz, a number or a signal (read per
     *    sample); unconnected reads the last number sent to it, else the
     *    creation argument (#1036).
     *  - Inlet 2: the Q, a number or a signal, likewise.
     *  - Any inlet takes ``clear``, which sets the filter state to 0, and a
     *    list of up to two numbers: the center frequency and the Q, setting
     *    the numbers the unconnected inlets read. A command that arrives as a
     *    list (text through a receive) is read the same way.
     *  - Outlet: the phase-shifted signal.
     *  - Parameter: ``frequency``, the creation arguments: the center
     *    frequency and the Q (``~phaseshift 800 2``). They are saved with the
     *    patch; a later number or message is not, as for ``~reson``.
     *
     *  Everything reaches the audio thread through atomics, applied to the
     *  DSP class at the start of each block.
     */
    PATCHER_CLASS(dPhaseshift, YSE::OBJ::D_PHASESHIFT)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetFrequency)
    _INT_IN(SetFrequencyInt)
    _FLOAT_IN(SetQ)
    _INT_IN(SetQInt)

  private:
    DSP::phaseShift fn;

    std::vector<std::string> arguments;

    std::atomic<float> frequency{DSP::phaseShift::DEFAULT_FREQUENCY};
    std::atomic<float> q{DSP::phaseShift::DEFAULT_Q};
    std::atomic<bool> clearRequested{false};
  };

} // namespace PATCHER
} // namespace YSE
