#pragma once
#include "../pObject.h"
#include "../../dsp/buffer.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~begin``: a constant signal of 0 that marks the start of a
     *         chain (issue #1137).
     *
     *  Ported from Max's ``begin~``, which once let a ``gate~`` or
     *  ``selector~`` switch off the chain between it and them, and has done
     *  nothing but output zeros since that was retired in favour of muting.
     *  This object does the same: connected to a signal inlet it adds 0, so a
     *  patch built with it sounds as it should. To switch a chain off, put it
     *  in a subpatcher and use ``~mute``.
     *
     *  No inlets, arguments or parameters, and no DSP class (a graph feature;
     *  the two-layer rule, #1035, is waived). A start point: ``Calculate()``
     *  sends a buffer it owns, zeroed every block in case something
     *  downstream wrote to it. No allocation, no lock.
     */
    PATCHER_CLASS(pBegin, YSE::OBJ::D_BEGIN)
    _NO_MESSAGES
    _DO_CALCULATE

  private:
    DSP::buffer zeros;
  };

} // namespace PATCHER
} // namespace YSE
