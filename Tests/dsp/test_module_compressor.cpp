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
// portaudioDeviceManager translation unit at static-initialisation time (48000
// by default; CI also forces 44100 via YSE_TEST_FORCED_RATE). Levels and
// windows are derived from the actual SAMPLERATE so the tests hold at any rate.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include "dsp/modules/compressor.hpp"
#include "dsp/smoother.hpp"
#include "headers/defines.hpp"
#include "support/alloc_probe.hpp"
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

  // ─── the compressor as it was before #1241, verbatim ──────────────────────
  //
  // The defaults of the controls #1241 added must leave the output bit for bit
  // what it was. This is the class's processing as of 50c799bc (#1235), kept
  // here as the reference those tests compare against.
  class legacyCompressor : public YSE::DSP::dspObject {
  public:
    legacyCompressor()
      : gainBuf(YSE::STANDARD_BUFFERSIZE),
        wet(YSE::STANDARD_BUFFERSIZE),
        dry(YSE::STANDARD_BUFFERSIZE) {}
    Flt threshold = -18.0f;
    Flt ratio = 4.0f;
    Flt attack = 10.0f;
    Flt release = 100.0f;
    Flt makeup = 0.0f;
    bool rms = false;
    Flt reductionDb = 0.0f;

    void create() override {
      rmsCoef = YSE::DSP::onePoleCoef(0.01f, static_cast<Flt>(YSE::SAMPLERATE));
      gain = 1.0f;
      msEnv = 0.0f;
    }
    void process(MULTICHANNELBUFFER& buffer) override {
      run(buffer, buffer);
    }
    void process(MULTICHANNELBUFFER& buffer, MULTICHANNELBUFFER& key) {
      run(buffer, key);
    }

  private:
    static Flt dbToLinL(Flt db) {
      return std::pow(10.0f, db / 20.0f);
    }
    static Flt linToDbL(Flt lin) {
      return 20.0f * std::log10(lin + 1.0e-9f);
    }
    void run(MULTICHANNELBUFFER& buffer, MULTICHANNELBUFFER& key) {
      createIfNeeded();
      if (buffer.empty()) return;
      const UInt length = buffer[0].getLength();
      for (UInt offset = 0; offset < length;) {
        const UInt piece = std::min<UInt>(length - offset, YSE::STANDARD_BUFFERSIZE);
        runPiece(buffer, key, offset, piece);
        offset += piece;
      }
    }
    void runPiece(MULTICHANNELBUFFER& buffer, MULTICHANNELBUFFER& key, UInt offset, UInt length) {
      const std::size_t n = buffer.size();
      const std::size_t keys = key.size();
      if (gainBuf.getLength() != length) gainBuf.resize(length);
      if (wet.getLength() != length) wet.resize(length);
      const Flt sr = static_cast<Flt>(YSE::SAMPLERATE);
      const Flt thresholdDb = std::clamp(threshold, -60.0f, 0.0f);
      const Flt ratioVal = std::clamp(ratio, 1.0f, 20.0f);
      const Flt slope = 1.0f / ratioVal - 1.0f;
      const Flt attackCoef = YSE::DSP::onePoleCoef(attack * 0.001f, sr);
      const Flt releaseCoef = YSE::DSP::onePoleCoef(release * 0.001f, sr);
      const Flt makeupLin = dbToLinL(makeup);
      rmsCoef = YSE::DSP::onePoleCoef(0.01f, sr);
      Flt g = gain;
      Flt ms = msEnv;
      Flt* gb = gainBuf.getPtr();
      for (UInt i = 0; i < length; ++i) {
        Flt level;
        if (rms) {
          Flt sumSq = 0.0f;
          for (std::size_t ch = 0; ch < keys; ++ch) {
            const Flt s = key[ch].getPtr()[offset + i];
            sumSq += s * s;
          }
          const Flt meanSq = sumSq / static_cast<Flt>(keys);
          ms = YSE::DSP::onePoleSmooth(ms, meanSq, rmsCoef);
          level = std::sqrt(ms);
        } else {
          Flt peak = 0.0f;
          for (std::size_t ch = 0; ch < keys; ++ch) {
            const Flt a = std::abs(key[ch].getPtr()[offset + i]);
            if (a > peak) peak = a;
          }
          level = peak;
        }
        const Flt levelDb = linToDbL(level);
        Flt targetGainDb = 0.0f;
        if (levelDb > thresholdDb) targetGainDb = (levelDb - thresholdDb) * slope;
        const Flt targetGain = dbToLinL(targetGainDb);
        const Flt coef = (targetGain < g) ? attackCoef : releaseCoef;
        g = YSE::DSP::onePoleSmooth(g, targetGain, coef);
        gb[i] = g * makeupLin;
      }
      gain = g;
      msEnv = ms;
      reductionDb = linToDbL(g);
      for (std::size_t ch = 0; ch < n; ++ch) {
        Flt* x = buffer[ch].getPtr() + offset;
        Flt* w = wet.getPtr();
        for (UInt i = 0; i < length; ++i)
          w[i] = x[i] * gb[i];
        if (length == buffer[ch].getLength()) {
          calculateImpact(buffer[ch], wet);
        } else {
          if (dry.getLength() != length) dry.resize(length);
          Flt* d = dry.getPtr();
          std::copy(x, x + length, d);
          calculateImpact(dry, wet);
          std::copy(d, d + length, x);
        }
      }
    }

    Flt gain = 1.0f;
    Flt msEnv = 0.0f;
    Flt rmsCoef = 0.0f;
    YSE::DSP::buffer gainBuf;
    YSE::DSP::buffer wet;
    YSE::DSP::buffer dry;
  };

  // Bit for bit, not within a tolerance.
  bool sameBits(YSE::DSP::buffer& a, YSE::DSP::buffer& b) {
    return a.getLength() == b.getLength() &&
           std::memcmp(a.getPtr(), b.getPtr(), a.getLength() * sizeof(Flt)) == 0;
  }

  // A signal with loud and quiet stretches, so the gain moves both ways.
  void fillBursts(YSE::DSP::buffer& b, UInt start, UInt channel) {
    for (UInt i = 0; i < b.getLength(); ++i) {
      const UInt n = start + i;
      const float level = (n / 900) % 3 == 0 ? 0.95f : ((n / 900) % 3 == 1 ? 0.02f : 0.3f);
      b.getPtr()[i] =
          level * std::sin(static_cast<float>(n) * (0.021f + 0.013f * static_cast<float>(channel)));
    }
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

  // ─── external sidechain (issue #1116) ──────────────────────────────────────

  TEST_CASE("compressor: a sidechain equal to the buffer gives process(buffer) bit for bit "
            "(#1116)") {
    for (int rms = 0; rms < 2; ++rms) {
      CAPTURE(rms);
      compressor plain;
      compressor keyed;
      for (compressor* c : {&plain, &keyed})
        c->threshold(-24.0f).ratio(6.0f).attack(2.0f).release(40.0f).makeup(3.0f).detector(
            rms != 0 ? YSE::DSP::MODULES::DETECT_RMS : YSE::DSP::MODULES::DETECT_PEAK);
      MULTICHANNELBUFFER a(2), b(2), key(2);
      for (auto* m : {&a, &b, &key})
        for (auto& ch : *m)
          ch.resize(128);
      SineGen g0(300.0f, 0.9f), g1(470.0f, 0.2f);
      for (int block = 0; block < 40; ++block) {
        g0.fill(a[0]);
        g1.fill(a[1]);
        b[0] = a[0];
        b[1] = a[1];
        key[0] = a[0];
        key[1] = a[1];
        plain.process(a);
        keyed.process(b, key);
        CHECK(TestHelpers::buffersNearlyEqual(a[0], b[0], 0.0f));
        CHECK(TestHelpers::buffersNearlyEqual(a[1], b[1], 0.0f));
      }
      CHECK(plain.gainReductionDb() == keyed.gainReductionDb());
      CHECK(plain.gainReductionDb() < -1.0f);
    }
  }

  TEST_CASE("compressor: a silent sidechain leaves a loud signal alone (#1116)") {
    compressor c;
    c.threshold(-30.0f).ratio(10.0f);
    MULTICHANNELBUFFER buf(1), key(1);
    buf[0].resize(256);
    key[0].resize(256);
    key[0] = 0.0f;
    SineGen g(440.0f, 1.0f);
    YSE::DSP::buffer dry(256);
    for (int block = 0; block < 50; ++block) {
      g.fill(buf[0]);
      dry = buf[0];
      c.process(buf, key);
      CHECK(TestHelpers::buffersNearlyEqual(buf[0], dry, 0.0f));
    }
    CHECK(c.gainReductionDb() == doctest::Approx(0.0f).epsilon(1e-6));
  }

  TEST_CASE("compressor: a loud sidechain ducks a quiet signal by the static curve (#1116)") {
    // A key at 0 dBFS against threshold -18, ratio 4: (0 + 18) * (1/4 - 1)
    // = -13.5 dB, applied to a -40 dBFS signal that alone would pass.
    compressor c;
    c.threshold(-18.0f).ratio(4.0f).attack(1.0f).release(50.0f);
    MULTICHANNELBUFFER buf(2), key(1);
    buf[0].resize(256);
    buf[1].resize(256);
    key[0].resize(256);
    key[0] = 1.0f;
    SineGen g0(440.0f, dbToLin(-40.0f)), g1(550.0f, dbToLin(-40.0f));
    YSE::DSP::buffer dry0(256), dry1(256);
    const int settle = static_cast<int>(0.5f * sr() / 256.0f) + 4;
    for (int block = 0; block < settle; ++block) {
      g0.fill(buf[0]);
      g1.fill(buf[1]);
      c.process(buf, key);
    }
    g0.fill(buf[0]);
    g1.fill(buf[1]);
    dry0 = buf[0];
    dry1 = buf[1];
    c.process(buf, key);
    const float expected = dbToLin(-13.5f);
    for (unsigned i = 0; i < 256; ++i) {
      CHECK(buf[0].getPtr()[i] == doctest::Approx(dry0.getPtr()[i] * expected).epsilon(1e-3));
      CHECK(buf[1].getPtr()[i] == doctest::Approx(dry1.getPtr()[i] * expected).epsilon(1e-3));
    }
    CHECK(c.gainReductionDb() == doctest::Approx(-13.5f).epsilon(1e-3));

    // The key's channels are linked: the loudest of them is the level.
    compressor two;
    two.threshold(-18.0f).ratio(4.0f).attack(1.0f).release(50.0f);
    MULTICHANNELBUFFER mono(1), keys(2);
    mono[0].resize(256);
    keys[0].resize(256);
    keys[1].resize(256);
    keys[0] = 0.01f;
    keys[1] = -1.0f;
    for (int block = 0; block < settle; ++block) {
      mono[0] = 0.001f;
      two.process(mono, keys);
    }
    CHECK(two.gainReductionDb() == doctest::Approx(-13.5f).epsilon(1e-3));
  }

  TEST_CASE("compressor: an empty or short sidechain falls back to the buffer (#1116)") {
    compressor plain, emptyKey, shortKey;
    for (compressor* c : {&plain, &emptyKey, &shortKey})
      c->threshold(-20.0f).ratio(8.0f);
    MULTICHANNELBUFFER a(1), b(1), d(1), none, brief(1);
    a[0].resize(128);
    b[0].resize(128);
    d[0].resize(128);
    brief[0].resize(64);
    brief[0] = 0.0f; // silent: would leave the signal alone if it were used
    SineGen g(330.0f, 0.8f);
    for (int block = 0; block < 20; ++block) {
      g.fill(a[0]);
      b[0] = a[0];
      d[0] = a[0];
      plain.process(a);
      emptyKey.process(b, none);
      shortKey.process(d, brief);
      CHECK(TestHelpers::buffersNearlyEqual(a[0], b[0], 0.0f));
      CHECK(TestHelpers::buffersNearlyEqual(a[0], d[0], 0.0f));
    }
    CHECK(plain.gainReductionDb() < -1.0f);
  }

  TEST_CASE("compressor: processing, with and without a sidechain, allocates nothing "
            "(#1116)") {
    compressor c;
    c.detector(YSE::DSP::MODULES::DETECT_RMS);
    MULTICHANNELBUFFER buf(2), key(1);
    for (auto& ch : buf)
      ch.resize(128);
    key[0].resize(128);
    buf[0] = 0.5f;
    buf[1] = 0.25f;
    key[0] = 1.0f;
    c.process(buf); // the first block sizes the scratch
    {
      TestHelpers::ProbeScope probe;
      c.process(buf);
      c.process(buf, key);
      c.threshold(-30.0f).ratio(2.0f).attack(5.0f).release(20.0f).makeup(2.0f).impact(0.5f);
      c.process(buf, key);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  // ─── no allocation on the process path (issue #1235) ──────────────────────

  TEST_CASE("compressor: the first block and longer blocks allocate nothing (#1235)") {
    // A freshly built compressor, no warm-up: the first block, then blocks
    // that grow past every length seen so far, with and without a sidechain.
    compressor c;
    c.detector(YSE::DSP::MODULES::DETECT_RMS).impact(0.5f);
    MULTICHANNELBUFFER first(2), longer(2), longest(1), key(1);
    for (auto& ch : first)
      ch.resize(64);
    for (auto& ch : longer)
      ch.resize(YSE::STANDARD_BUFFERSIZE);
    longest[0].resize(YSE::STANDARD_BUFFERSIZE * 4 + 37);
    key[0].resize(YSE::STANDARD_BUFFERSIZE * 4 + 37);
    first[0] = 0.5f;
    first[1] = 0.25f;
    longer[0] = 0.9f;
    longer[1] = -0.9f;
    longest[0] = 0.75f;
    key[0] = 1.0f;
    {
      TestHelpers::ProbeScope probe;
      c.process(first);
      c.process(longer);
      c.process(longest);
      c.process(longest, key);
      c.process(longer, key);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  TEST_CASE("compressor: a long block gives the same output as the same signal in short "
            "blocks (#1235)") {
    // Blocks longer than STANDARD_BUFFERSIZE are processed in pieces of at
    // most that size, so how the signal is cut into blocks does not change
    // the result while the settings stay put.
    const UInt total = YSE::STANDARD_BUFFERSIZE * 3 + 50;
    compressor whole, pieces;
    whole.threshold(-30.0f).ratio(6.0f).attack(1.0f).impact(0.7f);
    pieces.threshold(-30.0f).ratio(6.0f).attack(1.0f).impact(0.7f);

    MULTICHANNELBUFFER big(2);
    for (auto& ch : big)
      ch.resize(total);
    SineGen g0(300.0f, 0.9f), g1(450.0f, 0.6f);
    g0.fill(big[0]);
    g1.fill(big[1]);
    std::vector<float> in0(big[0].getPtr(), big[0].getPtr() + total);
    std::vector<float> in1(big[1].getPtr(), big[1].getPtr() + total);
    whole.process(big);

    MULTICHANNELBUFFER small(2);
    for (UInt offset = 0; offset < total;) {
      const UInt piece = std::min<UInt>(total - offset, YSE::STANDARD_BUFFERSIZE);
      small[0].resize(piece);
      small[1].resize(piece);
      std::copy(in0.begin() + offset, in0.begin() + offset + piece, small[0].getPtr());
      std::copy(in1.begin() + offset, in1.begin() + offset + piece, small[1].getPtr());
      pieces.process(small);
      for (UInt i = 0; i < piece; ++i) {
        REQUIRE(small[0].getPtr()[i] == big[0].getPtr()[offset + i]);
        REQUIRE(small[1].getPtr()[i] == big[1].getPtr()[offset + i]);
      }
      offset += piece;
    }
    CHECK(whole.gainReductionDb() == pieces.gainReductionDb());
  }

  // ─── range, gate, lookahead, sidechain filter, coupling, smoothing, freeze,
  //     progressive release (issue #1241) ────────────────────────────────────

  TEST_CASE("compressor: the controls of #1241 default to off") {
    compressor c;
    CHECK(c.range() == compressor::MAX_RANGE);
    CHECK_FALSE(c.freeze());
    CHECK_FALSE(c.progressiveRelease());
    CHECK(c.gainSmoothing() == 0.0f);
    CHECK_FALSE(c.gate());
    CHECK(c.gateThreshold() == compressor::DEFAULT_GATE_THRESHOLD);
    CHECK(c.lookahead() == 0u);
    CHECK(c.latency() == 0u);
    CHECK(c.maxLookahead() == compressor::DEFAULT_MAX_LOOKAHEAD);
    CHECK(c.maxChannels() == compressor::DEFAULT_MAX_CHANNELS);
    CHECK_FALSE(c.sidechainFilter());
    CHECK(c.sidechainFrequency() == compressor::DEFAULT_SIDECHAIN_FREQUENCY);
    CHECK(c.linked());

    c.range(12.0f)
        .freeze(true)
        .progressiveRelease(true)
        .gainSmoothing(30.0f)
        .gate(true)
        .gateThreshold(-50.0f)
        .lookahead(64)
        .sidechainFilter(true)
        .sidechainFrequency(250.0f)
        .linked(false);
    CHECK(c.range() == 12.0f);
    CHECK(c.freeze());
    CHECK(c.progressiveRelease());
    CHECK(c.gainSmoothing() == 30.0f);
    CHECK(c.gate());
    CHECK(c.gateThreshold() == -50.0f);
    CHECK(c.lookahead() == 64u);
    CHECK(c.latency() == 64u);
    CHECK(c.sidechainFilter());
    CHECK(c.sidechainFrequency() == 250.0f);
    CHECK_FALSE(c.linked());

    // Clamped; not finite: ignored.
    c.range(-3.0f);
    CHECK(c.range() == 0.0f);
    c.range(500.0f);
    CHECK(c.range() == compressor::MAX_RANGE);
    c.range(std::nanf(""));
    CHECK(c.range() == compressor::MAX_RANGE);
    c.gainSmoothing(-1.0f);
    CHECK(c.gainSmoothing() == 0.0f);
    c.gainSmoothing(1.0e6f);
    CHECK(c.gainSmoothing() == compressor::MAX_SMOOTHING);
    c.gateThreshold(-500.0f);
    CHECK(c.gateThreshold() == compressor::MIN_GATE_THRESHOLD);
    c.gateThreshold(10.0f);
    CHECK(c.gateThreshold() == 0.0f);
    c.sidechainFrequency(1.0f);
    CHECK(c.sidechainFrequency() == compressor::MIN_SIDECHAIN_FREQUENCY);
    c.sidechainFrequency(1.0e6f);
    CHECK(c.sidechainFrequency() == compressor::MAX_SIDECHAIN_FREQUENCY);
    c.lookahead(1000000);
    CHECK(c.lookahead() == c.maxLookahead());

    compressor big(4096, 2);
    CHECK(big.maxLookahead() == 4096u);
    CHECK(big.maxChannels() == 2u);
    compressor huge(10000000, 1000);
    CHECK(huge.maxLookahead() == compressor::MAX_LOOKAHEAD);
    CHECK(huge.maxChannels() == compressor::MAX_CHANNELS);
  }

  TEST_CASE("compressor: with the #1241 controls at their defaults the output is bit for bit "
            "the compressor before them") {
    struct config {
      bool rms;
      Flt threshold, ratio, attack, release, makeup, impact;
      UInt channels, keyChannels, block;
    };
    const config configs[] = {
        {false, -18.0f, 4.0f, 10.0f, 100.0f, 0.0f, 1.0f, 2, 0, 128},
        {true, -24.0f, 6.0f, 2.0f, 40.0f, 3.0f, 1.0f, 2, 0, 64},
        {false, -30.0f, 20.0f, 0.1f, 5.0f, 6.0f, 0.6f, 1, 0, 300},
        {true, -40.0f, 2.0f, 30.0f, 300.0f, -2.0f, 0.3f, 3, 0, 1000},
        {false, -20.0f, 8.0f, 1.0f, 50.0f, 0.0f, 1.0f, 2, 1, 128},
        {true, -20.0f, 8.0f, 1.0f, 50.0f, 0.0f, 0.8f, 1, 2, 77},
    };
    for (const config& cf : configs) {
      CAPTURE(cf.rms);
      CAPTURE(cf.block);
      CAPTURE(cf.channels);
      CAPTURE(cf.keyChannels);
      // The default constructor and the one ~comp uses.
      compressor now;
      compressor sized(4096, 2);
      legacyCompressor before;
      for (compressor* c : {&now, &sized}) {
        c->detector(cf.rms ? YSE::DSP::MODULES::DETECT_RMS : YSE::DSP::MODULES::DETECT_PEAK)
            .threshold(cf.threshold)
            .ratio(cf.ratio)
            .attack(cf.attack)
            .release(cf.release)
            .makeup(cf.makeup)
            .impact(cf.impact);
      }
      before.rms = cf.rms;
      before.threshold = cf.threshold;
      before.ratio = cf.ratio;
      before.attack = cf.attack;
      before.release = cf.release;
      before.makeup = cf.makeup;
      before.impact(cf.impact);

      MULTICHANNELBUFFER a(cf.channels), b(cf.channels), r(cf.channels), key(cf.keyChannels);
      for (auto* m : {&a, &b, &r, &key})
        for (auto& ch : *m)
          ch.resize(cf.block);
      UInt start = 0;
      Flt deepest = 0.0f;
      for (int block = 0; block < 60; ++block) {
        for (UInt ch = 0; ch < cf.channels; ++ch) {
          fillBursts(a[ch], start, ch);
          b[ch] = a[ch];
          r[ch] = a[ch];
        }
        for (UInt ch = 0; ch < cf.keyChannels; ++ch)
          fillBursts(key[ch], start + 450, ch + 3);
        start += cf.block;
        if (cf.keyChannels > 0) {
          now.process(a, key);
          sized.process(b, key);
          before.process(r, key);
        } else {
          now.process(a);
          sized.process(b);
          before.process(r);
        }
        for (UInt ch = 0; ch < cf.channels; ++ch) {
          REQUIRE(sameBits(a[ch], r[ch]));
          REQUIRE(sameBits(b[ch], r[ch]));
        }
        REQUIRE(now.gainReductionDb() == before.reductionDb);
        REQUIRE(sized.gainReductionDb() == before.reductionDb);
        deepest = std::min(deepest, before.reductionDb);
      }
      CHECK(deepest < -1.0f); // it did compress
    }
  }

  TEST_CASE("compressor: range caps the gain reduction at the set dB (#1241)") {
    // 0 dBFS against -40, ratio 20: (0 + 40) * (1/20 - 1) = -38 dB unlimited.
    auto reductionWith = [](Flt range) {
      compressor c;
      c.threshold(-40.0f).ratio(20.0f).attack(1.0f).release(50.0f).range(range);
      MULTICHANNELBUFFER buf(1);
      buf[0].resize(256);
      for (int b = 0; b < static_cast<int>(0.5f * sr() / 256.0f) + 4; ++b) {
        buf[0] = 1.0f;
        c.process(buf);
      }
      return c.gainReductionDb();
    };
    CHECK(reductionWith(compressor::MAX_RANGE) == doctest::Approx(-38.0f).epsilon(1e-3));
    CHECK(reductionWith(12.0f) == doctest::Approx(-12.0f).epsilon(1e-3));
    CHECK(reductionWith(3.0f) == doctest::Approx(-3.0f).epsilon(1e-3));
    CHECK(reductionWith(0.0f) == doctest::Approx(0.0f).epsilon(1e-5));
    // A range deeper than the curve reaches changes nothing.
    CHECK(reductionWith(60.0f) == doctest::Approx(-38.0f).epsilon(1e-3));
  }

  TEST_CASE("compressor: the noise gate attenuates below its threshold (#1241)") {
    // The compressor's own threshold is at 0 dBFS, so only the gate acts.
    auto outputDb = [](bool gate, Flt inputDb) {
      compressor c;
      c.threshold(0.0f).attack(1.0f).release(20.0f).gate(gate).gateThreshold(-50.0f);
      MULTICHANNELBUFFER buf(1);
      buf[0].resize(256);
      SineGen g(1000.0f, dbToLin(inputDb));
      for (int b = 0; b < static_cast<int>(1.0f * sr() / 256.0f); ++b) {
        g.fill(buf[0]);
        c.process(buf);
      }
      float peak = 0.0f;
      for (int b = 0; b < 8; ++b) {
        g.fill(buf[0]);
        c.process(buf);
        peak = std::max(peak, peakAbs(buf[0]));
      }
      return 20.0f * std::log10(peak + 1.0e-30f);
    };
    // Under the gate threshold: through without the gate, gone with it.
    CHECK(outputDb(false, -70.0f) == doctest::Approx(-70.0f).epsilon(0.01));
    CHECK(outputDb(true, -70.0f) < -150.0f);
    // Over it: the gate stays open (the held level keeps it open at the
    // zero crossings).
    CHECK(outputDb(true, -30.0f) == doctest::Approx(-30.0f).epsilon(0.01));
  }

  TEST_CASE("compressor: lookahead delays the output by the reported latency and catches a "
            "transient (#1241)") {
    const UInt lookahead = 48;
    {
      // Under the threshold the output is the input, delayed by latency().
      compressor c;
      c.lookahead(lookahead);
      REQUIRE(c.latency() == lookahead);
      MULTICHANNELBUFFER buf(2);
      buf[0].resize(100);
      buf[1].resize(100);
      std::vector<float> in0, in1, out0, out1;
      for (int block = 0; block < 6; ++block) {
        for (UInt i = 0; i < 100; ++i) {
          const float v = 0.01f * std::sin(0.05f * static_cast<float>(block * 100 + i));
          buf[0].getPtr()[i] = v;
          buf[1].getPtr()[i] = -0.5f * v;
          in0.push_back(v);
          in1.push_back(-0.5f * v);
        }
        c.process(buf);
        out0.insert(out0.end(), buf[0].getPtr(), buf[0].getPtr() + 100);
        out1.insert(out1.end(), buf[1].getPtr(), buf[1].getPtr() + 100);
      }
      for (UInt i = 0; i < lookahead; ++i) {
        CHECK(out0[i] == 0.0f);
        CHECK(out1[i] == 0.0f);
      }
      for (std::size_t i = lookahead; i < out0.size(); ++i) {
        REQUIRE(out0[i] == in0[i - lookahead]);
        REQUIRE(out1[i] == in1[i - lookahead]);
      }
    }
    // A step from silence to full scale against -20 dB, ratio 20, fastest
    // attack: without lookahead the first samples get through nearly
    // untouched; with it the gain is already down when the step comes out.
    auto firstPeak = [](UInt la) {
      compressor c;
      c.threshold(-20.0f).ratio(20.0f).attack(0.1f).release(100.0f).lookahead(la);
      MULTICHANNELBUFFER buf(1);
      buf[0].resize(128);
      buf[0] = 0.0f;
      c.process(buf);
      buf[0] = 1.0f;
      c.process(buf);
      return peakAbs(buf[0]);
    };
    CHECK(firstPeak(0) > 0.8f);
    CHECK(firstPeak(lookahead) < 0.15f);
  }

  TEST_CASE("compressor: the sidechain high-pass stops low-frequency ducking (#1241)") {
    // A quiet signal, keyed by a loud tone: 40 Hz is far under a 500 Hz
    // high-pass, 5 kHz far over it.
    auto reduction = [](bool filter, float keyHz) {
      compressor c;
      c.threshold(-18.0f).ratio(4.0f).attack(1.0f).release(50.0f);
      c.sidechainFilter(filter).sidechainFrequency(500.0f);
      MULTICHANNELBUFFER buf(1), key(1);
      buf[0].resize(256);
      key[0].resize(256);
      SineGen k(keyHz, 1.0f);
      float deepest = 0.0f;
      for (int b = 0; b < static_cast<int>(1.0f * sr() / 256.0f); ++b) {
        buf[0] = 0.001f;
        k.fill(key[0]);
        c.process(buf, key);
        if (b > 40) deepest = std::min(deepest, c.gainReductionDb());
      }
      return deepest;
    };
    CHECK(reduction(false, 40.0f) < -10.0f); // a bass key ducks unfiltered
    CHECK(reduction(true, 40.0f) > -0.5f); // and not through the high-pass
    CHECK(reduction(true, 5000.0f) < -10.0f); // a high key still ducks
    // The same with the buffer as its own key.
    compressor c;
    c.threshold(-18.0f).ratio(4.0f).attack(1.0f).sidechainFilter(true).sidechainFrequency(500.0f);
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(256);
    SineGen bass(40.0f, 1.0f);
    for (int b = 0; b < 200; ++b) {
      bass.fill(buf[0]);
      c.process(buf);
    }
    CHECK(c.gainReductionDb() > -0.5f);
  }

  TEST_CASE("compressor: unlinked coupling ducks only the loud channel (#1241)") {
    for (int rms = 0; rms < 2; ++rms) {
      CAPTURE(rms);
      compressor c;
      c.detector(rms != 0 ? YSE::DSP::MODULES::DETECT_RMS : YSE::DSP::MODULES::DETECT_PEAK)
          .threshold(-30.0f)
          .ratio(8.0f)
          .attack(5.0f)
          .release(120.0f)
          .linked(false);
      MULTICHANNELBUFFER buf(2);
      buf[0].resize(256);
      buf[1].resize(256);
      SineGen loud(440.0f, dbToLin(0.0f));
      SineGen quiet(660.0f, dbToLin(-40.0f));
      YSE::DSP::buffer dry1(256);
      float in0 = 0.0f, out0 = 0.0f;
      for (int b = 0; b < static_cast<int>(1.0f * sr() / 256.0f); ++b) {
        loud.fill(buf[0]);
        quiet.fill(buf[1]);
        dry1 = buf[1];
        in0 = TestHelpers::measureRms(buf[0]);
        c.process(buf);
        out0 = TestHelpers::measureRms(buf[0]);
        // The quiet channel is left exactly as it came in.
        REQUIRE(sameBits(buf[1], dry1));
      }
      CHECK(out0 < in0 * 0.3f); // the loud one is turned down
      CHECK(c.gainReductionDb() < -10.0f); // the deepest channel
    }

    // A stereo key keys each channel with its own key channel.
    compressor c;
    c.threshold(-18.0f).ratio(4.0f).attack(1.0f).release(50.0f).linked(false);
    MULTICHANNELBUFFER buf(2), key(2);
    for (auto* m : {&buf, &key})
      for (auto& ch : *m)
        ch.resize(256);
    key[0] = 0.0f;
    key[1] = 1.0f;
    for (int b = 0; b < 100; ++b) {
      buf[0] = 0.01f;
      buf[1] = 0.01f;
      c.process(buf, key);
    }
    CHECK(buf[0].getPtr()[255] == 0.01f);
    CHECK(buf[1].getPtr()[255] == doctest::Approx(0.01f * dbToLin(-13.5f)).epsilon(1e-3));
  }

  TEST_CASE("compressor: freeze holds the gain (#1241)") {
    compressor c;
    c.threshold(-24.0f).ratio(8.0f).attack(2.0f).release(50.0f);
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(256);
    for (int b = 0; b < 100; ++b) {
      buf[0] = 1.0f;
      c.process(buf);
    }
    const Flt held = c.gainReductionDb();
    REQUIRE(held < -10.0f);
    c.freeze(true);
    for (int b = 0; b < 200; ++b) {
      buf[0] = 0.0f;
      c.process(buf);
      REQUIRE(c.gainReductionDb() == held);
    }
    // Louder still: held too.
    buf[0] = 4.0f;
    c.process(buf);
    CHECK(c.gainReductionDb() == held);
    CHECK(buf[0].getPtr()[0] == doctest::Approx(4.0f * dbToLin(held)).epsilon(1e-4));
    c.freeze(false);
    for (int b = 0; b < 400; ++b) {
      buf[0] = 0.0f;
      c.process(buf);
    }
    CHECK(c.gainReductionDb() > -0.1f);
  }

  TEST_CASE("compressor: progressive release recovers faster from deep reduction (#1241)") {
    auto afterRelease = [](bool progressive) {
      compressor c;
      c.threshold(-40.0f).ratio(20.0f).attack(1.0f).release(200.0f).progressiveRelease(progressive);
      MULTICHANNELBUFFER buf(1);
      buf[0].resize(256);
      for (int b = 0; b < 100; ++b) {
        buf[0] = 1.0f;
        c.process(buf);
      }
      // 50 ms of silence.
      for (int b = 0; b < static_cast<int>(0.05f * sr() / 256.0f); ++b) {
        buf[0] = 0.0f;
        c.process(buf);
      }
      return c.gainReductionDb();
    };
    const Flt plain = afterRelease(false);
    const Flt progressive = afterRelease(true);
    CAPTURE(plain);
    CAPTURE(progressive);
    CHECK(plain < -10.0f);
    CHECK(progressive > plain + 6.0f);
  }

  TEST_CASE("compressor: gain smoothing slows the gain down (#1241)") {
    auto afterAttack = [](Flt smoothing) {
      compressor c;
      c.threshold(-30.0f).ratio(10.0f).attack(0.1f).gainSmoothing(smoothing);
      MULTICHANNELBUFFER buf(1);
      buf[0].resize(static_cast<UInt>(0.002f * sr())); // 2 ms
      buf[0] = 1.0f;
      c.process(buf);
      return c.gainReductionDb();
    };
    const Flt sharp = afterAttack(0.0f);
    const Flt smooth = afterAttack(20.0f);
    CHECK(sharp < -20.0f);
    CHECK(smooth > -5.0f);
    CHECK(smooth < -0.5f);
    // Settled, both reach the same reduction.
    compressor c;
    c.threshold(-30.0f).ratio(10.0f).attack(0.1f).gainSmoothing(20.0f);
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(256);
    for (int b = 0; b < static_cast<int>(0.5f * sr() / 256.0f); ++b) {
      buf[0] = 1.0f;
      c.process(buf);
    }
    CHECK(c.gainReductionDb() == doctest::Approx(-27.0f).epsilon(1e-3));
  }

  TEST_CASE("compressor: every #1241 control on, with lookahead changes at run time, "
            "allocates nothing") {
    compressor c(1024, 2);
    c.detector(YSE::DSP::MODULES::DETECT_RMS)
        .range(10.0f)
        .freeze(false)
        .progressiveRelease(true)
        .gainSmoothing(5.0f)
        .gate(true)
        .gateThreshold(-50.0f)
        .lookahead(100)
        .sidechainFilter(true)
        .sidechainFrequency(200.0f)
        .linked(false)
        .impact(0.7f);
    MULTICHANNELBUFFER first(2), longer(3), key(1);
    for (auto& ch : first)
      ch.resize(64);
    for (auto& ch : longer)
      ch.resize(YSE::STANDARD_BUFFERSIZE * 3 + 11);
    key[0].resize(YSE::STANDARD_BUFFERSIZE * 3 + 11);
    first[0] = 0.5f;
    first[1] = 0.25f;
    for (auto& ch : longer)
      ch = 0.8f;
    key[0] = 1.0f;
    {
      TestHelpers::ProbeScope probe;
      c.process(first);
      c.process(longer);
      c.process(longer, key);
      c.lookahead(1000000); // clamped to the delay line
      c.process(first);
      c.lookahead(7);
      c.linked(true).freeze(true).sidechainFrequency(3000.0f);
      c.process(longer, key);
      c.process(longer);
      c.linked(false).detector(YSE::DSP::MODULES::DETECT_PEAK).lookahead(0);
      c.process(longer, key);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(c.lookahead() == 0u);
  }

} // TEST_SUITE("dsp")
