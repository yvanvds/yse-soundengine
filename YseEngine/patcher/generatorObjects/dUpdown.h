#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/generators/updown.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~updown``: a trapezoid envelope with fixed attack and release
     *         times, driven by a ramp or a gate, wrapping ``DSP::updown``
     *         (issue #1070).
     *
     *  Ported from Max's ``updown~``. The wrapper only maps pins, messages
     *  and parameters onto the DSP class.
     *
     *  - Inlet: the ramp (a ``~phasor``) or a gate, as a signal or a number
     *    (``float`` / ``int``; the number is used while no signal is
     *    connected, see #1036). ``bang`` restarts the rise from 0. Also
     *    takes ``up <ms>``, ``down <ms>``, ``level <x>`` and ``reset <x>``,
     *    Max's attributes.
     *  - Outlet: the envelope, 0 to ``level``.
     *  - Parameters: ``up`` (argument 1, default 10 ms), ``down``
     *    (argument 2, default 10 ms), ``level`` (argument 3, default 1) and
     *    ``reset`` (argument 4, default 0.5). Max's ``updown~`` takes no
     *    arguments; these are YSE's so a patch saves its settings.
     *
     *  Control values reach the audio thread through atomics; a ``bang`` is
     *  posted and applied at the top of the next block. Messages are not
     *  saved; the arguments are.
     */
    PATCHER_CLASS(dUpdown, YSE::OBJ::D_UPDOWN)
    _DO_MESSAGES
    _DO_CALCULATE

    _BANG_IN(Restart)
    _FLOAT_IN(SetInput)
    _INT_IN(SetInputInt)

  private:
    DSP::updown fn;

    aFlt input;
    aFlt up;
    aFlt down;
    aFlt level;
    aFlt reset;

    // A bang waiting for the audio thread, exchanged in CALC().
    std::atomic<bool> restart{false};
  };

} // namespace PATCHER
} // namespace YSE
