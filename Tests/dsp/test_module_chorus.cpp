// Behavioural tests for the multichannel chorus/flanger MODULE (issue #161).
//
// chorus is a modulated fractional-delay effect with a mode switch:
//   - MODE_CHORUS  : longer base delay, wide gentle sweep, no feedback by
//                    default — a moving delay pitch-modulates the wet copy
//                    (detune/vibrato), the classic thickening effect.
//   - MODE_FLANGER : short base delay + feedback — mixing the swept delayed
//                    copy with the dry signal makes a comb filter whose notches
//                    sweep as the delay moves (the "jet" sound).
//
// The delay read is fractional (linearly interpolated) so the sweep is
// click-free by construction. The wet/dry balance is the inherited impact();
// default impact = 1 is fully wet.
//
// No audio device required — SAMPLERATE is initialised by the
// portaudioDeviceManager translation unit at static-initialisation time (44100
// by default; CI also forces 48000 via YSE_TEST_FORCED_RATE). Every tone and
// observation window below is derived from the *actual* SAMPLERATE so the tests
// hold at any rate.

#include <doctest/doctest.h>
#include <cmath>
#include <vector>
#include "dsp/modules/chorus.hpp"
#include "headers/defines.hpp"
#include "support/audio_helpers.hpp"

static constexpr float kPi = 3.14159265358979323846f;

namespace {

  // The engine sample rate this test run is using (honours the forced-rate env).
  inline float sr() {
    return static_cast<float>(YSE::SAMPLERATE);
  }

  // Phase-continuous sine generator: the *input* stream has no block-boundary
  // discontinuity of its own, so any jump in the output comes purely from the
  // effect — exactly what the click-free test needs to isolate. The tone is
  // generated at the actual engine rate, so `freq` is correct in the stream.
  struct SineGen {
    double phase = 0.0;
    float freq;
    explicit SineGen(float f) : freq(f) {}
    void fill(YSE::DSP::buffer& buf) {
      float* p = buf.getPtr();
      double inc = 2.0 * static_cast<double>(kPi) * freq / static_cast<double>(sr());
      for (unsigned i = 0; i < buf.getLength(); ++i) {
        p[i] = static_cast<float>(std::sin(phase));
        phase += inc;
      }
    }
  };

  inline void zeroFill(YSE::DSP::buffer& buf) {
    buf = 0.0f;
  }

  inline void fillSine(YSE::DSP::buffer& buf, float freq) {
    float* p = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i)
      p[i] = std::sin(2.0f * kPi * freq * static_cast<float>(i) / sr());
  }

  inline float maxAbs(YSE::DSP::buffer& buf) {
    float* p = buf.getPtr();
    float m = 0.0f;
    for (unsigned i = 0; i < buf.getLength(); ++i) {
      float a = std::abs(p[i]);
      if (a > m) m = a;
    }
    return m;
  }

  // Run a mono chorus over `blocks` blocks of a continuous sine and return the
  // concatenated output samples.
  std::vector<float> runContinuous(YSE::DSP::MODULES::chorus& c, float carrierHz, int blocks,
                                   unsigned len = 128) {
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(len);
    SineGen gen(carrierHz);
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(blocks) * len);
    for (int b = 0; b < blocks; ++b) {
      gen.fill(buf[0]);
      c.process(buf);
      float* p = buf[0].getPtr();
      for (unsigned i = 0; i < len; ++i)
        out.push_back(p[i]);
    }
    return out;
  }

  // Peak-to-peak spread of the interval (in samples) between successive upward
  // zero crossings — a proxy for the pitch-modulation depth of a sine. The
  // crossing position is interpolated to sub-sample precision, so the metric is
  // continuous (not quantised to the sample grid) and a pure, unmodulated tone
  // reads a spread of ~0 regardless of the sample rate.
  float zeroCrossPeriodSpread(const std::vector<float>& sig, std::size_t skip) {
    float minP = 1e30f, maxP = -1e30f;
    double lastCross = -1.0;
    for (std::size_t i = skip + 1; i < sig.size(); ++i) {
      if (sig[i - 1] <= 0.0f && sig[i] > 0.0f) { // rising zero crossing
        // Linear-interpolated sub-sample position where the signal hits zero.
        double denom = static_cast<double>(sig[i]) - static_cast<double>(sig[i - 1]);
        double cross = static_cast<double>(i - 1);
        if (denom != 0.0) cross += -static_cast<double>(sig[i - 1]) / denom;
        if (lastCross >= 0.0) {
          float period = static_cast<float>(cross - lastCross);
          if (period < minP) minP = period;
          if (period > maxP) maxP = period;
        }
        lastCross = cross;
      }
    }
    if (maxP < 0.0f) return 0.0f;
    return maxP - minP;
  }

} // anonymous namespace

