#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/filters/reson.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~reson``: resonant bandpass, wrapping ``DSP::reson``
     *         (issue #1088).
     *
     *  Ported from Max's ``reson~``. The wrapper only maps the pins,
     *  messages and parameter onto the DSP class.
     *
     *  - Inlet 0: the signal to filter (unconnected reads 0).
     *  - Inlet 1: the gain, a number or a signal (read per sample);
     *    unconnected reads the last number sent to it, else the creation
     *    argument (#1036).
     *  - Inlet 2: the center frequency in Hz, a number or a signal, likewise.
     *  - Inlet 3: the Q, a number or a signal, likewise.
     *  - Any inlet takes ``clear``, which sets the filter state to 0,
     *    ``gain <x>``, ``cf <x>`` (or ``freq <x>``) and ``q <x>``, and a list
     *    of up to three numbers: the gain, the center frequency and the Q.
     *    Each sets the numbers the unconnected inlets read. A command that
     *    arrives as a list (text through a receive) is read the same way.
     *  - Outlet: the filtered signal.
     *  - Parameter: ``gain``, the creation arguments: the gain, the center
     *    frequency and the Q as up to three numbers (``~reson 1 800 20``), or
     *    as the keywords above. They are saved with the patch; a later number
     *    or message is not, as for ``~lores``.
     *
     *  Everything reaches the audio thread through atomics, applied to the
     *  DSP class at the start of each block.
     */
    PATCHER_CLASS(dReson, YSE::OBJ::D_RESON)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetGain)
    _INT_IN(SetGainInt)
    _FLOAT_IN(SetFrequency)
    _INT_IN(SetFrequencyInt)
    _FLOAT_IN(SetQ)
    _INT_IN(SetQInt)

  private:
    DSP::reson fn;

    std::vector<std::string> arguments;

    std::atomic<float> gain{0.f};
    std::atomic<float> frequency{0.f};
    std::atomic<float> q{DSP::reson::MIN_Q};
    std::atomic<bool> clearRequested{false};
  };

} // namespace PATCHER
} // namespace YSE
