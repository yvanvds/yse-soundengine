// Behavioural tests for the channel-strip parametric EQ MODULE (issue #163).
//
// parametricEQ is four cascaded RBJ biquad bands — a low shelf, two peaking
// (bell) bands, and a high shelf — each with independent frequency, gain (dB)
// and Q. The wet/dry balance is the inherited impact(); default impact = 1 is
// fully wet. A band at 0 dB is a mathematical identity (unity passthrough), so
// each band can be exercised in isolation.
//
// No audio device required — SAMPLERATE is initialised by the
// portaudioDeviceManager translation unit at static-initialisation time (44100
// by default; CI also forces 48000 via YSE_TEST_FORCED_RATE). Every probe tone
// and observation window is derived from the *actual* SAMPLERATE so the tests
// hold at any rate.

#include <doctest/doctest.h>
#include <cmath>
#include <vector>
#include "dsp/modules/parametricEQ.hpp"
#include "headers/defines.hpp"
#include "support/audio_helpers.hpp"

static constexpr float kPi = 3.14159265358979323846f;

namespace {

  using YSE::DSP::MODULES::parametricEQ;

  inline float sr() {
    return static_cast<float>(YSE::SAMPLERATE);
  }

  // Phase-continuous sine so steady-state RMS is not disturbed by block-boundary
  // discontinuities.
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

  // Steady-state magnitude response of `eq` at `probeHz`, in dB relative to a
  // unit sine. Runs the filter well past its settling time, then averages RMS
  // over a final window.
  float responseDb(parametricEQ& eq, float probeHz) {
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(256);
    SineGen gen(probeHz);
    // Settle: at least ~0.2 s of audio at any rate.
    int settleBlocks = static_cast<int>(0.2f * sr() / 256.0f) + 4;
    for (int b = 0; b < settleBlocks; ++b) {
      gen.fill(buf[0]);
      eq.process(buf);
    }
    // Measure: average RMS over several blocks.
    float acc = 0.0f;
    int measBlocks = 16;
    for (int b = 0; b < measBlocks; ++b) {
      gen.fill(buf[0]);
      eq.process(buf);
      acc += TestHelpers::measureRms(buf[0]);
    }
    float outRms = acc / static_cast<float>(measBlocks);
    float inRms = 0.70710678f; // RMS of a unit sine
    return 20.0f * std::log10(outRms / inRms);
  }

} // anonymous namespace

