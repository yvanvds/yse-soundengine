/*
  ==============================================================================

    reson.hpp
    A resonant bandpass filter with a gain, a center frequency and a Q, all
    settable at signal rate (issue #1088).

  ==============================================================================
*/

#ifndef YSE_DSP_FILTERS_RESON_HPP
#define YSE_DSP_FILTERS_RESON_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Resonant bandpass filter with a gain, a center frequency in Hz
     *         and a Q (center frequency over bandwidth): formants, modal
     *         synthesis, resonators.
     *
     *  Ported from Max's ``reson~``, whose documented equation it follows:
     *
     *  ``y(n) = a0 * (x(n) - r * x(n-2)) + b1 * y(n-1) + b2 * y(n-2)``
     *
     *  Two poles at radius ``r`` and the angle of the center frequency, and
     *  two zeros at ``+-sqrt(r)``: the constant-gain resonator of Smith and
     *  Angell ("A Constant-Gain Digital Resonator Tuned by a Single
     *  Coefficient", Computer Music Journal 6(4), 1982). The reference does
     *  not give the coefficients; these are that design's:
     *
     *  - ``bandwidth = cf / Q`` and ``r = exp(-pi * bandwidth / samplerate)``,
     *    the pole radius for that -3 dB bandwidth;
     *  - ``b1 = 2 * r * cos(2 * pi * cf / samplerate)``, ``b2 = -r^2``;
     *  - ``a0 = gain * (1 - r)``.
     *
     *  With these the gain at the center frequency is exactly ``gain``,
     *  whatever the center frequency and the Q, so sweeping either does not
     *  change the loudness of the resonance. Unlike ``~bp`` (Pure Data's
     *  ``bp~``, two poles only) the zeros pull the response down towards
     *  0 Hz and the Nyquist frequency; at 0 Hz the gain is
     *  ``gain * (1 - r)^2 / (1 - b1 - b2)``.
     *
     *  The center frequency is clamped to 0 .. half the sample rate and the Q
     *  to ``MIN_Q`` .. ``MAX_Q`` (0.01 .. 10000); a center frequency of 0
     *  passes nothing, as does one so low against the Q that ``r`` rounds to
     *  1. Otherwise ``r`` is below 1 and the filter is stable; the
     *  coefficients and state are kept in double precision so that holds at
     *  a high Q. The gain is any finite number, including a negative one. A
     *  setting that is not a finite number is ignored.
     *
     *  The settings are held as given and the coefficients follow the live
     *  sample rate: ``r``, ``b1`` and ``b2`` are derived again whenever the
     *  center frequency, the Q or ``SAMPLERATE`` changes, ``a0`` whenever the
     *  gain does too. The four-buffer call reads all three settings at signal
     *  rate, one value per sample, deriving the coefficients again only on
     *  the samples where one of them changed; the getters keep the last
     *  finite values read. An input sample that is not a finite number is
     *  skipped (the output and state hold), so one stray NaN cannot latch
     *  the filter. ``clear()`` sets the state to 0.
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples. Call it
     *  from one thread at a time. Processing allocates nothing: the output is
     *  sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API reson {
    public:
      /** @brief The lowest Q; smaller values are clamped to it. */
      static constexpr Flt MIN_Q = 0.01f;

      /** @brief The highest Q; larger values are clamped to it. */
      static constexpr Flt MAX_Q = 10000.f;

      /** @brief The filter coefficients, as the class description defines
       *         them. */
      struct coefficients {
        Dbl a0 = 0.0;
        Dbl r = 0.0;
        Dbl b1 = 0.0;
        Dbl b2 = 0.0;
      };

      reson();

      /** @brief The gain at the center frequency. Non-finite: ignored. */
      reson& gain(Flt value);

      /** @brief The center frequency in Hz. Non-finite: ignored. */
      reson& frequency(Flt hz);

      /** @brief The Q, center frequency over bandwidth. Non-finite:
       *         ignored. */
      reson& q(Flt value);

      /** @brief Sets the filter state (two inputs, two outputs) to 0. */
      reson& clear();

      /** @brief The gain as last set. */
      Flt gain() const {
        return amp;
      }

      /** @brief The center frequency as last set. */
      Flt frequency() const {
        return freq;
      }

      /** @brief The Q as last set. */
      Flt q() const {
        return quality;
      }

      /** @brief The coefficients for the current settings at the live
       *         sample rate. */
      coefficients coefficient();

      /** @brief The coefficients for @p gain, @p hz and @p q at
       *         @p sampleRate (the class description gives the formulas and
       *         clamping). */
      static coefficients Coefficients(Flt gain, Flt hz, Flt q, Flt sampleRate);

      /** @brief One sample. */
      Flt process(Flt in);

      /** @brief One block at the current settings. */
      buffer& operator()(buffer& in);

      /** @brief One block with the gain, the center frequency and the Q read
       *         per sample from @p gainSignal, @p frequencySignal and
       *         @p qSignal (each at least as long as @p in), which also set
       *         the getters. */
      buffer& operator()(buffer& in, buffer& gainSignal, buffer& frequencySignal, buffer& qSignal);

    private:
      void Update();
      Flt Step(Flt in);

      buffer output;

      Flt amp = 0.f;
      Flt freq = 0.f;
      Flt quality = MIN_Q;
      Dbl x1 = 0.0;
      Dbl x2 = 0.0;
      Dbl y1 = 0.0;
      Dbl y2 = 0.0;

      // The coefficients and what they were derived from.
      coefficients c;
      Flt cachedAmp = 0.f;
      Flt cachedFreq = 0.f;
      Flt cachedQ = MIN_Q;
      UInt cachedRate = 0;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_FILTERS_RESON_HPP
