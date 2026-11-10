#pragma once

#include <string>
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~typeroute``: one inlet that takes a signal or messages, and
     *         sends each out its own outlet (issue #1135).
     *
     *  Ported from Max's ``typeroute~``. A pure graph object: it has no DSP
     *  class behind it, because there is no signal processing to put in one
     *  (the two-layer rule is waived for it, as #1135 asks).
     *
     *  - Inlet 0: a signal, and any message: bang, int, float, a list, or a
     *    message that starts with a word. Signal cords sum (#1036).
     *  - Outlet 0: the signal, unchanged, as the buffer that arrived (no
     *    copy). Unconnected, silence: a number sent to the inlet is a
     *    message, so it leaves by outlet 1 and does not become the signal.
     *  - Outlet 1: every message, sent on at once with the type it arrived
     *    with. A message that starts with a word leaves as text, the way
     *    ``.gate`` passes one, so the next object reads it as that message.
     *
     *  The inlet is cold: a message never recomputes the signal side, which
     *  it has nothing to do with. No parameters, as the reference has no
     *  arguments.
     */
    PATCHER_CLASS(dTypeRoute, YSE::OBJ::D_TYPEROUTE)
    _NO_MESSAGES
    _DO_CALCULATE

    _BANG_IN(PassBang)
    _INT_IN(PassInt)
    _FLOAT_IN(PassFloat)
    _LIST_IN(PassList)
  };

} // namespace PATCHER
} // namespace YSE
