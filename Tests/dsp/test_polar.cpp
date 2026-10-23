// Tests for YSE::DSP::cartToPolar, polarToCart and phaseWrap
// (YseEngine/dsp/operators/polar.hpp), issue #1053. Reference output is closed
// form: std::hypot / std::atan2 / std::cos / std::sin of each sample, and the
// unit circle for a cosine / sine pair.

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include "dsp/operators/polar.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

namespace {
  const float kPi = 3.14159265f;

  // -3.1 to just under 3.1 radians across one block.
  float Angle(unsigned int i) {
    return (static_cast<float>(i) - 63.5f) / 64.f * 3.1f;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("cartToPolar: quadrants, axes and the origin (#1053)") {
    float m = -1.f, p = -1.f;
    YSE::DSP::cartToPolar::process(3.f, 4.f, m, p);
    CHECK(m == doctest::Approx(5.f));
    CHECK(p == doctest::Approx(std::atan2(4.f, 3.f)));
    YSE::DSP::cartToPolar::process(-1.f, 1.f, m, p);
    CHECK(m == doctest::Approx(std::sqrt(2.f)));
    CHECK(p == doctest::Approx(3.f * kPi / 4.f));
    YSE::DSP::cartToPolar::process(-1.f, -1.f, m, p);
    CHECK(p == doctest::Approx(-3.f * kPi / 4.f));
    YSE::DSP::cartToPolar::process(0.f, -2.f, m, p);
    CHECK(m == 2.f);
    CHECK(p == doctest::Approx(-kPi / 2.f));
    YSE::DSP::cartToPolar::process(0.f, 0.f, m, p);
    CHECK(m == 0.f);
    CHECK(p == 0.f);
  }

  TEST_CASE("polarToCart: closed-form points (#1053)") {
    float re = -1.f, im = -1.f;
    YSE::DSP::polarToCart::process(2.f, 0.f, re, im);
    CHECK(re == 2.f);
    CHECK(im == 0.f);
    YSE::DSP::polarToCart::process(2.f, kPi / 2.f, re, im);
    CHECK(re == doctest::Approx(0.f).scale(1.f));
    CHECK(im == doctest::Approx(2.f));
    YSE::DSP::polarToCart::process(1.f, kPi, re, im);
    CHECK(re == doctest::Approx(-1.f));
    CHECK(im == doctest::Approx(0.f).scale(1.f));
    YSE::DSP::polarToCart::process(0.f, 1.f, re, im);
    CHECK(re == 0.f);
    CHECK(im == 0.f);
  }

  TEST_CASE("cartToPolar: a cosine / sine pair gives magnitude and angle back (#1053)") {
    YSE::DSP::buffer re(YSE::STANDARD_BUFFERSIZE), im(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < re.getLength(); ++i) {
      re.getPtr()[i] = 0.5f * std::cos(Angle(i));
      im.getPtr()[i] = 0.5f * std::sin(Angle(i));
    }
    YSE::DSP::cartToPolar fn;
    fn(re, im);
    REQUIRE(fn.magnitude().getLength() == YSE::STANDARD_BUFFERSIZE);
    REQUIRE(fn.phase().getLength() == YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < re.getLength(); ++i) {
      CAPTURE(i);
      CHECK(fn.magnitude().getPtr()[i] == doctest::Approx(0.5f).epsilon(1e-6));
      CHECK(fn.phase().getPtr()[i] == doctest::Approx(Angle(i)).epsilon(1e-5));
    }
    // Against a constant imaginary part, sample by sample against <cmath>.
    fn(re, 0.25f);
    for (unsigned int i = 0; i < re.getLength(); ++i) {
      CAPTURE(i);
      CHECK(fn.magnitude().getPtr()[i] == std::hypot(re.getPtr()[i], 0.25f));
      CHECK(fn.phase().getPtr()[i] == std::atan2(0.25f, re.getPtr()[i]));
    }
  }

  TEST_CASE("polarToCart: inverts cartToPolar, signal and constant phase (#1053)") {
    YSE::DSP::buffer re(YSE::STANDARD_BUFFERSIZE), im(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < re.getLength(); ++i) {
      re.getPtr()[i] = (static_cast<float>(i) - 60.f) / 16.f;
      im.getPtr()[i] = std::sin(static_cast<float>(i) * 0.3f) * 2.f;
    }
    YSE::DSP::cartToPolar toPolar;
    YSE::DSP::polarToCart toCart;
    toPolar(re, im);
    toCart(toPolar.magnitude(), toPolar.phase());
    REQUIRE(toCart.real().getLength() == YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < re.getLength(); ++i) {
      CAPTURE(i);
      CHECK(toCart.real().getPtr()[i] == doctest::Approx(re.getPtr()[i]).epsilon(1e-5).scale(1.f));
      CHECK(toCart.imaginary().getPtr()[i] ==
            doctest::Approx(im.getPtr()[i]).epsilon(1e-5).scale(1.f));
    }
    toCart(re, kPi / 3.f);
    for (unsigned int i = 0; i < re.getLength(); ++i) {
      CAPTURE(i);
      CHECK(toCart.real().getPtr()[i] == doctest::Approx(re.getPtr()[i] * std::cos(kPi / 3.f)));
      CHECK(toCart.imaginary().getPtr()[i] ==
            doctest::Approx(re.getPtr()[i] * std::sin(kPi / 3.f)));
    }
  }

  TEST_CASE("phaseWrap: whole turns removed, result in -pi..pi (#1053)") {
    CHECK(YSE::DSP::phaseWrap::process(0.f) == 0.f);
    CHECK(YSE::DSP::phaseWrap::process(1.f) == doctest::Approx(1.f));
    CHECK(YSE::DSP::phaseWrap::process(-1.f) == doctest::Approx(-1.f));
    CHECK(YSE::DSP::phaseWrap::process(1.5f * kPi) == doctest::Approx(-0.5f * kPi));
    CHECK(YSE::DSP::phaseWrap::process(-1.5f * kPi) == doctest::Approx(0.5f * kPi));
    CHECK(YSE::DSP::phaseWrap::process(kPi) == doctest::Approx(-kPi));
    // The float nearest -pi lies just below it, so it wraps to just under +pi.
    CHECK(std::fabs(YSE::DSP::phaseWrap::process(-kPi)) == doctest::Approx(kPi));
    CHECK(YSE::DSP::phaseWrap::process(-3.14159f) == doctest::Approx(-3.14159f));
    CHECK(YSE::DSP::phaseWrap::process(7.f * 2.f * kPi + 0.25f) ==
          doctest::Approx(0.25f).epsilon(1e-4));
    CHECK(YSE::DSP::phaseWrap::process(-100.f) ==
          doctest::Approx(-100.f + 16.f * 2.f * 3.14159265358979).epsilon(1e-5));

    // A ramp over several turns: every sample within -pi..pi and a whole
    // number of turns from its input.
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < in.getLength(); ++i)
      in.getPtr()[i] = (static_cast<float>(i) - 64.f) * 0.37f;
    YSE::DSP::phaseWrap wrap;
    YSE::DSP::buffer& out = wrap(in);
    REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      const float v = out.getPtr()[i];
      CHECK(v >= -3.1415927f);
      CHECK(v <= 3.1415927f);
      const double turns = (static_cast<double>(in.getPtr()[i]) - v) / (2.0 * 3.14159265358979);
      CHECK(turns == doctest::Approx(std::round(turns)).epsilon(1e-5).scale(1.0));
      CHECK(YSE::DSP::phaseWrap::process(in.getPtr()[i]) == v);
    }
  }

  TEST_CASE("cartToPolar / polarToCart / phaseWrap: NaN and infinity never leave (#1053)") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    float a = -1.f, b = -1.f;
    YSE::DSP::cartToPolar::process(nan, 1.f, a, b);
    CHECK(a == 0.f);
    CHECK(b == 0.f);
    YSE::DSP::cartToPolar::process(inf, 1.f, a, b);
    CHECK(a == 0.f);
    CHECK(b == doctest::Approx(0.f)); // atan2(1, inf) is a finite 0
    YSE::DSP::polarToCart::process(1.f, inf, a, b);
    CHECK(a == 0.f);
    CHECK(b == 0.f);
    YSE::DSP::polarToCart::process(inf, 0.f, a, b);
    CHECK(a == 0.f);
    CHECK(std::isfinite(b));
    CHECK(YSE::DSP::phaseWrap::process(nan) == 0.f);
    CHECK(YSE::DSP::phaseWrap::process(inf) == 0.f);
    CHECK(YSE::DSP::phaseWrap::process(-inf) == 0.f);

    YSE::DSP::buffer in(4);
    in.getPtr()[0] = nan;
    in.getPtr()[1] = inf;
    in.getPtr()[2] = -inf;
    in.getPtr()[3] = 1e30f;
    YSE::DSP::cartToPolar toPolar;
    YSE::DSP::polarToCart toCart;
    YSE::DSP::phaseWrap wrap;
    toPolar(in, in);
    toCart(in, in);
    YSE::DSP::buffer& w = wrap(in);
    for (unsigned int i = 0; i < 4; ++i) {
      CAPTURE(i);
      CHECK(std::isfinite(toPolar.magnitude().getPtr()[i]));
      CHECK(std::isfinite(toPolar.phase().getPtr()[i]));
      CHECK(std::isfinite(toCart.real().getPtr()[i]));
      CHECK(std::isfinite(toCart.imaginary().getPtr()[i]));
      CHECK(std::isfinite(w.getPtr()[i]));
    }
    toCart(in, nan);
    for (unsigned int i = 0; i < 4; ++i) {
      CAPTURE(i);
      CHECK(toCart.real().getPtr()[i] == 0.f);
      CHECK(toCart.imaginary().getPtr()[i] == 0.f);
    }
  }

  TEST_CASE("cartToPolar / polarToCart / phaseWrap: block length follows the input (#1053)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE), shortIn(16);
    in = 0.5f;
    shortIn = 0.25f;
    YSE::DSP::cartToPolar toPolar;
    YSE::DSP::polarToCart toCart;
    YSE::DSP::phaseWrap wrap;
    toPolar(in, shortIn);
    CHECK(toPolar.magnitude().getLength() == 16);
    CHECK(toPolar.phase().getLength() == 16);
    toCart(shortIn, 1.f);
    CHECK(toCart.real().getLength() == 16);
    CHECK(toCart.imaginary().getLength() == 16);
    CHECK(wrap(shortIn).getLength() == 16);
    toPolar(in, 0.f);
    CHECK(toPolar.magnitude().getLength() == YSE::STANDARD_BUFFERSIZE);
    toCart(in, in);
    CHECK(toCart.real().getLength() == YSE::STANDARD_BUFFERSIZE);
    CHECK(wrap(in).getLength() == YSE::STANDARD_BUFFERSIZE);
  }

  TEST_CASE("cartToPolar / polarToCart / phaseWrap: processing allocates nothing (#1053)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE), shortIn(16);
    in = 0.5f;
    shortIn = 0.25f;
    YSE::DSP::cartToPolar toPolar;
    YSE::DSP::polarToCart toCart;
    YSE::DSP::phaseWrap wrap;
    {
      TestHelpers::ProbeScope probe;
      toPolar(in, in);
      toPolar(in, shortIn);
      toPolar(in, 0.3f);
      toCart(in, in);
      toCart(shortIn, in);
      toCart(in, 0.3f);
      wrap(in);
      wrap(shortIn);
      wrap(in);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
