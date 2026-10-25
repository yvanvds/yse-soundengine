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
     *  interactive parameter visualiser. The low-pass, high-pass, band-pass
     *  and notch coefficients are ``filterCoefficients::Compute()``'s
     *  (issue #1092), whose frequency and Q clamping they follow; that class
     *  also computes coefficients for the coefficient path below at signal
     *  rate.
     *
     *  **The coefficient path** (issue #1090, behind ``~biquad``, ported from
     *  Max's ``biquad~``) drives the filter directly by five coefficients, in
     *  that object's naming and signs:
     *
     *  ``y(n) = a0 x(n) + a1 x(n-1) + a2 x(n-2) - b1 y(n-1) - b2 y(n-2)``
     *
     *  so the feedback coefficients are subtracted (``setRaw()`` takes them
     *  with the opposite sign, as Pure Data's ``biquad~`` does). The type
     *  modes' coefficients map onto it as ``a0 a1 a2`` = the feedforward and
     *  ``b1 b2`` = the feedback of the usual textbook normalisation, e.g. a
     *  low-pass is ``coefficients(n k^2, 2 n k^2, n k^2, 2 n (k^2 - 1),
     *  n (1 - k/Q + k^2))`` with ``k = tan(pi f / samplerate)`` and
     *  ``n = 1 / (1 + k/Q + k^2)``.
     *
     *  It is selected by ``coefficients()``, ``process()`` and the
     *  coefficient-signal call, and left again by any of the type setters or
     *  ``setRaw()``; ``operator()(buffer&)`` runs whichever was selected last.
     *  The type path is unchanged by it. The coefficient path:
     *
     *  - runs the equation above as written (direct form I), with the two
     *    previous inputs and outputs as its state, in double precision;
     *  - takes any finite coefficients and does not check stability, as
     *    ``biquad~`` does not: feedback coefficients that make the filter
     *    explode can be lowered again and the state recovered with
     *    ``clear()``. An output that overflows a float, or is not a finite
     *    number, zeroes the state (the sample outputs 0), so a blown-up
     *    filter starts again instead of latching infinity;
     *  - smooths a change of the coefficients by ``smooth()`` (on by
     *    default): each moves in a straight line from its value to the new
     *    one over the next ``SMOOTH_SAMPLES`` samples, reaching it on the
     *    last of them, so a list of coefficients does
     *    not click. With smoothing off a change applies at the next sample.
     *    A change made before the path has processed its first sample
     *    applies at once either way, so the first coefficients set are the
     *    starting ones;
     *  - reads a coefficient given as a signal per sample, unsmoothed; it
     *    becomes that coefficient's value, from which a later scalar
     *    ``coefficients()`` moves;
     *  - ignores a coefficient that is not a finite number (keeping that
     *    coefficient's value), and skips an input sample that is not one
     *    (the output and state hold);
     *  - can be preloaded with ``stoke()``, which sets the previous outputs
     *    and inputs, so a filter with poles on the unit circle rings as an
     *    oscillator from its first sample.
     *
     *  Nothing on the coefficient path depends on the sample rate. Its
     *  setters are for the thread that processes (or between blocks). A
     *  block call on the coefficient path processes at most
     *  ``STANDARD_BUFFERSIZE`` samples and allocates nothing.
     */
    class API biQuad : public filterBase {
    public:
      /** @brief Samples over which a smoothed coefficient change moves. */
      static constexpr UInt SMOOTH_SAMPLES = 64;

      /** @brief The five coefficients of the coefficient path, named and
       *         signed as in the class description. */
      struct coefficientSet {
        Flt a0 = 0.f;
        Flt a1 = 0.f;
        Flt a2 = 0.f;
        Flt b1 = 0.f;
        Flt b2 = 0.f;
      };

      biQuad();
      biQuad(const biQuad&);
      biQuad& operator=(const biQuad&) = delete;
      ~biQuad() override = default;

      /** @brief Selects the coefficient path with these coefficients (the
       *         class description gives the equation, smoothing and
       *         non-finite handling). */
      biQuad& coefficients(Flt a0, Flt a1, Flt a2, Flt b1, Flt b2);

      /** @brief The coefficients the coefficient path moves towards (the
       *         last set or read from a signal). */
      coefficientSet coefficients() const;

      /** @brief Turns smoothing of coefficient changes on or off. */
      biQuad& smooth(Bool on);

      /** @brief Whether coefficient changes are smoothed. */
      Bool smooth() const {
        return smoothing;
      }

      /** @brief Sets the state of both paths to 0: the previous inputs and
       *         outputs. Coefficients are kept. */
      biQuad& clear();

      /** @brief Sets the coefficient path's previous outputs y(n-1), y(n-2)
       *         and inputs x(n-1), x(n-2). Non-finite values are read as 0. */
      biQuad& stoke(Flt y1, Flt y2 = 0.f, Flt x1 = 0.f, Flt x2 = 0.f);

      /** @brief One sample on the coefficient path (selects it). */
      Flt process(Flt in);

      /** @brief One block on the coefficient path (selects it), each
       *         coefficient read per sample from its buffer, or from the
       *         scalar coefficients (smoothed) where the pointer is null. A
       *         buffer must be at least as long as @p in. */
      YSE::DSP::buffer& operator()(YSE::DSP::buffer& in, YSE::DSP::buffer* a0, YSE::DSP::buffer* a1,
                                   YSE::DSP::buffer* a2, YSE::DSP::buffer* b1,
                                   YSE::DSP::buffer* b2);

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
      // One sample of the coefficient path at the current coefficients.
      Flt Step(Flt in);
      // Moves the smoothed coefficients one sample on.
      void Advance();
      BQ_TYPE type;

      // Which path operator()(buffer&) runs: set by the coefficient path,
      // cleared by the type setters and setRaw() (any thread).
      aBool direct{false};

      // The coefficient path (processing thread). current[] is in use,
      // target[] where it is going, step[] the per-sample move while
      // `ramp` samples remain. Order: a0 a1 a2 b1 b2.
      Dbl current[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
      Dbl target[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
      Dbl step[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
      UInt ramp = 0;
      Bool smoothing = true;
      Bool started = false;
      Dbl x1 = 0.0;
      Dbl x2 = 0.0;
      Dbl y1 = 0.0;
      Dbl y2 = 0.0;
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
