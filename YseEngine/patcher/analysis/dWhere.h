#pragma once

#include "../pObject.h"
#include "../../dsp/generators/phasorWhere.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~where``: the time elapsed in, and remaining of, the current
     *         cycle of a 0..1 ramp, wrapping ``DSP::phasorWhere`` (issue
     *         #1078).
     *
     *  Ported from Max's ``where~``, which takes no arguments, messages or
     *  attributes. The wrapper only maps pins onto the DSP class.
     *
     *  - Inlet: the ramp to measure, usually a ``~phasor`` (unconnected reads
     *    0, so both outlets give 0).
     *  - Left outlet: elapsed time in ms. Right outlet: remaining time in ms.
     */
    PATCHER_CLASS(dWhere, YSE::OBJ::D_WHERE)
    _NO_MESSAGES
    _DO_CALCULATE

  private:
    DSP::phasorWhere fn;
  };

} // namespace PATCHER
} // namespace YSE
