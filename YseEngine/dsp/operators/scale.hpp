/*
  ==============================================================================

    scale.hpp
    Maps a signal from an input range onto an output range, linearly or
    along an exponential curve (issue #1114).

  ==============================================================================
*/

#ifndef YSE_DSP_OPERATORS_SCALE_HPP
#define YSE_DSP_OPERATORS_SCALE_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Maps a signal from the input range inLow..inHigh onto the
     *         output range outLow..outHigh, linearly or along a curve.
     *
     *  Ported from MSP's ``scale~``; the signal counterpart of the control
     *  objects ``.scale`` and ``.zmap``, which compute their value with
     *  ``Map()``. The mode follows the values of MSP's ``classic``
     *  attribute:
     *
     *  - ``MODE::MODERN`` (0, the default) normalizes the input to
     *    ``n = (x - inLow) / (inHigh - inLow)`` and raises it to the
     *    exponent: ``y = outLow + (outHigh - outLow) * n^exponent``. Below
     *    the input range, where ``n`` is negative, the curve is mirrored:
     *    ``-(-n)^exponent``. An exponent of 1 is a straight line, and a
     *    sample then maps exactly as ``outLow + (outHigh - outLow) * n``.
     *    The exponent should be above 0.
     *  - ``MODE::CLASSIC`` (1) is MSP's legacy IRCAM "linedrive" formula,
     *    computed as MSP prints it, with ``r = outHigh - outLow``:
     *    ``y = outLow + r * (r * exp(-(inHigh - inLow) * log(exponent))
     *    * exp(x * log(exponent)))``, negated as a whole when ``r`` is
     *    negative. The exponent is a base here (MSP's typical value is
     *    1.06) and should be above 0. Kept for compatibility, not
     *    recommended for new work.
     *  - ``MODE::MODERN_INVERSE`` (2) is ``MODE::MODERN``, except that when
     *    the output range runs downwards (outLow above outHigh) the
     *    exponent is inverted (``1 / exponent``), so the curve keeps the
     *    shape it has for the upward range.
     *
     *  Neither range limits anything: an input outside the input range maps
     *  past the output range. Either range may run from high to low. A
     *  collapsed input range (inLow equal to inHigh) gives outLow in the
     *  modern modes, where the division has no answer. A result that is
     *  not a finite number (a NaN input, an overflow, an exponent at or
     *  below 0) is replaced with 0, as ``.scale`` does. A setting that is
     *  not a finite number is ignored. There is no state between samples
     *  and no time is counted, so the class has no sample-rate state: it
     *  follows the live rate by construction.
     *
     *  The six-buffer call reads every setting per sample; the getters keep
     *  the last values read.
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples. Call it
     *  from one thread at a time. Processing allocates nothing: the output
     *  is sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API scale {
    public:
      /** @brief The mapping formula, numbered as the values of MSP's
       *         ``classic`` attribute. */
      enum class MODE : int {
        MODERN = 0, ///< power curve on the normalized input
        CLASSIC = 1, ///< legacy "linedrive" formula
        MODERN_INVERSE = 2, ///< power curve, inverted for a downward output range
      };

      /** @brief The settings until they are set: 0..1 onto 0..1, a straight
       *         line, so the signal passes unchanged. */
      static constexpr Flt DEFAULT_IN_LOW = 0.f;
      static constexpr Flt DEFAULT_IN_HIGH = 1.f;
      static constexpr Flt DEFAULT_OUT_LOW = 0.f;
      static constexpr Flt DEFAULT_OUT_HIGH = 1.f;
      static constexpr Flt DEFAULT_EXPONENT = 1.f;
      static constexpr MODE DEFAULT_MODE = MODE::MODERN;

      scale();

      /** @brief The mapping formula. */
      scale& mode(MODE value);

      /** @brief The mapping formula as last set. */
      MODE mode() const {
        return how;
      }

      /** @brief The input value that maps to outLow. Not finite: ignored. */
      scale& inLow(Flt value);

      /** @brief The input value that maps to outHigh. Not finite: ignored. */
      scale& inHigh(Flt value);

      /** @brief The output for inLow. Not finite: ignored. */
      scale& outLow(Flt value);

      /** @brief The output for inHigh. Not finite: ignored. */
      scale& outHigh(Flt value);

      /** @brief The curve: a power in the modern modes, a base in the
       *         classic one. Not finite: ignored. */
      scale& exponent(Flt value);

      /** @brief Both input limits in one call. */
      scale& inRange(Flt lowValue, Flt highValue);

      /** @brief Both output limits in one call. */
      scale& outRange(Flt lowValue, Flt highValue);

      /** @brief The settings as last set or read. */
      Flt inLow() const {
        return inLowValue;
      }
      Flt inHigh() const {
        return inHighValue;
      }
      Flt outLow() const {
        return outLowValue;
      }
      Flt outHigh() const {
        return outHighValue;
      }
      Flt exponent() const {
        return exponentValue;
      }

      /** @brief One value mapped by the rules above. Stateless: this is
       *         what ``process`` and the block calls compute for every
       *         sample, and what the control objects ``.scale`` and
       *         ``.zmap`` send (in ``MODE::MODERN``). */
      static Flt Map(Flt value, Flt inLowValue, Flt inHighValue, Flt outLowValue, Flt outHighValue,
                     Flt exponentValue, MODE how);

      /** @brief One sample at the current settings. */
      Flt process(Flt in) const;

      /** @brief One block at the current settings. */
      buffer& operator()(buffer& in);

      /** @brief One block with every setting read per sample from its
       *         signal; as long as the shortest. It also sets the getters. */
      buffer& operator()(buffer& in, buffer& inLowSignal, buffer& inHighSignal,
                         buffer& outLowSignal, buffer& outHighSignal, buffer& exponentSignal);

    private:
      buffer output;

      Flt inLowValue = DEFAULT_IN_LOW;
      Flt inHighValue = DEFAULT_IN_HIGH;
      Flt outLowValue = DEFAULT_OUT_LOW;
      Flt outHighValue = DEFAULT_OUT_HIGH;
      Flt exponentValue = DEFAULT_EXPONENT;
      MODE how = DEFAULT_MODE;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_OPERATORS_SCALE_HPP
