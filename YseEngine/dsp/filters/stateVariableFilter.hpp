/*
  ==============================================================================

    stateVariableFilter.hpp
    A state-variable filter with simultaneous lowpass, highpass, bandpass and
    notch outputs, and Hz, linear and radians frequency modes (issue #1089).

  ==============================================================================
*/

#ifndef YSE_DSP_FILTERS_STATEVARIABLEFILTER_HPP
#define YSE_DSP_FILTERS_STATEVARIABLEFILTER_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Two-pole state-variable filter producing a lowpass, a highpass,
     *         a bandpass and a notch output at once, from one center
     *         frequency and one resonance.
     *
     *  Ported from Max's ``svf~``, which implements Hal Chamberlin's
     *  state-variable filter ("Musical Applications of Microprocessors").
     *  This class computes the same four responses with the topology-
     *  preserving (zero-delay-feedback) form of that filter, after Vadim
     *  Zavalishin ("The Art of VA Filter Design", 2012) in Andrew Simper's
     *  formulation ("Linear Trapezoidal Integrated State Variable Filter",
     *  Cytomic, 2013). Chamberlin's form drifts from the intended frequency
     *  and goes unstable as the cutoff rises with little resonance; this one
     *  is stable for every setting the class accepts and puts the cutoff
     *  exactly where it is asked (the bilinear transform of the analog
     *  prototype), so the response near the top of the range differs from
     *  Max's slightly.
     *
     *  With ``g = tan(pi * fc / samplerate)`` and the damping
     *  ``k = 2 * (1 - resonance)`` (at least ``MIN_DAMPING``), each sample
     *  computes ``a1 = 1 / (1 + g * (g + k))``, ``a2 = g * a1``,
     *  ``a3 = g * a2`` and
     *
     *  - ``v3 = x - s2``, ``v1 = a1 * s1 + a2 * v3``,
     *    ``v2 = s2 + a2 * s1 + a3 * v3``;
     *  - ``s1 = 2 * v1 - s1``, ``s2 = 2 * v2 - s2``;
     *  - lowpass ``v2``, bandpass ``v1``, highpass ``x - k * v1 - v2``,
     *    notch ``x - k * v1`` (lowpass plus highpass).
     *
     *  The transfer functions are those of the analog filter
     *  ``s^2 + k s + 1`` at the warped frequency: the lowpass and highpass
     *  have unit gain in their passbands and ``1 / k`` (the Q) at the center
     *  frequency, the bandpass ``1 / k`` at the center, the notch 0 there.
     *
     *  The frequency is read as ``mode()`` says, always within 0 .. a quarter
     *  of the sample rate, the range Max's object takes:
     *
     *  - ``HZ`` (the default): the center frequency in Hz, clamped to that
     *    range.
     *  - ``LINEAR``: 0..1 spanning that range linearly, so ``v`` is
     *    ``v * samplerate / 4`` Hz.
     *  - ``RADIANS``: 0..1 read as ``sin(2 * pi * fc / samplerate)``, the
     *    frequency coefficient ``~onepole`` uses in the same mode, so ``v``
     *    is ``asin(v) * samplerate / (2 * pi)`` Hz.
     *
     *  The resonance runs 0..1 (clamped), higher being sharper, as Max's
     *  ``svf~`` takes it; Max does not give its mapping, so this one is the
     *  class's own: 0 is the damping 2 (a Q of 0.5, no peak), 0.5 the
     *  damping 1 (a Q of 1), and 1 the damping ``MIN_DAMPING`` (a Q of
     *  1000), which still decays. A frequency of 0 makes the lowpass and the
     *  bandpass silent and passes the input on the highpass and the notch:
     *  the integrators would otherwise freeze there, so reaching it sets the
     *  state to 0. A setting that is not a finite number is ignored.
     *
     *  The settings are held as given and the coefficients follow the live
     *  sample rate: they are derived again whenever the frequency, the
     *  resonance, the mode or ``SAMPLERATE`` changes. The three-buffer call
     *  reads the frequency and the resonance at signal rate, one value per
     *  sample, deriving the coefficients again only on the samples where one
     *  of them changed; the getters keep the last finite values read. An
     *  input sample that is not a finite number is skipped (the outputs and
     *  state hold), so one stray NaN cannot latch the filter. ``clear()``
     *  sets the state to 0. Coefficients and state are double precision.
     *
     *  Each block call fills all four outputs (``lowpass()``, ``highpass()``,
     *  ``bandpass()``, ``notch()``) and returns the lowpass. Each call
     *  processes at most ``STANDARD_BUFFERSIZE`` samples. Call it from one
     *  thread at a time. Processing allocates nothing: the outputs are sized
     *  to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API stateVariableFilter {
    public:
      /** @brief How ``frequency()`` is read; see the class description. */
      enum FREQUENCY_MODE {
        HZ = 0,
        LINEAR = 1,
        RADIANS = 2,
      };

      /** @brief The damping at a resonance of 1, the smallest used. */
      static constexpr Dbl MIN_DAMPING = 0.001;

      /** @brief The filter coefficients, as the class description defines
       *         them. */
      struct coefficients {
        Dbl g = 0.0;
        Dbl k = 2.0;
        Dbl a1 = 1.0;
        Dbl a2 = 0.0;
        Dbl a3 = 0.0;
      };

      /** @brief The four outputs for one sample. */
      struct outputs {
        Flt lowpass = 0.f;
        Flt highpass = 0.f;
        Flt bandpass = 0.f;
        Flt notch = 0.f;
      };

      stateVariableFilter();

      /** @brief The center frequency, read as ``mode()`` says. Non-finite:
       *         ignored. */
      stateVariableFilter& frequency(Flt value);

      /** @brief The resonance, 0..1. Non-finite: ignored. */
      stateVariableFilter& resonance(Flt value);

      /** @brief How the frequency is read. */
      stateVariableFilter& mode(FREQUENCY_MODE m);

      /** @brief Sets the filter state to 0. */
      stateVariableFilter& clear();

      /** @brief The frequency as last set. */
      Flt frequency() const {
        return freq;
      }

      /** @brief The resonance as last set. */
      Flt resonance() const {
        return res;
      }

      /** @brief The frequency mode. */
      FREQUENCY_MODE mode() const {
        return freqMode;
      }

      /** @brief The coefficients for the current settings at the live
       *         sample rate. */
      coefficients coefficient();

      /** @brief The center frequency in Hz that @p value means in @p mode at
       *         @p sampleRate, clamped to 0 .. a quarter of it. */
      static Dbl CenterFrequency(Flt value, FREQUENCY_MODE mode, Flt sampleRate);

      /** @brief The coefficients for @p value read in @p mode and
       *         @p resonance at @p sampleRate (the class description gives
       *         the formulas and clamping). */
      static coefficients Coefficients(Flt value, FREQUENCY_MODE mode, Flt resonance,
                                       Flt sampleRate);

      /** @brief One sample. */
      outputs process(Flt in);

      /** @brief One block at the current settings; fills all four outputs
       *         and returns the lowpass. */
      buffer& operator()(buffer& in);

      /** @brief One block with the frequency and the resonance read per
       *         sample from @p frequencySignal and @p resonanceSignal (each at
       *         least as long as @p in), which also set the getters; fills all
       *         four outputs and returns the lowpass. */
      buffer& operator()(buffer& in, buffer& frequencySignal, buffer& resonanceSignal);

      /** @brief The lowpass output of the last block call. */
      buffer& lowpass() {
        return low;
      }

      /** @brief The highpass output of the last block call. */
      buffer& highpass() {
        return high;
      }

      /** @brief The bandpass output of the last block call. */
      buffer& bandpass() {
        return band;
      }

      /** @brief The notch (band-reject) output of the last block call. */
      buffer& notch() {
        return reject;
      }

    private:
      void Update();
      void Step(Flt in);
      void Fit(UInt length);

      buffer low;
      buffer high;
      buffer band;
      buffer reject;

      Flt freq = 0.f;
      Flt res = 0.f;
      FREQUENCY_MODE freqMode = HZ;

      Dbl s1 = 0.0;
      Dbl s2 = 0.0;
      outputs last;

      // The coefficients and what they were derived from.
      coefficients c;
      Flt cachedFreq = 0.f;
      Flt cachedRes = 0.f;
      FREQUENCY_MODE cachedMode = HZ;
      UInt cachedRate = 0;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_FILTERS_STATEVARIABLEFILTER_HPP
