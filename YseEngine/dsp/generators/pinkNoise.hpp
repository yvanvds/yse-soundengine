/*
  ==============================================================================

    pinkNoise.hpp
    A pink noise generator (issue #1056).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_PINKNOISE_HPP
#define YSE_DSP_GENERATORS_PINKNOISE_HPP

#include <cstdint>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Pink noise: random samples with equal energy in every octave.
     *
     *  White noise (``DSP::noise``) has equal energy per hertz, so each octave
     *  up holds twice the energy of the one below and it sounds bright and
     *  hissy. Pink noise falls by 3 dB per octave instead, which spreads the
     *  energy evenly over the octaves the ear hears: it sounds like wind, rain
     *  or a waterfall, and it is the usual test signal for measuring a room or
     *  a speaker. As a slow modulation source it wanders more naturally than
     *  white noise.
     *
     *  The white source is the same generator as ``DSP::noise`` (a linear
     *  congruential generator with the same constants and default seed). It
     *  is shaped with Paul Kellet's refined pink filter, a sum of six one-pole
     *  filters, which holds the -3 dB/octave slope to within a fraction of a
     *  decibel from about 10 Hz (at 44.1 kHz) to the Nyquist frequency. The
     *  filter works in fractions of the sample rate, so nothing in it depends
     *  on ``SAMPLERATE`` and an engine restart at another rate needs nothing
     *  recomputed; only that 10 Hz corner moves with the rate.
     *
     *  The output has an RMS of about 0.2 (-14 dBFS), with peaks rarely
     *  above 0.9; it is clamped to -1..1 as a safeguard.
     *
     *  ``seed`` restarts the sequence, so two generators with the same seed
     *  produce the same samples, which makes a test or a render repeatable.
     *
     *  Apart from the constructor and ``seed``, call it only from the audio
     *  thread. Processing allocates nothing: the output is sized to
     *  ``STANDARD_BUFFERSIZE`` in the constructor, and a block is at most that
     *  long (a longer one is processed up to ``STANDARD_BUFFERSIZE`` samples).
     */
    class API pinkNoise {
    public:
      /** @brief The seed a new generator starts from, the same as
       *         ``DSP::noise``'s. */
      static constexpr std::uint32_t DEFAULT_SEED = 307u * 1319u;

      pinkNoise();

      /** @brief Generate a fresh block of ``length`` samples. */
      buffer& operator()(UInt length = STANDARD_BUFFERSIZE);

      /**
       *  @brief Restart the random sequence from @p value and clear the
       *         filter, so the next samples are exactly those of a new
       *         generator with this seed.
       */
      pinkNoise& seed(std::uint32_t value);

    private:
      std::uint32_t state;
      Dbl b0, b1, b2, b3, b4, b5, b6;
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_PINKNOISE_HPP
