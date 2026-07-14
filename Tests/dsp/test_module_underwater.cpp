// Behavioural tests for the underwater effect MODULE (issue #327).
//
// DSP::MODULES::underWater packages the legacy channel-path underwater
// treatment as a chainable dspObject whose depth is a *control input*: all
// channels are mixed down to a position-neutral average, darkened with a
// depth-driven low-pass (cutoff = MidiToFreq(140 - 5 * depth), floored at
// 200 Hz), and crossfaded against the positioned signal as the listener
// sinks. Writes to depth() are atomic stores, callable from any control
// thread at control rate.
//
// The acceptance criteria (issue #327) drive the cases below:
//   - the module reproduces the legacy treatment: transparent at depth <= 1,
//     a linear crossfade toward the low-passed neutral mix up to depth 5,
//     fully position-neutral beyond
//   - depth-driven parameter updates work as ordinary control-rate writes
//     (manual driving from user code)
//   - steady-state process() allocates nothing; a channel-count change is
//     tolerated (the one allocation-permitted path)
//
// No audio device required — SAMPLERATE is initialised by the
// portaudioDeviceManager translation unit at static-initialisation time.

#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules/underWater.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

namespace {

  // Fill every channel of `buf` with a constant value.
  void fill(MULTICHANNELBUFFER& buf, float value) {
    for (auto& ch : buf)
      ch = value;
  }

  // Write a sine of the given frequency into `ch`, continuing from `phase`
  // (updated in place) so multi-block signals stay continuous.
  void fillSine(YSE::DSP::buffer& ch, float freq, double& phase) {
    float* ptr = ch.getPtr();
    const double step = 2.0 * 3.14159265358979323846 * freq / YSE::SAMPLERATE;
    for (unsigned i = 0; i < ch.getLength(); ++i) {
      ptr[i] = static_cast<float>(std::sin(phase));
      phase += step;
    }
  }

  bool allFinite(MULTICHANNELBUFFER& buf) {
    for (auto& ch : buf) {
      float* ptr = ch.getPtr();
      for (unsigned i = 0; i < ch.getLength(); ++i)
        if (!std::isfinite(ptr[i])) return false;
    }
    return true;
  }

  // Run `blocks` blocks of constant per-channel input through the module so
  // the one-pole filter settles, returning the buffer state after the last
  // block. `values` supplies one constant per channel.
  MULTICHANNELBUFFER settle(YSE::DSP::MODULES::underWater& fx, const std::vector<float>& values,
                            int blocks = 30) {
    MULTICHANNELBUFFER buf(values.size());
    for (int i = 0; i < blocks; ++i) {
      for (size_t ch = 0; ch < values.size(); ++ch)
        buf[ch] = values[ch];
      fx.process(buf);
    }
    return buf;
  }

} // anonymous namespace

