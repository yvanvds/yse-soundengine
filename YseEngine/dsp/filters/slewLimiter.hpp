/*
  ==============================================================================

    slewLimiter.hpp
    Limits the change from one output sample to the next, with separate
    limits for falling and rising, both settable at signal rate (issue #1107).

  ==============================================================================
*/

#ifndef YSE_DSP_FILTERS_SLEWLIMITER_HPP
#define YSE_DSP_FILTERS_SLEWLIMITER_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A slew limiter: the output follows the input, but changes by no
     *         less than a minimum and no more than a maximum per sample.
     *
     *  Ported from MSP's ``deltaclip~``. Each sample, the change the input
     *  asks for, ``d = x(n) - y(n-1)``, is clipped to
     *  ``[minimum, maximum]`` and added to the previous output:
     *
     *  ``y(n) = y(n-1) + clip(x(n) - y(n-1), minimum, maximum)``
     *
     *  The minimum is the steepest fall and is normally negative, the
     *  maximum the steepest rise and normally positive. A change within the
     *  limits is taken whole, so the output is then the input exactly, not
     *  the previous output plus a rounded difference. A step becomes a
     *  straight ramp; a square wave a trapezoid; a click a short slope. Wide
     *  limits (or infinities) pass the signal through bit for bit.
     *
     *  The limits are amounts per *sample*, as MSP's are: the class does not
     *  depend on the sample rate. Both default to 0, as in MSP, which holds
     *  the output where it is. A minimum above the maximum always moves the
     *  output by the maximum, as ``clip`` takes its high limit then; a
     *  positive minimum forces the output up every sample, a negative
     *  maximum forces it down. A limit that is NaN is ignored; infinities
     *  are limits like any other.
     *
     *  The three-buffer call reads the two limits per sample; the getters
     *  keep the last values read. An input sample that is not a finite
     *  number is skipped (the output holds), so one stray NaN cannot latch
     *  the output. ``reset()`` sets the output to 0, ``set()`` to any finite
     *  value; both keep the limits.
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples. Call it
     *  from one thread at a time. Processing allocates nothing: the output is
     *  sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API slewLimiter {
    public:
      /** @brief Both limits until they are set: the output holds. */
      static constexpr Flt DEFAULT_LIMIT = 0.f;

      slewLimiter();

      /** @brief The smallest change per sample, the steepest fall when
       *         negative. NaN: ignored. */
      slewLimiter& minimum(Flt slope);

      /** @brief The largest change per sample, the steepest rise when
       *         positive. NaN: ignored. */
      slewLimiter& maximum(Flt slope);

      /** @brief The minimum as last set or read. */
      Flt minimum() const {
        return lowest;
      }

      /** @brief The maximum as last set or read. */
      Flt maximum() const {
        return highest;
      }

      /** @brief Sets the output the next change starts from. Not finite:
       *         ignored. */
      slewLimiter& set(Flt value);

      /** @brief Sets the output to 0, as after construction. */
      slewLimiter& reset();

      /** @brief The current output. */
      Flt value() const {
        return current;
      }

      /** @brief One sample. */
      Flt process(Flt in);

      /** @brief One block at the current limits. */
      buffer& operator()(buffer& in);

      /** @brief One block with the two limits read per sample from
       *         @p minimumSignal and @p maximumSignal; as long as the
       *         shortest. They also set the getters. */
      buffer& operator()(buffer& in, buffer& minimumSignal, buffer& maximumSignal);

    private:
      Flt Step(Flt in);

      buffer output;

      Flt lowest = DEFAULT_LIMIT;
      Flt highest = DEFAULT_LIMIT;
      Flt current = 0.f;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_FILTERS_SLEWLIMITER_HPP
