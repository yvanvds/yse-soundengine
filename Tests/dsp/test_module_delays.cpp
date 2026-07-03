// Tests for the DSP delay MODULES — basicDelay, highPassDelay, lowPassDelay.
// These wrap a DSP::delay line with up to three taps (FIRST/SECOND/THIRD).
//
// basicDelay::set takes the delay time as a Flt but the underlying delay line
// (delay::read in delay.cpp) reads the value in *milliseconds* via
//   delaySamples = (UInt)(SAMPLERATE * delayTime * 0.001f) + currentLength
// so every tap is quantised to integer ms; at 44100 Hz, 1 ms ≈ 44 samples.
//
// In basicDelay::process(), the wet result is (dry + Σ gain_i * delayed_i) /
// delayCount, then calculateImpact (with default impact=1.0) replaces buffer[0]
// with the wet output.  With all gains 0, the output equals the dry input.
//
// No audio device required — SAMPLERATE is initialised to 44100 by the
// portaudioDeviceManager translation unit at static-initialisation time.

#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules/delay/basicDelay.hpp"
#include "dsp/modules/delay/highpassDelay.hpp"
#include "dsp/modules/delay/lowpassDelay.hpp"
#include "headers/defines.hpp"
#include "support/audio_helpers.hpp"

static constexpr float kPi = 3.14159265358979323846f;

namespace {

  inline void fillSine(YSE::DSP::buffer& buf, float freq, float sr = 44100.0f) {
    float* p = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i)
      p[i] = std::sin(2.0f * kPi * freq * static_cast<float>(i) / sr);
  }

  inline void zeroFill(YSE::DSP::buffer& buf) {
    buf = 0.0f;
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

} // anonymous namespace