TEST_SUITE("dsp") {

  // ─── the depth control input ──────────────────────────────────────────────

  TEST_CASE("underWater: defaults to zero depth (transparent)") {
    YSE::DSP::MODULES::underWater fx;
    CHECK(fx.depth() == doctest::Approx(0.0f));
  }

  TEST_CASE("underWater: depth is clamped at the surface (no negative depth)") {
    YSE::DSP::MODULES::underWater fx;
    fx.depth(-4.0f);
    CHECK(fx.depth() == doctest::Approx(0.0f));
    fx.depth(2.5f);
    CHECK(fx.depth() == doctest::Approx(2.5f));
  }

  // ─── transparency at / above the surface threshold ────────────────────────

  TEST_CASE("underWater: depth <= 1 passes the buffer through untouched") {
    YSE::DSP::MODULES::underWater fx;

    MULTICHANNELBUFFER buf(2);
    for (float depth : {0.0f, 0.5f, 1.0f}) {
      fx.depth(depth);
      buf[0] = 0.4f;
      buf[1] = -0.2f;
      fx.process(buf);

      float* p0 = buf[0].getPtr();
      float* p1 = buf[1].getPtr();
      for (unsigned i = 0; i < buf[0].getLength(); ++i) {
        CHECK(p0[i] == doctest::Approx(0.4f));
        CHECK(p1[i] == doctest::Approx(-0.2f));
      }
    }
  }

  // ─── the crossfade toward the position-neutral mix ────────────────────────

  TEST_CASE("underWater: intermediate depth blends toward the channel average") {
    // ch0 = 0.8, ch1 = 0.2 -> neutral mix = 0.5. At depth 2.5 the blend is
    // half positioned / half neutral: ch0 -> 0.65, ch1 -> 0.35. DC input, so
    // once the one-pole low-pass settles the values are exact.
    YSE::DSP::MODULES::underWater fx;
    fx.depth(2.5f);

    MULTICHANNELBUFFER buf = settle(fx, {0.8f, 0.2f});

    float* p0 = buf[0].getPtr();
    float* p1 = buf[1].getPtr();
    const unsigned last = buf[0].getLength() - 1;
    CHECK(p0[last] == doctest::Approx(0.65f).epsilon(1e-3));
    CHECK(p1[last] == doctest::Approx(0.35f).epsilon(1e-3));
  }

  TEST_CASE("underWater: beyond depth 5 position information is discarded") {
    // Fully submerged: every channel must carry the identical neutral mix
    // (0.8 + 0.2) / 2 = 0.5.
    YSE::DSP::MODULES::underWater fx;
    fx.depth(6.0f);

    MULTICHANNELBUFFER buf = settle(fx, {0.8f, 0.2f});

    CHECK(TestHelpers::buffersNearlyEqual(buf[0], buf[1]));
    const unsigned last = buf[0].getLength() - 1;
    CHECK(buf[0].getPtr()[last] == doctest::Approx(0.5f).epsilon(1e-3));
  }

  // ─── the depth-driven low-pass (TEST_PLAN Phase 10 energy check) ──────────

  TEST_CASE("underWater: high frequencies lose more power than low frequencies") {
    // At depth 10 the cutoff is MidiToFreq(90) ~ 1.47 kHz. An 8 kHz sine must
    // come out well below its input level; a 200 Hz sine mostly survives.
    auto surviving = [](float freq) {
      YSE::DSP::MODULES::underWater fx;
      fx.depth(10.0f);
      MULTICHANNELBUFFER buf(1);
      double phase = 0.0;
      float rms = 0.0f;
      for (int i = 0; i < 40; ++i) { // let the filter reach steady state
        fillSine(buf[0], freq, phase);
        fx.process(buf);
        rms = TestHelpers::measureRms(buf[0]);
      }
      return rms; // input sine RMS is 1/sqrt(2) ~ 0.707
    };

    const float high = surviving(8000.0f);
    const float low = surviving(200.0f);

    CHECK(high < 0.5f * 0.707f); // strongly attenuated above the cutoff
    CHECK(low > 0.7f * 0.707f); // mostly preserved below it
    CHECK(low > 2.0f * high);
  }

  TEST_CASE("underWater: extreme depth stays finite (cutoff floors at 200 Hz)") {
    YSE::DSP::MODULES::underWater fx;
    fx.depth(1000.0f);

    MULTICHANNELBUFFER buf(2);
    double phase = 0.0;
    for (int i = 0; i < 50; ++i) {
      fillSine(buf[0], 440.0f, phase);
      buf[1] = 0.3f;
      fx.process(buf);
    }
    CHECK(allFinite(buf));
  }

  TEST_CASE("underWater: mono input stays bounded over many blocks") {
    // Regression guard: the legacy channel-path code accumulated the previous
    // block's mixdown into the next one (divided by the channel count), which
    // diverges on a single channel. The module zeroes its scratch per block,
    // so a long constant mono feed must settle, not grow.
    YSE::DSP::MODULES::underWater fx;
    fx.depth(3.0f);

    MULTICHANNELBUFFER buf(1);
    float peak = 0.0f;
    for (int i = 0; i < 400; ++i) {
      buf[0] = 0.5f;
      fx.process(buf);
      peak = buf[0].maxValue();
    }
    CHECK(peak == doctest::Approx(0.5f).epsilon(1e-2));
  }

  // ─── control-rate driving ─────────────────────────────────────────────────

  TEST_CASE("underWater: depth moves engage and release the effect between blocks") {
    YSE::DSP::MODULES::underWater fx;
    MULTICHANNELBUFFER buf(2);

    // Engaged: distinct channels converge toward each other.
    fx.depth(6.0f);
    for (int i = 0; i < 30; ++i) {
      buf[0] = 0.8f;
      buf[1] = 0.2f;
      fx.process(buf);
    }
    CHECK(TestHelpers::buffersNearlyEqual(buf[0], buf[1]));

    // Released: the very next block passes through untouched.
    fx.depth(0.0f);
    buf[0] = 0.8f;
    buf[1] = 0.2f;
    fx.process(buf);
    const unsigned last = buf[0].getLength() - 1;
    CHECK(buf[0].getPtr()[last] == doctest::Approx(0.8f));
    CHECK(buf[1].getPtr()[last] == doctest::Approx(0.2f));
  }

  // ─── RT discipline ────────────────────────────────────────────────────────

  TEST_CASE("underWater: steady-state process allocates nothing") {
    YSE::DSP::MODULES::underWater fx;
    fx.depth(3.0f);

    // Warm up: the first engaged process sizes the mixdown scratch (the one
    // permitted allocation).
    MULTICHANNELBUFFER buf(2);
    fill(buf, 0.25f);
    fx.process(buf);

    {
      TestHelpers::ProbeScope probe;
      for (int i = 0; i < 20; ++i) {
        fx.depth(2.0f + 0.1f * static_cast<float>(i)); // live control writes
        fill(buf, 0.25f);
        fx.process(buf);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  TEST_CASE("underWater: tolerates a changing channel count between calls") {
    YSE::DSP::MODULES::underWater fx;
    fx.depth(4.0f);

    MULTICHANNELBUFFER mono(1);
    mono[0] = 0.5f;
    fx.process(mono);

    MULTICHANNELBUFFER quad(4);
    fill(quad, 0.5f);
    fx.process(quad);

    MULTICHANNELBUFFER stereo(2);
    fill(stereo, 0.5f);
    fx.process(stereo);

    CHECK(allFinite(stereo));
  }

} // TEST_SUITE("dsp")
