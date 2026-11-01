/*
  ==============================================================================

    downsample.hpp
    Sample and hold every N samples: aliasing, lo-fi downsampling of a
    signal (issue #1109).

  ==============================================================================
*/

#ifndef YSE_DSP_OPERATORS_DOWNSAMPLE_HPP
#define YSE_DSP_OPERATORS_DOWNSAMPLE_HPP

#include <limits>

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Downsamples a signal: takes an input sample every *period*
     *         samples and holds it in between.
     *
     *  Ported from MSP's ``downsamp~``. The *period*, 1 or more, is the
     *  number of samples each take is held: 1 takes every sample, 4 every
     *  fourth one. The first sample after construction or ``reset()`` is a
     *  take. A count of the samples since the last take rises by 1 every
     *  sample; a sample is taken when it reaches the period, and the period
     *  is subtracted from it. At a constant period ``N`` the k-th take (from
     *  0) is therefore at sample ``ceil(k * N)``: a fractional period such
     *  as 2.5 takes at uneven distances (0, 3, 5, 8, 10 ...) whose average
     *  is exact, and the count is kept in double precision, so it does not
     *  drift. An integer period is counted exactly.
     *
     *  A shorter period takes effect at once: when the count is already
     *  past it, the next sample is taken and the count starts again from 0,
     *  so lowering the period never makes a burst of takes. An infinite
     *  period holds the last take for good. The period counts samples, not
     *  seconds, so the class has no sample-rate state: it follows the live
     *  rate by construction.
     *
     *  A period below 1 is raised to 1; NaN is ignored. An input sample
     *  that is not a finite number is not taken: the held sample stays, so
     *  a stray NaN cannot latch the output. There is no quantization, so a
     *  take passes bit for bit.
     *
     *  The two-buffer call reads the period per sample; the getter keeps
     *  the last value read. ``reset()`` sets the held sample to 0 and makes
     *  the next sample a take; it keeps the period.
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples. Call it
     *  from one thread at a time. Processing allocates nothing: the output
     *  is sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API downsample {
    public:
      /** @brief The period until it is set: every sample is taken. */
      static constexpr Flt DEFAULT_PERIOD = 1.f;
      /** @brief The shortest period. */
      static constexpr Flt MIN_PERIOD = 1.f;

      downsample();

      /** @brief The number of samples each take is held, at least 1. NaN:
       *         ignored. */
      downsample& period(Flt value);

      /** @brief The period as last set or read. */
      Flt period() const {
        return length;
      }

      /** @brief Sets the held sample to 0 and makes the next sample a take,
       *         as after construction. Keeps the period. */
      downsample& reset();

      /** @brief The sample last taken. */
      Flt held() const {
        return hold;
      }

      /** @brief One sample. */
      Flt process(Flt in);

      /** @brief One block at the current period. */
      buffer& operator()(buffer& in);

      /** @brief One block with the period read per sample from
       *         @p periodSignal; as long as the shorter. It also sets the
       *         getter. */
      buffer& operator()(buffer& in, buffer& periodSignal);

    private:
      Flt Step(Flt in);

      buffer output;

      // Samples since the last take; infinite after construction or reset(),
      // so the next sample is a take.
      double count = std::numeric_limits<double>::infinity();
      Flt hold = 0.f;
      Flt length = DEFAULT_PERIOD;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_OPERATORS_DOWNSAMPLE_HPP
