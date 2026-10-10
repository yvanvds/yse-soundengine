#pragma once
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Control-rate exponentiation ``.pow`` (issue #440).
     *
     *  Two-inlet shape of the ``.+`` family: inlet 0 is the base and fires the
     *  evaluation, inlet 1 stores the exponent. Float in, float out.
     *
     *  Unlike ``.abs`` / ``.sqrt`` this is not part of the gUnaryMathBase
     *  family, and unlike ``.round`` its stored operand means something else
     *  entirely, so it stays a plain object next to ``.+`` and ``./``.
     */
    PATCHER_CLASS(gPow, YSE::OBJ::G_POW)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetLeftFloat)
    _FLOAT_IN(SetRightFloat)

    _INT_IN(SetLeftInt)
    _INT_IN(SetRightInt)

  private:
    float leftIn;
    float rightIn;
  };
}
}
