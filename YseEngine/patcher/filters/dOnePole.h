#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/filters/onePole.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~onepole``: single-pole lowpass, wrapping ``DSP::onePole``
     *         (issue #1086).
     *
     *  Ported from Max's ``onepole~``. The wrapper only maps the pins,
     *  messages and parameter onto the DSP class.
     *
     *  - Inlet 0: the signal to filter (unconnected reads 0).
     *  - Inlet 1: the frequency, a number or a signal (read once per block,
     *    its first sample); unconnected reads the last number sent to it,
     *    else the creation argument (#1036).
     *  - Either inlet takes ``hz`` (or ``Hz``), ``linear`` and ``radians``,
     *    the frequency mode (see ``DSP::onePole``), and ``clear``, which
     *    sets the filter state to 0. A command that arrives as a list (text
     *    through a receive) is read the same way.
     *  - Outlet: the filtered signal.
     *  - Parameter: ``frequency``, the creation arguments: a number, the
     *    frequency, and a mode word (``~onepole 0.5 linear``). They are saved
     *    with the patch; a later number or message is not, as for ``~sah``.
     *
     *  Everything reaches the audio thread through atomics, applied to the
     *  DSP class at the start of each block.
     */
    PATCHER_CLASS(dOnePole, YSE::OBJ::D_ONEPOLE)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetFrequency)
    _INT_IN(SetFrequencyInt)

  private:
    DSP::onePole fn;

    std::vector<std::string> arguments;

    std::atomic<float> frequency{0.f};
    std::atomic<int> mode{DSP::onePole::HZ};
    std::atomic<bool> clearRequested{false};
  };

} // namespace PATCHER
} // namespace YSE
