#pragma once
#include <algorithm>
#include <cmath>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief The mapping core shared by ``.scale`` (#443) and ``.zmap`` (#444).
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
     *  @param clip  when true the result is clamped to the output range. The
     *               clamp uses the *ordered* output pair, so a descending
     *               mapping (outLow > outHigh) clips correctly.
     *
     *  A divide, at most one ``std::pow``, a handful of branches: no
     *  allocation, no lock, no I/O, so it is safe on the audio thread.
     */
    inline float MapRange(float input, float inLow, float inHigh, float outLow, float outHigh,
                          float exponent, bool clip) {
      const float span = inHigh - inLow;
      float result;

      if (span == 0.f) {
        // A collapsed input range has no mapping to give: every input would
        // divide by zero and come out as an infinity or a NaN.
        result = outLow;
      } else {
        const float normalized = (input - inLow) / span;
        float curved;
        if (exponent == 1.f) {
          // The overwhelmingly common case, and std::pow(x, 1) is not
          // guaranteed to be exact. Skip it.
          curved = normalized;
        } else if (normalized >= 0.f) {
          curved = std::pow(normalized, exponent);
        } else {
          // Max mirrors the curve below the input range rather than handing a
          // negative base to pow (which has no real result for a fractional
          // exponent).
          curved = -std::pow(-normalized, exponent);
        }
        result = outLow + (outHigh - outLow) * curved;
      }

      // Reachable with a zero base and a negative exponent, with an operand
      // that overflows, or simply when a neighbouring object hands us an
      // infinity.
      if (!std::isfinite(result)) result = 0.f;

      if (clip) {
        const float lo = std::min(outLow, outHigh);
        const float hi = std::max(outLow, outHigh);
        result = std::min(std::max(result, lo), hi);
      }

      return result;
    }

  } // namespace PATCHER
} // namespace YSE
