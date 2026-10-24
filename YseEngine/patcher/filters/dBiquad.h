#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/filters.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~biquad``: two-pole, two-zero filter driven by five
     *         coefficients, wrapping the coefficient path of ``DSP::biQuad``
     *         (issue #1090).
     *
     *  Ported from Max's ``biquad~``:
     *  ``y(n) = a0 x(n) + a1 x(n-1) + a2 x(n-2) - b1 y(n-1) - b2 y(n-2)``.
     *  The wrapper only maps the pins, messages and parameter onto the DSP
     *  class.
     *
     *  - Inlet 0: the signal to filter (unconnected reads 0).
     *  - Inlets 1-5: the coefficients a0, a1, a2, b1 and b2, each a number
     *    or a signal read per sample. An unconnected one reads the last
     *    number sent to it, else the creation argument (#1036); a number
     *    change is smoothed while ``smooth`` is on.
     *  - Any inlet takes ``clear``, which sets the previous inputs and
     *    outputs to 0, ``smooth <0|1>``, ``stoke <y1> [<y2> [<x1> [<x2>]]]``,
     *    which preloads the previous outputs and inputs, and a list of up to
     *    five numbers: a0, a1, a2, b1, b2, the numbers the coefficient
     *    inlets read. A command that arrives as a list (text through a
     *    receive) is read the same way. Max's ``dictionary`` message is not
     *    taken: there is no filter-design dictionary to name.
     *  - Outlet: the filtered signal.
     *  - Parameter: ``a0``, the creation arguments: up to five coefficients
     *    (``~biquad 0.5 0 0 -0.5 0``) and ``smooth <0|1>``. They are saved
     *    with the patch; a later number or message is not, as for
     *    ``~reson``.
     *
     *  Everything reaches the audio thread through atomics, applied to the
     *  DSP class at the start of each block.
     */
    PATCHER_CLASS(dBiquad, YSE::OBJ::D_BIQUAD)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetCoefficient)
    _INT_IN(SetCoefficientInt)

  private:
    DSP::biQuad fn;

    std::vector<std::string> arguments;

    // a0 a1 a2 b1 b2, the numbers the unconnected coefficient inlets read.
    std::atomic<float> coefficient[5] = {{0.f}, {0.f}, {0.f}, {0.f}, {0.f}};
    std::atomic<bool> smoothing{true};
    std::atomic<bool> clearRequested{false};
    // stoke: y1 y2 x1 x2, published by the flag.
    std::atomic<float> stokeValue[4] = {{0.f}, {0.f}, {0.f}, {0.f}};
    std::atomic<bool> stokeRequested{false};
  };

} // namespace PATCHER
} // namespace YSE
