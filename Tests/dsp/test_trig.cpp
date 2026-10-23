// Tests for YSE::DSP::trig and arcTangent2 (YseEngine/dsp/operators/trig.hpp),
// issue #1049. Reference output is closed form: the <cmath> function of each
// sample, and the clamped edge values for out-of-domain input.

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include "dsp/operators/trig.hpp"
#include "dsp/oscillators.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

using FN = YSE::DSP::trig::FN;

namespace {

  // -2 to just under 2 across one block: inside every domain except acosh's
  // and past the edges of acos / asin / atanh.
  void FillRamp(YSE::DSP::buffer& b) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = (static_cast<float>(i) - 64.f) / 32.f;
  }

  float Reference(FN fn, float x) {
    const float unit = std::fmin(std::fmax(x, -1.f), 1.f);
    switch (fn) {
    case FN::COS:
      return std::cos(x);
    case FN::SIN:
      return std::sin(x);
    case FN::TAN:
      return std::tan(x);
    case FN::ACOS:
      return std::acos(unit);
    case FN::ASIN:
      return std::asin(unit);
    case FN::ATAN:
      return std::atan(x);
    case FN::COSH:
      return std::cosh(x);
    case FN::SINH:
      return std::sinh(x);
    case FN::TANH:
      return std::tanh(x);
    case FN::ACOSH:
      return std::acosh(std::fmax(x, 1.f));
    case FN::ASINH:
      return std::asinh(x);
    case FN::ATANH:
    default:
      return std::atanh(std::fmin(std::fmax(x, -0.99999994f), 0.99999994f));
    }
  }

  const FN kAll[] = {FN::COS,  FN::SIN,  FN::TAN,  FN::ACOS,  FN::ASIN,  FN::ATAN,
                     FN::COSH, FN::SINH, FN::TANH, FN::ACOSH, FN::ASINH, FN::ATANH};

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("trig: every function matches <cmath> sample by sample (#1049)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    FillRamp(in);
    YSE::DSP::trig fn;
    for (FN f : kAll) {
      CAPTURE(static_cast<int>(f));
      YSE::DSP::buffer& out = fn.setFunction(f)(in);
      CHECK(fn.getFunction() == f);
      REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
      for (unsigned int i = 0; i < out.getLength(); ++i) {
        CAPTURE(i);
        const float expected = Reference(f, in.getPtr()[i]);
        CHECK(out.getPtr()[i] == doctest::Approx(expected).epsilon(1e-6));
        CHECK(YSE::DSP::trig::process(f, in.getPtr()[i]) == out.getPtr()[i]);
      }
    }
  }

  TEST_CASE("trig: closed-form values (#1049)") {
    const float pi = 3.14159265f;
    CHECK(YSE::DSP::trig::process(FN::COS, 0.f) == 1.f);
    CHECK(YSE::DSP::trig::process(FN::COS, pi) == doctest::Approx(-1.f));
    CHECK(YSE::DSP::trig::process(FN::SIN, pi / 2.f) == doctest::Approx(1.f));
    CHECK(YSE::DSP::trig::process(FN::TAN, pi / 4.f) == doctest::Approx(1.f));
    CHECK(YSE::DSP::trig::process(FN::ACOS, 0.f) == doctest::Approx(pi / 2.f));
    CHECK(YSE::DSP::trig::process(FN::ASIN, 1.f) == doctest::Approx(pi / 2.f));
    CHECK(YSE::DSP::trig::process(FN::ATAN, 1.f) == doctest::Approx(pi / 4.f));
    CHECK(YSE::DSP::trig::process(FN::COSH, 0.f) == 1.f);
    CHECK(YSE::DSP::trig::process(FN::SINH, 0.f) == 0.f);
    CHECK(YSE::DSP::trig::process(FN::TANH, 0.f) == 0.f);
    CHECK(YSE::DSP::trig::process(FN::ACOSH, 1.f) == 0.f);
    CHECK(YSE::DSP::trig::process(FN::ASINH, 0.f) == 0.f);
    CHECK(YSE::DSP::trig::process(FN::ATANH, 0.5f) == doctest::Approx(0.549306f));
  }

  TEST_CASE("trig: tanh soft-clips a hot sine into -1..1 (#1049)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < in.getLength(); ++i)
      in.getPtr()[i] = 10.f * std::sin(static_cast<float>(i) * 0.1f);
    YSE::DSP::trig clip(FN::TANH);
    YSE::DSP::buffer& out = clip(in);
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      CHECK(out.getPtr()[i] >= -1.f);
      CHECK(out.getPtr()[i] <= 1.f);
      CHECK(out.getPtr()[i] == doctest::Approx(std::tanh(in.getPtr()[i])));
    }
  }

  TEST_CASE("trig: out-of-domain input clamps to the domain edge, never NaN or inf (#1049)") {
    const float pi = 3.14159265f;
    CHECK(YSE::DSP::trig::process(FN::ACOS, 2.f) == 0.f);
    CHECK(YSE::DSP::trig::process(FN::ACOS, -2.f) == doctest::Approx(pi));
    CHECK(YSE::DSP::trig::process(FN::ASIN, 2.f) == doctest::Approx(pi / 2.f));
    CHECK(YSE::DSP::trig::process(FN::ASIN, -5.f) == doctest::Approx(-pi / 2.f));
    CHECK(YSE::DSP::trig::process(FN::ACOSH, 0.f) == 0.f);
    CHECK(YSE::DSP::trig::process(FN::ACOSH, -3.f) == 0.f);

    const float edge = YSE::DSP::trig::process(FN::ATANH, 1.f);
    CHECK(std::isfinite(edge));
    CHECK(edge == doctest::Approx(8.66f).epsilon(0.01));
    CHECK(YSE::DSP::trig::process(FN::ATANH, -1.f) == -edge);
    CHECK(YSE::DSP::trig::process(FN::ATANH, 3.f) == edge);

    const float coshEdge = YSE::DSP::trig::process(FN::COSH, 1000.f);
    CHECK(std::isfinite(coshEdge));
    CHECK(coshEdge == doctest::Approx(std::cosh(88.f)));
    CHECK(YSE::DSP::trig::process(FN::COSH, -1000.f) == coshEdge);
    CHECK(std::isfinite(YSE::DSP::trig::process(FN::SINH, 1000.f)));
    CHECK(YSE::DSP::trig::process(FN::SINH, -1000.f) == -YSE::DSP::trig::process(FN::SINH, 1000.f));
  }

  TEST_CASE("trig: NaN and infinite input never reach the output (#1049)") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    YSE::DSP::buffer in(6);
    in.getPtr()[0] = nan;
    in.getPtr()[1] = inf;
    in.getPtr()[2] = -inf;
    in.getPtr()[3] = 1.f;
    in.getPtr()[4] = -1.f;
    in.getPtr()[5] = 1e30f;
    YSE::DSP::trig fn;
    for (FN f : kAll) {
      CAPTURE(static_cast<int>(f));
      YSE::DSP::buffer& out = fn.setFunction(f)(in);
      REQUIRE(out.getLength() == 6);
      for (unsigned int i = 0; i < 6; ++i) {
        CAPTURE(i);
        CHECK(std::isfinite(out.getPtr()[i]));
      }
      CHECK(out.getPtr()[0] == 0.f); // NaN in, 0 out
    }
    // Limits that are finite stay what they are.
    CHECK(YSE::DSP::trig::process(FN::TANH, inf) == 1.f);
    CHECK(YSE::DSP::trig::process(FN::ATAN, -inf) == doctest::Approx(-1.5707963f));
  }

  TEST_CASE("arcTangent2: all four quadrants, the axes and the origin (#1049)") {
    const float pi = 3.14159265f;
    CHECK(YSE::DSP::arcTangent2::process(1.f, 1.f) == doctest::Approx(pi / 4.f));
    CHECK(YSE::DSP::arcTangent2::process(1.f, -1.f) == doctest::Approx(3.f * pi / 4.f));
    CHECK(YSE::DSP::arcTangent2::process(-1.f, -1.f) == doctest::Approx(-3.f * pi / 4.f));
    CHECK(YSE::DSP::arcTangent2::process(-1.f, 1.f) == doctest::Approx(-pi / 4.f));
    CHECK(YSE::DSP::arcTangent2::process(1.f, 0.f) == doctest::Approx(pi / 2.f));
    CHECK(YSE::DSP::arcTangent2::process(0.f, 0.f) == 0.f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(YSE::DSP::arcTangent2::process(nan, 1.f) == 0.f);
    CHECK(YSE::DSP::arcTangent2::process(1.f, nan) == 0.f);
  }

  TEST_CASE("arcTangent2: a cosine / sine pair recovers its angle (#1049)") {
    // (x, y) = (cos t, sin t) for t across -pi..pi: atan2 gives t back.
    YSE::DSP::buffer y(YSE::STANDARD_BUFFERSIZE), x(YSE::STANDARD_BUFFERSIZE);
    auto angle = [](unsigned int i) { return (static_cast<float>(i) - 63.5f) / 64.f * 3.1f; };
    for (unsigned int i = 0; i < y.getLength(); ++i) {
      y.getPtr()[i] = std::sin(angle(i));
      x.getPtr()[i] = std::cos(angle(i));
    }
    YSE::DSP::arcTangent2 a2;
    YSE::DSP::buffer& out = a2(y, x);
    REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      CHECK(out.getPtr()[i] == doctest::Approx(angle(i)).epsilon(1e-5));
    }
    // Against a constant x.
    YSE::DSP::buffer& constant = a2(y, 0.5f);
    for (unsigned int i = 0; i < constant.getLength(); ++i) {
      CAPTURE(i);
      CHECK(constant.getPtr()[i] == std::atan2(y.getPtr()[i], 0.5f));
    }
  }

  TEST_CASE("trig / arcTangent2: block length follows the input (#1049)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE), shortIn(16);
    FillRamp(in);
    shortIn = 0.5f;
    YSE::DSP::trig fn(FN::SIN);
    YSE::DSP::arcTangent2 a2;
    CHECK(fn(shortIn).getLength() == 16);
    CHECK(a2(in, shortIn).getLength() == 16);
    CHECK(a2(shortIn, 1.f).getLength() == 16);
    CHECK(fn(in).getLength() == YSE::STANDARD_BUFFERSIZE);
    CHECK(a2(in, in).getLength() == YSE::STANDARD_BUFFERSIZE);
  }

  TEST_CASE("cosine: a 0..1 phase ramp gives one cosine cycle (#1049)") {
    // The DSP class behind ~cos: table lookup, so compare loosely.
    YSE::DSP::buffer phase(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < phase.getLength(); ++i)
      phase.getPtr()[i] = static_cast<float>(i) / static_cast<float>(phase.getLength());
    YSE::DSP::cosine cosine;
    YSE::DSP::buffer& out = cosine(phase);
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      CHECK(out.getPtr()[i] ==
            doctest::Approx(std::cos(6.2831853f * phase.getPtr()[i])).epsilon(1e-3).scale(1.f));
    }
  }

  TEST_CASE("trig / arcTangent2: processing allocates nothing (#1049)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE), shortIn(16);
    FillRamp(in);
    shortIn = 0.5f;
    YSE::DSP::trig fn;
    YSE::DSP::arcTangent2 a2;
    {
      TestHelpers::ProbeScope probe;
      for (FN f : kAll) {
        fn.setFunction(f);
        fn(in);
        fn(shortIn);
      }
      a2(in, 0.5f);
      a2(in, in);
      a2(in, shortIn);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
