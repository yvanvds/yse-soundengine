#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/analysis/schmittTrigger.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~thresh``: a threshold with hysteresis, wrapping
     *         ``DSP::schmittTrigger`` (issue #1122).
     *
     *  Ported from Max's ``thresh~``. The output is 1 from the moment the
     *  input reaches the high threshold until it falls to the low one. The
     *  wrapper only maps the pins, the list and the parameter onto the DSP
     *  class.
     *
     *  - Inlet 0: the signal, or a number used while no signal is connected
     *    (#1036).
     *  - Inlet 1: the low (reset) threshold, a number or a signal read per
     *    sample; unconnected reads the last number sent to it, else the
     *    creation argument (#1036).
     *  - Inlet 2: the high (set) threshold, likewise.
     *  - Any inlet takes a list of two numbers, Max's middle-inlet list: the
     *    low and high thresholds, setting the numbers the unconnected inlets
     *    read. A list that arrives as text through a receive is read the same
     *    way.
     *  - Outlet: 1 or 0.
     *  - Parameter: ``thresh``, the creation arguments: the low and high
     *    thresholds (``~thresh 0.1 0.5``), both 0 by default. They are saved
     *    with the patch; a later number or list is not, as for ``~slide``.
     *
     *  Everything reaches the audio thread through atomics, read at the start
     *  of each block.
     */
    PATCHER_CLASS(dThresh, YSE::OBJ::D_THRESH)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetIn)
    _INT_IN(SetInInt)
    _FLOAT_IN(SetLow)
    _INT_IN(SetLowInt)
    _FLOAT_IN(SetHigh)
    _INT_IN(SetHighInt)

  private:
    DSP::schmittTrigger fn;

    std::vector<std::string> arguments;

    std::atomic<float> in{0.f};
    std::atomic<float> low{0.f};
    std::atomic<float> high{0.f};
  };

} // namespace PATCHER
} // namespace YSE