TEST_SUITE("dsp") {

  // ─── basicDelay ───────────────────────────────────────────────────────────────

  TEST_CASE("basicDelay: default time/gain are zero for all taps") {
    YSE::DSP::MODULES::basicDelay d;
    using D = YSE::DSP::MODULES::basicDelay;
    CHECK(d.time(D::FIRST) == doctest::Approx(0.0f));
    CHECK(d.time(D::SECOND) == doctest::Approx(0.0f));
    CHECK(d.time(D::THIRD) == doctest::Approx(0.0f));
    CHECK(d.gain(D::FIRST) == doctest::Approx(0.0f));
    CHECK(d.gain(D::SECOND) == doctest::Approx(0.0f));
    CHECK(d.gain(D::THIRD) == doctest::Approx(0.0f));
  }

  TEST_CASE("basicDelay: set/get round-trip on all three taps") {
    YSE::DSP::MODULES::basicDelay d;
    using D = YSE::DSP::MODULES::basicDelay;
    d.set(D::FIRST, 10.0f, 0.5f);
    d.set(D::SECOND, 20.0f, 0.3f);
    d.set(D::THIRD, 30.0f, 0.2f);
    CHECK(d.time(D::FIRST) == doctest::Approx(10.0f).epsilon(1e-5f));
    CHECK(d.gain(D::FIRST) == doctest::Approx(0.5f).epsilon(1e-5f));
    CHECK(d.time(D::SECOND) == doctest::Approx(20.0f).epsilon(1e-5f));
    CHECK(d.gain(D::SECOND) == doctest::Approx(0.3f).epsilon(1e-5f));
    CHECK(d.time(D::THIRD) == doctest::Approx(30.0f).epsilon(1e-5f));
    CHECK(d.gain(D::THIRD) == doctest::Approx(0.2f).epsilon(1e-5f));
  }

  TEST_CASE("basicDelay: with all gains 0, output equals input (single dry path)") {
    YSE::DSP::MODULES::basicDelay d;
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    fillSine(buf[0], 440.0f);
    // Copy reference before processing.
    YSE::DSP::buffer ref(128);
    ref = buf[0];
    d.process(buf);
    // result = dry / 1 = dry; calculateImpact then writes result back into buf[0].
    CHECK(TestHelpers::buffersNearlyEqual(buf[0], ref, 1e-5f));
  }

  TEST_CASE("basicDelay: tap with positive gain produces a delayed copy of the input") {
    YSE::DSP::MODULES::basicDelay d;
    using D = YSE::DSP::MODULES::basicDelay;
    d.set(D::FIRST, 5.0f, 1.0f); // 5 ms tap at unit gain

    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);

    // Block 1: feed an impulse so the delay line has known contents.
    zeroFill(buf[0]);
    buf[0].getPtr()[0] = 1.0f;
    d.process(buf);

    float energyBefore = TestHelpers::measureRms(buf[0]);

    // Subsequent silent blocks should surface the delayed impulse with non-zero
    // RMS at some point within the first ~10 blocks (10 blocks ≈ 29 ms).
    bool sawNonZeroTail = false;
    for (int iter = 0; iter < 10; ++iter) {
      zeroFill(buf[0]);
      d.process(buf);
      if (TestHelpers::measureRms(buf[0]) > 1e-4f) {
        sawNonZeroTail = true;
        break;
      }
    }
    CHECK(energyBefore > 0.0f);
    CHECK(sawNonZeroTail);
  }

  TEST_CASE("basicDelay: enabling a tap changes the output relative to dry input") {
    // With all gains 0 the wet path equals dry (verified above).  With one
    // tap at unit gain the steady-state output averages dry + delayed and
    // therefore differs from the dry signal on at least some samples.
    YSE::DSP::MODULES::basicDelay d;
    using D = YSE::DSP::MODULES::basicDelay;
    d.set(D::FIRST, 5.0f, 1.0f);

    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    YSE::DSP::buffer dry(128);

    // Warm up to a steady state so the delay line has content.
    for (int iter = 0; iter < 20; ++iter) {
      fillSine(buf[0], 440.0f);
      d.process(buf);
    }
    // Final iteration: capture the dry input and the wet output side-by-side.
    fillSine(buf[0], 440.0f);
    fillSine(dry, 440.0f);
    d.process(buf);
    CHECK_FALSE(TestHelpers::buffersNearlyEqual(buf[0], dry, 1e-3f));
  }

  TEST_CASE("basicDelay: three taps produce a bounded, non-zero output for sustained sine") {
    YSE::DSP::MODULES::basicDelay d;
    using D = YSE::DSP::MODULES::basicDelay;
    d.set(D::FIRST, 3.0f, 1.0f);
    d.set(D::SECOND, 6.0f, 1.0f);
    d.set(D::THIRD, 9.0f, 1.0f);

    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    for (int iter = 0; iter < 20; ++iter) {
      fillSine(buf[0], 440.0f);
      d.process(buf);
      CHECK(maxAbs(buf[0]) < 4.0f);
    }
    CHECK(TestHelpers::measureRms(buf[0]) > 0.0f);
  }

  TEST_CASE("basicDelay: process tolerates a change in input buffer length") {
    YSE::DSP::MODULES::basicDelay d;
    using D = YSE::DSP::MODULES::basicDelay;
    d.set(D::FIRST, 2.0f, 0.5f);

    MULTICHANNELBUFFER buf(1);
    buf[0].resize(64);
    fillSine(buf[0], 500.0f);
    d.process(buf);
    CHECK(buf[0].getLength() == 64u);

    buf[0].resize(256);
    fillSine(buf[0], 500.0f);
    d.process(buf);
    CHECK(buf[0].getLength() == 256u);
  }

  TEST_CASE("basicDelay: output stays bounded for sustained sine input") {
    YSE::DSP::MODULES::basicDelay d;
    using D = YSE::DSP::MODULES::basicDelay;
    d.set(D::FIRST, 4.0f, 0.7f);
    d.set(D::SECOND, 8.0f, 0.5f);

    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    for (int iter = 0; iter < 30; ++iter) {
      fillSine(buf[0], 1000.0f);
      d.process(buf);
      CHECK(maxAbs(buf[0]) < 4.0f);
    }
  }

  // ─── highPassDelay ────────────────────────────────────────────────────────────

  TEST_CASE("highPassDelay: default frequency is 1000 Hz") {
    YSE::DSP::MODULES::highPassDelay d;
    CHECK(d.frequency() == doctest::Approx(1000.0f).epsilon(1e-5f));
  }

  TEST_CASE("highPassDelay: frequency setter/getter round-trip") {
    YSE::DSP::MODULES::highPassDelay d;
    d.frequency(2500.0f);
    CHECK(d.frequency() == doctest::Approx(2500.0f).epsilon(1e-5f));
  }

  TEST_CASE("highPassDelay: process is bounded and shapes the tail energy") {
    YSE::DSP::MODULES::highPassDelay d;
    using D = YSE::DSP::MODULES::basicDelay;
    d.frequency(2000.0f);
    d.set(D::FIRST, 5.0f, 0.8f);

    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    for (int iter = 0; iter < 20; ++iter) {
      fillSine(buf[0], 500.0f); // 500 Hz, below the 2 kHz pre-filter
      d.process(buf);
      CHECK(maxAbs(buf[0]) < 5.0f);
    }
  }

  TEST_CASE("highPassDelay: low-frequency tap is attenuated relative to high-frequency tap") {
    // Two identical delay-line configurations, both with a 2 kHz highpass
    // pre-filter, fed with a 500 Hz sine vs a 5 kHz sine.  The pre-filter
    // attenuates the low-frequency signal before it enters the delay line,
    // so the steady-state output RMS should be smaller for the 500 Hz feed.
    YSE::DSP::MODULES::highPassDelay dLow;
    YSE::DSP::MODULES::highPassDelay dHi;
    using D = YSE::DSP::MODULES::basicDelay;
    dLow.frequency(2000.0f);
    dHi.frequency(2000.0f);
    dLow.set(D::FIRST, 5.0f, 1.0f);
    dHi.set(D::FIRST, 5.0f, 1.0f);

    MULTICHANNELBUFFER bufLow(1);
    bufLow[0].resize(128);
    MULTICHANNELBUFFER bufHi(1);
    bufHi[0].resize(128);

    for (int iter = 0; iter < 30; ++iter) {
      fillSine(bufLow[0], 500.0f);
      fillSine(bufHi[0], 5000.0f);
      dLow.process(bufLow);
      dHi.process(bufHi);
    }
    float rmsLow = TestHelpers::measureRms(bufLow[0]);
    float rmsHi = TestHelpers::measureRms(bufHi[0]);
    CHECK(rmsHi > rmsLow);
  }

  // ─── lowPassDelay ─────────────────────────────────────────────────────────────

  TEST_CASE("lowPassDelay: default frequency is 1000 Hz") {
    YSE::DSP::MODULES::lowPassDelay d;
    CHECK(d.frequency() == doctest::Approx(1000.0f).epsilon(1e-5f));
  }

  TEST_CASE("lowPassDelay: frequency setter/getter round-trip") {
    YSE::DSP::MODULES::lowPassDelay d;
    d.frequency(3500.0f);
    CHECK(d.frequency() == doctest::Approx(3500.0f).epsilon(1e-5f));
  }

  TEST_CASE("lowPassDelay: process is bounded and shapes the tail energy") {
    YSE::DSP::MODULES::lowPassDelay d;
    using D = YSE::DSP::MODULES::basicDelay;
    d.frequency(1000.0f);
    d.set(D::FIRST, 5.0f, 0.8f);

    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    for (int iter = 0; iter < 20; ++iter) {
      fillSine(buf[0], 5000.0f); // 5 kHz, above the 1 kHz pre-filter
      d.process(buf);
      CHECK(maxAbs(buf[0]) < 5.0f);
    }
  }

  TEST_CASE("lowPassDelay: high-frequency tap is attenuated relative to low-frequency tap") {
    // Mirror of highPassDelay's frequency-shape test.
    YSE::DSP::MODULES::lowPassDelay dLow;
    YSE::DSP::MODULES::lowPassDelay dHi;
    using D = YSE::DSP::MODULES::basicDelay;
    dLow.frequency(1000.0f);
    dHi.frequency(1000.0f);
    dLow.set(D::FIRST, 5.0f, 1.0f);
    dHi.set(D::FIRST, 5.0f, 1.0f);

    MULTICHANNELBUFFER bufLow(1);
    bufLow[0].resize(128);
    MULTICHANNELBUFFER bufHi(1);
    bufHi[0].resize(128);

    for (int iter = 0; iter < 30; ++iter) {
      fillSine(bufLow[0], 300.0f);
      fillSine(bufHi[0], 6000.0f);
      dLow.process(bufLow);
      dHi.process(bufHi);
    }
    float rmsLow = TestHelpers::measureRms(bufLow[0]);
    float rmsHi = TestHelpers::measureRms(bufHi[0]);
    CHECK(rmsLow > rmsHi);
  }

} // TEST_SUITE("dsp")
