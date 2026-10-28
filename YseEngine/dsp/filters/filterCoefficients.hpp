/*
  ==============================================================================

    filterCoefficients.hpp
    Biquad coefficients from a filter type, a frequency, a gain and a Q,
    computed per sample (issue #1092).

  ==============================================================================
*/

#ifndef YSE_DSP_FILTERS_FILTERCOEFFICIENTS_HPP
#define YSE_DSP_FILTERS_FILTERCOEFFICIENTS_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"
#include "../filters.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Biquad filter coefficients from a filter type, a frequency in
     *         Hz, a linear gain and a Q, computed at signal rate: audio-rate
     *         filter sweeps with the coefficient path of ``biQuad``.
     *
     *  Ported from Max's ``filtercoeff~``. Its five outputs are the
     *  coefficients of
     *
     *  ``y(n) = a0 x(n) + a1 x(n-1) + a2 x(n-2) - b1 y(n-1) - b2 y(n-2)``
     *
     *  in the naming and signs of ``biQuad::coefficients()`` (the feedback is
     *  subtracted), so they drive ``biQuad``'s coefficient-signal call, or
     *  ``~biquad``, directly.
     *
     *  **The formulas** are those of Robert Bristow-Johnson's "Cookbook
     *  formulae for audio EQ biquad filter coefficients", normalised so the
     *  feedback has a leading 1. With ``w = 2 pi f / samplerate``,
     *  ``alpha = sin(w) / (2 Q)`` and ``A = sqrt(gain)``:
     *
     *  - ``LOWPASS`` and ``HIGHPASS``: the second-order lowpass and highpass,
     *    with a gain of ``Q`` at ``f`` (0.7071 is Butterworth).
     *  - ``BANDPASS``: the bandpass with a gain of 1 at ``f``; ``BANDSTOP``
     *    the notch, a gain of 0 at ``f``.
     *  - ``RESONANT``: the bandpass with a constant skirt, a gain of ``Q`` at
     *    ``f``, so a higher Q rings louder as well as narrower.
     *  - ``ALLPASS``: a flat amplitude response, the phase turning by 180
     *    degrees at ``f``, faster the higher the Q.
     *  - ``PEAKNOTCH``: a bell with a gain of ``gain`` at ``f`` (above 1 a
     *    peak, below 1 a notch) and 1 far from it.
     *  - ``LOWSHELF`` and ``HIGHSHELF``: a gain of ``gain`` below / above
     *    ``f`` and 1 on the other side, the Q read as the cookbook's shelf
     *    slope S (1 the steepest without overshoot). A slope steeper than
     *    the gain allows is held at the steepest it does.
     *  - ``GAINLPASS``, ``GAINHPASS``, ``GAINBPASS``, ``GAINBSTOP``,
     *    ``GAINRESONANT`` and ``GAINAPASS``: the filter without the prefix,
     *    its feedforward coefficients ``a0 a1 a2`` multiplied by ``gain``.
     *  - ``OFF``: ``a0 = 1`` and the rest 0, so the input passes unchanged.
     *
     *  The types without a gain ignore it. The frequency is clamped to just
     *  above 0 and just below half the sample rate, the Q to ``MIN_Q`` ..
     *  ``MAX_Q``; the peak and shelf types read a gain below ``MIN_GAIN``
     *  (including a negative one) as ``MIN_GAIN``, the ``GAIN`` types take
     *  any finite gain. The coefficients are computed in double precision
     *  and given as floats: with a very high Q very close to 0 Hz or to half
     *  the sample rate the poles come nearer the unit circle than a float
     *  tells apart, and the filter they drive may not settle (``biQuad``'s
     *  coefficient path then starts again from silence when it overflows).
     *  ``biQuad``'s type path uses the same formulas for its lowpass,
     *  highpass, bandpass and notch.
     *
     *  **At signal rate** the block calls compute the coefficients on one
     *  sample in every ``resampling()`` (default 1, every sample) and hold
     *  them on the samples between; the count runs on across blocks. They
     *  are derived again only when the type, a setting or ``SAMPLERATE``
     *  changed since, so constant settings cost one computation. There is no
     *  smoothing: a step in a setting is a step in the coefficients. The
     *  signal call reads the frequency, the gain and the Q per sample, which
     *  also sets the getters; a sample that is not a finite number keeps the
     *  value before it, as does a setter given one.
     *
     *  Each call computes at most ``STANDARD_BUFFERSIZE`` samples. Call it
     *  from one thread at a time. Processing allocates nothing: the five
     *  outputs are sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API filterCoefficients {
    public:
      /** @brief The filter types; see the class description. */
      enum TYPE : int {
        OFF = 0,
        LOWPASS = 1,
        HIGHPASS = 2,
        BANDPASS = 3,
        BANDSTOP = 4,
        PEAKNOTCH = 5,
        LOWSHELF = 6,
        HIGHSHELF = 7,
        RESONANT = 8,
        ALLPASS = 9,
        GAINLPASS = 10,
        GAINHPASS = 11,
        GAINBPASS = 12,
        GAINBSTOP = 13,
        GAINRESONANT = 14,
        GAINAPASS = 15,
      };

      /** @brief The number of filter types. */
      static constexpr int TYPE_COUNT = 16;

      /** @brief The lowest Q (or slope); smaller values are clamped to it. */
      static constexpr Flt MIN_Q = 0.01f;

      /** @brief The highest Q (or slope); larger values are clamped to it. */
      static constexpr Flt MAX_Q = 10000.f;

      /** @brief The lowest gain the peak and shelf types read (-60 dB): a
       *         deeper cut puts a shelf's poles too near the unit circle for a
       *         float at low frequencies. */
      static constexpr Flt MIN_GAIN = 1e-3f;

      /** @brief The highest resampling factor; larger ones are clamped. */
      static constexpr UInt MAX_RESAMPLING = 4096;

      /** @brief The five coefficients, as ``biQuad`` takes them. */
      using coefficientSet = biQuad::coefficientSet;

      /** @brief The coefficients of @p type for @p frequency in Hz,
       *         @p gain and @p q at @p sampleRate (the class description
       *         gives the formulas and clamping). A non-finite setting, or a
       *         sample rate that is not above 0, gives all 0 (nothing
       *         passes), except for ``OFF``. A type out of range is ``OFF``. */
      static coefficientSet Compute(TYPE type, Flt frequency, Flt gain, Flt q, Flt sampleRate);

      /** @brief The five coefficients in double precision, as computed
       *         before ``Compute`` rounds them to floats. */
      struct preciseSet {
        Dbl a0 = 0.0;
        Dbl a1 = 0.0;
        Dbl a2 = 0.0;
        Dbl b1 = 0.0;
        Dbl b2 = 0.0;
      };

      /** @brief ``Compute`` without the rounding to floats, for a filter that
       *         runs in double precision (``phaseShift``): with a high Q near
       *         0 Hz a float puts the poles on the unit circle. */
      static preciseSet ComputePrecise(TYPE type, Flt frequency, Flt gain, Flt q, Flt sampleRate);

      /** @brief The "imitation analog" form of ``BANDPASS``, ``GAINBPASS``
       *         and ``PEAKNOTCH`` (issue #1209): a biquad whose magnitude
       *         matches the analog prototype's at 0 Hz, at @p frequency and
       *         at half the sample rate, so it keeps its shape near half the
       *         sample rate (the cookbook bandpass has a zero there). Any
       *         other type is ``Compute``'s.
       *
       *  The prototypes are the cookbook's, on the digital frequency axis
       *  without prewarping (``W = w / w0``): the bandpass
       *  ``(W/Q) / sqrt((1 - W^2)^2 + (W/Q)^2)``, gain 1 at ``w0``; the
       *  peak ``sqrt(((1 - W^2)^2 + (W/Qz)^2) / ((1 - W^2)^2 + (W/Qp)^2))``
       *  with ``Qz = Q / A``, ``Qp = Q A`` and ``A = sqrt(gain)``, gain
       *  ``gain`` at ``w0``. The poles are the prototype's mapped by
       *  ``z = exp(s)``; the zeroes are solved so that ``|H|^2`` takes the
       *  three values above, after Martin Vicanek, "Matched Second Order
       *  Digital Filters" (2016). A cut (gain below 1) is the boost by
       *  ``1 / gain`` turned over, which matches the same three points.
       *  Between those points the response follows the prototype closely
       *  but not exactly: within about 1.5 dB for a frequency up to a
       *  quarter of the sample rate, more above it. The settings are
       *  clamped and checked as ``Compute`` clamps and checks them. */
      static coefficientSet ComputeMatched(TYPE type, Flt frequency, Flt gain, Flt q,
                                           Flt sampleRate);

      /** @brief ``ComputeMatched`` without the rounding to floats. */
      static preciseSet ComputeMatchedPrecise(TYPE type, Flt frequency, Flt gain, Flt q,
                                              Flt sampleRate);

      /** @brief Starts as a Butterworth lowpass at 1000 Hz, gain 1. */
      filterCoefficients();

      /** @brief The filter type. A value out of range is ignored. */
      filterCoefficients& type(TYPE value);

      /** @brief The frequency in Hz. Non-finite: ignored. */
      filterCoefficients& frequency(Flt hz);

      /** @brief The linear gain. Non-finite: ignored. */
      filterCoefficients& gain(Flt value);

      /** @brief The Q, or the slope of a shelf. Non-finite: ignored. */
      filterCoefficients& q(Flt value);

      /** @brief Compute on one sample in @p factor and hold between, clamped
       *         to 1 .. ``MAX_RESAMPLING``. The next block computes on its
       *         first sample. */
      filterCoefficients& resampling(UInt factor);

      /** @brief The filter type as last set. */
      TYPE type() const {
        return filter;
      }

      /** @brief The frequency as last set or read. */
      Flt frequency() const {
        return freq;
      }

      /** @brief The gain as last set or read. */
      Flt gain() const {
        return amp;
      }

      /** @brief The Q as last set or read. */
      Flt q() const {
        return quality;
      }

      /** @brief The resampling factor. */
      UInt resampling() const {
        return factor;
      }

      /** @brief The coefficients for the current settings at the live sample
       *         rate. */
      coefficientSet coefficients() const;

      /** @brief One block of @p length samples at the current settings. */
      void operator()(UInt length);

      /** @brief One block with the frequency, the gain and the Q read per
       *         sample from the three buffers; as long as the shortest. */
      void operator()(buffer& frequencySignal, buffer& gainSignal, buffer& qSignal);

      /** @brief The output of the last block: coefficient @p index, 0 .. 4
       *         for a0 a1 a2 b1 b2 (a larger index reads b2). */
      buffer& output(UInt index);

      /** @brief The a0 output of the last block. */
      buffer& a0() {
        return out[0];
      }

      /** @brief The a1 output of the last block. */
      buffer& a1() {
        return out[1];
      }

      /** @brief The a2 output of the last block. */
      buffer& a2() {
        return out[2];
      }

      /** @brief The b1 output of the last block. */
      buffer& b1() {
        return out[3];
      }

      /** @brief The b2 output of the last block. */
      buffer& b2() {
        return out[4];
      }

    private:
      // Derives the held coefficients again if anything changed.
      void Update();
      // Writes the held coefficients to sample i of the outputs, computing
      // them first on the resampling count's first sample.
      void Emit(UInt i);
      void Fit(UInt length);

      buffer out[5];

      TYPE filter = LOWPASS;
      Flt freq = 1000.f;
      Flt amp = 1.f;
      Flt quality = 0.70710678f;
      UInt factor = 1;
      UInt phase = 0;

      // The held coefficients and what they were derived from.
      coefficientSet held;
      Bool valid = false;
      TYPE cachedType = LOWPASS;
      Flt cachedFreq = 0.f;
      Flt cachedAmp = 0.f;
      Flt cachedQ = 0.f;
      UInt cachedRate = 0;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_FILTERS_FILTERCOEFFICIENTS_HPP
