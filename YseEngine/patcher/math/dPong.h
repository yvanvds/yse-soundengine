#pragma once

#include "../pObject.h"
#include "../../dsp/operators/fold.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~pong``: keeps a signal inside a range by folding, wrapping
     *         or clipping it, wrapping ``DSP::fold`` (issue #1113).
     *
     *  Ported from MSP's ``pong~``; the signal counterpart of ``.pong``. The
     *  wrapper only maps the pins, messages and parameters onto the DSP
     *  class.
     *
     *  - Inlet 0: the signal; a number (float or int) is used while no
     *    signal is connected (unconnected and no number reads 0). Takes the
     *    messages ``mode fold|wrap|clip`` (or ``mode 0|1|2``) and
     *    ``range <low> <high>``.
     *  - Inlet 1: the lower limit, a number or a signal read per sample;
     *    unconnected reads the last number, else the argument (#1036).
     *  - Inlet 2: the upper limit, likewise.
     *  - Outlet: the signal folded, wrapped or clipped into the range.
     *  - Parameters, in MSP's argument order: ``mode`` (0 fold, the default;
     *    1 wrap; 2 clip), ``low`` (default 0) and ``high`` (default 1). They
     *    are saved with the patch; a later number or message is not, as for
     *    ``~slide``.
     *
     *  Numbers and messages reach the audio thread through atomics, read at
     *  the start of each block.
     */
    PATCHER_CLASS(dPong, YSE::OBJ::D_PONG)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetInput)
    _INT_IN(SetInputInt)
    _FLOAT_IN(SetLow)
    _INT_IN(SetLowInt)
    _FLOAT_IN(SetHigh)
    _INT_IN(SetHighInt)

  private:
    DSP::fold fn;

    aFlt input;
    aInt mode;
    aFlt low;
    aFlt high;
  };

} // namespace PATCHER
} // namespace YSE
