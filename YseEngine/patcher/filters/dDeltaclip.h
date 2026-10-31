#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/filters/slewLimiter.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~deltaclip``: limits the change from one sample to the next,
     *         wrapping ``DSP::slewLimiter`` (issue #1107).
     *
     *  Ported from MSP's ``deltaclip~``. The wrapper only maps the pins,
     *  messages and parameter onto the DSP class.
     *
     *  - Inlet 0: the signal to limit (unconnected reads 0).
     *  - Inlet 1: the minimum change per sample (the steepest fall, normally
     *    negative), a number or a signal read per sample; unconnected reads
     *    the last number sent to it, else the creation argument (#1036).
     *  - Inlet 2: the maximum change per sample (the steepest rise), likewise.
     *  - Any inlet takes ``reset``, which sets both limit numbers back to 0
     *    as MSP's does, and a list of up to two numbers: the minimum and the
     *    maximum, setting the numbers the unconnected inlets read. A command
     *    that arrives as a list (text through a receive) is read the same
     *    way.
     *  - Outlet: the limited signal.
     *  - Parameter: ``limits``, the creation arguments: the minimum and the
     *    maximum (``~deltaclip -0.01 0.01``). They are saved with the patch;
     *    a later number or message is not, as for ``~slide``.
     *
     *  Everything reaches the audio thread through atomics, applied to the
     *  DSP class at the start of each block.
     */
    PATCHER_CLASS(dDeltaclip, YSE::OBJ::D_DELTACLIP)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetMinimum)
    _INT_IN(SetMinimumInt)
    _FLOAT_IN(SetMaximum)
    _INT_IN(SetMaximumInt)

  private:
    DSP::slewLimiter fn;

    std::vector<std::string> arguments;

    std::atomic<float> minimum{DSP::slewLimiter::DEFAULT_LIMIT};
    std::atomic<float> maximum{DSP::slewLimiter::DEFAULT_LIMIT};
  };

} // namespace PATCHER
} // namespace YSE
