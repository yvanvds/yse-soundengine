#pragma once
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~pass``: passes its input on, and silence while the subpatcher
     *         it is in is muted (issue #1137).
     *
     *  Ported from Max's ``pass~``, placed before a ``~outlet``. In YSE every
     *  muted object's signal outlets already carry silence (the render calls
     *  pObject::SendSilence instead of computing it), so a muted subpatcher
     *  never repeats a stale block and this object needs no mute logic of its
     *  own: unmuted it forwards the buffer that arrived, as ``~inlet`` does;
     *  muted, the render silences it like any other object. It is kept so a
     *  patch that guards its outlets with it reads and behaves the same.
     *
     *  No arguments, messages or parameters, and no DSP class (a graph
     *  feature; the two-layer rule, #1035, is waived). ``Calculate()`` is one
     *  send of a pointer: no allocation, no lock.
     */
    PATCHER_CLASS(pPass, YSE::OBJ::D_PASS)
    _NO_MESSAGES
    _DO_CALCULATE
  };

} // namespace PATCHER
} // namespace YSE
