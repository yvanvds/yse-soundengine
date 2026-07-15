// Behavioural tests for the channel-strip dynamics compressor MODULE (issue
// #163).
//
// compressor is a feed-forward dynamics processor: a switchable peak/RMS
// detector drives a static threshold/ratio curve, the gain is smoothed by
// separate attack/release time constants, and a makeup gain restores level. The
// detector and gain are SHARED across every channel (stereo-linked) so a
// transient on one channel ducks all channels by the same amount — no
// channel-independent pumping / image wobble. The wet/dry balance is the
// inherited impact(); default impact = 1 is fully compressed.
//
// No audio device required — SAMPLERATE is initialised by the
// portaudioDeviceManager translation unit at static-initialisation time (44100
// by default; CI also forces 48000 via YSE_TEST_FORCED_RATE). Levels and
// windows are derived from the actual SAMPLERATE so the tests hold at any rate.

#include <doctest/doctest.h>
#include <cmath>
#include <vector>
#include "dsp/modules/compressor.hpp"
#include "headers/defines.hpp"
#include "support/audio_helpers.hpp"

static constexpr float kPi = 3.14159265358979323846f;

namespace {

  using YSE::DSP::MODULES::compressor;

  inline float sr() {
    return static_cast<float>(YSE::SAMPLERATE);
  }

  struct SineGen {
    double phase = 0.0;
    float freq;
    float amp;
    SineGen(float f, float a) : freq(f), amp(a) {}
    void fill(YSE::DSP::buffer& buf) {
      float* p = buf.getPtr();
      double inc = 2.0 * static_cast<double>(kPi) * freq / static_cast<double>(sr());
      for (unsigned i = 0; i < buf.getLength(); ++i) {
        p[i] = amp * static_cast<float>(std::sin(phase));
        phase += inc;
      }
    }
  };

  inline float dbToLin(float db) {
    return std::pow(10.0f, db / 20.0f);
  }

  // Peak absolute value across a buffer.
  inline float peakAbs(YSE::DSP::buffer& buf) {
    float* p = buf.getPtr();
    float m = 0.0f;
    for (unsigned i = 0; i < buf.getLength(); ++i) {
      float a = std::abs(p[i]);
      if (a > m) m = a;
    }
    return m;
  }

  // Drive a mono compressor with a steady sine of the given peak level (dBFS)
  // until the gain settles, then return the steady output peak level (dBFS).
  float steadyOutputPeakDb(compressor& c, float inputPeakDb, float carrierHz = 1000.0f) {
    float amp = dbToLin(inputPeakDb);
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(256);
    SineGen gen(carrierHz, amp);
    // Settle well past the (default) release time.
    int settle = static_cast<int>(1.0f * sr() / 256.0f) + 8;
    for (int b = 0; b < settle; ++b) {
      gen.fill(buf[0]);
      c.process(buf);
    }
    float peak = 0.0f;
    for (int b = 0; b < 8; ++b) {
      gen.fill(buf[0]);
      c.process(buf);
      float p = peakAbs(buf[0]);
      if (p > peak) peak = p;
    }
    return 20.0f * std::log10(peak);
  }

} // anonymous namespace

