// Tests for YSE::DSP::ladderFilter (issue #175) — the reusable Moog-style
// 4-pole resonant low-pass ladder.
//
// Coverage:
//   - low-pass frequency response (a tone above cutoff is attenuated more than
//     one below it),
//   - cutoff moves the corner (raising cutoff passes more of a fixed tone),
//   - resonance emphasises energy at the cutoff,
//   - self-oscillation at maximum resonance sits at the cutoff frequency
//     (FFT peak-bin check) and stays bounded (no NaN/blow-up),
//   - no heap allocation in process() after warm-up,
//   - the cutoff glide keeps its ~1 ms wall-clock time constant across a
//     sample-rate change (issue #634).
//
// No audio device required; SAMPLERATE is initialised to 48000 by the
// portaudioDeviceManager translation unit at static-initialisation time. The
// #634 cases force it to other rates temporarily via ScopedSampleRate below.

#include <doctest/doctest.h>
#include <cmath>
#include "dsp/ladderFilter.hpp"
#include "dsp/oscillators.hpp"
#include "dsp/buffer.hpp"
#include "dsp/fourier/fft.hpp"
#include "support/audio_helpers.hpp"
#include "support/alloc_probe.hpp"

TEST_SUITE("dsp") {

  using YSE::DSP::ladderFilter;

  // RMS of a steady sine of `freq` Hz after passing through a fresh ladder set
  // to `cutoff` Hz / `res` resonance. Warm-up blocks let the coefficient glide
  // and filter state settle before measuring.
  static float filteredRms(float cutoff, float res, float freq) {
    ladderFilter f;
    f.setCutoff(cutoff);
    f.setResonance(res);
    f.reset(); // snap the glide to the target cutoff
    f.setResonance(res);
    YSE::DSP::sine osc;
    const int warmBlocks = 40;
    const int measureBlocks = 40;
    double sumsq = 0.0;
    unsigned count = 0;
    for (int b = 0; b < warmBlocks + measureBlocks; ++b) {
      YSE::DSP::buffer blk = osc(freq); // copy of the oscillator block
      f(blk);
      if (b >= warmBlocks) {
        float* p = blk.getPtr();
        for (unsigned i = 0; i < blk.getLength(); ++i) {
          sumsq += static_cast<double>(p[i]) * p[i];
          ++count;
        }
      }
    }
    return count ? static_cast<float>(std::sqrt(sumsq / count)) : 0.f;
  }

  // ─── frequency response ─────────────────────────────────────────────────────

  TEST_CASE("ladderFilter: attenuates a tone above the cutoff") {
    const float cutoff = 500.f;
    float belowRms = filteredRms(cutoff, 0.f, 100.f); // well below cutoff
    float aboveRms = filteredRms(cutoff, 0.f, 5000.f); // decade above cutoff

    CHECK(belowRms > 0.2f); // pass-band tone survives
    CHECK(aboveRms < belowRms * 0.2f); // stop-band tone heavily attenuated
  }

  TEST_CASE("ladderFilter: raising the cutoff passes more of a fixed tone") {
    const float tone = 2000.f;
    float lowCutoff = filteredRms(500.f, 0.f, tone);
    float highCutoff = filteredRms(8000.f, 0.f, tone);
    CHECK(highCutoff > lowCutoff);
    CHECK(highCutoff > lowCutoff * 1.5f); // clearly more energy passes
  }

  TEST_CASE("ladderFilter: resonance emphasises energy at the cutoff") {
    const float cutoff = 1500.f;
    float lowRes = filteredRms(cutoff, 0.1f, cutoff);
    float highRes = filteredRms(cutoff, 0.85f, cutoff);
    CHECK(highRes > lowRes); // resonant peak boosts the tone at the corner
  }

  // ─── self-oscillation ───────────────────────────────────────────────────────

  TEST_CASE("ladderFilter: self-oscillates at the cutoff frequency") {
    const float cutoff = 1000.f;
    ladderFilter f;
    f.setCutoff(cutoff);
    f.reset();
    f.setResonance(1.f); // past the self-oscillation threshold

    // Kick the filter so the limit cycle can grow, then let it settle.
    f.process(1e-3f);
    for (int i = 0; i < 20000; ++i)
      f.process(0.f);

    const unsigned N = 4096;
    YSE::DSP::buffer re(N), im(N);
    re = 0.f;
    im = 0.f;
    float* rp = re.getPtr();
    for (unsigned i = 0; i < N; ++i)
      rp[i] = f.process(0.f);

    // Bounded — the loop nonlinearity must keep the oscillation finite.
    CHECK(std::isfinite(TestHelpers::measureRms(re)));
    CHECK(TestHelpers::measureRms(re) > 1e-3f); // actually oscillating

    YSE::DSP::fft ft;
    ft(re, im);
    unsigned peak = TestHelpers::peakBinIndex(ft.getReal().getPtr(), ft.getImaginary().getPtr(), N);
    const float binHz = static_cast<float>(YSE::SAMPLERATE) / static_cast<float>(N);
    const float peakHz = static_cast<float>(peak) * binHz;
    // Within ~10% of the cutoff — the ladder rings at its corner frequency.
    CHECK(std::abs(peakHz - cutoff) < cutoff * 0.1f);
  }

  // ─── sample-rate changes (issue #634) ───────────────────────────────────────

  static const double kPi = 3.14159265358979323846;

  // Temporarily force YSE::SAMPLERATE, restoring it on scope exit. Safe here:
  // the unit-test process runs its cases one at a time and the engine's audio
  // stream is paused (see support/null_device.hpp), so nothing reads the
  // global concurrently.
  struct ScopedSampleRate {
    UInt saved;
    explicit ScopedSampleRate(UInt rate) : saved(YSE::SAMPLERATE) {
      YSE::SAMPLERATE = rate;
    }
    ~ScopedSampleRate() {
      YSE::SAMPLERATE = saved;
    }
    ScopedSampleRate(const ScopedSampleRate&) = delete;
    ScopedSampleRate& operator=(const ScopedSampleRate&) = delete;
  };

  // The glide time the ladder documents, as wall clock. Asserted in
  // milliseconds with a *relative* tolerance: doctest's Approx adds a default
  // scale of 1.0 to the tolerance, so `.scale(0.0)` is required to stop a
  // 2%-of-1 ms check from silently accepting anything under 0.02.
  static const double kGlideTauMs = 1.0;
  static doctest::Approx approxGlideTauMs() {
    return doctest::Approx(kGlideTauMs).epsilon(0.02).scale(0.0);
  }

  // Exact read-out of the ladder's cutoff-glide time constant, in MILLISECONDS
  // of wall clock — the quantity that must survive a sample-rate change.
  //
  // The glide advances once per process() call and does not depend on the
  // signal, so a single probe sample recovers it exactly. With all four
  // integrator states at zero and resonance 0, the first sample out is
  //
  //     y = G^4 * tanh(x),   G = gCur / (1 + gCur)
  //
  // where gCur is the glide value *after* that one step. Invert for gCur, then
  // solve gCur = g0 + (gTarget - g0) * coef for the one-pole coefficient and
  // coef = 1 - exp(-1 / (tau * sr)) for tau.
  //
  // `f` must have zeroed states and a current glide value of `g0`; `sr` is the
  // rate it is being run at.
  static double measureGlideTauMs(ladderFilter & f, double sr, double g0, double toHz) {
    const double gTarget = std::tan(kPi * toHz / sr);

    f.setCutoff(static_cast<float>(toHz));
    const double y = f.process(1.f);

    const double G = std::pow(y / std::tanh(1.0), 0.25);
    const double gCur = G / (1.0 - G);
    const double coef = (gCur - g0) / (gTarget - g0);
    return -1000.0 / (sr * std::log(1.0 - coef));
  }

  // Glide time constant of a filter constructed at `constructRate`, then reset
  // and run at `runRate`.
  static double glideTauMsAt(UInt constructRate, UInt runRate) {
    const double fromHz = 20.0; // the ladder's minimum cutoff
    const double toHz = 8000.0; // well below 0.45 * Nyquist at both rates

    ScopedSampleRate atConstruct(constructRate);
    ladderFilter f; // coefficient derived at constructRate

    ScopedSampleRate atRun(runRate);
    f.setCutoff(static_cast<float>(fromHz));
    f.reset(); // zero the states and snap the glide to fromHz at runRate
    return measureGlideTauMs(f, static_cast<double>(runRate),
                             std::tan(kPi * fromHz / static_cast<double>(runRate)), toHz);
  }

  TEST_CASE("ladderFilter: the cutoff glide is ~1 ms of wall clock at any rate") {
    // Same rate throughout: the baseline the documented ~1 ms glide promises.
    CHECK(glideTauMsAt(44100, 44100) == approxGlideTauMs());
    CHECK(glideTauMsAt(96000, 96000) == approxGlideTauMs());
  }

  TEST_CASE("ladderFilter: the glide keeps its time constant across a rate change") {
    // Issue #634: a filter constructed at one rate and then run at another kept
    // the coefficient derived at construction time, so the glide lasted ~0.46 ms
    // at 96 kHz (and ~2.2 ms the other way) instead of the intended 1 ms. The
    // time constant must follow the rate the filter is *run* at, not the one it
    // happened to be built at.
    CHECK(glideTauMsAt(44100, 96000) == approxGlideTauMs());
    CHECK(glideTauMsAt(96000, 44100) == approxGlideTauMs());
  }

  TEST_CASE("ladderFilter: setCutoff alone refreshes the glide after a rate change") {
    // The realistic victim is SYNTH::vaVoice: constructed once at engine
    // startup, then driven purely through setCutoff() every block. It never
    // calls reset(), so the refresh cannot live in reset() alone.
    const UInt constructRate = 44100;
    const UInt runRate = 96000;

    ScopedSampleRate atConstruct(constructRate);
    ladderFilter f;
    // No reset(): the constructor already zeroed the states and snapped the
    // glide to its 1000 Hz default cutoff, prewarped at the construction rate.
    const double g0 = std::tan(kPi * 1000.0 / static_cast<double>(constructRate));

    ScopedSampleRate atRun(runRate);
    CHECK(measureGlideTauMs(f, static_cast<double>(runRate), g0, 8000.0) == approxGlideTauMs());
  }

  // ─── real-time discipline ───────────────────────────────────────────────────

  TEST_CASE("ladderFilter: process() does not allocate after warm-up") {
    ladderFilter f;
    f.setCutoff(1200.f);
    f.setResonance(0.6f);
    f.reset();
    f.setResonance(0.6f);
    YSE::DSP::buffer blk(YSE::STANDARD_BUFFERSIZE);
    blk = 0.3f;
    f(blk); // warm-up

    {
      TestHelpers::ProbeScope probe;
      for (int b = 0; b < 40; ++b) {
        blk = 0.3f;
        f(blk);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
