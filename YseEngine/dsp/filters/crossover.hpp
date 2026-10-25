/*
  ==============================================================================

    crossover.hpp
    A third-order Butterworth crossover: a lowpass and a highpass at one
    cutoff that sum back to a flat response (issue #1095).

  ==============================================================================
*/

#ifndef YSE_DSP_FILTERS_CROSSOVER_HPP
#define YSE_DSP_FILTERS_CROSSOVER_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Third-order crossover: splits a signal into a lowpass and a
     *         highpass band at one cutoff, 18 dB per octave each, that add
     *         back up to a flat response.
     *
     *  Ported from Max's ``cross~``. Both halves are third-order Butterworth
     *  filters on the same analog denominator
     *  ``B(s) = (s + 1)(s^2 + s + 1)``: the lowpass is ``1 / B(s)``, the
     *  highpass ``s^3 / B(s)``. Their sum is
     *  ``(s^2 - s + 1) / (s^2 + s + 1)``, an allpass, so ``low + high`` has a
     *  gain of exactly 1 at every frequency and only its phase differs from
     *  the input. At the cutoff each band is 3 dB down.
     *
     *  Each half is mapped to the digital domain by the bilinear transform,
     *  prewarped so the cutoff lands where it is asked: with
     *  ``K = tan(pi * cutoff / samplerate)`` it runs a first-order section
     *  and then a second-order section,
     *
     *  - first order, denominator ``(K + 1) + (K - 1) z^-1``, numerator
     *    ``K (1 + z^-1)`` (lowpass) or ``1 - z^-1`` (highpass);
     *  - second order, denominator
     *    ``(1 + K + K^2) + 2 (K^2 - 1) z^-1 + (1 - K + K^2) z^-2``, numerator
     *    ``K^2 (1 + z^-1)^2`` (lowpass) or ``(1 - z^-1)^2`` (highpass).
     *
     *  The bilinear transform keeps the sum an allpass, so the bands still
     *  add up flat after it. ``Coefficients()`` gives the normalised values.
     *
     *  The cutoff is clamped to 0 .. ``MAX_CUTOFF_RATIO`` (0.49) times the
     *  sample rate, short of the Nyquist frequency where ``K`` is infinite. A
     *  cutoff of 0 passes nothing on the lowpass and the input unchanged on
     *  the highpass; reaching it sets the state to 0, as the highpass would
     *  otherwise keep an offset there. A setting that is not a finite number
     *  is ignored.
     *
     *  The cutoff is held as given and the coefficients follow the live
     *  sample rate: they are derived again whenever the cutoff or
     *  ``SAMPLERATE`` changes. The two-buffer call reads the cutoff at signal
     *  rate, one value per sample, deriving the coefficients again only on
     *  the samples where it changed; ``cutoff()`` keeps the last finite value
     *  read. An input sample that is not a finite number is skipped (the
     *  outputs and state hold), so one stray NaN cannot latch the filter.
     *  ``clear()`` sets the state to 0. Coefficients and state are double
     *  precision.
     *
     *  The class is meant to be cascaded: a multiband splitter feeds the
     *  ``highpass()`` of one crossover into the next. Each block call fills
     *  both outputs and returns the lowpass; each call processes at most
     *  ``STANDARD_BUFFERSIZE`` samples. Call it from one thread at a time.
     *  Processing allocates nothing: the outputs are sized to
     *  ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API crossover {
    public:
      /** @brief The default cutoff in Hz. */
      static constexpr Flt DEFAULT_CUTOFF = 1000.f;

      /** @brief The highest cutoff as a fraction of the sample rate. */
      static constexpr Dbl MAX_CUTOFF_RATIO = 0.49;

      /** @brief One section's coefficients, normalised so the leading
       *         denominator term is 1: ``b0 + b1 z^-1 + b2 z^-2`` over
       *         ``1 + a1 z^-1 + a2 z^-2`` (``b2`` and ``a2`` are 0 for the
       *         first-order section). */
      struct section {
        Dbl b0 = 0.0;
        Dbl b1 = 0.0;
        Dbl b2 = 0.0;
        Dbl a1 = 0.0;
        Dbl a2 = 0.0;
      };

      /** @brief The four sections, as the class description defines them. */
      struct coefficients {
        section low1;
        section low2;
        section high1;
        section high2;
      };

      /** @brief Both outputs for one sample. */
      struct outputs {
        Flt lowpass = 0.f;
        Flt highpass = 0.f;
      };

      crossover();

      /** @brief The cutoff in Hz. Non-finite: ignored. */
      crossover& cutoff(Flt hz);

      /** @brief Sets the filter state to 0. */
      crossover& clear();

      /** @brief The cutoff as last set. */
      Flt cutoff() const {
        return freq;
      }

      /** @brief The coefficients for the current cutoff at the live sample
       *         rate. */
      coefficients coefficient();

      /** @brief The coefficients for @p hz at @p sampleRate (the class
       *         description gives the formulas and clamping). */
      static coefficients Coefficients(Flt hz, Flt sampleRate);

      /** @brief One sample. */
      outputs process(Flt in);

      /** @brief One block at the current cutoff; fills both outputs and
       *         returns the lowpass. */
      buffer& operator()(buffer& in);

      /** @brief One block with the cutoff read per sample from
       *         @p cutoffSignal (at least as long as @p in), which also sets
       *         ``cutoff()``; fills both outputs and returns the lowpass. */
      buffer& operator()(buffer& in, buffer& cutoffSignal);

      /** @brief The lowpass output of the last block call. */
      buffer& lowpass() {
        return low;
      }

      /** @brief The highpass output of the last block call. */
      buffer& highpass() {
        return high;
      }

    private:
      // Transposed direct form II state of one section.
      struct state {
        Dbl s1 = 0.0;
        Dbl s2 = 0.0;
      };

      void Update();
      void Step(Flt in);
      void Fit(UInt length);
      static Dbl Run(const section& c, state& s, Dbl x);

      buffer low;
      buffer high;

      Flt freq = DEFAULT_CUTOFF;

      state low1;
      state low2;
      state high1;
      state high2;
      outputs last;

      // The coefficients and what they were derived from.
      coefficients c;
      Flt cachedFreq = 0.f;
      UInt cachedRate = 0;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_FILTERS_CROSSOVER_HPP
