// Tests for YSE::DSP::triangleShape and YSE::DSP::trapezoidShape
// (YseEngine/dsp/generators/phaseShapes.hpp), issue #1063. Reference output
// is closed form: straight lines between the corner points of each shape.

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include "dsp/generators/phaseShapes.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::trapezoidShape;
using YSE::DSP::triangleShape;

namespace {
  // The textbook shapes in double precision, on a phase already in [0, 1).
  double TriangleUnit(double x, double p) {
    if (x < p) return x / p;
    return (1.0 - x) / (1.0 - p);
  }

  double TrapezoidUnit(double x, double a, double d) {
    if (x < a) return x / a;
    if (x < d || d >= 1.0) return 1.0;
    return (1.0 - x) / (1.0 - d);
  }

  float Ramp(unsigned int i) {
    return static_cast<float>(i) / static_cast<float>(YSE::STANDARD_BUFFERSIZE);
  }

  void FillRamp(buffer& b) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = Ramp(i);
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("triangleShape: closed-form points (#1063)") {
    // Default peak 0.5, range -1..1.
    CHECK(triangleShape::process(0.f, 0.5f) == -1.f);
    CHECK(triangleShape::process(0.25f, 0.5f) == doctest::Approx(0.f));
    CHECK(triangleShape::process(0.5f, 0.5f) == doctest::Approx(1.f));
    CHECK(triangleShape::process(0.75f, 0.5f) == doctest::Approx(0.f));
    // Peak 0.2: rise to 0.2, fall over 0.8.
    CHECK(triangleShape::process(0.1f, 0.2f, 0.f, 1.f) == doctest::Approx(0.5f));
    CHECK(triangleShape::process(0.2f, 0.2f, 0.f, 1.f) == doctest::Approx(1.f));
    CHECK(triangleShape::process(0.6f, 0.2f, 0.f, 1.f) == doctest::Approx(0.5f));
    // Peak 0: a falling ramp from hi. Peak 1: a rising sawtooth from lo.
    CHECK(triangleShape::process(0.f, 0.f, 0.f, 1.f) == 1.f);
    CHECK(triangleShape::process(0.25f, 0.f, 0.f, 1.f) == doctest::Approx(0.75f));
    CHECK(triangleShape::process(0.f, 1.f, 0.f, 1.f) == 0.f);
    CHECK(triangleShape::process(0.25f, 1.f, 0.f, 1.f) == doctest::Approx(0.25f));
    // lo above hi turns the shape upside down.
    CHECK(triangleShape::process(0.5f, 0.5f, 2.f, -2.f) == doctest::Approx(-2.f));
    CHECK(triangleShape::process(0.f, 0.5f, 2.f, -2.f) == doctest::Approx(2.f));
  }

  TEST_CASE("triangleShape: matches the reference over a ramp at many peaks (#1063)") {
    buffer phase(YSE::STANDARD_BUFFERSIZE);
    FillRamp(phase);
    triangleShape fn;
    fn.range(-0.5f, 3.f);
    for (float p : {0.f, 0.01f, 0.25f, 0.5f, 0.7f, 0.99f, 1.f}) {
      CAPTURE(p);
      buffer& out = fn(phase, p);
      REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
      for (unsigned int i = 0; i < out.getLength(); ++i) {
        CAPTURE(i);
        const double u = TriangleUnit(Ramp(i), p);
        CHECK(out.getPtr()[i] == doctest::Approx(-0.5 + 3.5 * u).epsilon(1e-5));
      }
    }
  }

  TEST_CASE("triangleShape: the phase wraps and the peak is held within 0-1 (#1063)") {
    CHECK(triangleShape::process(1.25f, 0.5f) ==
          doctest::Approx(triangleShape::process(0.25f, 0.5f)));
    CHECK(triangleShape::process(-0.4f, 0.5f) ==
          doctest::Approx(triangleShape::process(0.6f, 0.5f)));
    CHECK(triangleShape::process(3.f, 0.5f) == -1.f);
    // A tiny negative phase is the start of a cycle, not 1.
    CHECK(triangleShape::process(-1e-9f, 0.5f) == -1.f);
    CHECK(triangleShape::process(0.3f, 2.f) == triangleShape::process(0.3f, 1.f));
    CHECK(triangleShape::process(0.3f, -1.f) == triangleShape::process(0.3f, 0.f));
  }

