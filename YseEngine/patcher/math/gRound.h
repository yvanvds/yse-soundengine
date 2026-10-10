#pragma once
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Control-rate rounding ``.round`` (issue #440).
     *
     *  Two-inlet shape of the ``.+`` family: inlet 0 is the value and fires the
     *  evaluation, inlet 1 stores the step. Float in, float out.
     *
     *  Matching Max's ``round``, the value is snapped to the nearest *multiple*
     *  of the step rather than to the nearest integer — a step of 0.5 snaps to
     *  halves — which is why the step defaults to 1 instead of the 0 the rest
     *  of the two-inlet math objects default to. A step of 0 disables rounding
     *  and passes the value straight through, again as in Max.
     */
    PATCHER_CLASS(gRound, YSE::OBJ::G_ROUND)
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
