/*
  ==============================================================================

    smoother.hpp
    Shared one-pole (exponential) smoother primitives — see issue #614.

  ==============================================================================
*/

#ifndef YSE_DSP_SMOOTHER_HPP
#define YSE_DSP_SMOOTHER_HPP

#include "../headers/types.hpp"

#include <cmath>

namespace YSE {
  namespace DSP {

    /**
     *  @brief One-pole coefficient for a *sample-clocked* smoother with a time
     *         constant in seconds.
     *
     *  Returns @f$ \alpha = 1 - e^{-1/(\tau f_s)} @f$: the fraction of the
     *  remaining distance a smoother should cover per sample so that it reaches
     *  63% of a step after @p tauSeconds. The exponential form (rather than the
     *  naive @f$ 1/(\tau f_s) @f$) keeps the time constant correct at short
     *  taus and can never produce a coefficient above 1, so the smoother stays
     *  stable at any sample rate.
     *
     *  @p tauSeconds is clamped to a floor of one sample: a time constant
     *  shorter than the sample period is not representable, and the clamp makes
     *  it degrade to the fastest stable smoother instead of overshooting.
     *
     *  Call this off the audio thread (construction, ``create()``) or at block
     *  rate — never per sample; it evaluates ``exp``. Pair it with
     *  ``onePoleSmooth`` on the audio thread.
     *
     *  @param tauSeconds Time constant in seconds.
     *  @param sampleRate Sample rate in Hz the smoother is clocked at.
     *
     *  @note Smoothers clocked by something other than the sample rate do not
     *        belong here: the patcher's ``.slide`` (#459) advances per *event*
     *        and takes its coefficient directly, and the device CPU-load EMA
     *        advances per audio *callback* with a variable dt. Damping filters
     *        parameterised by a cut-off in Hz — ``plateReverb`` — use
     *        @f$ 1 - e^{-2\pi f_c/f_s} @f$ instead; routing those through this
     *        function would apply the one-sample floor above
     *        @f$ f_s/2\pi @f$ (~7 kHz at 44.1 kHz) and audibly change them.
     */
    inline Flt onePoleCoef(Flt tauSeconds, Flt sampleRate) {
      Flt samples = tauSeconds * sampleRate;
      if (samples < 1.0f) samples = 1.0f;
      return 1.0f - std::exp(-1.0f / samples);
    }

    /**
     *  @brief Advance a one-pole smoother by one step.
     *
     *  @f$ y \mathrel{+}= (x - y) \cdot \alpha @f$ — move @p current a fraction
     *  @p coef of the way toward @p target. @p coef comes from
     *  ``onePoleCoef`` (or an equivalent derivation for a differently clocked
     *  smoother).
     *
     *  Audio-thread safe: branch-free, allocation-free, and ``inline`` so it
     *  compiles to the same two instructions the open-coded idiom does.
     */
    inline Flt onePoleSmooth(Flt current, Flt target, Flt coef) {
      return current + (target - current) * coef;
    }

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SMOOTHER_HPP
