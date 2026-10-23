#pragma once

#include "../pObject.h"
#include "../../dsp/generators/pinkNoise.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~pink``: pink noise, wrapping ``DSP::pinkNoise`` (issue #1056).
     *
     *  Ported from Max's ``pink~``, which has no arguments, messages or
     *  attributes. Like ``~noise`` it has no inlets and one signal outlet; the
     *  wrapper only hands each block of ``DSP::pinkNoise`` to the outlet.
     */
    PATCHER_CLASS(dPink, YSE::OBJ::D_PINK)
    _NO_MESSAGES
    _DO_CALCULATE

  private:
    DSP::pinkNoise pink;
  };
} // namespace PATCHER
} // namespace YSE
