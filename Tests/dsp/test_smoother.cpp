// Tests for the shared one-pole smoother primitives in YseEngine/dsp/smoother.hpp
// (issue #614). These are pure computations with no audio-device or system
// dependencies.
//
// The suite has two jobs:
//   1. Pin the maths of onePoleCoef / onePoleSmooth.
//   2. Guard the refactor: every call site that was converted to the helper is
//      re-derived here from its *pre-refactor* expression and checked to be
//      bit-identical, so the shared version can never silently drift from the
//      six hand-rolled copies it replaced.

#include <doctest/doctest.h>
#include <cmath>
#include "dsp/smoother.hpp"

TEST_SUITE("dsp") {

  TEST_CASE("onePoleCoef: matches 1 - exp(-1 / (tau * sr)) bit for bit") {
    const Flt sr = 44100.0f;
    for (Flt tau : {0.001f, 0.005f, 0.01f, 0.03f, 0.1f, 2.0f}) {
      const Flt expected = 1.0f - std::exp(-1.0f / (tau * sr));
      CHECK(YSE::DSP::onePoleCoef(tau, sr) == expected);
    }
  }

  TEST_CASE("onePoleCoef: floors the time constant at one sample") {
    const Flt sr = 44100.0f;
    const Flt oneSample = 1.0f - std::exp(-1.0f);

    // Exactly one sample, and everything shorter, collapses to the same
    // fastest-stable coefficient rather than overshooting past 1.
    CHECK(YSE::DSP::onePoleCoef(1.0f / sr, sr) == doctest::Approx(oneSample).epsilon(1e-6f));
    CHECK(YSE::DSP::onePoleCoef(0.0f, sr) == oneSample);
    CHECK(YSE::DSP::onePoleCoef(-1.0f, sr) == oneSample);
  }

  TEST_CASE("onePoleCoef: stays inside (0, 1] for every sane input") {
    for (Flt sr : {8000.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f}) {
      for (Flt tau : {0.0f, 1e-6f, 0.001f, 0.05f, 1.0f, 60.0f}) {
        const Flt c = YSE::DSP::onePoleCoef(tau, sr);
        CHECK(c > 0.0f);
        CHECK(c <= 1.0f);
      }
    }
  }

  TEST_CASE("onePoleCoef: longer time constants give smaller coefficients") {
    const Flt sr = 48000.0f;
    CHECK(YSE::DSP::onePoleCoef(0.001f, sr) > YSE::DSP::onePoleCoef(0.01f, sr));
    CHECK(YSE::DSP::onePoleCoef(0.01f, sr) > YSE::DSP::onePoleCoef(0.1f, sr));
  }

  TEST_CASE("onePoleSmooth: is exactly the y += (x - y) * coef idiom") {
    const Flt cases[][3] = {
        {0.0f, 1.0f, 0.25f}, {1.0f, 0.0f, 0.5f},  {-3.5f, 7.25f, 0.001f},
        {2.0f, 2.0f, 0.75f}, {0.1f, -0.2f, 1.0f},
    };
    for (const auto& c : cases) {
      Flt legacy = c[0];
      legacy += (c[1] - legacy) * c[2];
      CHECK(YSE::DSP::onePoleSmooth(c[0], c[1], c[2]) == legacy);
    }
  }

  TEST_CASE("onePoleSmooth: coef 0 holds, coef 1 snaps") {
    CHECK(YSE::DSP::onePoleSmooth(0.25f, 1.0f, 0.0f) == 0.25f);
    CHECK(YSE::DSP::onePoleSmooth(0.25f, 1.0f, 1.0f) == 1.0f);
  }

  TEST_CASE("onePoleSmooth: a unit step reaches 63% after tau seconds") {
    const Flt sr = 44100.0f;
    const Flt tau = 0.01f;
    const Flt coef = YSE::DSP::onePoleCoef(tau, sr);

    Flt y = 0.0f;
    const int steps = static_cast<int>(tau * sr);
    for (int i = 0; i < steps; ++i)
      y = YSE::DSP::onePoleSmooth(y, 1.0f, coef);

    CHECK(y == doctest::Approx(0.632f).epsilon(0.01f));
  }

  TEST_CASE("onePoleSmooth: converges monotonically without overshoot") {
    const Flt coef = YSE::DSP::onePoleCoef(0.005f, 44100.0f);
    Flt y = 0.0f;
    for (int i = 0; i < 44100; ++i) {
      const Flt next = YSE::DSP::onePoleSmooth(y, 1.0f, coef);
      CHECK(next >= y);
      CHECK(next <= 1.0f);
      y = next;
    }
    CHECK(y == doctest::Approx(1.0f).epsilon(1e-4f));
  }

  // ── Refactor guards (#614) ─────────────────────────────────────────────────
  // Each of these reproduces the exact expression the call site used before it
  // was converted, and requires the helper to return the identical float.

  TEST_CASE("onePoleCoef: reproduces ladderFilter's previously unclamped form") {
    // Pre-refactor: 1.f - std::exp(-1.f / (0.001f * SAMPLERATE)).
    // The helper adds a one-sample floor; it is inert for every sample rate at
    // or above 1 kHz, so the result must be bit-identical.
    for (Flt sr : {8000.0f, 22050.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f}) {
      INFO("sample rate ", sr);
      const Flt legacy = 1.0f - std::exp(-1.0f / (0.001f * sr));
      CHECK(YSE::DSP::onePoleCoef(0.001f, sr) == legacy);
    }
  }

  TEST_CASE("onePoleCoef: reproduces the clamped tau form of the other call sites") {
    // Pre-refactor, identically in chorus (5 ms), feedbackDelay (30 ms) and
    // compressor's file-local timeCoef (RMS window + the 0.1 ms .. 2000 ms
    // attack/release range): tau * sr, clamp to >= 1, 1 - exp(-1/tau).
    for (Flt sr : {8000.0f, 22050.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f}) {
      for (Flt tau : {0.005f, 0.03f, 0.01f, 0.0001f, 0.002f, 0.1f, 2.0f}) {
        INFO("sample rate ", sr, ", tau ", tau);
        Flt samples = tau * sr;
        if (samples < 1.0f) samples = 1.0f;
        const Flt legacy = 1.0f - std::exp(-1.0f / samples);
        CHECK(YSE::DSP::onePoleCoef(tau, sr) == legacy);
      }
    }
  }

  TEST_CASE("onePoleCoef: is NOT interchangeable with the cut-off form") {
    // plateReverb derives its damping coefficient from a cut-off in Hz:
    //   1 - exp(-2*pi*fc/sr)
    // Re-expressing that as a time constant tau = 1/(2*pi*fc) and pushing it
    // through onePoleCoef would hit the one-sample floor above sr / 2*pi and
    // audibly change the reverb. This test pins that difference so nobody
    // "finishes" the refactor by folding plateReverb in.
    const Flt sr = 44100.0f;
    const Flt twoPi = 6.283185307179586f;

    const Flt lowFc = 2000.0f; // below sr / 2*pi (~7019 Hz): forms agree
    CHECK(1.0f - std::exp(-twoPi * lowFc / sr) ==
          doctest::Approx(YSE::DSP::onePoleCoef(1.0f / (twoPi * lowFc), sr)).epsilon(1e-5f));

    const Flt highFc = 16000.0f; // above the floor: forms diverge sharply
    const Flt cutoffForm = 1.0f - std::exp(-twoPi * highFc / sr);
    const Flt tauForm = YSE::DSP::onePoleCoef(1.0f / (twoPi * highFc), sr);
    CHECK(cutoffForm > 0.85f);
    CHECK(tauForm == doctest::Approx(1.0f - std::exp(-1.0f)).epsilon(1e-5f));
    CHECK(cutoffForm - tauForm > 0.2f);
  }

} // TEST_SUITE("dsp")
