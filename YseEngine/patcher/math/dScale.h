#pragma once

#include "../pObject.h"
#include "../../dsp/operators/scale.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~scale``: maps a signal from an input range onto an output
     *         range, wrapping ``DSP::scale`` (issue #1114).
     *
     *  Ported from MSP's ``scale~``; the signal counterpart of ``.scale``.
     *  The wrapper only maps the pins, messages and parameters onto the DSP
     *  class.
     *
     *  - Inlet 0: the signal; a number (float or int) is used while no
     *    signal is connected (unconnected and no number reads 0). Takes the
     *    message ``classic 0|1|2`` (MSP's attribute: 0 modern, 1 legacy
     *    linedrive, 2 modern with the curve inverted for a downward output
     *    range).
     *  - Inlets 1 to 5: inLow, inHigh, outLow, outHigh and the exponent,
     *    each a number or a signal read per sample; unconnected reads the
     *    last number, else the argument (#1036).
     *  - Outlet: the mapped signal.
     *  - Parameters, in MSP's argument order: ``inLow`` (default 0),
     *    ``inHigh`` (1), ``outLow`` (0), ``outHigh`` (1), ``exponent`` (1),
     *    then ``classic`` (0). They are saved with the patch; a later number
     *    or message is not, as for ``~slide``.
     *
     *  Numbers and messages reach the audio thread through atomics, read at
     *  the start of each block.
     */
    PATCHER_CLASS(dScale, YSE::OBJ::D_SCALE)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)

  private:
    // Shared by both number handlers: inlet 0 stores the input, 1-5 the
    // settings.
    void Store(float value, int inlet);

    DSP::scale fn;

    aFlt input;
    aFlt inLow;
    aFlt inHigh;
    aFlt outLow;
    aFlt outHigh;
    aFlt exponent;
    aInt classic;
  };

} // namespace PATCHER
} // namespace YSE