TEST_SUITE("dsp") {

  // ─── parameters ───────────────────────────────────────────────────────────────

  TEST_CASE("compressor: sensible defaults") {
    compressor c;
    CHECK(c.detector() == YSE::DSP::MODULES::DETECT_PEAK);
    CHECK(c.threshold() == doctest::Approx(-18.0f));
    CHECK(c.ratio() == doctest::Approx(4.0f));
    CHECK(c.attack() == doctest::Approx(10.0f));
    CHECK(c.release() == doctest::Approx(100.0f));
    CHECK(c.makeup() == doctest::Approx(0.0f));
  }

  TEST_CASE("compressor: setter/getter round-trips") {
    compressor c;
    c.detector(YSE::DSP::MODULES::DETECT_RMS)
        .threshold(-24.0f)
        .ratio(8.0f)
        .attack(5.0f)
        .release(250.0f)
        .makeup(6.0f);
    CHECK(c.detector() == YSE::DSP::MODULES::DETECT_RMS);
    CHECK(c.threshold() == doctest::Approx(-24.0f));
    CHECK(c.ratio() == doctest::Approx(8.0f));
    CHECK(c.attack() == doctest::Approx(5.0f));
    CHECK(c.release() == doctest::Approx(250.0f));
    CHECK(c.makeup() == doctest::Approx(6.0f));
  }

  TEST_CASE("compressor: parameters clamp to safe ranges") {
    compressor c;
    c.threshold(50.0f);
    CHECK(c.threshold() <= 0.0f);
    c.threshold(-200.0f);
    CHECK(c.threshold() >= -60.0f);
    c.ratio(1000.0f);
    CHECK(c.ratio() <= 20.0f);
    c.ratio(0.1f);
    CHECK(c.ratio() >= 1.0f);
    c.attack(1.0e6f);
    CHECK(c.attack() <= 2000.0f);
    c.attack(0.0f);
    CHECK(c.attack() >= 0.1f);
    c.makeup(100.0f);
    CHECK(c.makeup() <= 24.0f);
  }

  // ─── static curve ─────────────────────────────────────────────────────────────

  TEST_CASE("compressor: below threshold passes unchanged") {
    compressor c;
    c.threshold(-18.0f).ratio(4.0f).makeup(0.0f).attack(1.0f).release(50.0f);
    // Input peak -30 dBFS is well below the -18 dB threshold: no reduction.
    float out = steadyOutputPeakDb(c, -30.0f);
    CHECK(out == doctest::Approx(-30.0f).epsilon(0.03)); // within ~0.4 dB
    CHECK(c.gainReductionDb() == doctest::Approx(0.0f).epsilon(0.05));
  }

  TEST_CASE("compressor: static curve matches threshold and ratio") {
    // threshold -18 dB, ratio 4. Input -6 dBFS is 12 dB over threshold, so the
    // output should sit at threshold + 12/4 = -15 dBFS, i.e. 9 dB of reduction.
    compressor c;
    c.threshold(-18.0f).ratio(4.0f).makeup(0.0f).attack(2.0f).release(80.0f);
    float out = steadyOutputPeakDb(c, -6.0f);
    CHECK(out == doctest::Approx(-15.0f).epsilon(0.06)); // within ~0.9 dB
    CHECK(c.gainReductionDb() == doctest::Approx(-9.0f).epsilon(0.12));
  }

  TEST_CASE("compressor: higher ratio compresses harder") {
    auto reductionAt = [](float ratio) {
      compressor c;
      c.threshold(-18.0f).ratio(ratio).makeup(0.0f).attack(2.0f).release(80.0f);
      steadyOutputPeakDb(c, -6.0f);
      return c.gainReductionDb(); // <= 0
    };
    float gr2 = reductionAt(2.0f);
    float gr8 = reductionAt(8.0f);
    // A higher ratio applies more (more negative) gain reduction.
    CHECK(gr8 < gr2 - 1.0f);
  }

  TEST_CASE("compressor: makeup gain lifts the output level") {
    compressor c;
    c.threshold(-18.0f).ratio(4.0f).makeup(6.0f).attack(2.0f).release(80.0f);
    float out = steadyOutputPeakDb(c, -6.0f);
    // Same as the static-curve case (-15 dB) plus 6 dB of makeup.
    CHECK(out == doctest::Approx(-9.0f).epsilon(0.08));
  }

  // ─── attack / release timing ──────────────────────────────────────────────────

  TEST_CASE("compressor: attack time is within tolerance") {
    // Step from silence to a loud tone; measure how long the applied gain takes
    // to fall to ~63% of its final linear change (one time constant).
    compressor c;
    const float attackMs = 20.0f;
    c.threshold(-24.0f).ratio(8.0f).makeup(0.0f).attack(attackMs).release(400.0f);

    MULTICHANNELBUFFER buf(1);
    const unsigned N = 32; // fine-grained blocks so the timing resolves
    buf[0].resize(N);
    SineGen gen(1000.0f, dbToLin(-3.0f)); // well above threshold

    // Steady-state gain after a long hold.
    for (int b = 0; b < static_cast<int>(2.0f * sr() / N); ++b) {
      gen.fill(buf[0]);
      c.process(buf);
    }
    float finalGain = dbToLin(c.gainReductionDb());

    // Reset the follower by constructing a fresh compressor, then time attack.
    compressor c2;
    c2.threshold(-24.0f).ratio(8.0f).makeup(0.0f).attack(attackMs).release(400.0f);
    SineGen gen2(1000.0f, dbToLin(-3.0f));
    float target = 1.0f + 0.63f * (finalGain - 1.0f); // 63% of the way down
    int blocksToTarget = -1;
    int maxBlocks = static_cast<int>(0.5f * sr() / N);
    for (int b = 0; b < maxBlocks; ++b) {
      gen2.fill(buf[0]);
      c2.process(buf);
      float g = dbToLin(c2.gainReductionDb());
      if (g <= target) {
        blocksToTarget = b + 1;
        break;
      }
    }
    REQUIRE(blocksToTarget > 0);
    float measuredMs = 1000.0f * static_cast<float>(blocksToTarget) * static_cast<float>(N) / sr();
    // Generous tolerance: the block granularity and detector rise add slack.
    CHECK(measuredMs > attackMs * 0.4f);
    CHECK(measuredMs < attackMs * 3.0f);
  }

  TEST_CASE("compressor: release is slower than attack") {
    // With attack << release, after a loud burst the gain must recover slowly.
    compressor c;
    c.threshold(-24.0f).ratio(8.0f).makeup(0.0f).attack(2.0f).release(300.0f);

    MULTICHANNELBUFFER buf(1);
    const unsigned N = 64;
    buf[0].resize(N);
    SineGen loud(1000.0f, dbToLin(-3.0f));
    // Drive into heavy reduction.
    for (int b = 0; b < static_cast<int>(0.5f * sr() / N); ++b) {
      loud.fill(buf[0]);
      c.process(buf);
    }
    float grLoud = c.gainReductionDb();
    CHECK(grLoud < -3.0f); // clearly compressing

    // Now feed silence and check the gain recovers, but not instantly.
    buf[0] = 0.0f;
    c.process(buf); // one short block
    float grJustAfter = c.gainReductionDb();
    // After a single short block of silence the slow release has barely moved.
    CHECK(grJustAfter < grLoud + 3.0f);

    // After a long stretch of silence it returns to ~0 dB.
    for (int b = 0; b < static_cast<int>(3.0f * sr() / N); ++b) {
      buf[0] = 0.0f;
      c.process(buf);
    }
    CHECK(c.gainReductionDb() == doctest::Approx(0.0f).epsilon(0.05));
  }

  // ─── stereo-linked detection ──────────────────────────────────────────────────

  TEST_CASE("compressor: detection is stereo-linked (identical gain on all channels)") {
    // Different tones per channel, but the loud channel drives the shared
    // detector. Both channels must receive the SAME gain — i.e. out/in ratio is
    // equal on both channels (no channel-independent pumping).
    compressor c;
    c.detector(YSE::DSP::MODULES::DETECT_PEAK)
        .threshold(-24.0f)
        .ratio(6.0f)
        .makeup(0.0f)
        .attack(5.0f)
        .release(120.0f);

    MULTICHANNELBUFFER buf(2);
    buf[0].resize(256);
    buf[1].resize(256);
    SineGen loud(440.0f, dbToLin(-3.0f)); // channel 0: loud
    SineGen quiet(660.0f, dbToLin(-3.0f)); // channel 1: same level, different freq

    // Settle.
    for (int b = 0; b < static_cast<int>(1.0f * sr() / 256); ++b) {
      loud.fill(buf[0]);
      quiet.fill(buf[1]);
      c.process(buf);
    }
    // Measure per-channel out/in RMS ratio over a window.
    float in0 = 0, out0 = 0, in1 = 0, out1 = 0;
    for (int b = 0; b < 8; ++b) {
      loud.fill(buf[0]);
      quiet.fill(buf[1]);
      float i0 = TestHelpers::measureRms(buf[0]);
      float i1 = TestHelpers::measureRms(buf[1]);
      c.process(buf);
      in0 += i0;
      in1 += i1;
      out0 += TestHelpers::measureRms(buf[0]);
      out1 += TestHelpers::measureRms(buf[1]);
    }
    float gain0 = out0 / in0;
    float gain1 = out1 / in1;
    CHECK(gain0 == doctest::Approx(gain1).epsilon(0.02)); // same gain on both
    CHECK(gain0 < 0.9f); // and both are actually compressed
  }

  TEST_CASE("compressor: a loud channel ducks a quiet channel (linked, no wobble)") {
    // Channel 1 carries a fixed quiet tone. When channel 0 is loud, the linked
    // detector ducks BOTH channels, so channel 1's output is quieter than when
    // channel 0 is silent. Independent detection would leave channel 1 untouched.
    auto ch1OutWith = [](float ch0Db) {
      compressor c;
      c.threshold(-30.0f).ratio(8.0f).makeup(0.0f).attack(5.0f).release(120.0f);
      MULTICHANNELBUFFER buf(2);
      buf[0].resize(256);
      buf[1].resize(256);
      SineGen ch0(440.0f, dbToLin(ch0Db));
      SineGen ch1(660.0f, dbToLin(-40.0f)); // always quiet, below threshold
      for (int b = 0; b < static_cast<int>(1.0f * sr() / 256); ++b) {
        ch0.fill(buf[0]);
        ch1.fill(buf[1]);
        c.process(buf);
      }
      float acc = 0.0f;
      for (int b = 0; b < 8; ++b) {
        ch0.fill(buf[0]);
        ch1.fill(buf[1]);
        c.process(buf);
        acc += TestHelpers::measureRms(buf[1]);
      }
      return acc / 8.0f;
    };
    float quietNeighbour = ch1OutWith(-40.0f); // ch0 also quiet: no reduction
    float loudNeighbour = ch1OutWith(0.0f); // ch0 loud: linked ducking
    CHECK(loudNeighbour < quietNeighbour * 0.7f);
  }

  TEST_CASE("compressor: RMS detector also compresses") {
    compressor c;
    c.detector(YSE::DSP::MODULES::DETECT_RMS)
        .threshold(-20.0f)
        .ratio(4.0f)
        .makeup(0.0f)
        .attack(5.0f)
        .release(120.0f);
    // A loud tone must be reduced; a quiet one must pass.
    float loud = steadyOutputPeakDb(c, -3.0f);
    CHECK(loud < -3.0f - 1.0f); // clearly reduced

    compressor c2;
    c2.detector(YSE::DSP::MODULES::DETECT_RMS)
        .threshold(-20.0f)
        .ratio(4.0f)
        .makeup(0.0f)
        .attack(5.0f)
        .release(120.0f);
    float quiet = steadyOutputPeakDb(c2, -40.0f);
    CHECK(quiet == doctest::Approx(-40.0f).epsilon(0.05));
  }

  // ─── robustness ───────────────────────────────────────────────────────────────

  TEST_CASE("compressor: tolerates a change in input buffer length and channel count") {
    compressor c;
    c.threshold(-18.0f).ratio(4.0f);

    MULTICHANNELBUFFER mono(1);
    mono[0].resize(64);
    SineGen g(500.0f, 0.5f);
    g.fill(mono[0]);
    c.process(mono);
    CHECK(mono[0].getLength() == 64u);

    // Grow to stereo with a different block length — must not crash or allocate
    // per-channel history (the gain is global).
    MULTICHANNELBUFFER stereo(2);
    stereo[0].resize(256);
    stereo[1].resize(256);
    SineGen g0(500.0f, 0.5f), g1(700.0f, 0.5f);
    g0.fill(stereo[0]);
    g1.fill(stereo[1]);
    c.process(stereo);
    CHECK(stereo[0].getLength() == 256u);
    CHECK(stereo[1].getLength() == 256u);
  }

  TEST_CASE("compressor: output stays bounded under extreme settings") {
    compressor c;
    c.threshold(-40.0f).ratio(20.0f).makeup(24.0f).attack(0.1f).release(1.0f);
    MULTICHANNELBUFFER buf(2);
    buf[0].resize(128);
    buf[1].resize(128);
    for (int b = 0; b < 300; ++b) {
      SineGen g0(440.0f, 1.0f), g1(660.0f, 1.0f);
      g0.fill(buf[0]);
      g1.fill(buf[1]);
      c.process(buf);
      CHECK(peakAbs(buf[0]) < 50.0f);
      CHECK(peakAbs(buf[1]) < 50.0f);
    }
  }

} // TEST_SUITE("dsp")
