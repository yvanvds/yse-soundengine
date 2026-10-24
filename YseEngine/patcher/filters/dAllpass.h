#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/filters/allpass.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~allpass``: Schroeder allpass on an interpolated delay line,
     *         wrapping ``DSP::allpass`` (issue #1093).
     *
     *  Ported from Max's ``allpass~``. The wrapper only maps the pins,
     *  messages and parameter onto the DSP class.
     *
     *  - Inlet 0: the signal to filter (unconnected reads 0).
     *  - Inlet 1: the delay in ms, a number or a signal (read per sample);
     *    unconnected reads the last number sent to it, else the creation
     *    argument (#1036).
     *  - Inlet 2: the gain, a number or a signal, likewise.
     *  - Any inlet takes ``clear``, which silences the delay lines, and a list
     *    of up to two numbers: the delay and the gain, which set the numbers
     *    the unconnected inlets read. A command that arrives as a list (text
     *    through a receive) is read the same way.
     *  - Outlet: the filtered signal.
     *  - Parameter: ``maxdelay``, the creation arguments: the maximum delay in
     *    ms, the initial delay and the gain (``~allpass 50 12.5 0.7``). The
     *    maximum delay sizes the delay lines; changing it rebuilds the object,
     *    so the allocation never happens on a filter the audio thread is
     *    running. They are saved with the patch; a later number or message is
     *    not, as for ``~reson``.
     *
     *  Everything reaches the audio thread through atomics, applied to the
     *  DSP class at the start of each block.
     */
    PATCHER_CLASS(dAllpass, YSE::OBJ::D_ALLPASS)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetDelay)
    _INT_IN(SetDelayInt)
    _FLOAT_IN(SetGain)
    _INT_IN(SetGainInt)

  private:
    DSP::allpass fn;

    std::vector<std::string> arguments;

    std::atomic<float> delay{0.f};
    std::atomic<float> gain{0.f};
    std::atomic<bool> clearRequested{false};
  };

} // namespace PATCHER
} // namespace YSE
