#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/delay/sampleDelay.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~delay``: delays a signal by a number of samples, down to no
     *         delay at all, wrapping ``DSP::sampleDelay`` (issue #1104).
     *
     *  Ported from Max's ``delay~``; not the control-rate ``.delay``, which
     *  delays a bang. The wrapper only maps the pins, messages and parameter
     *  onto the DSP class.
     *
     *  - Inlet 0: the signal to delay (unconnected reads 0).
     *  - Inlet 1: the delay time. A number (float or int) is samples, the
     *    fraction dropped, crossfaded over the ramp time; a signal is read per
     *    sample, in samples, interpolated, and wins over the number while it
     *    is connected (#1036).
     *  - Any inlet takes ``clear`` (silence the line), ``maxsize <n>`` (the
     *    longest delay in samples, up to the memory the first argument
     *    allocated; clears the line), ``ramp <ms>``, ``delay <time>`` (Max's
     *    attribute) and a bare time: ``<n>`` or ``<n> samples`` in samples,
     *    ``<n> ms`` in milliseconds, which follow the sample rate. A command
     *    that arrives as a list (text through a receive) is read the same
     *    way.
     *  - Outlet: the delayed signal.
     *  - Parameter: ``delay``, the creation arguments: the memory in samples
     *    (default 512), the initial delay in samples and the ramp time in ms
     *    (``~delay 44100 200 10``). The memory is allocated when the object
     *    is built; changing it rebuilds the object, so the allocation never
     *    happens on a line the audio thread is running. They are saved with
     *    the patch; a later number or message is not.
     *
     *  Everything reaches the audio thread through atomics, applied to the
     *  DSP class at the start of each block. The delay time and its unit
     *  travel in one atomic word, so a block never pairs a new number with an
     *  old unit.
     */
    PATCHER_CLASS(dDelay, YSE::OBJ::D_DELAY)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetDelay)
    _INT_IN(SetDelayInt)

  private:
    // The delay time as float bits in the low word, 1 in the high word for
    // milliseconds.
    static std::uint64_t Pack(float value, bool ms);
    void SetTime(float value, bool ms);

    DSP::sampleDelay fn;

    std::vector<std::string> arguments;

    std::atomic<std::uint64_t> time{0};
    std::atomic<float> rampMs{0.f};
    std::atomic<std::uint32_t> pendingMaximum{0};
    std::atomic<bool> maximumRequested{false};
    std::atomic<bool> clearRequested{false};
  };

} // namespace PATCHER
} // namespace YSE
