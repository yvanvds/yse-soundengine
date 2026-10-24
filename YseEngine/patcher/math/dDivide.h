#pragma once
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~/``: left / right per sample, on the ``DSP::buffer``
     *         operators. Ported from Max's ``div~`` (issue #1044).
     *
     *  Both inlets take a signal or a number, int or float. A number is used
     *  while no signal is connected to that inlet; a connected signal wins
     *  (#1036). The right number is the ``right`` parameter (creation
     *  argument 1, default 1), so it survives a save.
     *  A divisor of 0, number or signal sample, gives 0 rather than an
     *  infinity, as ``DSP::buffer::operator/=`` does.
     *
     *  The wrapper holds the numbers as atomics, written by the control
     *  thread and read once per block by the audio thread, and contains no
     *  sample math.
     */
    PATCHER_CLASS(dDivide, YSE::OBJ::D_DIVIDE)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetLeftFloat)
    _INT_IN(SetLeftInt)
    _FLOAT_IN(SetRightFloat)
    _INT_IN(SetRightInt)

  private:
    aFlt leftFloatIn;
    aFlt rightFloatIn;

    DSP::buffer output;
  };
}
}