  TEST_CASE("triangleShape: no NaN or infinity leaves it (#1063)") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(triangleShape::process(nan, 0.5f) == -1.f);
    CHECK(triangleShape::process(inf, 0.5f) == -1.f);
    // A non-finite peak counts as 0.5.
    CHECK(triangleShape::process(0.25f, nan) == doctest::Approx(0.f));
    // The widest finite range does not overflow.
    const float big = std::numeric_limits<float>::max();
    CHECK(triangleShape::process(0.25f, 0.5f, -big, big) == doctest::Approx(0.f));
    CHECK(std::isfinite(triangleShape::process(0.4f, 0.5f, -big, big)));
    CHECK(triangleShape::process(0.4f, 0.5f, -inf, 1.f) == 0.f);
    // range() ignores values that are not finite.
    triangleShape fn;
    fn.range(nan, 4.f);
    CHECK(fn.lo() == -1.f);
    CHECK(fn.hi() == 4.f);
  }

  TEST_CASE("triangleShape: a peak signal is applied sample by sample (#1063)") {
    buffer phase(YSE::STANDARD_BUFFERSIZE);
    buffer peak(YSE::STANDARD_BUFFERSIZE);
    FillRamp(phase);
    for (unsigned int i = 0; i < peak.getLength(); ++i)
      peak.getPtr()[i] = 1.f - Ramp(i);
    triangleShape fn;
    buffer& out = fn(phase, peak);
    REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      CHECK(out.getPtr()[i] == triangleShape::process(phase.getPtr()[i], peak.getPtr()[i]));
    }
    buffer shortPeak(32);
    shortPeak = 0.5f;
    CHECK(fn(phase, shortPeak).getLength() == 32);
    CHECK(fn(phase, 0.5f).getLength() == YSE::STANDARD_BUFFERSIZE);
  }

  TEST_CASE("trapezoidShape: closed-form points (#1063)") {
    // Defaults: up 0.1, down 0.9, range 0..1, wrapping.
    CHECK(trapezoidShape::process(0.f, 0.1f, 0.9f) == 0.f);
    CHECK(trapezoidShape::process(0.05f, 0.1f, 0.9f) == doctest::Approx(0.5f));
    CHECK(trapezoidShape::process(0.1f, 0.1f, 0.9f) == 1.f);
    CHECK(trapezoidShape::process(0.5f, 0.1f, 0.9f) == 1.f);
    CHECK(trapezoidShape::process(0.9f, 0.1f, 0.9f) == doctest::Approx(1.f));
    CHECK(trapezoidShape::process(0.95f, 0.1f, 0.9f) == doctest::Approx(0.5f));
    // 0.5 0.5: a triangle. 0 0: a falling ramp.
    CHECK(trapezoidShape::process(0.25f, 0.5f, 0.5f) == doctest::Approx(0.5f));
    CHECK(trapezoidShape::process(0.5f, 0.5f, 0.5f) == doctest::Approx(1.f));
    CHECK(trapezoidShape::process(0.75f, 0.5f, 0.5f) == doctest::Approx(0.5f));
    CHECK(trapezoidShape::process(0.f, 0.f, 0.f) == 1.f);
    CHECK(trapezoidShape::process(0.25f, 0.f, 0.f) == doctest::Approx(0.75f));
    // A range, upside down.
    CHECK(trapezoidShape::process(0.5f, 0.1f, 0.9f, 1.f, -1.f) == -1.f);
    CHECK(trapezoidShape::process(0.f, 0.1f, 0.9f, 1.f, -1.f) == 1.f);
    // The class defaults are Max's.
    trapezoidShape fn;
    CHECK(fn.lo() == 0.f);
    CHECK(fn.hi() == 1.f);
    CHECK(fn.wrap());
  }

  TEST_CASE("trapezoidShape: matches the reference over a ramp at many points (#1063)") {
    buffer phase(YSE::STANDARD_BUFFERSIZE);
    FillRamp(phase);
    trapezoidShape fn;
    fn.range(-2.f, 0.5f);
    const float points[][2] = {{0.1f, 0.9f}, {0.f, 0.f},   {0.5f, 0.5f}, {0.f, 1.f},
                               {1.f, 1.f},   {0.3f, 0.4f}, {0.02f, 0.6f}};
    for (const auto& ad : points) {
      CAPTURE(ad[0]);
      CAPTURE(ad[1]);
      buffer& out = fn(phase, ad[0], ad[1]);
      REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
      for (unsigned int i = 0; i < out.getLength(); ++i) {
        CAPTURE(i);
        const double u = TrapezoidUnit(Ramp(i), ad[0], ad[1]);
        CHECK(out.getPtr()[i] == doctest::Approx(-2.0 + 2.5 * u).epsilon(1e-5));
      }
    }
  }

  TEST_CASE("trapezoidShape: points are held within 0-1, down no earlier than up (#1063)") {
    CHECK(trapezoidShape::process(0.5f, -1.f, 2.f) == trapezoidShape::process(0.5f, 0.f, 1.f));
    // down before up: no plateau, a triangle peaking at up.
    CHECK(trapezoidShape::process(0.6f, 0.6f, 0.2f) == doctest::Approx(1.f));
    CHECK(trapezoidShape::process(0.8f, 0.6f, 0.2f) == doctest::Approx(0.5f));
    // Non-finite points count as their defaults.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(trapezoidShape::process(0.05f, nan, nan) == trapezoidShape::process(0.05f, 0.1f, 0.9f));
    CHECK(trapezoidShape::process(0.95f, nan, nan) == trapezoidShape::process(0.95f, 0.1f, 0.9f));
  }

  TEST_CASE("trapezoidShape: wrap mode wraps, clip mode clips (#1063)") {
    // Wrap: 4.3 is 0.3, -0.4 is 0.6, -0.95 is 0.05.
    CHECK(trapezoidShape::process(4.3f, 0.5f, 0.5f) ==
          doctest::Approx(trapezoidShape::process(0.3f, 0.5f, 0.5f)));
    CHECK(trapezoidShape::process(-0.4f, 0.5f, 0.5f) ==
          doctest::Approx(trapezoidShape::process(0.6f, 0.5f, 0.5f)));
    CHECK(trapezoidShape::process(-0.95f, 0.1f, 0.9f) == doctest::Approx(0.5f));
    // Clip: above 1 is 1 (the end of the fall), below 0 is 0.
    CHECK(trapezoidShape::process(4.3f, 0.5f, 0.5f, 0.f, 1.f, false) == 0.f);
    CHECK(trapezoidShape::process(-0.4f, 0.5f, 0.5f, 0.f, 1.f, false) == 0.f);
    CHECK(trapezoidShape::process(1.5f, 0.1f, 1.f, 0.f, 1.f, false) == 1.f);
    CHECK(trapezoidShape::process(0.3f, 0.5f, 0.5f, 0.f, 1.f, false) ==
          trapezoidShape::process(0.3f, 0.5f, 0.5f));

    buffer phase(4);
    phase.getPtr()[0] = 1.25f;
    phase.getPtr()[1] = -0.75f;
    phase.getPtr()[2] = 2.f;
    phase.getPtr()[3] = 0.5f;
    trapezoidShape fn;
    buffer& wrapped = fn(phase, 0.5f, 0.5f);
    CHECK(wrapped.getPtr()[0] == doctest::Approx(0.5f));
    CHECK(wrapped.getPtr()[1] == doctest::Approx(0.5f));
    CHECK(wrapped.getPtr()[2] == 0.f);
    fn.wrap(false);
    CHECK_FALSE(fn.wrap());
    buffer& clipped = fn(phase, 0.5f, 0.5f);
    CHECK(clipped.getPtr()[0] == 0.f);
    CHECK(clipped.getPtr()[1] == 0.f);
    CHECK(clipped.getPtr()[2] == 0.f);
    CHECK(clipped.getPtr()[3] == doctest::Approx(1.f));
  }

  TEST_CASE("trapezoidShape: no NaN or infinity leaves it (#1063)") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(trapezoidShape::process(nan, 0.1f, 0.9f) == 0.f);
    CHECK(trapezoidShape::process(inf, 0.1f, 0.9f, 0.f, 1.f, false) == 0.f);
    const float big = std::numeric_limits<float>::max();
    CHECK(std::isfinite(trapezoidShape::process(0.05f, 0.1f, 0.9f, -big, big)));
    CHECK(trapezoidShape::process(0.05f, 0.1f, 0.9f, nan, 1.f) == 0.f);
    trapezoidShape fn;
    fn.range(-3.f, inf);
    CHECK(fn.lo() == -3.f);
    CHECK(fn.hi() == 1.f);
  }

  TEST_CASE("trapezoidShape: point signals are applied sample by sample (#1063)") {
    buffer phase(YSE::STANDARD_BUFFERSIZE);
    buffer up(YSE::STANDARD_BUFFERSIZE);
    buffer down(YSE::STANDARD_BUFFERSIZE);
    FillRamp(phase);
    for (unsigned int i = 0; i < up.getLength(); ++i) {
      up.getPtr()[i] = 0.5f * Ramp(i);
      down.getPtr()[i] = 1.f - 0.4f * Ramp(i);
    }
    trapezoidShape fn;
    fn.range(-1.f, 1.f);
    buffer& out = fn(phase, up, down);
    REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      CHECK(out.getPtr()[i] == trapezoidShape::process(phase.getPtr()[i], up.getPtr()[i],
                                                       down.getPtr()[i], -1.f, 1.f));
    }
    buffer shortDown(16);
    shortDown = 0.9f;
    CHECK(fn(phase, up, shortDown).getLength() == 16);
  }

  TEST_CASE("phase shapes: the same output at any sample rate (#1063)") {
    buffer phase(YSE::STANDARD_BUFFERSIZE);
    FillRamp(phase);
    triangleShape triReference;
    trapezoidShape trapReference;
    buffer triExpected(YSE::STANDARD_BUFFERSIZE);
    buffer trapExpected(YSE::STANDARD_BUFFERSIZE);
    triExpected = triReference(phase, 0.3f);
    trapExpected = trapReference(phase, 0.2f, 0.7f);
    for (unsigned int rate : {22050u, 44100u, 96000u}) {
      CAPTURE(rate);
      TestHelpers::ScopedSampleRate scoped(rate);
      triangleShape tri;
      trapezoidShape trap;
      CHECK(TestHelpers::buffersNearlyEqual(tri(phase, 0.3f), triExpected, 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(trap(phase, 0.2f, 0.7f), trapExpected, 0.f));
    }
  }

  TEST_CASE("phase shapes: processing allocates nothing (#1063)") {
    triangleShape tri;
    trapezoidShape trap;
    buffer phase(YSE::STANDARD_BUFFERSIZE);
    buffer points(YSE::STANDARD_BUFFERSIZE);
    buffer shortPoints(16);
    FillRamp(phase);
    points = 0.5f;
    shortPoints = 0.5f;
    tri(phase, 0.5f);
    trap(phase, 0.1f, 0.9f);
    {
      TestHelpers::ProbeScope probe;
      tri.range(0.f, 2.f);
      tri(phase, 0.3f);
      tri(phase, points);
      tri(phase, shortPoints);
      tri(phase, 0.5f);
      trap.range(-1.f, 1.f).wrap(false);
      trap(phase, 0.2f, 0.8f);
      trap(phase, points, points);
      trap(phase, shortPoints, points);
      trap(phase, 0.1f, 0.9f);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
