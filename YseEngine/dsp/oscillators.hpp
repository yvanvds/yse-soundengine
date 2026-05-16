/*
  ==============================================================================

    oscillators.h
    Created: 31 Jan 2014 2:54:59pm
    Author:  yvan

  ==============================================================================
*/

#ifndef OSCILLATORS_H_INCLUDED
#define OSCILLATORS_H_INCLUDED

#include "../headers/defines.hpp"
#include "../headers/types.hpp"
#include "../headers/constants.hpp"
#include "buffer.hpp"
#include "wavetable.hpp"

/**
 * @note Apart from their constructors, the oscillator and ``vcf`` classes
 *       must only be invoked from inside a DSP callback / ``process`` body.
 */

namespace YSE {
  namespace DSP {

    /**
     *  @brief Naive (non band-limited) sawtooth oscillator.
     *
     *  Cheap; aliases at high pitches. Use the wavetable-driven
     *  ``oscillator`` with a band-limited ``wavetable::createSaw`` table for
     *  alias-free output.
     */
    class API saw {
    public:
      /** @brief Generate a fresh block at ``frequency`` Hz. */
      YSE::DSP::buffer & operator()(Flt frequency, UInt length = STANDARD_BUFFERSIZE);

      /** @brief Generate one block driven by per-sample frequency in ``in``. */
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
      saw();

    private:
      Dbl phase;
      Flt conv;
      Flt frequency;
      YSE::DSP::buffer buffer;

      Flt *inPtr;
      void calc(Bool useFrequency);
    };

    /**
     *  @brief Cosine wave oscillator.
     *
     *  Use as a building block for FM synthesis, modulators, or filter sweeps.
     */
    class API cosine {
    public:
      /** @brief Generate one block driven by per-sample frequency in ``in``. */
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
      cosine();

    private:
      YSE::DSP::buffer buffer;
    };

    /**
     *  @brief Sine wave oscillator.
     *
     *  The fundamental building block for additive synthesis, vibrato, and
     *  test tones.
     */
    class API sine {
    public:
      /** @brief Generate a fresh block at ``frequency`` Hz. */
      YSE::DSP::buffer & operator()(Flt frequency, UInt length = STANDARD_BUFFERSIZE);

      /** @brief Generate one block driven by per-sample frequency in ``in``. */
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
      sine();

      /** @brief Reset the phase to zero. */
      void reset();

    private:
      YSE::DSP::buffer buffer;
      Dbl phase;
      Flt conv;
      Flt frequency;

      Flt *inPtr;
      void calc(Bool useFrequency);
    };

    /**
     *  @brief Wavetable-driven oscillator.
     *
     *  The general-purpose oscillator — supply a ``wavetable`` via
     *  ``initialize`` and the oscillator plays it back at any frequency.
     *  Combine with ``wavetable::createSaw`` / ``createSquare`` /
     *  ``createTriangle`` for band-limited classic shapes, or with
     *  ``createFourierTable`` for custom timbres.
     */
    class API oscillator {
    public:
      oscillator();

      /** @brief Generate a fresh block at ``frequency`` Hz. */
      YSE::DSP::buffer & operator()(Flt frequency, UInt length = STANDARD_BUFFERSIZE);

      /** @brief Generate one block driven by per-sample frequency in ``in``. */
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);

      /** @brief Attach a wavetable as the oscillator's waveform source. */
      void initialize(wavetable & source);

      /** @brief Reset the phase to zero. */
      void reset();

    private:
      YSE::DSP::buffer buffer;
      Dbl phase;
      Flt conv;
      Flt frequency;
      wavetable * table;

      Flt *inPtr;
      void calc(Bool useFrequency);
    };

    /**
     *  @brief White-noise generator.
     *
     *  Useful as a percussion source, for granular textures, or as a test
     *  signal for filters.
     */
    class API noise {
    public:
      /** @brief Generate a fresh noise block of ``length`` samples. */
      YSE::DSP::buffer & operator()(UInt length = STANDARD_BUFFERSIZE);
      noise();

    private:
      YSE::DSP::buffer buffer;
      Int value;
    };

    /**
     *  @brief Voltage-controlled (variable-cutoff) filter.
     *
     *  A resonant filter whose centre frequency tracks a control buffer. Use
     *  it for filter sweeps driven by an LFO, an envelope, or any other DSP
     *  buffer.
     */
    class API vcf {
    public:
      /** @brief Set the resonance (Q factor). Higher values give a sharper peak. */
      vcf& sharpness(Flt q);

      /** @brief Filter ``in`` with cutoff tracked from ``center``. */
      vcf& operator()(YSE::DSP::buffer & in, YSE::DSP::buffer & center);

      /** @brief Most recent real-part output buffer. */
      YSE::DSP::buffer & real();

      /** @brief Most recent imaginary-part output buffer. */
      YSE::DSP::buffer & imag();
      vcf();

    private:
      Flt re;
      Flt im;
      Flt q;
      Flt isr;
      YSE::DSP::buffer realBuffer;
      YSE::DSP::buffer imagBuffer;
    };

  }
}



#endif  // OSCILLATORS_H_INCLUDED
