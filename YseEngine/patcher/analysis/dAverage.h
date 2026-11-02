#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/analysis/movingAverage.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~average``: the running average of a signal over its last N
     *         samples, bipolar, absolute or RMS, wrapping
     *         ``DSP::movingAverage`` (issue #1118).
     *
     *  Ported from Max's ``average~``. The wrapper only maps the pins,
     *  messages and parameter onto the DSP class.
     *
     *  - Inlet: the signal; unconnected it reads as silence. A number sets
     *    the averaging interval in samples, clamped to 1 .. the maximum, as
     *    Max's int. ``bipolar``, ``absolute`` and ``rms`` set the mode, as
     *    does ``mode <bipolar|absolute|rms>`` (Max's ``mode`` attribute).
     *  - Outlet: the average, a signal.
     *  - Parameter: ``average``, the creation arguments: the maximum
     *    averaging interval in samples (default 100), which is also the
     *    initial interval, then the mode word (default ``bipolar``):
     *    ``~average 1000 rms``. The maximum sizes the history; changing it
     *    rebuilds the object, so the allocation never happens on an object
     *    the audio thread is running. They are saved with the patch; a later
     *    number or message is not, as for ``~slide``.
     *
     *  The interval and the mode reach the audio thread through atomics,
     *  applied to the DSP class at the start of each block.
     */
    PATCHER_CLASS(dAverage, YSE::OBJ::D_AVERAGE)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetInterval)
    _INT_IN(SetIntervalInt)

  private:
    DSP::movingAverage fn;

    std::vector<std::string> arguments;

    std::atomic<int> interval{static_cast<int>(DSP::movingAverage::DEFAULT_CAPACITY)};
    std::atomic<int> mode{DSP::movingAverage::BIPOLAR};
  };

} // namespace PATCHER
} // namespace YSE
