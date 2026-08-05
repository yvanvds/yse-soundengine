#pragma once
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Control-rate two-argument arc-tangent ``.atan2`` (issue #441).
     *
     *  Two-inlet shape of the ``.+`` family: inlet 0 is y and fires the
     *  evaluation, inlet 1 stores x. Float in, float out, result in radians.
     *
     *  Unlike ``.atan`` it knows the quadrant, because it sees the signs of both
     *  operands separately — which is what makes it the cartesian-to-polar
     *  angle. That second inlet is why it is not part of the gUnaryMathBase
     *  family in gTrig.h and sits next to ``.pow`` instead.
     */
    PATCHER_CLASS(gAtan2, YSE::OBJ::G_ATAN2)
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
