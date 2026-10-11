// Tests for YSE::DSP::phasor (YseEngine/dsp/generators/phasor.hpp), issue
// #1045. Reference output is closed form: at frequency f the sample n of a
// ramp starting at phase p0 is frac(p0 + n * f / SAMPLERATE).

#include <doctest/doctest.h>
#include <cmath>
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

namespace {

  // Restores the engine rate whatever the test does, so the shared test binary
  // is not left running at another rate.
  struct SampleRateGuard {
    UInt saved;
    SampleRateGuard() : saved(YSE::SAMPLERATE) {}
    ~SampleRateGuard() {
      YSE::SAMPLERATE = saved;
    }
  };

  double Expected(double start, double frequency, unsigned int n) {
    const double p =
        start + static_cast<double>(n) * frequency / static_cast<double>(YSE::SAMPLERATE);
    return p - std::floor(p);
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("phasor: a constant frequency produces the closed-form ramp (#1045)") {
    YSE::DSP::phasor ramp;
    const float frequency = static_cast<float>(YSE::SAMPLERATE) / 100.f; // 100 samples a cycle
    unsigned int n = 0;
    for (int block = 0; block < 4; ++block) {
      YSE::DSP::buffer& out = ramp(frequency);
      REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
      for (unsigned int i = 0; i < out.getLength(); ++i, ++n) {
        CAPTURE(n);
        CHECK(out.getPtr()[i] == doctest::Approx(Expected(0.0, frequency, n)).epsilon(1e-5));
      }
    }
  }

  TEST_CASE("phasor: output stays in [0, 1) and wraps once per cycle (#1045)") {
    YSE::DSP::phasor ramp;
    const float frequency = static_cast<float>(YSE::SAMPLERATE) / 37.f;
    int wraps = 0;
    float previous = 0.f;
    for (int block = 0; block < 10; ++block) {
      YSE::DSP::buffer& out = ramp(frequency);
      for (unsigned int i = 0; i < out.getLength(); ++i) {
        const float v = out.getPtr()[i];
        CHECK(v >= 0.f);
        CHECK(v < 1.f);
        if (v < previous) wraps++;
        previous = v;
      }
    }
    // 1280 samples at 37 samples a cycle: 34 complete wraps.
    CHECK(wraps == 1280 / 37);
  }

  TEST_CASE("phasor: a negative frequency runs the ramp backwards (#1045)") {
    YSE::DSP::phasor ramp;
    ramp.phase(0.5f);
    const float frequency = -static_cast<float>(YSE::SAMPLERATE) / 100.f;
    YSE::DSP::buffer& out = ramp(frequency);
    CHECK(out.getPtr()[0] == doctest::Approx(0.5f));
    CHECK(out.getPtr()[1] == doctest::Approx(0.49f));
    // 0.5 - 0.51 wraps to 0.99.
    CHECK(out.getPtr()[51] == doctest::Approx(0.99f).epsilon(1e-4));
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CHECK(out.getPtr()[i] >= 0.f);
      CHECK(out.getPtr()[i] < 1.f);
    }
  }

  TEST_CASE("phasor: a frequency signal is followed sample by sample (#1045)") {
    YSE::DSP::phasor ramp;
    YSE::DSP::buffer frequency(YSE::STANDARD_BUFFERSIZE);
    // First half still, second half at 100 samples a cycle.
    for (unsigned int i = 0; i < frequency.getLength(); ++i)
      frequency.getPtr()[i] = i < 64 ? 0.f : static_cast<float>(YSE::SAMPLERATE) / 100.f;

    YSE::DSP::buffer& out = ramp(frequency);
    CHECK(out.getPtr()[64] == doctest::Approx(0.f));
    CHECK(out.getPtr()[65] == doctest::Approx(0.01f));
    CHECK(out.getPtr()[127] == doctest::Approx(0.63f).epsilon(1e-4));
  }

  TEST_CASE("phasor: phase() jumps and wraps into 0..1 (#1045)") {
    YSE::DSP::phasor ramp;
    ramp.phase(0.25f);
    CHECK(ramp.phase() == doctest::Approx(0.25f));
    ramp.phase(1.25f);
    CHECK(ramp.phase() == doctest::Approx(0.25f));
    ramp.phase(-0.75f);
    CHECK(ramp.phase() == doctest::Approx(0.25f));
    CHECK(ramp(0.f).getPtr()[0] == doctest::Approx(0.25f));
  }

  TEST_CASE("phasor: follows a sample-rate change after construction (#1045)") {
    SampleRateGuard guard;
    YSE::DSP::phasor ramp;
    ramp(1000.f); // run once at the construction-time rate

    YSE::SAMPLERATE = 44100;
    ramp.phase(0.f);
    YSE::DSP::buffer& out = ramp(441.f); // 100 samples a cycle at 44.1 kHz
    CHECK(out.getPtr()[50] == doctest::Approx(0.5f).epsilon(1e-5));

    YSE::SAMPLERATE = 96000;
    ramp.phase(0.f);
    YSE::DSP::buffer& out2 = ramp(960.f); // 100 samples a cycle at 96 kHz
    CHECK(out2.getPtr()[50] == doctest::Approx(0.5f).epsilon(1e-5));
  }

  TEST_CASE("phasor: a shorter block, and a longer one, allocate nothing (#1045)") {
    YSE::DSP::phasor ramp;
    YSE::DSP::buffer frequency(YSE::STANDARD_BUFFERSIZE);
    frequency = 440.f;
    {
      TestHelpers::ProbeScope probe;
      ramp(440.f);
      ramp(440.f, 32);
      ramp(440.f, YSE::STANDARD_BUFFERSIZE * 4); // processed up to STANDARD_BUFFERSIZE
      ramp(frequency);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(ramp(440.f, YSE::STANDARD_BUFFERSIZE * 4).getLength() == YSE::STANDARD_BUFFERSIZE);
    CHECK(ramp(440.f, 32).getLength() == 32);
  }

} // TEST_SUITE("dsp")
