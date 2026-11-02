#pragma once
#include <algorithm>
#include "../../dsp/operators/scale.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief The mapping core shared by ``.scale`` (#443) and ``.zmap`` (#444),
     *         on ``DSP::scale::Map()`` (#1114).
     *
     *  Maps @p input from the input range onto the output range, optionally
     *  bending the mapping with an exponential curve and optionally clamping
     *  the result to the output range. The formula is Max's, including its
     *  sign-symmetric treatment of the exponent, so a value below the input
     *  range curves the same way one above it does:
     *
     *  \code
     *    n = (in - inLow) / (inHigh - inLow)
     *    out = outLow + (outHigh - outLow) * (n >= 0 ?  pow( n, exponent)
     *                                                : -pow(-n, exponent))
     *  \endcode
     *
     *  Two behaviours are deliberate choices rather than ports of Max, and both
     *  objects inherit them from here:
     *
     *  - A degenerate input range (low equal to high) returns the output low
     *    rather than the infinity the division would produce — a value that is
     *    at least inside the output range, which 0 need not be.
     *  - Anything still non-finite is replaced with 0 before it is returned,
     *    the convention ``./``, ``.sqrt`` and ``.pow`` already use, so a NaN
     *    arriving from a neighbour cannot poison everything downstream.
     *
     *  @param input     the value to map.
     *  @param inLow     input value that maps to @p outLow.
     *  @param inHigh    input value that maps to @p outHigh.
     *  @param outLow    output value for @p inLow.
     *  @param outHigh   output value for @p inHigh.
     *  @param exponent  curve exponent; 1 is a straight line.
     *  @param clip  when true the result is clamped to the output range. The
     *               clamp uses the *ordered* output pair, so a descending
     *               mapping (outLow > outHigh) clips correctly.
     *
     *  A divide, at most one ``std::pow``, a handful of branches: no
     *  allocation, no lock, no I/O, so it is safe on the audio thread.
     */
    inline float MapRange(float input, float inLow, float inHigh, float outLow, float outHigh,
                          float exponent, bool clip) {
      // The mapping itself, with the collapsed-range guard and the non-finite
      // substitution, is DSP::scale's (#1114), which ~scale computes per
      // sample.
      float result = DSP::scale::Map(input, inLow, inHigh, outLow, outHigh, exponent,
                                     DSP::scale::MODE::MODERN);

      if (clip) {
        const float lo = std::min(outLow, outHigh);
        const float hi = std::max(outLow, outHigh);
        result = std::min(std::max(result, lo), hi);
      }

      return result;
    }

  } // namespace PATCHER
} // namespace YSE
