/*
  ==============================================================================

    filterBank.hpp
    A bank of parallel bandpass filters on one input, each with its own
    frequency, Q, gain and output (issue #1096).

  ==============================================================================
*/

#ifndef YSE_DSP_FILTERS_FILTERBANK_HPP
#define YSE_DSP_FILTERS_FILTERBANK_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"
#include "filterCoefficients.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A bank of bandpass filters in parallel: one input feeds every
     *         filter, and each filter has its own center frequency, Q, gain
     *         and output. For vocoders, spectral shaping and resonator banks.
     *
     *  Ported from Max's ``fffb~`` (fast fixed filter bank). Each filter is
     *  the second-order bandpass of ``filterCoefficients`` (the audio EQ
     *  cookbook's constant 0 dB peak bandpass, type ``GAINBPASS``): with
     *  ``w = 2 pi f / samplerate`` and ``alpha = sin(w) / (2 Q)``,
     *
     *  ``H(z) = gain * alpha (1 - z^-2) / ((1 + alpha) - 2 cos(w) z^-1 + (1 - alpha) z^-2)``.
     *
     *  Its gain at the center frequency is exactly ``gain``, whatever the
     *  frequency and the Q, as for ``reson``, so a filter can be swept or
     *  narrowed without changing its loudness; it passes nothing at 0 Hz and
     *  at half the sample rate. The -3 dB bandwidth is ``f / Q``.
     *  ``filterCoefficients::Compute`` gives the coefficients, with its
     *  clamping: the frequency to just above 0 and just below half the
     *  sample rate, the Q to ``filterCoefficients::MIN_Q`` .. ``MAX_Q``; the
     *  gain is any finite number, including a negative one.
     *
     *  The number of filters is set in the constructor (default 4, clamped
     *  to 1 .. ``MAX_FILTERS``), which allocates the filters and their
     *  outputs; nothing allocates afterwards. A new bank starts with its
     *  filters spaced in octaves from ``DEFAULT_FREQUENCY`` (100, 200 and
     *  400 Hz and so on), a Q of ``DEFAULT_Q`` and a gain of 1.
     *
     *  The settings of one filter (indexed from 0) are set one at a time,
     *  for every filter at once (the ``All`` setters), or as a spread from a
     *  first frequency: ``frequencyRatio(first, ratio)`` gives filter ``i``
     *  the frequency ``first * ratio^i`` and ``harmonic(first)`` the
     *  frequency ``first * (i + 1)``. An index beyond the bank and a setting
     *  that is not a finite number are ignored, as is a ratio that is not
     *  above 0.
     *
     *  The settings are held as given and the coefficients follow the live
     *  sample rate: a filter's coefficients are derived again when one of
     *  its settings or ``SAMPLERATE`` changes, so constant settings cost
     *  nothing per block. The coefficients are those ``Compute`` gives, as
     *  floats; the state is kept in double precision (transposed direct form
     *  II). An input sample that is not a finite number is skipped (every
     *  output holds its last value and the state is kept), so one stray NaN
     *  cannot latch the bank; a filter whose state overflows starts again
     *  from silence. ``clear()`` sets every filter's state to 0.
     *
     *  Each block call processes at most ``STANDARD_BUFFERSIZE`` samples and
     *  fills every output. Call it from one thread at a time.
     */
    class API filterBank {
    public:
      /** @brief The number of filters of a default bank. */
      static constexpr UInt DEFAULT_FILTERS = 4;

      /** @brief The most filters a bank holds (as Max's ``fffb~``). */
      static constexpr UInt MAX_FILTERS = 128;

      /** @brief The frequency in Hz of the first filter of a new bank. */
      static constexpr Flt DEFAULT_FREQUENCY = 100.f;

      /** @brief The ratio between the frequencies of successive filters of a
       *         new bank. */
      static constexpr Flt DEFAULT_RATIO = 2.f;

      /** @brief The Q of every filter of a new bank. */
      static constexpr Flt DEFAULT_Q = 10.f;

      /** @brief The gain of every filter of a new bank. */
      static constexpr Flt DEFAULT_GAIN = 1.f;

      /** @brief The five coefficients of one filter, as ``biQuad`` takes
       *         them: ``y(n) = a0 x(n) + a1 x(n-1) + a2 x(n-2) - b1 y(n-1) - b2 y(n-2)``. */
      using coefficientSet = filterCoefficients::coefficientSet;

      /** @brief A bank of @p filters filters, clamped to
       *         1 .. ``MAX_FILTERS``. */
      explicit filterBank(UInt filters = DEFAULT_FILTERS);

      /** @brief The number of filters. */
      UInt size() const {
        return static_cast<UInt>(bank.size());
      }

      /** @brief The center frequency in Hz of filter @p index. */
      filterBank& frequency(UInt index, Flt hz);

      /** @brief The Q of filter @p index. */
      filterBank& q(UInt index, Flt value);

      /** @brief The gain of filter @p index at its center frequency. */
      filterBank& gain(UInt index, Flt value);

      /** @brief The center frequency of every filter. */
      filterBank& frequencyAll(Flt hz);

      /** @brief The Q of every filter. */
      filterBank& qAll(Flt value);

      /** @brief The gain of every filter. */
      filterBank& gainAll(Flt value);

      /** @brief Filter ``i`` at ``first * ratio^i`` Hz. A ratio that is not
       *         above 0 is ignored. */
      filterBank& frequencyRatio(Flt first, Flt ratio);

      /** @brief Filter ``i`` at ``first * (i + 1)`` Hz: a harmonic series. */
      filterBank& harmonic(Flt first);

      /** @brief Sets every filter's state to 0. */
      filterBank& clear();

      /** @brief The center frequency of filter @p index as last set (the
       *         last filter's for an index beyond the bank). */
      Flt frequency(UInt index) const;

      /** @brief The Q of filter @p index as last set. */
      Flt q(UInt index) const;

      /** @brief The gain of filter @p index as last set. */
      Flt gain(UInt index) const;

      /** @brief The coefficients of filter @p index at the live sample
       *         rate. */
      coefficientSet coefficients(UInt index) const;

      /** @brief The coefficients of one filter at @p hz with @p q and
       *         @p gain at @p sampleRate: ``filterCoefficients::Compute`` for
       *         ``GAINBPASS``. */
      static coefficientSet Coefficients(Flt hz, Flt q, Flt gain, Flt sampleRate);

      /** @brief The frequency ``frequencyRatio(first, ratio)`` gives filter
       *         @p index. */
      static Flt RatioFrequency(Flt first, Flt ratio, UInt index);

      /** @brief The frequency ``harmonic(first)`` gives filter @p index. */
      static Flt HarmonicFrequency(Flt first, UInt index);

      /** @brief One block of @p in through every filter; fills every
       *         output. */
      void operator()(buffer& in);

      /** @brief The output of filter @p index from the last block call (the
       *         last filter's for an index beyond the bank). */
      buffer& output(UInt index);

    private:
      struct filter {
        Flt freq = DEFAULT_FREQUENCY;
        Flt quality = DEFAULT_Q;
        Flt amp = DEFAULT_GAIN;
        Bool dirty = true;
        // Coefficients as Compute gives them, widened.
        Dbl a0 = 0.0;
        Dbl a1 = 0.0;
        Dbl a2 = 0.0;
        Dbl b1 = 0.0;
        Dbl b2 = 0.0;
        // Transposed direct form II state, and the last output.
        Dbl s1 = 0.0;
        Dbl s2 = 0.0;
        Flt last = 0.f;
      };

      void Update();
      UInt Index(UInt index) const;

      std::vector<filter> bank;
      std::vector<buffer> outputs;
      UInt cachedRate = 0;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_FILTERS_FILTERBANK_HPP
