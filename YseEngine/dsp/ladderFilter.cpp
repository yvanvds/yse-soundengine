/*
  ==============================================================================

    ladderFilter.cpp
    Implementation of the ZDF Moog-style ladder low-pass — see ladderFilter.hpp
    and issue #175.

  ==============================================================================
*/

#include "ladderFilter.hpp"
#include "../headers/constants.hpp"

#include <cmath>

namespace YSE {
  namespace DSP {

    // Feedback gain at maximum resonance. The normalised trapezoidal ladder
    // reaches its self-oscillation threshold at a loop feedback of 4; a small
    // margin above guarantees the filter actually sings at resonance == 1.
    static const Flt kMaxFeedback = 4.2f;

    // Cutoff bounds. The lower bound keeps the prewarp well away from DC; the
    // upper bound is a safe fraction of Nyquist so tan() never explodes.
    static const Flt kMinCutoff = 20.f;

    ladderFilter::ladderFilter()
      : cutoffHz(1000.f),
        resonance(0.f),
        gTarget(0.f),
        gCur(0.f),
        smoothCoef(0.f),
        s1(0.f),
        s2(0.f),
        s3(0.f),
        s4(0.f) {
      computeTargetG();
      gCur = gTarget;
      // ~1 ms coefficient glide at the engine sample rate — fast enough to
      // track played sweeps, slow enough to suppress zipper noise.
      smoothCoef = 1.f - std::exp(-1.f / (0.001f * static_cast<Flt>(SAMPLERATE)));
    }

    void ladderFilter::computeTargetG() {
      const Flt sr = static_cast<Flt>(SAMPLERATE);
      Flt fc = cutoffHz;
      const Flt maxCutoff = 0.45f * sr;
      if (fc < kMinCutoff) fc = kMinCutoff;
      if (fc > maxCutoff) fc = maxCutoff;
      // Prewarped one-pole coefficient g = tan(pi * fc / fs).
      gTarget = std::tan(3.14159265358979323846f * fc / sr);
    }

    void ladderFilter::setCutoff(Flt hz) {
      cutoffHz = hz;
      computeTargetG();
    }

    void ladderFilter::setResonance(Flt r) {
      if (r < 0.f) r = 0.f;
      if (r > 1.f) r = 1.f;
      resonance = r;
    }

    void ladderFilter::reset() {
      s1 = s2 = s3 = s4 = 0.f;
      computeTargetG();
      gCur = gTarget;
    }

    Flt ladderFilter::process(Flt x) {
      // Glide the coefficient toward its target for click-free cutoff sweeps.
      gCur += (gTarget - gCur) * smoothCoef;

      const Flt G = gCur / (1.f + gCur);
      const Flt k = resonance * kMaxFeedback;

      // Zero-delay feedback resolution. Each TPT one-pole stage outputs
      //   y_i = G * (in_i - s_i) + s_i = G * in_i + (1 - G) * s_i
      // so the 4th stage output as a function of the (unknown) ladder input u
      // is  y4 = G^4 * u + Y4state,  and with feedback u = x - k * y4 we get
      //   u = (x - k * Y4state) / (1 + k * G^4).
      const Flt b1 = (1.f - G) * s1;
      const Flt b2 = (1.f - G) * s2;
      const Flt b3 = (1.f - G) * s3;
      const Flt b4 = (1.f - G) * s4;
      const Flt G2 = G * G;
      const Flt G3 = G2 * G;
      const Flt G4 = G3 * G;
      const Flt Y4state = G3 * b1 + G2 * b2 + G * b3 + b4;

      Flt u = (x - k * Y4state) / (1.f + k * G4);
      // Saturating nonlinearity in the loop bounds the self-oscillation
      // amplitude and keeps the filter stable at extreme resonance.
      u = std::tanh(u);

      // Forward evaluation of the four stages, updating the integrator states.
      const Flt v1 = G * (u - s1);
      const Flt y1 = v1 + s1;
      s1 = y1 + v1;
      const Flt v2 = G * (y1 - s2);
      const Flt y2 = v2 + s2;
      s2 = y2 + v2;
      const Flt v3 = G * (y2 - s3);
      const Flt y3 = v3 + s3;
      s3 = y3 + v3;
      const Flt v4 = G * (y3 - s4);
      const Flt y4 = v4 + s4;
      s4 = y4 + v4;

      return y4;
    }

    buffer& ladderFilter::operator()(buffer& in) {
      Flt* ptr = in.getPtr();
      const UInt n = in.getLength();
      for (UInt i = 0; i < n; i++) {
        ptr[i] = process(ptr[i]);
      }
      return in;
    }

  } // namespace DSP
} // namespace YSE
