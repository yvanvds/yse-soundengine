#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/modules/hilbert.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~hilbert``: phase quadrature filter, wrapping
     *         ``DSP::hilbert`` (issue #1097).
     *
     *  Ported from Max's ``hilbert~``, which takes no arguments. The wrapper
     *  only maps the pins and the message onto the DSP class.
     *
     *  - Inlet 0: the signal to transform (unconnected reads 0). It takes
     *    ``clear``, also as text through a receive, which sets the filter
     *    state to 0 before the next block.
     *  - Outlet 0: the real part; outlet 1: the imaginary part, 90 degrees
     *    behind the real part.
     *
     *  The clear request reaches the audio thread through an atomic flag.
     */
    PATCHER_CLASS(dHilbert, YSE::OBJ::D_HILBERT)
    _DO_MESSAGES
    _DO_CALCULATE

  private:
    DSP::hilbert fn;

    std::atomic<bool> clearRequested{false};
  };

} // namespace PATCHER
} // namespace YSE
