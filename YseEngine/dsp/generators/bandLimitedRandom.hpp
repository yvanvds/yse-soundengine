/*
  ==============================================================================

    bandLimitedRandom.hpp
    An interpolated random signal (issue #1057).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_BANDLIMITEDRANDOM_HPP
#define YSE_DSP_GENERATORS_BANDLIMITEDRANDOM_HPP

#include <cstdint>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Band-limited random signal: a new random value at a given
     *         frequency, with straight lines in between.
     *
     *  Every ``1 / frequency`` seconds the generator picks a new random value
     *  in -1..1 and glides to it in a straight line from the previous one.
     *  The result is a smooth, wandering signal whose energy lies below the
     *  frequency: slow random modulation for vibrato drift, a wandering
     *  filter cutoff or a gently moving pan. At audio rates it is a rough,
     *  band-limited noise. Ported from Max's ``rand~``.
     *
     *  The random values come from the same generator as ``DSP::noise`` (the
     *  same constants and default seed). The output starts at 0 and glides
     *  to the first random value, so a new generator does not jump. Output
     *  stays within -1..1.
     *
     *  The sign of the frequency is ignored. A frequency of 0 holds the
     *  current value; one at or above the sample rate picks a new value
     *  every sample (the sequence of ``DSP::noise``, one sample late, with
     *  the leading 0). A frequency that is not a finite number holds as 0
     *  does. The sample rate is read from ``SAMPLERATE`` on every block, so
     *  the frequency stays right across an engine restart at another rate.
     *
     *  ``seed`` restarts the sequence, so two generators with the same seed
     *  produce the same samples, which makes a test or a render repeatable.
     *
     *  Apart from the constructor and ``seed``, call it only from the audio
     *  thread. Processing allocates nothing: the output is sized to
     *  ``STANDARD_BUFFERSIZE`` in the constructor, and a block is at most that
     *  long (a longer one is processed up to ``STANDARD_BUFFERSIZE`` samples).
     */
    class API bandLimitedRandom {
    public:
      /** @brief The seed a new generator starts from, the same as
       *         ``DSP::noise``'s. */
      static constexpr std::uint32_t DEFAULT_SEED = 307u * 1319u;

      bandLimitedRandom();

      /** @brief One block at a constant frequency, in Hz. */
      buffer& operator()(Flt frequency, UInt length = STANDARD_BUFFERSIZE);

      /** @brief One block driven by a per-sample frequency, in Hz. */
      buffer& operator()(buffer& frequency);

      /**
       *  @brief Restart the random sequence from @p value: the output goes
       *         back to 0 and the next samples are exactly those of a new
       *         generator with this seed.
       */
      bandLimitedRandom& seed(std::uint32_t value);

    private:
      template <class Frequency> buffer& process(Frequency frequency, UInt length);

      std::uint32_t state;
      Dbl from; // the value the current segment starts at
      Dbl to; // the value it glides to
      Dbl phase; // how far along the segment, 0..1
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_BANDLIMITEDRANDOM_HPP