TEST_SUITE("dsp") {

  // ─── parameters ───────────────────────────────────────────────────────────────

  TEST_CASE("chorus: sensible defaults") {
    YSE::DSP::MODULES::chorus c;
    CHECK(c.mode() == YSE::DSP::MODULES::MODE_CHORUS);
    CHECK(c.rate() == doctest::Approx(0.8f).epsilon(1e-5f));
    CHECK(c.depth() == doctest::Approx(0.5f).epsilon(1e-5f));
    CHECK(c.feedback() == doctest::Approx(0.0f).epsilon(1e-5f));
    CHECK(c.spread() == doctest::Approx(0.0f).epsilon(1e-5f));
  }

  TEST_CASE("chorus: setter/getter round-trips") {
    YSE::DSP::MODULES::chorus c;
    c.mode(YSE::DSP::MODULES::MODE_FLANGER).rate(3.0f).depth(0.7f).feedback(0.4f).spread(0.5f);
    CHECK(c.mode() == YSE::DSP::MODULES::MODE_FLANGER);
    CHECK(c.rate() == doctest::Approx(3.0f).epsilon(1e-5f));
    CHECK(c.depth() == doctest::Approx(0.7f).epsilon(1e-5f));
    CHECK(c.feedback() == doctest::Approx(0.4f).epsilon(1e-5f));
    CHECK(c.spread() == doctest::Approx(0.5f).epsilon(1e-5f));
  }

  TEST_CASE("chorus: parameters are clamped to safe ranges") {
    YSE::DSP::MODULES::chorus c;
    c.depth(5.0f);
    CHECK(c.depth() == doctest::Approx(1.0f));
    c.depth(-1.0f);
    CHECK(c.depth() == doctest::Approx(0.0f));
    c.feedback(5.0f);
    CHECK(c.feedback() <= 0.95f);
    c.feedback(-5.0f);
    CHECK(c.feedback() >= -0.95f);
    c.spread(3.0f);
    CHECK(c.spread() == doctest::Approx(1.0f));
    c.rate(1.0e6f);
    CHECK(c.rate() <= 20.0f);
    c.rate(0.0f);
    CHECK(c.rate() >= 0.01f);
  }

  // ─── wet signal is a delayed copy ─────────────────────────────────────────────

  TEST_CASE("chorus: fully-wet output is a delayed (non-trivial) copy of the input") {
    // impact defaults to 1 (fully wet). Feed one impulse block then silence;
    // within the base-delay window the delayed impulse must surface.
    YSE::DSP::MODULES::chorus c;
    c.mode(YSE::DSP::MODULES::MODE_CHORUS).depth(0.0f); // static ~15 ms delay

    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    zeroFill(buf[0]);
    buf[0].getPtr()[0] = 1.0f;
    c.process(buf);
    // First block: read position is ~15 ms back = silence (line just primed).
    CHECK(TestHelpers::measureRms(buf[0]) < 1e-4f);

    // The ~15 ms base delay is a rate-dependent number of blocks; cover it with
    // margin so the delayed impulse is guaranteed to surface at any sample rate.
    const int blocks = static_cast<int>(std::ceil(0.015f * sr() / 128.0f)) + 6;
    bool sawWet = false;
    for (int iter = 0; iter < blocks; ++iter) {
      zeroFill(buf[0]);
      c.process(buf);
      if (TestHelpers::measureRms(buf[0]) > 1e-3f) {
        sawWet = true;
        break;
      }
    }
    CHECK(sawWet);
  }

  // ─── chorus detune: pitch deviation scales with depth ─────────────────────────

  TEST_CASE("chorus: detune depth scales with the depth parameter") {
    // A moving delay pitch-modulates the wet copy. With a larger depth the
    // delay sweeps further per LFO cycle, so the instantaneous pitch deviates
    // more — measured here as a wider spread of zero-crossing periods.
    // Skip past the delay-line fill + smoother settle (well beyond the ~15 ms
    // base delay) before measuring, at whatever the sample rate is.
    const std::size_t skip = static_cast<std::size_t>(0.05f * sr());
    auto spreadFor = [skip](float depth) {
      YSE::DSP::MODULES::chorus c;
      c.mode(YSE::DSP::MODULES::MODE_CHORUS).rate(2.0f).depth(depth).impact(1.0f);
      // ~1.5 LFO cycles at 2 Hz.
      std::vector<float> out = runContinuous(c, 1000.0f, 260);
      return zeroCrossPeriodSpread(out, skip);
    };
    float shallow = spreadFor(0.1f);
    float deep = spreadFor(0.9f);
    // Deeper modulation must widen the pitch swing appreciably.
    CHECK(deep > shallow * 1.5f);
    // A zero-depth chorus is a fixed delay — no pitch modulation at all, so its
    // (sub-sample-interpolated) period spread is essentially zero.
    YSE::DSP::MODULES::chorus flat;
    flat.mode(YSE::DSP::MODULES::MODE_CHORUS).rate(2.0f).depth(0.0f).impact(1.0f);
    std::vector<float> flatOut = runContinuous(flat, 1000.0f, 260);
    CHECK(zeroCrossPeriodSpread(flatOut, skip) < shallow);
  }

  // ─── flanger: comb notches, and they move ─────────────────────────────────────

  TEST_CASE("chorus: flanger comb attenuates a tuned notch frequency") {
    // Static flanger (depth 0) at the 1 ms base delay. Dry+wet at impact 0.5 is
    // a comb: with a 1 ms delay the first notch sits at 500 Hz and the first
    // peak at 1000 Hz. The notch tone must come out quieter than the peak tone.
    auto steadyRms = [](float probeHz) {
      YSE::DSP::MODULES::chorus c;
      c.mode(YSE::DSP::MODULES::MODE_FLANGER).depth(0.0f).feedback(0.0f).impact(0.5f);
      float energy = 0.0f;
      int counted = 0;
      MULTICHANNELBUFFER buf(1);
      buf[0].resize(128);
      SineGen gen(probeHz);
      for (int b = 0; b < 60; ++b) {
        gen.fill(buf[0]);
        c.process(buf);
        if (b >= 20) { // let the delay line and smoother settle
          energy += TestHelpers::measureRms(buf[0]);
          ++counted;
        }
      }
      return energy / static_cast<float>(counted);
    };
    float notch = steadyRms(500.0f);
    float peak = steadyRms(1000.0f);
    CHECK(notch < peak * 0.8f);
  }

  TEST_CASE("chorus: flanger sweep makes the comb notches move") {
    // A swept flanger's comb filter walks across the spectrum, so a fixed probe
    // tone is alternately notched and reinforced — its per-block RMS varies over
    // the LFO cycle. A depth-0 flanger has a static comb, so its RMS is flat.
    auto rmsRange = [](float depth) {
      YSE::DSP::MODULES::chorus c;
      c.mode(YSE::DSP::MODULES::MODE_FLANGER).rate(1.0f).depth(depth).feedback(0.5f).impact(0.5f);
      MULTICHANNELBUFFER buf(1);
      buf[0].resize(128);
      SineGen gen(1500.0f);
      // Warm up past the smoother, then observe one full LFO period (1 s).
      for (int b = 0; b < 40; ++b) {
        gen.fill(buf[0]);
        c.process(buf);
      }
      float minR = 1e30f, maxR = -1e30f;
      // Observe a full LFO period (1 s at rate 1 Hz), plus a block of slack.
      int blocksPerSecond = static_cast<int>(sr() / 128.0f) + 2;
      for (int b = 0; b < blocksPerSecond; ++b) {
        gen.fill(buf[0]);
        c.process(buf);
        float r = TestHelpers::measureRms(buf[0]);
        if (r < minR) minR = r;
        if (r > maxR) maxR = r;
      }
      return maxR - minR;
    };
    float moving = rmsRange(1.0f);
    float still = rmsRange(0.0f);
    // The moving comb modulates the probe amplitude far more than the static one.
    CHECK(moving > still * 3.0f);
    CHECK(moving > 0.01f);
  }

  // ─── fractional interpolation is click-free under a fast LFO ───────────────────

  TEST_CASE("chorus: fast LFO sweep stays click-free (fractional interpolation)") {
    // Fully-wet, fast, deep sweep. The fractional read keeps the output
    // continuous — an integer-quantised read would step the pointer and click.
    YSE::DSP::MODULES::chorus c;
    c.mode(YSE::DSP::MODULES::MODE_CHORUS).rate(8.0f).depth(1.0f).feedback(0.0f).impact(1.0f);

    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    SineGen gen(300.0f);

    // Warm up so the line holds continuous content.
    for (int b = 0; b < 40; ++b) {
      gen.fill(buf[0]);
      c.process(buf);
    }

    float prev = buf[0].getPtr()[buf[0].getLength() - 1];
    float worstStep = 0.0f;
    for (int b = 0; b < 200; ++b) {
      gen.fill(buf[0]);
      c.process(buf);
      float* p = buf[0].getPtr();
      float boundary = std::abs(p[0] - prev); // across the block boundary
      if (boundary > worstStep) worstStep = boundary;
      for (unsigned i = 1; i < buf[0].getLength(); ++i) {
        float step = std::abs(p[i] - p[i - 1]);
        if (step > worstStep) worstStep = step;
      }
      prev = p[buf[0].getLength() - 1];
    }
    // A 300 Hz sine steps at most ~0.05/sample; Doppler from the sweep widens
    // that a little. A hard delay jump would spike toward ~2.0.
    CHECK(worstStep < 0.5f);
  }

  // ─── multichannel contract ────────────────────────────────────────────────────

  TEST_CASE("chorus: channels are processed independently (no cross-bleed)") {
    // Impulse on channel 0 only; channel 1 must stay silent (each channel owns
    // its delay line). feedback off, spread 0.
    YSE::DSP::MODULES::chorus c;
    c.mode(YSE::DSP::MODULES::MODE_CHORUS).depth(0.3f).feedback(0.0f).spread(0.0f);

    MULTICHANNELBUFFER buf(2);
    buf[0].resize(128);
    buf[1].resize(128);
    zeroFill(buf[0]);
    zeroFill(buf[1]);
    buf[0].getPtr()[0] = 1.0f; // impulse on channel 0 only

    float ch1Energy = 0.0f;
    c.process(buf);
    ch1Energy += TestHelpers::measureRms(buf[1]);
    for (int b = 0; b < 12; ++b) {
      zeroFill(buf[0]);
      zeroFill(buf[1]);
      c.process(buf);
      ch1Energy += TestHelpers::measureRms(buf[1]);
    }
    CHECK(ch1Energy < 1e-6f);
  }

  TEST_CASE("chorus: at spread 0 every channel gets identical processing") {
    YSE::DSP::MODULES::chorus c;
    c.mode(YSE::DSP::MODULES::MODE_CHORUS).rate(3.0f).depth(0.6f).spread(0.0f).impact(1.0f);

    MULTICHANNELBUFFER buf(2);
    buf[0].resize(128);
    buf[1].resize(128);
    for (int b = 0; b < 30; ++b) {
      fillSine(buf[0], 440.0f);
      fillSine(buf[1], 440.0f); // same signal on both channels
      c.process(buf);
      CHECK(TestHelpers::buffersNearlyEqual(buf[0], buf[1], 1e-6f));
    }
  }

  TEST_CASE("chorus: spread makes the channels differ") {
    YSE::DSP::MODULES::chorus c;
    c.mode(YSE::DSP::MODULES::MODE_CHORUS).rate(3.0f).depth(0.8f).spread(1.0f).impact(1.0f);

    MULTICHANNELBUFFER buf(2);
    buf[0].resize(128);
    buf[1].resize(128);
    bool differed = false;
    for (int b = 0; b < 60; ++b) {
      fillSine(buf[0], 440.0f);
      fillSine(buf[1], 440.0f);
      c.process(buf);
      if (!TestHelpers::buffersNearlyEqual(buf[0], buf[1], 1e-3f)) {
        differed = true;
        break;
      }
    }
    CHECK(differed);
  }

  // ─── robustness ───────────────────────────────────────────────────────────────

  TEST_CASE("chorus: output stays bounded under sustained input and feedback") {
    YSE::DSP::MODULES::chorus c;
    c.mode(YSE::DSP::MODULES::MODE_FLANGER).rate(2.0f).depth(1.0f).feedback(0.9f).impact(0.7f);

    MULTICHANNELBUFFER buf(2);
    buf[0].resize(128);
    buf[1].resize(128);
    for (int b = 0; b < 300; ++b) {
      fillSine(buf[0], 440.0f);
      fillSine(buf[1], 660.0f);
      c.process(buf);
      CHECK(maxAbs(buf[0]) < 20.0f);
      CHECK(maxAbs(buf[1]) < 20.0f);
    }
  }

  TEST_CASE("chorus: tolerates a change in input buffer length") {
    YSE::DSP::MODULES::chorus c;
    c.mode(YSE::DSP::MODULES::MODE_CHORUS).depth(0.5f);

    MULTICHANNELBUFFER buf(1);
    buf[0].resize(64);
    fillSine(buf[0], 500.0f);
    c.process(buf);
    CHECK(buf[0].getLength() == 64u);

    buf[0].resize(256);
    fillSine(buf[0], 500.0f);
    c.process(buf);
    CHECK(buf[0].getLength() == 256u);
  }

} // TEST_SUITE("dsp")
