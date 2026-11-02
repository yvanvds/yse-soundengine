/*
  ==============================================================================

    fold.hpp
    Keeps a signal inside a range by folding, wrapping or clipping it, with
    limits that may change per sample (issue #1113).

  ==============================================================================
*/

#ifndef YSE_DSP_OPERATORS_FOLD_HPP
#define YSE_DSP_OPERATORS_FOLD_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Keeps a signal inside the range low..high: a sample outside it
     *         is folded back from the limits, wrapped around to the other
     *         side, or clipped.
     *
     *  Ported from MSP's ``pong~``; the signal counterpart of the control
     *  object ``.pong``, which computes its value with ``Limit()``. A sample
     *  inside the range passes unchanged (bit for bit). Outside it:
     *
     *  - ``MODE::FOLD`` (the default) reflects the sample back into the
     *    range, as often as it takes: the result is a triangle wave of the
     *    input with a period of twice the range, and both limits are fixed
     *    points. Driving a sine past the limits gives wavefolding.
     *  - ``MODE::WRAP`` carries the sample around to the other side: the
     *    range is half-open at the top, so ``high`` itself gives ``low``,
     *    and a rising ramp becomes a sawtooth, as a phase wraps.
     *  - ``MODE::CLIP`` pins the sample to the nearest limit, like
     *    ``DSP::clip``.
     *
     *  The limits are used as an ordered pair: a range given high to low
     *  behaves as the same range given low to high. A collapsed range (low
     *  equal to high) gives that value. A limit that is not a finite number
     *  is ignored. An input sample that is NaN counts as 0 and is then
     *  folded, wrapped or clipped; an infinite one gives the limit it ran
     *  past. The result is always within low..high (for a sample many ranges
     *  away, rounding can make a wrapped result land on ``high``). There is no
     *  state between samples and no time is counted, so the class has no
     *  sample-rate state: it follows the live rate by construction.
     *
     *  The three-buffer call reads both limits per sample; the getters keep
     *  the last values read.
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples. Call it
     *  from one thread at a time. Processing allocates nothing: the output
     *  is sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API fold {
    public:
      /** @brief What happens to a sample outside the range, numbered as the
       *         ``folding-mode`` argument of MSP's ``pong~``. */
      enum class MODE : int {
        FOLD = 0, ///< reflected back from the limits
        WRAP = 1, ///< carried around to the other side
        CLIP = 2, ///< pinned to the nearest limit
      };

      /** @brief The range and the mode until they are set: fold into 0..1. */
      static constexpr Flt DEFAULT_LOW = 0.f;
      static constexpr Flt DEFAULT_HIGH = 1.f;
      static constexpr MODE DEFAULT_MODE = MODE::FOLD;

      fold();

      /** @brief The folding mode. */
      fold& mode(MODE value);

      /** @brief The folding mode as last set. */
      MODE mode() const {
        return how;
      }

      /** @brief The lower limit. Not finite: ignored. */
      fold& low(Flt value);

      /** @brief The upper limit. Not finite: ignored. */
      fold& high(Flt value);

      /** @brief Both limits in one call; each that is not finite is
       *         ignored. */
      fold& range(Flt lowValue, Flt highValue);

      /** @brief The lower limit as last set or read. */
      Flt low() const {
        return lowLimit;
      }

      /** @brief The upper limit as last set or read. */
      Flt high() const {
        return highLimit;
      }

      /** @brief One value folded, wrapped or clipped into the range
       *         @p lowValue .. @p highValue (an ordered pair, both finite),
       *         by the rules above. Stateless: this is what ``process``
       *         and the block calls compute for every sample, and what the
       *         control object ``.pong`` sends. */
      static Flt Limit(Flt value, Flt lowValue, Flt highValue, MODE how);

      /** @brief One sample at the current range and mode. */
      Flt process(Flt in) const;

      /** @brief One block at the current range and mode. */
      buffer& operator()(buffer& in);

      /** @brief One block with the limits read per sample from
       *         @p lowSignal and @p highSignal; as long as the shortest. It
       *         also sets the getters. */
      buffer& operator()(buffer& in, buffer& lowSignal, buffer& highSignal);

    private:
      buffer output;

      Flt lowLimit = DEFAULT_LOW;
      Flt highLimit = DEFAULT_HIGH;
      MODE how = DEFAULT_MODE;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_OPERATORS_FOLD_HPP
