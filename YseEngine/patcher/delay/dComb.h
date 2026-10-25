#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/delay/comb.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~comb``: comb filter with one delay for the feedforward and
     *         the feedback path, wrapping ``DSP::comb`` (issue #1094).
     *
     *  Ported from Max's ``comb~``: ``y(n) = a x(n) + b x(n-D) + c y(n-D)``.
     *  The wrapper only maps the pins, messages and parameter onto the DSP
     *  class.
     *
     *  - Inlet 0: the signal to filter (unconnected reads 0).
     *  - Inlet 1: the delay ``D`` in ms, a number or a signal (read per
     *    sample); unconnected reads the last number sent to it, else the
     *    creation argument (#1036).
     *  - Inlets 2-4: the gain ``a``, the feedforward gain ``b`` and the
     *    feedback gain ``c``, likewise.
     *  - Any inlet takes ``clear``, which silences the delay lines, and a list
     *    of up to four numbers: the delay and the three gains, which set the
     *    numbers the unconnected inlets read. A command that arrives as a list
     *    (text through a receive) is read the same way.
     *  - Outlet: the filtered signal.
     *  - Parameter: ``maxdelay``, the creation arguments: the maximum delay in
     *    ms, the initial delay, ``a``, ``b`` and ``c`` (``~comb 50 5 1 0 0.9``).
     *    The maximum delay sizes the delay lines; changing it rebuilds the
     *    object, so the allocation never happens on a filter the audio thread
     *    is running. They are saved with the patch; a later number or message
     *    is not.
     *
     *  Everything reaches the audio thread through atomics, applied to the
     *  DSP class at the start of each block.
     */
    PATCHER_CLASS(dComb, YSE::OBJ::D_COMB)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetDelay)
    _INT_IN(SetDelayInt)
    _FLOAT_IN(SetGain)
    _INT_IN(SetGainInt)
    _FLOAT_IN(SetFeedforward)
    _INT_IN(SetFeedforwardInt)
    _FLOAT_IN(SetFeedback)
    _INT_IN(SetFeedbackInt)

  private:
    DSP::comb fn;

    std::vector<std::string> arguments;

    std::atomic<float> delay{0.f};
    std::atomic<float> gain{0.f};
    std::atomic<float> feedforward{0.f};
    std::atomic<float> feedback{0.f};
    std::atomic<bool> clearRequested{false};
  };

} // namespace PATCHER
} // namespace YSE
