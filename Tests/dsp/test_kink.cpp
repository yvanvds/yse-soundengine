// Tests for YSE::DSP::kink (YseEngine/dsp/generators/kink.hpp), issue #1060.
// Reference output is closed form: x * s below the knee x = 0.5 / s, and the
// complementary line from (0.5 / s, 0.5) to (1, 1) above it.

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include "dsp/generators/kink.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::kink;

namespace {
  // The textbook definition, in double precision, with the knee written out.
  double Reference(double x, double s) {
    if (s <= 0.5 || x * s < 0.5) return x * s;
    const double knee = 0.5 / s;
    return 0.5 + (x - knee) * 0.5 / (1.0 - knee);
  }

  // One phasor cycle across one block: 0 to just under 1.
  float Ramp(unsigned int i) {
    return static_cast<float>(i) / static_cast<float>(YSE::STANDARD_BUFFERSIZE);
  }

  void FillRamp(buffer& b) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = Ramp(i);
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("kink: closed-form points (#1060)") {
    // Slope 1: the identity.
    CHECK(kink::process(0.f, 1.f) == 0.f);
    CHECK(kink::process(0.3f, 1.f) == doctest::Approx(0.3f));
    CHECK(kink::process(0.5f, 1.f) == doctest::Approx(0.5f));
    CHECK(kink::process(0.9f, 1.f) == doctest::Approx(0.9f));
    // Slope 2: knee at 0.25, then a line of slope 2/3 to (1, 1).
    CHECK(kink::process(0.1f, 2.f) == doctest::Approx(0.2f));
    CHECK(kink::process(0.25f, 2.f) == doctest::Approx(0.5f));
    CHECK(kink::process(0.625f, 2.f) == doctest::Approx(0.75f));
    CHECK(kink::process(1.f, 2.f) == doctest::Approx(1.f));
    // Slope 0.75: knee at 2/3, then a line of slope 1.5.
    CHECK(kink::process(0.4f, 0.75f) == doctest::Approx(0.3f));
    CHECK(kink::process(2.f / 3.f, 0.75f) == doctest::Approx(0.5f));
    CHECK(kink::process(5.f / 6.f, 0.75f) == doctest::Approx(0.75f));
    CHECK(kink::process(1.f, 0.75f) == doctest::Approx(1.f));
  }

  TEST_CASE("kink: matches the reference over a ramp at many slopes (#1060)") {
    buffer phase(YSE::STANDARD_BUFFERSIZE);
    FillRamp(phase);
    kink fn;
    for (float s : {0.f, 0.25f, 0.5f, 0.51f, 0.75f, 1.f, 1.5f, 2.f, 4.f, 10.f, 100.f}) {
      CAPTURE(s);
      buffer& out = fn(phase, s);
      REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
      for (unsigned int i = 0; i < out.getLength(); ++i) {
        CAPTURE(i);
        CHECK(out.getPtr()[i] == doctest::Approx(Reference(Ramp(i), s)).epsilon(1e-5));
      }
    }
  }

  TEST_CASE("kink: the bent ramp is continuous and still runs 0 to 1 (#1060)") {
    for (float s : {0.6f, 1.f, 3.f, 8.f}) {
      CAPTURE(s);
      CHECK(kink::process(0.f, s) == 0.f);
      CHECK(kink::process(1.f, s) == doctest::Approx(1.f));
      // Both lines meet at the knee.
      const float knee = 0.5f / s;
      CHECK(kink::process(knee * (1.f - 1e-6f), s) == doctest::Approx(0.5f).epsilon(1e-4));
      CHECK(kink::process(knee * (1.f + 1e-6f), s) == doctest::Approx(0.5f).epsilon(1e-4));
      // Monotonic rise over a fine ramp.
      float last = -1.f;
      for (int i = 0; i <= 1000; ++i) {
        const float y = kink::process(static_cast<float>(i) / 1000.f, s);
        CHECK(y >= last);
        last = y;
      }
    }
  }

  TEST_CASE("kink: at a slope of 0.5 or less there is no second line (#1060)") {
    CHECK(kink::process(0.9f, 0.5f) == doctest::Approx(0.45f));
    CHECK(kink::process(1.f, 0.5f) == doctest::Approx(0.5f));
    CHECK(kink::process(0.8f, 0.25f) == doctest::Approx(0.2f));
    CHECK(kink::process(0.8f, 0.f) == 0.f);
    CHECK(kink::process(0.8f, -1.f) == doctest::Approx(-0.8f));
  }

  TEST_CASE("kink: inputs outside 0-1 are not wrapped (#1060)") {
    // Slope 2: the second line, extended past 1.
    CHECK(kink::process(1.375f, 2.f) == doctest::Approx(1.25f));
    // Negative input: the first line.
    CHECK(kink::process(-0.25f, 2.f) == doctest::Approx(-0.5f));
  }

  TEST_CASE("kink: no NaN or infinity leaves it (#1060)") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(kink::process(nan, 2.f) == 0.f);
    CHECK(kink::process(inf, 2.f) == 0.f);
    // A non-finite slope counts as 1.
    CHECK(kink::process(0.7f, nan) == doctest::Approx(0.7f));
    CHECK(kink::process(0.7f, inf) == doctest::Approx(0.7f));
    // An overflowing result is 0.
    CHECK(kink::process(3e38f, 0.25f) == doctest::Approx(7.5e37f));
    CHECK(kink::process(3e38f, 1e10f) == 0.f);
  }

  TEST_CASE("kink: a slope signal is applied sample by sample (#1060)") {
    buffer phase(YSE::STANDARD_BUFFERSIZE);
    buffer slope(YSE::STANDARD_BUFFERSIZE);
    FillRamp(phase);
    for (unsigned int i = 0; i < slope.getLength(); ++i)
      slope.getPtr()[i] = 0.5f + 0.05f * static_cast<float>(i);
    kink fn;
    buffer& out = fn(phase, slope);
    REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      CHECK(out.getPtr()[i] == kink::process(phase.getPtr()[i], slope.getPtr()[i]));
    }

    // The block is as long as the shorter input.
    buffer shortSlope(32);
    shortSlope = 2.f;
    CHECK(fn(phase, shortSlope).getLength() == 32);
    CHECK(fn(phase, 2.f).getLength() == YSE::STANDARD_BUFFERSIZE);
  }

  TEST_CASE("kink: the same output at any sample rate (#1060)") {
    buffer phase(YSE::STANDARD_BUFFERSIZE);
    FillRamp(phase);
    kink reference;
    buffer expected(YSE::STANDARD_BUFFERSIZE);
    expected = reference(phase, 3.f);
    for (unsigned int rate : {22050u, 44100u, 96000u}) {
      CAPTURE(rate);
      TestHelpers::ScopedSampleRate scoped(rate);
      kink fn;
      CHECK(TestHelpers::buffersNearlyEqual(fn(phase, 3.f), expected, 0.f));
    }
  }

  TEST_CASE("kink: processing allocates nothing (#1060)") {
    kink fn;
    buffer phase(YSE::STANDARD_BUFFERSIZE);
    buffer slope(YSE::STANDARD_BUFFERSIZE);
    buffer shortSlope(16);
    FillRamp(phase);
    slope = 2.f;
    shortSlope = 2.f;
    fn(phase, 2.f);
    {
      TestHelpers::ProbeScope probe;
      fn(phase, 3.f);
      fn(phase, slope);
      fn(phase, shortSlope);
      fn(phase, 1.f);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
