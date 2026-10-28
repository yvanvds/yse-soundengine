#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/filters/slide.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~slide``: logarithmic slide, wrapping ``DSP::slide``
     *         (issue #1099).
     *
     *  Ported from Max's ``slide~``; the audio-rate counterpart of
     *  ``.slide`` (#459), with the same equation and defaults. The wrapper
     *  only maps the pins, messages and parameter onto the DSP class.
     *
     *  - Inlet 0: the signal to smooth (unconnected reads 0).
     *  - Inlet 1: the slide-up amount in samples, a number or a signal (read
     *    per sample); unconnected reads the last number sent to it, else the
     *    creation argument (#1036).
     *  - Inlet 2: the slide-down amount, likewise.
     *  - Any inlet takes ``reset``, which sets the output to 0,
     *    ``slideup <n>`` and ``slidedown <n>`` (Max's attributes), and a list
     *    of up to two numbers: the slide-up and slide-down amounts, setting
     *    the numbers the unconnected inlets read. A command that arrives as a
     *    list (text through a receive) is read the same way.
     *  - Outlet: the smoothed signal.
     *  - Parameter: ``slide``, the creation arguments: the slide-up and
     *    slide-down amounts (``~slide 10 1000``). They are saved with the
     *    patch; a later number or message is not, as for ``~phaseshift``.
     *
     *  Everything reaches the audio thread through atomics, applied to the
     *  DSP class at the start of each block.
     */
    PATCHER_CLASS(dSlide, YSE::OBJ::D_SLIDE)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetUp)
    _INT_IN(SetUpInt)
    _FLOAT_IN(SetDown)
    _INT_IN(SetDownInt)

  private:
    DSP::slide fn;

    std::vector<std::string> arguments;

    std::atomic<float> up{DSP::slide::DEFAULT_SLIDE};
    std::atomic<float> down{DSP::slide::DEFAULT_SLIDE};
    std::atomic<bool> resetRequested{false};
  };

} // namespace PATCHER
} // namespace YSE
