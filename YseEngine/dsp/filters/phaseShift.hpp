/*
  ==============================================================================

    phaseShift.hpp
    A second-order allpass with a center frequency and a Q, both settable at
    signal rate (issue #1098).

  ==============================================================================
*/

#ifndef YSE_DSP_FILTERS_PHASESHIFT_HPP
#define YSE_DSP_FILTERS_PHASESHIFT_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"
#include "filterCoefficients.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Second-order allpass with a center frequency in Hz and a Q:
     *         phaser stages and phase distortion.
     *
     *  Ported from Max's ``phaseshift~``. The amplitude response is flat;
     *  only the phase changes with frequency, from 0 at 0 Hz through -180
     *  degrees at the center frequency to -360 degrees at half the sample
     *  rate. The higher the Q, the narrower the band over which the phase
     *  turns. On its own the effect is mostly inaudible; mixed with the dry
     *  signal it cancels the center frequency, and a cascade of stages so
     *  mixed is a phaser.
     *
     *  The coefficients are those of ``filterCoefficients::ALLPASS`` (the
     *  audio EQ cookbook allpass), computed by
     *  ``filterCoefficients::ComputePrecise`` with the same clamping: the
     *  frequency just inside 0 .. half the sample rate, the Q to ``MIN_Q``
     *  .. ``MAX_Q``. With ``b1`` and ``b2`` its feedback coefficients the
     *  filter is
     *
     *  ``y(n) = b2 x(n) + b1 x(n-1) + x(n-2) - b1 y(n-1) - b2 y(n-2)``
     *
     *  The feedforward coefficients are the feedback ones mirrored, by
     *  construction, so the gain is exactly 1 at every frequency whatever
     *  the rounding of ``b1`` and ``b2``. The coefficients and the state are
     *  kept in double precision (direct form I, whose state is the input
     *  and output themselves, so a coefficient changing per sample stores
     *  no stale products), and the poles stay inside the unit circle at a
     *  high Q near 0 Hz, where a float would put them on it.
     *
     *  Max's reference gives no defaults; these are 1000 Hz and a Q of 1. A
     *  setting that is not a finite number is ignored. The settings are held
     *  as given and the coefficients follow the live sample rate: they are
     *  derived again whenever the frequency, the Q or ``SAMPLERATE`` changes.
     *  The three-buffer call reads the frequency and the Q at signal rate,
     *  one value per sample, deriving the coefficients again only on the
     *  samples where one of them changed; the getters keep the last finite
     *  values read. An input sample that is not a finite number is skipped
     *  (the output and state hold), so one stray NaN cannot latch the filter;
     *  an output that is not finite starts the filter again from silence.
     *  ``clear()`` sets the state to 0.
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples. Call it
     *  from one thread at a time. Processing allocates nothing: the output is
     *  sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API phaseShift {
    public:
      /** @brief The lowest Q; smaller values are clamped to it. */
      static constexpr Flt MIN_Q = filterCoefficients::MIN_Q;

      /** @brief The highest Q; larger values are clamped to it. */
      static constexpr Flt MAX_Q = filterCoefficients::MAX_Q;

      /** @brief The center frequency until one is set, in Hz. */
      static constexpr Flt DEFAULT_FREQUENCY = 1000.f;

      /** @brief The Q until one is set. */
      static constexpr Flt DEFAULT_Q = 1.f;

      /** @brief The two feedback coefficients; the feedforward ones are
       *         ``b2 b1 1``. */
      struct coefficients {
        Dbl b1 = 0.0;
        Dbl b2 = 0.0;
      };

      phaseShift();

      /** @brief The center frequency in Hz, where the phase is -180
       *         degrees. Non-finite: ignored. */
      phaseShift& frequency(Flt hz);

      /** @brief The Q: the higher, the narrower the band over which the
       *         phase turns. Non-finite: ignored. */
      phaseShift& q(Flt value);

      /** @brief Sets the filter state to 0. */
      phaseShift& clear();

      /** @brief The center frequency as last set or read. */
      Flt frequency() const {
        return freq;
      }

      /** @brief The Q as last set or read. */
      Flt q() const {
        return quality;
      }

      /** @brief The coefficients for the current settings at the live
       *         sample rate. */
      coefficients coefficient();

      /** @brief The coefficients for @p hz and @p q at @p sampleRate:
       *         ``b1`` and ``b2`` of ``filterCoefficients::ComputePrecise`` with
       *         ``ALLPASS``. A non-finite setting, or a sample rate that is
       *         not above 0, gives 0 for both (a pure two-sample delay). */
      static coefficients Coefficients(Flt hz, Flt q, Flt sampleRate);

      /** @brief One sample. */
      Flt process(Flt in);

      /** @brief One block at the current settings. */
      buffer& operator()(buffer& in);

      /** @brief One block with the frequency and the Q read per sample from
       *         @p frequencySignal and @p qSignal; as long as the shortest.
       *         They also set the getters. */
      buffer& operator()(buffer& in, buffer& frequencySignal, buffer& qSignal);

    private:
      void Update();
      Flt Step(Flt in);

      buffer output;

      Flt freq = DEFAULT_FREQUENCY;
      Flt quality = DEFAULT_Q;
      Dbl x1 = 0.0;
      Dbl x2 = 0.0;
      Dbl y1 = 0.0;
      Dbl y2 = 0.0;

      // The coefficients and what they were derived from.
      coefficients c;
      Bool valid = false;
      Flt cachedFreq = 0.f;
      Flt cachedQ = 0.f;
      UInt cachedRate = 0;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_FILTERS_PHASESHIFT_HPP
