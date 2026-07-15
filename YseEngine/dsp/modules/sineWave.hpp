/*
  ==============================================================================

    sineWave.h
    Created: 31 Jan 2014 2:56:47pm
    Author:  yvan

  ==============================================================================
*/

#ifndef SINEWAVE_H_INCLUDED
#define SINEWAVE_H_INCLUDED

#include "../dspObject.hpp"
#include "../oscillators.hpp"
#include "../drawableBuffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Sine-wave sound source.
     *
     *  The simplest possible source for ``YSE::sound::create(dspSourceObject&, ...)``.
     *  Useful as a test tone, a tuning reference, or the building block for an
     *  additive synth.
     */
    class API sineWave : public dspSourceObject {
    public:
      sineWave();

      /**
       *  @brief Set the sine frequency in Hz.
       *
       *  Takes effect immediately, or on the next note-on transition if the
       *  sound is in a stopped/paused state.
       */
      void frequency(float value) override;

      /** @brief Current frequency. */
      float frequency();

      using dspSourceObject::process;

      /** @brief dspSourceObject audio-thread entry point. */
      void process(SOUND_STATUS& intent) override;

      /** @brief Variant of ``process`` that reports latency. Audio-thread only. */
      virtual void process(SOUND_STATUS& intent, Int& latency);

    private:
      sine sineGen;
      YSE::DSP::drawableBuffer volumeCurve;

      YSE::DSP::drawableBuffer frequencyCurve;
      aFlt parmFrequency;
      Flt currentFrequency;
    };

  } // namespace DSP
} // namespace YSE

#endif // SINEWAVE_H_INCLUDED
