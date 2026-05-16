/*
  ==============================================================================

    wavetable.hpp
    Created: 22 Jul 2015 10:51:14am
    Author:  yvan

  ==============================================================================
*/

#ifndef WAVETABLE_HPP_INCLUDED
#define WAVETABLE_HPP_INCLUDED

#include "fileBuffer.hpp"

namespace YSE {

  namespace DSP {

    /**
     *  @brief Pre-computed single-cycle waveform table.
     *
     *  ``wavetable`` is a ``fileBuffer`` with a one-sample overflow tail used
     *  for wrap-around interpolation, plus generators for the classic
     *  band-limited waveforms. Combine with the oscillator family in
     *  ``YSE::DSP`` for fast playback of complex timbres without per-sample
     *  trigonometry.
     */
    class API wavetable : public fileBuffer {
    public:
      /** @brief Construct an empty wavetable of ``length`` samples. */
      wavetable(UInt length = STANDARD_BUFFERSIZE) : fileBuffer(length, 1) {}

      /**
       *  @brief Fill the table with a band-limited sawtooth wave.
       *
       *  @param harmonics Number of partials to sum. Higher values give a
       *                   brighter timbre but risk aliasing when played at
       *                   high pitches.
       *  @param length    Resolution of the table in samples.
       */
      void createSaw     (Int harmonics, Int length);

      /** @brief Fill the table with a band-limited square wave. See ``createSaw``. */
      void createSquare  (Int harmonics, Int length);

      /** @brief Fill the table with a band-limited triangle wave. See ``createSaw``. */
      void createTriangle(Int harmonics, Int length);

      /**
       *  @brief Fill the table with a Fourier sum.
       *
       *  @param harmonics Amplitude per harmonic. ``harmonics[0]`` is the
       *                   fundamental, ``harmonics[1]`` is the second
       *                   harmonic, and so on.
       *  @param length    Resolution of the table in samples.
       *  @param phase     Initial phase offset in radians.
       */
      void createFourierTable(const std::vector<Flt> & harmonics, Int length, Flt phase);
    };

  }
}



#endif  // WAVETABLE_HPP_INCLUDED
