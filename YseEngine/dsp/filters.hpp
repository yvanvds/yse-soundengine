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
      filterBase(const filterBase&);
      virtual ~filterBase() = default;

      /** @brief Process the buffer in place. */
      virtual buffer& operator()(buffer& in) = 0;

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
      YSE::DSP::buffer& operator()(YSE::DSP::buffer& in) override;
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
      YSE::DSP::buffer& operator()(YSE::DSP::buffer& in) override;
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
      YSE::DSP::buffer& operator()(YSE::DSP::buffer& in) override;

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
      YSE::DSP::buffer& operator()(YSE::DSP::buffer& in) override;

    private:
      void calc();
      BQ_TYPE type;
    };

    /**
     *  @brief Sample-and-hold gate.
     *
     *  Each time the control ``signal`` meets the trigger condition, the
     *  input sample at that moment is captured and held in the output until
     *  the next trigger. Classic for stepped-pitch effects, quantised
     *  modulation and latching a value once per step of a phasor patch.
     *
     *  **The trigger condition** is set by ``triggerMode()``, comparing each
     *  control sample with the one before it and with ``threshold()``
     *  (default 0). The first seven modes are those of Max's ``sah~``
     *  (issue #1082), numbered as there:
     *
     *  - ``ASCENDING``: the previous sample is at or below the threshold and
     *    the current one above it.
     *  - ``DESCENDING``: the previous sample is at or above the threshold and
     *    the current one below it.
     *  - ``BOTH``: either of the two.
     *  - ``EQUALS``: the current sample is exactly the threshold, at every
     *    such sample (the input is followed while it stays there).
     *  - ``ZERO_TO_NONZERO``: the previous sample is 0 and the current one
     *    is not; the threshold is ignored.
     *  - ``NONZERO_TO_ZERO``: the previous sample is not 0 and the current
     *    one is; the threshold is ignored.
     *  - ``PHASE``: the control is a 0..1 phase (wrapped into 0..1) and the
     *    threshold a phase on it, also wrapped; the trigger is the control
     *    passing it in either direction, read as ``phasorWhat`` reads a
     *    ramp: a jump of more than half a cycle is a wrap the short way
     *    round, so with threshold 0 every reset of a ``phasor`` triggers,
     *    for a positive or a negative frequency.
     *  - ``DECREASE`` (the default, the class's behaviour before #1082, as
     *    Pure Data's ``samphold~``): the current sample is lower than the
     *    previous one, so a rising ramp triggers at each reset; the
     *    threshold is ignored.
     *
     *  **Duration.** With ``duration(n)`` above 0 the held value is output
     *  for ``n`` samples from the trigger, the trigger sample being the
     *  first, and 0 after that until the next trigger (0 before the first
     *  one). With 0 (the default) it is held until the next trigger.
     *
     *  The previous control sample starts at 0; ``reset()`` sets it. A
     *  control sample that is not a finite number never triggers and is
     *  skipped: the next one is compared with the sample before it.
     *
     *  Nothing depends on the sample rate. Each call processes at most
     *  ``STANDARD_BUFFERSIZE`` samples (the shorter of the two buffers). The
     *  setters may be called from another thread; they take effect at the
     *  next block. Processing allocates nothing: the output is sized to
     *  ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API sampleHold {
    public:
      /** @brief The trigger condition; see the class description. */
      enum TRIGGER_MODE {
        ASCENDING = 0,
        DESCENDING = 1,
        BOTH = 2,
        EQUALS = 3,
        ZERO_TO_NONZERO = 4,
        NONZERO_TO_ZERO = 5,
        PHASE = 6,
        DECREASE = 7,
      };

      /** @brief Sets the previous control sample to ``value``. In
       *         ``DECREASE`` mode the default makes the next sample trigger. */
      sampleHold& reset(Flt value = 1e20);

      /** @brief Sets the held output value, as if it had been sampled (it
       *         does not start a ``duration``). */
      sampleHold& set(Flt value);

      /** @brief The threshold the control is compared with. */
      sampleHold& threshold(Flt value);

      /** @brief The trigger condition. */
      sampleHold& triggerMode(TRIGGER_MODE mode);

      /** @brief Samples the held value lasts after a trigger; 0 holds it until
       *         the next one. */
      sampleHold& duration(UInt samples);

      /** @brief Process ``in`` gated by ``signal``. */
      YSE::DSP::buffer& operator()(YSE::DSP::buffer& in, YSE::DSP::buffer& signal);

      sampleHold();

    private:
      aFlt lastIn, lastOut;
      aFlt thresh;
      aInt mode;
      aUInt length;
      // Samples of the held value left to output with a duration (audio
      // thread only).
      UInt remaining = 0;
      YSE::DSP::buffer samples;
    };

    // The variable-cutoff filter (``vcf``) lives in oscillators.hpp because
    // it shares state and code paths with the oscillator family.

  } // namespace DSP
} // namespace YSE

#endif // FILTERS_H_INCLUDED
