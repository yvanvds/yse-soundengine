/*
  ==============================================================================

    filters.hpp
    Created: 31 Jan 2014 2:53:30pm
    Author:  yvan

  ==============================================================================
*/

#ifndef FILTERS_H_INCLUDED
#define FILTERS_H_INCLUDED

#include "buffer.hpp"
#include "../headers/enums.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Common state shared by the simple one-pole filters.
     *
     *  Not intended to be instantiated directly — use ``highPass``, ``lowPass``,
     *  ``bandPass``, or ``biQuad``.
     */
    class API filterBase {
    public:
      filterBase();
      filterBase( const filterBase &);
      virtual ~filterBase() = default;

      /** @brief Process the buffer in place. */
      virtual buffer & operator()(buffer & in) = 0;

    protected:
      aFlt freq;
      aFlt gain;
      Flt q;
      Flt last;
      Flt previous;
      aFlt coef1, coef2;
      aFlt ff1, ff2, ff3, fb1, fb2;
      buffer samples;
    };

    /**
     *  @brief Simple high-pass filter.
     *
     *  Attenuates frequencies below the cutoff. Reach for this when you need
     *  to remove low-frequency rumble or thin out a signal cheaply.
     */
    class API highPass : public filterBase {
    public:
      /** @brief Set the cutoff frequency in Hz. */
      highPass& setFrequency(Flt f);

      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
    };

    /**
     *  @brief Simple low-pass filter.
     *
     *  Attenuates frequencies above the cutoff. Use for darkening, occlusion
     *  fakes, or anti-aliasing.
     */
    class API lowPass : public filterBase {
    public:
      /** @brief Set the cutoff frequency in Hz. */
      lowPass& setFrequency(Flt f);

      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
    };

    /**
     *  @brief Resonant band-pass filter.
     *
     *  Passes a band centred on the chosen frequency. Higher ``q`` produces a
     *  narrower, more resonant band.
     */
    class API bandPass : public filterBase {
    public:
      /** @brief Set both centre frequency (Hz) and resonance simultaneously. */
      bandPass& set(Flt freq, Flt q);

      /** @brief Set the centre frequency in Hz. */
      bandPass& setFrequency(Flt freq);

      /** @brief Set the resonance (Q factor). */
      bandPass& setQ(Flt q);

      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);

      bandPass();

    private:
      void calcCoef();
      static float qCos(Flt omega);
    };

    /**
     *  @brief Configurable biquad filter — the workhorse of DSP filtering.
     *
     *  Pick one of the ``BQ_TYPE`` modes (low-pass, high-pass, band-pass,
     *  notch, peak, low-shelf, high-shelf) and tune frequency, Q, and peak
     *  gain. See `earlevel.com/main/2010/12/20/biquad-calculator/` for an
     *  interactive parameter visualiser.
     */
    class API biQuad : public filterBase {
    public:
      /** @brief Configure the filter in one call.
       *
       *  @param type      Filter mode (low-pass, high-pass, peak, shelf, ...).
       *  @param frequency Centre / cutoff frequency in Hz.
       *  @param Q         Resonance.
       *  @param peakGain  Peak gain in dB. Only meaningful for ``BQ_PEAK`` and
       *                   the shelving modes.
       */
      biQuad& set(BQ_TYPE type, Flt frequency, Flt Q, Flt peakGain = 4);

      /** @brief Set the filter mode. */
      biQuad& setType(BQ_TYPE type);

      /** @brief Set the centre / cutoff frequency in Hz. */
      biQuad& setFreq(Flt frequency);

      /** @brief Set the Q factor. */
      biQuad& setQ(Flt Q);

      /** @brief Set the peak gain in dB. */
      biQuad& setPeak(Flt peakGain);

      /** @brief Set the raw biquad coefficients directly.
       *
       *  Bypasses the parameter-to-coefficient mapping — useful when porting a
       *  filter design from elsewhere. Only for callers who know exactly what
       *  the coefficients mean.
       */
      biQuad& setRaw(Flt fb1, Flt fb2, Flt ff1, Flt ff2, Flt ff3);

      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);

    private:
      void calc();
      BQ_TYPE type;
    };

    /**
     *  @brief Sample-and-hold gate.
     *
     *  When the control ``signal`` rises above a threshold, the latest input
     *  sample is captured and held in the output until the next trigger.
     *  Classic for stepped-pitch effects and quantised modulation.
     */
    class API sampleHold {
    public:
      /** @brief Reset the held value to ``value``. */
      sampleHold& reset(Flt value = 1e20);

      /** @brief Set the trigger threshold for the control signal. */
      sampleHold& set(Flt value);

      /** @brief Process ``in`` gated by ``signal``. */
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in, YSE::DSP::buffer & signal);

      sampleHold();

    private:
      aFlt lastIn, lastOut;
      YSE::DSP::buffer samples;
    };

    // The variable-cutoff filter (``vcf``) lives in oscillators.hpp because
    // it shares state and code paths with the oscillator family.

  }
}




#endif  // FILTERS_H_INCLUDED