TEST_SUITE("dsp") {

  // ─── parameters ───────────────────────────────────────────────────────────────

  TEST_CASE("parametricEQ: sensible defaults (bands flat)") {
    parametricEQ eq;
    for (int b = 0; b < YSE::DSP::MODULES::EQ_BAND_COUNT; ++b) {
      auto band = static_cast<YSE::DSP::MODULES::eqBand>(b);
      CHECK(eq.gain(band) == doctest::Approx(0.0f));
      CHECK(eq.frequency(band) > 0.0f);
      CHECK(eq.q(band) > 0.0f);
    }
  }

  TEST_CASE("parametricEQ: setter/getter round-trips") {
    parametricEQ eq;
    eq.frequency(YSE::DSP::MODULES::EQ_PEAK_1, 630.0f)
        .gain(YSE::DSP::MODULES::EQ_PEAK_1, 6.0f)
        .q(YSE::DSP::MODULES::EQ_PEAK_1, 2.5f);
    CHECK(eq.frequency(YSE::DSP::MODULES::EQ_PEAK_1) == doctest::Approx(630.0f));
    CHECK(eq.gain(YSE::DSP::MODULES::EQ_PEAK_1) == doctest::Approx(6.0f));
    CHECK(eq.q(YSE::DSP::MODULES::EQ_PEAK_1) == doctest::Approx(2.5f));
  }

  TEST_CASE("parametricEQ: parameters clamp to safe ranges") {
    parametricEQ eq;
    eq.gain(YSE::DSP::MODULES::EQ_PEAK_1, 100.0f);
    CHECK(eq.gain(YSE::DSP::MODULES::EQ_PEAK_1) <= 24.0f);
    eq.gain(YSE::DSP::MODULES::EQ_PEAK_1, -100.0f);
    CHECK(eq.gain(YSE::DSP::MODULES::EQ_PEAK_1) >= -24.0f);
    eq.frequency(YSE::DSP::MODULES::EQ_PEAK_1, 1.0e9f);
    CHECK(eq.frequency(YSE::DSP::MODULES::EQ_PEAK_1) <= 20000.0f);
    eq.frequency(YSE::DSP::MODULES::EQ_PEAK_1, 0.0f);
    CHECK(eq.frequency(YSE::DSP::MODULES::EQ_PEAK_1) >= 20.0f);
    eq.q(YSE::DSP::MODULES::EQ_PEAK_1, 1000.0f);
    CHECK(eq.q(YSE::DSP::MODULES::EQ_PEAK_1) <= 18.0f);
    eq.q(YSE::DSP::MODULES::EQ_PEAK_1, 0.0f);
    CHECK(eq.q(YSE::DSP::MODULES::EQ_PEAK_1) >= 0.1f);
  }

  // ─── flat EQ is transparent ───────────────────────────────────────────────────

  TEST_CASE("parametricEQ: all bands flat is unity (transparent)") {
    parametricEQ eq; // every band 0 dB
    for (float f : {80.0f, 400.0f, 2000.0f, 6000.0f}) {
      float g = responseDb(eq, f);
      CHECK(g == doctest::Approx(0.0f).epsilon(0.02)); // within ~0.2 dB
    }
  }

  // ─── peaking band: boost/cut at centre, transparent far away ──────────────────

  TEST_CASE("parametricEQ: peaking band boosts its centre frequency") {
    parametricEQ eq;
    eq.frequency(YSE::DSP::MODULES::EQ_PEAK_1, 1000.0f)
        .q(YSE::DSP::MODULES::EQ_PEAK_1, 2.0f)
        .gain(YSE::DSP::MODULES::EQ_PEAK_1, 12.0f);
    float atCentre = responseDb(eq, 1000.0f);
    // Peak gain should closely match the requested +12 dB.
    CHECK(atCentre == doctest::Approx(12.0f).epsilon(0.06)); // ~ +/-0.7 dB
    // Well below the band it is essentially flat (other bands are at 0 dB).
    float farBelow = responseDb(eq, 100.0f);
    CHECK(std::abs(farBelow) < 1.5f);
  }

  TEST_CASE("parametricEQ: peaking band cuts its centre frequency") {
    parametricEQ eq;
    eq.frequency(YSE::DSP::MODULES::EQ_PEAK_2, 1500.0f)
        .q(YSE::DSP::MODULES::EQ_PEAK_2, 2.0f)
        .gain(YSE::DSP::MODULES::EQ_PEAK_2, -12.0f);
    float atCentre = responseDb(eq, 1500.0f);
    CHECK(atCentre == doctest::Approx(-12.0f).epsilon(0.06));
  }

  // ─── shelves ──────────────────────────────────────────────────────────────────

  TEST_CASE("parametricEQ: low shelf lifts lows, leaves highs alone") {
    parametricEQ eq;
    eq.frequency(YSE::DSP::MODULES::EQ_LOW_SHELF, 200.0f)
        .gain(YSE::DSP::MODULES::EQ_LOW_SHELF, 9.0f);
    float low = responseDb(eq, 50.0f); // well below the corner
    float high = responseDb(eq, 5000.0f); // well above the corner
    CHECK(low == doctest::Approx(9.0f).epsilon(0.08)); // approaches shelf gain
    CHECK(std::abs(high) < 1.0f); // untouched
    CHECK(low > high + 6.0f); // clear low-vs-high tilt
  }

  TEST_CASE("parametricEQ: high shelf lifts highs, leaves lows alone") {
    parametricEQ eq;
    eq.frequency(YSE::DSP::MODULES::EQ_HIGH_SHELF, 4000.0f)
        .gain(YSE::DSP::MODULES::EQ_HIGH_SHELF, 9.0f);
    float high = responseDb(eq, 12000.0f);
    float low = responseDb(eq, 200.0f);
    CHECK(high == doctest::Approx(9.0f).epsilon(0.08));
    CHECK(std::abs(low) < 1.0f);
    CHECK(high > low + 6.0f);
  }

  // ─── multichannel contract ────────────────────────────────────────────────────

  TEST_CASE("parametricEQ: channels are filtered independently (no cross-bleed)") {
    parametricEQ eq;
    eq.gain(YSE::DSP::MODULES::EQ_PEAK_1, 12.0f);

    MULTICHANNELBUFFER buf(2);
    buf[0].resize(128);
    buf[1].resize(128);
    buf[0] = 0.0f;
    buf[1] = 0.0f;
    buf[0].getPtr()[0] = 1.0f; // impulse on channel 0 only

    float ch1Energy = 0.0f;
    eq.process(buf);
    ch1Energy += TestHelpers::measureRms(buf[1]);
    for (int b = 0; b < 8; ++b) {
      buf[0] = 0.0f;
      buf[1] = 0.0f;
      eq.process(buf);
      ch1Energy += TestHelpers::measureRms(buf[1]);
    }
    CHECK(ch1Energy < 1e-9f); // channel 1 never receives channel 0's energy
  }

  TEST_CASE("parametricEQ: identical channels get identical processing") {
    parametricEQ eq;
    eq.gain(YSE::DSP::MODULES::EQ_LOW_SHELF, 6.0f).gain(YSE::DSP::MODULES::EQ_HIGH_SHELF, -6.0f);

    MULTICHANNELBUFFER buf(2);
    buf[0].resize(128);
    buf[1].resize(128);
    SineGen g0(500.0f), g1(500.0f);
    for (int b = 0; b < 40; ++b) {
      g0.fill(buf[0]);
      g1.fill(buf[1]);
      eq.process(buf);
      CHECK(TestHelpers::buffersNearlyEqual(buf[0], buf[1], 1e-6f));
    }
  }

  // ─── robustness ───────────────────────────────────────────────────────────────

  TEST_CASE("parametricEQ: tolerates a change in input buffer length") {
    parametricEQ eq;
    eq.gain(YSE::DSP::MODULES::EQ_PEAK_1, 6.0f);

    MULTICHANNELBUFFER buf(1);
    buf[0].resize(64);
    SineGen g(500.0f);
    g.fill(buf[0]);
    eq.process(buf);
    CHECK(buf[0].getLength() == 64u);

    buf[0].resize(256);
    g.fill(buf[0]);
    eq.process(buf);
    CHECK(buf[0].getLength() == 256u);
  }

  TEST_CASE("parametricEQ: output stays bounded under extreme boost") {
    parametricEQ eq;
    eq.gain(YSE::DSP::MODULES::EQ_LOW_SHELF, 24.0f)
        .gain(YSE::DSP::MODULES::EQ_PEAK_1, 24.0f)
        .gain(YSE::DSP::MODULES::EQ_PEAK_2, 24.0f)
        .gain(YSE::DSP::MODULES::EQ_HIGH_SHELF, 24.0f);

    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    SineGen g(1000.0f);
    for (int b = 0; b < 200; ++b) {
      g.fill(buf[0]);
      eq.process(buf);
      float* p = buf[0].getPtr();
      for (unsigned i = 0; i < buf[0].getLength(); ++i)
        CHECK(std::abs(p[i]) < 100.0f);
    }
  }

} // TEST_SUITE("dsp")
