#pragma once
#include "gUnaryMath.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief The single-inlet trigonometric and hyperbolic functions
     *         (issue #441).
     *
     *  Twelve objects that are each one ``<cmath>`` call behind the shape
     *  gUnaryMathBase already provides for ``.abs`` / ``.sqrt``: one inlet, one
     *  outlet, no parameters, float in / float out. Adding a thirteenth is one
     *  UNARY_MATH_CLASS line here plus one constructor in gTrig.cpp.
     *
     *  Angles are in radians throughout, matching Max.
     *
     *  The two-argument arc-tangent ``.atan2`` is not here — it needs a second
     *  inlet, so it lives in gAtan2.h next to ``.pow``.
     */

    // Circular functions.
    UNARY_MATH_CLASS(gSin, YSE::OBJ::G_SIN)
    UNARY_MATH_CLASS(gCos, YSE::OBJ::G_COS)
    UNARY_MATH_CLASS(gTan, YSE::OBJ::G_TAN)
    UNARY_MATH_CLASS(gAsin, YSE::OBJ::G_ASIN)
    UNARY_MATH_CLASS(gAcos, YSE::OBJ::G_ACOS)
    UNARY_MATH_CLASS(gAtan, YSE::OBJ::G_ATAN)

    // Hyperbolic functions.
    UNARY_MATH_CLASS(gSinh, YSE::OBJ::G_SINH)
    UNARY_MATH_CLASS(gCosh, YSE::OBJ::G_COSH)
    UNARY_MATH_CLASS(gTanh, YSE::OBJ::G_TANH)
    UNARY_MATH_CLASS(gAsinh, YSE::OBJ::G_ASINH)
    UNARY_MATH_CLASS(gAcosh, YSE::OBJ::G_ACOSH)
    UNARY_MATH_CLASS(gAtanh, YSE::OBJ::G_ATANH)

  } // namespace PATCHER
} // namespace YSE
