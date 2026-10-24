/*
  ==============================================================================

    onePole.hpp
    A single-pole lowpass filter with Hz, linear and radians frequency modes
    (issue #1086).

  ==============================================================================
*/

#ifndef YSE_DSP_FILTERS_ONEPOLE_HPP
#define YSE_DSP_FILTERS_ONEPOLE_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Single-pole lowpass filter, 6 dB per octave: the cheapest IIR
     *         filter, for gently rolling off harsh highs or smoothing a
     *         control signal.
     *
     *  Ported from Max's ``onepole~``. Each sample computes
     *
     *  ``y(n) = y(n-1) + a0 * (x(n) - y(n-1))``
     *
     *  so a lower ``a0`` smooths more. ``a0`` comes from the frequency, read
     *  as ``mode()`` says:
     *
     *  - ``HZ`` (the default): a cutoff in Hz, clamped to 0 .. a quarter of
     *    the sample rate; ``a0 = sin(2 * pi * f / samplerate)``, so a quarter
     *    of the sample rate gives ``a0 = 1`` and passes the input unchanged.
     *  - ``LINEAR``: 0..1 spanning that same range linearly in frequency, so
     *    ``v`` is the cutoff ``v * samplerate / 4`` and
     *    ``a0 = sin(v * pi / 2)``.
     *  - ``RADIANS``: 0..1 sets ``a0`` itself, a curve in frequency that rises
     *    slowly at first, closer to how pitch is heard.
     *
     *  Values outside 0..1 (or outside the Hz range) are clamped; a
     *  frequency that is not a finite number is ignored. Unlike
     *  ``lowPass``, whose coefficient ``2 * pi * f / samplerate`` is a
     *  low-frequency approximation that saturates near a sixth of the sample
     *  rate, the sine keeps the cutoff meaningful up to a quarter of it.
     *
     *  The frequency is held as given and the coefficient follows the live
     *  sample rate: it is derived again whenever the frequency, the mode or
     *  ``SAMPLERATE`` changes. The two-buffer call takes the frequency from
     *  the first sample of its frequency buffer, once per block, as a signal
     *  frequency is read there; ``frequency()`` keeps that value. An input
     *  sample that is not a finite number is skipped (the output holds), so
     *  one stray NaN cannot latch the filter. ``clear()`` sets the state to 0.
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples. Call it
     *  from one thread at a time. Processing allocates nothing: the output is
     *  sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API onePole {
    public:
      /** @brief How ``frequency()`` is read; see the class description. */
      enum FREQUENCY_MODE {
        HZ = 0,
        LINEAR = 1,
        RADIANS = 2,
      };

      onePole();

      /** @brief The cutoff, read as ``mode()`` says. Non-finite: ignored. */
      onePole& frequency(Flt value);

      /** @brief How the frequency is read. */
      onePole& mode(FREQUENCY_MODE m);

      /** @brief Sets the filter state (the previous output) to 0. */
      onePole& clear();

      /** @brief The frequency as last set. */
      Flt frequency() const {
        return freq;
      }

      /** @brief The frequency mode. */
      FREQUENCY_MODE mode() const {
        return freqMode;
      }

      /** @brief ``a0`` for the current frequency and mode at the live
       *         sample rate. */
      Flt coefficient();

      /** @brief ``a0`` for ``value`` read in ``mode`` at ``sampleRate``
       *         (the class description gives the formulas). */
      static Flt Coefficient(Flt value, FREQUENCY_MODE mode, Flt sampleRate);

      /** @brief One sample. */
      Flt process(Flt in);

      /** @brief One block at the current frequency. */
      buffer& operator()(buffer& in);

      /** @brief One block with the frequency taken from the first sample of
       *         ``frequency``, which also sets ``frequency()``. */
      buffer& operator()(buffer& in, buffer& frequency);

    private:
      Flt Step(Flt in, Flt a0);

      buffer output;

      Flt freq = 0.f;
      FREQUENCY_MODE freqMode = HZ;
      Flt last = 0.f;

      // The coefficient and what it was derived from.
      Flt a0 = 0.f;
      Flt cachedFreq = 0.f;
      FREQUENCY_MODE cachedMode = HZ;
      UInt cachedRate = 0;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_FILTERS_ONEPOLE_HPP
