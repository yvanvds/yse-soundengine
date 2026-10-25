#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/filters/rampSmooth.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~rampsmooth``: linear smoothing, wrapping ``DSP::rampSmooth``
     *         (issue #1100).
     *
     *  Ported from Max's ``rampsmooth~``: each change of the input starts a
     *  straight ramp to it over the ramp-up or ramp-down number of samples.
     *  The wrapper only maps the pins, messages and parameter onto the DSP
     *  class.
     *
     *  - Inlet 0: the signal to smooth (unconnected reads 0).
     *  - Inlet 1: the ramp-up length in samples, a number or a signal (read
     *    per sample); unconnected reads the last number sent to it, else the
     *    creation argument (#1036).
     *  - Inlet 2: the ramp-down length, likewise.
     *  - Any inlet takes ``ramp <n>`` (both lengths), ``rampup <n>`` and
     *    ``rampdown <n>`` (Max's attributes), and a list of up to two numbers:
     *    the ramp-up and ramp-down lengths, setting the numbers the
     *    unconnected inlets read. A command that arrives as a list (text
     *    through a receive) is read the same way.
     *  - Outlet: the smoothed signal.
     *  - Parameter: ``ramp``, the creation arguments: the ramp-up and
     *    ramp-down lengths (``~rampsmooth 100 1000``). They are saved with the
     *    patch; a later number or message is not, as for ``~slide``.
     *
     *  Everything reaches the audio thread through atomics, applied to the
     *  DSP class at the start of each block.
     */
    PATCHER_CLASS(dRampsmooth, YSE::OBJ::D_RAMPSMOOTH)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetUp)
    _INT_IN(SetUpInt)
    _FLOAT_IN(SetDown)
    _INT_IN(SetDownInt)

  private:
    DSP::rampSmooth fn;

    std::vector<std::string> arguments;

    std::atomic<float> up{DSP::rampSmooth::DEFAULT_RAMP};
    std::atomic<float> down{DSP::rampSmooth::DEFAULT_RAMP};
  };

} // namespace PATCHER
} // namespace YSE
