/*
  ==============================================================================

    slide.hpp
    Logarithmic slide: a one-pole smoother with separate slide-up and
    slide-down amounts in samples, both settable at signal rate (issue #1099).

  ==============================================================================
*/

#ifndef YSE_DSP_FILTERS_SLIDE_HPP
#define YSE_DSP_FILTERS_SLIDE_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Logarithmic slide: smooths a signal with separate amounts for
     *         rising and falling, for envelope followers and portamento.
     *
     *  Ported from Max's ``slide~``, with its difference equation exactly:
     *
     *  ``y(n) = y(n-1) + (x(n) - y(n-1)) / slide``
     *
     *  where ``slide`` is the slide-up amount when ``x(n)`` is above
     *  ``y(n-1)`` and the slide-down amount otherwise. Each sample moves the
     *  output that fraction of the way towards the input: a slide of 1
     *  passes the input through, 10 covers a tenth of the distance per
     *  sample. A fast rise and a slow fall make an envelope follower out of
     *  a rectified signal; equal amounts make a portamento.
     *
     *  The amounts count *samples*, as Max's do: they are not times, so the
     *  filter does not depend on the sample rate. An amount below 1, or one
     *  that is not a number, is used as 1: below 1 the step overshoots the
     *  input and the filter rings, and 0 divides by zero (the patcher's
     *  ``.slide`` does the same, #459). A setting that is not finite is
     *  ignored; the getters return the amounts as set, not as used. Max's
     *  defaults are 1 and 1.
     *
     *  The running value is kept in double precision. Once within one part
     *  in a million of the input (never finer than 1e-6 absolute) the output
     *  equals the input, as ``.slide``'s does: a constant input is reached
     *  exactly rather than stalled short of in floating point, and a decay
     *  towards 0 ends at 0 instead of in denormals.
     *
     *  The three-buffer call reads the two amounts per sample; the getters
     *  keep the last finite values read. An input sample that is not a
     *  finite number is skipped (the output holds), so one stray NaN cannot
     *  latch the filter. ``reset()`` sets the output to 0, from where it
     *  slides towards the input again.
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples. Call it
     *  from one thread at a time. Processing allocates nothing: the output is
     *  sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API slide {
    public:
      /** @brief Both amounts until they are set: the input passes through. */
      static constexpr Flt DEFAULT_SLIDE = 1.f;

      slide();

      /** @brief The slide-up amount in samples, used while the input is
       *         above the output. Non-finite: ignored. */
      slide& up(Flt amount);

      /** @brief The slide-down amount in samples, used while the input is at
       *         or below the output. Non-finite: ignored. */
      slide& down(Flt amount);

      /** @brief Sets the output to 0. */
      slide& reset();

      /** @brief The slide-up amount as last set or read. */
      Flt up() const {
        return slideUp;
      }

      /** @brief The slide-down amount as last set or read. */
      Flt down() const {
        return slideDown;
      }

      /** @brief The current output. */
      Flt value() const {
        return static_cast<Flt>(current);
      }

      /** @brief The amount a setting is used as: @p amount, or 1 when it is
       *         below 1 or not a number. */
      static Flt Effective(Flt amount);

      /** @brief One sample. */
      Flt process(Flt in);

      /** @brief One block at the current amounts. */
      buffer& operator()(buffer& in);

      /** @brief One block with the two amounts read per sample from
       *         @p upSignal and @p downSignal; as long as the shortest. They
       *         also set the getters. */
      buffer& operator()(buffer& in, buffer& upSignal, buffer& downSignal);

    private:
      Flt Step(Flt in);

      buffer output;

      Flt slideUp = DEFAULT_SLIDE;
      Flt slideDown = DEFAULT_SLIDE;
      Dbl current = 0.0;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_FILTERS_SLIDE_HPP
