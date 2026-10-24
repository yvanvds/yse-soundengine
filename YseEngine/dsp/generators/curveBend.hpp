/*
  ==============================================================================

    curveBend.hpp
    The curvature shared by ~shape (shapeEnvelope, #1071) and ~ramp
    (triggeredRamp, #1072). Internal: not part of yse.hpp.

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_CURVEBEND_HPP
#define YSE_DSP_GENERATORS_CURVEBEND_HPP

#include <cmath>
#include "../../headers/types.hpp"

namespace YSE {
  namespace DSP {
    namespace curveBend {

      /** The exponent for curvature `c` (-1..1), as `.function` bends its
       *  segments: (1 + c) / (1 - c). Infinite at +1 (hold until the end),
       *  0 at -1 (jump at the start), 1 (linear) at 0. */
      inline Flt Exponent(Flt c) {
        if (c >= 1.f) return INFINITY;
        if (c <= -1.f) return 0.f;
        return (1.f + c) / (1.f - c);
      }

      /** `f` (0..1) raised to `exponent`, with the two limits as steps. */
      inline Flt Bend(Flt f, Flt exponent) {
        if (exponent == 1.f) return f;
        if (std::isinf(exponent)) return f >= 1.f ? 1.f : 0.f;
        if (exponent == 0.f) return f > 0.f ? 1.f : 0.f;
        return std::pow(f, exponent);
      }

    } // namespace curveBend
  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_CURVEBEND_HPP
