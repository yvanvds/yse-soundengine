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
//   - no heap allocation in process() after warm-up.
//
// No audio device required; SAMPLERATE is initialised to 44100 by the
// portaudioDeviceManager translation unit at static-initialisation time.

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
