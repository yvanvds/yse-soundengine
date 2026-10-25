/*
  ==============================================================================

    rampSmooth.hpp
    Linear smoothing: each change of the input starts a straight ramp to it
    over a number of samples, with separate ramp-up and ramp-down lengths,
    both settable at signal rate (issue #1100).

  ==============================================================================
*/

#ifndef YSE_DSP_FILTERS_RAMPSMOOTH_HPP
#define YSE_DSP_FILTERS_RAMPSMOOTH_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Linear smoothing: ramps in a straight line to each new input
     *         value over a set number of samples, with separate lengths for
     *         rising and falling. For declicking and parameter smoothing.
     *
     *  Ported from Max's ``rampsmooth~``. Each time the input differs from
     *  the value the output is heading for, a new ramp starts from the
     *  current output: it takes the ramp-up length when the new value is
     *  above the current output and the ramp-down length otherwise, and
     *  reaches the value exactly on its last sample. With a length of 10, a
     *  step from 0 to 1 gives 0.1, 0.2, ... 1.0 over the next ten samples,
     *  then holds. A ramp that is interrupted by another change restarts
     *  from wherever the output is, so an input that changes every sample
     *  (an audio signal) moves the output 1/length of the way towards it per
     *  sample, like a one-pole lowpass.
     *
     *  The lengths count *samples*, as Max's do: they are not times, so the
     *  filter does not depend on the sample rate. A length is used as its
     *  whole number of samples (Max's are ints): 10.7 ramps over 10. A length
     *  below 1, or one that is not a number, is used as 1, which jumps to the
     *  input at once; Max's 0 does the same. A setting that is not finite is
     *  ignored; the getters return the lengths as set, not as used. Max's
     *  defaults are 1 and 1: the input passes through.
     *
     *  A length applies to the ramps that start after it is set; a ramp
     *  already running keeps its length. The three-buffer call reads the two
     *  lengths per sample, so a ramp takes the lengths of the sample it
     *  starts on; the getters keep the last finite values read.
     *
     *  The output and the step are kept in double precision, and the last
     *  sample of a ramp is the target itself, so the output lands on the
     *  input exactly. An input sample that is not a finite number is skipped
     *  (the ramp carries on), so one stray NaN cannot latch the output.
     *  ``reset()`` sets the output, and the value it heads for, to 0.
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples. Call it
     *  from one thread at a time. Processing allocates nothing: the output is
     *  sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API rampSmooth {
    public:
      /** @brief Both lengths until they are set: the input passes through. */
      static constexpr Flt DEFAULT_RAMP = 1.f;

      rampSmooth();

      /** @brief The ramp-up length in samples, used when the input moves above
       *         the output. Non-finite: ignored. */
      rampSmooth& up(Flt samples);

      /** @brief The ramp-down length in samples, used when the input moves to
       *         or below the output. Non-finite: ignored. */
      rampSmooth& down(Flt samples);

      /** @brief Both lengths at once, as Max's ``ramp`` message. */
      rampSmooth& ramp(Flt samples);

      /** @brief Sets the output, and the value it heads for, to 0. */
      rampSmooth& reset();

      /** @brief The ramp-up length as last set or read. */
      Flt up() const {
        return rampUp;
      }

      /** @brief The ramp-down length as last set or read. */
      Flt down() const {
        return rampDown;
      }

      /** @brief The current output. */
      Flt value() const {
        return static_cast<Flt>(current);
      }

      /** @brief The number of samples a setting ramps over: its whole part,
       *         or 1 when it is below 1 or not a number. */
      static UInt Samples(Flt samples);

      /** @brief One sample. */
      Flt process(Flt in);

      /** @brief One block at the current lengths. */
      buffer& operator()(buffer& in);

      /** @brief One block with the two lengths read per sample from
       *         @p upSignal and @p downSignal; as long as the shortest. They
       *         also set the getters. */
      buffer& operator()(buffer& in, buffer& upSignal, buffer& downSignal);

    private:
      Flt Step(Flt in);

      buffer output;

      Flt rampUp = DEFAULT_RAMP;
      Flt rampDown = DEFAULT_RAMP;
      Dbl current = 0.0;
      Dbl increment = 0.0;
      Flt target = 0.f;
      UInt remaining = 0;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_FILTERS_RAMPSMOOTH_HPP
