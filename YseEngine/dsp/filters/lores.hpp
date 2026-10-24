/*
  ==============================================================================

    lores.hpp
    A two-pole resonant lowpass filter with its resonance on a 0-1 scale
    (issue #1087).

  ==============================================================================
*/

#ifndef YSE_DSP_FILTERS_LORES_HPP
#define YSE_DSP_FILTERS_LORES_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Two-pole resonant lowpass filter, 12 dB per octave, with a
     *         cutoff in Hz and a resonance from 0 (no peak) to 1 (a sharp,
     *         ringing peak at the cutoff): the classic synth filter.
     *
     *  Ported from Max's ``lores~``. Each sample computes
     *
     *  ``y(n) = g * x(n) - a1 * y(n-1) - a2 * y(n-2)``
     *
     *  an all-pole filter with no zeros. Its two poles are the analog
     *  lowpass ``1 / (s^2 + s * w / Q + w^2)`` at the cutoff ``w``, mapped by
     *  impulse invariance (``z = exp(s / samplerate)``), so the filter costs
     *  two multiplies and adds per sample past ``g``:
     *
     *  - ``Q = 1 / (sqrt(2) * (1 - resonance))``: resonance 0 is the flat
     *    Butterworth response (``Q = 0.707``), 0.5 doubles ``Q`` and 0.9
     *    gives a peak about 17 dB above the passband.
     *  - ``r = exp(-w / (2 * Q * samplerate))`` and
     *    ``theta = w / samplerate * sqrt(1 - 1 / (4 * Q^2))``, the poles'
     *    radius and angle; ``a1 = -2 * r * cos(theta)``, ``a2 = r^2``.
     *  - ``g = 1 + a1 + a2``, so the gain at 0 Hz is exactly 1.
     *
     *  The cutoff is clamped to 0 .. half the sample rate and the resonance
     *  to 0 .. ``MAX_RESONANCE`` (0.999, ``Q`` about 707). A pole radius
     *  below 1 for every cutoff above 0 keeps the filter stable however close
     *  the resonance comes to 1; the coefficients and state are kept in
     *  double precision so that holds at low cutoffs too, where ``r`` is
     *  within a float's rounding of 1. A cutoff of 0 passes nothing. Near the
     *  Nyquist frequency the response is approximate, as impulse invariance
     *  folds the analog response there: at resonance 0 it stays within 0.01
     *  dB of flat for a cutoff up to a tenth of the sample rate, but a cutoff
     *  at the Nyquist frequency lifts the top of the band by about 2.3 dB. A
     *  setting that is not a finite number is ignored.
     *
     *  Cutoff and resonance are held as given and the coefficients follow
     *  the live sample rate: they are derived again whenever either value or
     *  ``SAMPLERATE`` changes. The three-buffer call reads both at signal
     *  rate, one value per sample, deriving the coefficients again only on
     *  the samples where one of them changed; ``cutoff()`` and
     *  ``resonance()`` keep the last finite values read. An input sample that
     *  is not a finite number is skipped (the output and state hold), so one
     *  stray NaN cannot latch the filter. ``clear()`` sets the state to 0.
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples. Call it
     *  from one thread at a time. Processing allocates nothing: the output is
     *  sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API lores {
    public:
      /** @brief The highest resonance; larger values are clamped to it. */
      static constexpr Flt MAX_RESONANCE = 0.999f;

      /** @brief The filter coefficients, as the class description defines
       *         them. */
      struct coefficients {
        Dbl g = 0.0;
        Dbl a1 = 0.0;
        Dbl a2 = 0.0;
      };

      lores();

      /** @brief The cutoff in Hz. Non-finite: ignored. */
      lores& cutoff(Flt hz);

      /** @brief The resonance, 0-1. Non-finite: ignored. */
      lores& resonance(Flt value);

      /** @brief Sets the filter state (the two previous outputs) to 0. */
      lores& clear();

      /** @brief The cutoff as last set. */
      Flt cutoff() const {
        return freq;
      }

      /** @brief The resonance as last set. */
      Flt resonance() const {
        return res;
      }

      /** @brief The coefficients for the current settings at the live
       *         sample rate. */
      coefficients coefficient();

      /** @brief The coefficients for @p hz and @p res at @p sampleRate
       *         (the class description gives the formulas and clamping). */
      static coefficients Coefficients(Flt hz, Flt res, Flt sampleRate);

      /** @brief ``Q`` for a resonance, after clamping it. */
      static Dbl Q(Flt res);

      /** @brief One sample. */
      Flt process(Flt in);

      /** @brief One block at the current settings. */
      buffer& operator()(buffer& in);

      /** @brief One block with the cutoff and the resonance read per sample
       *         from @p cutoffSignal and @p resonanceSignal (each at least
       *         as long as @p in), which also set ``cutoff()`` and
       *         ``resonance()``. */
      buffer& operator()(buffer& in, buffer& cutoffSignal, buffer& resonanceSignal);

    private:
      void Update();
      Flt Step(Flt in);

      buffer output;

      Flt freq = 0.f;
      Flt res = 0.f;
      Dbl y1 = 0.0;
      Dbl y2 = 0.0;

      // The coefficients and what they were derived from.
      coefficients c;
      Flt cachedFreq = 0.f;
      Flt cachedRes = 0.f;
      UInt cachedRate = 0;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_FILTERS_LORES_HPP
