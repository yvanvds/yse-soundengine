// Tests for YSE::DSP::reverseSubtract, reverseDivide and modulo
// (YseEngine/dsp/operators/arithmetic.hpp), issue #1046. Reference output is
// closed form: right - x, right / x and fmod(x, divisor) per sample.

#include <doctest/doctest.h>
#include <cmath>
#include "dsp/operators/arithmetic.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

namespace {

  struct SampleRateGuard {
    UInt saved;
    SampleRateGuard() : saved(YSE::SAMPLERATE) {}
    ~SampleRateGuard() {
      YSE::SAMPLERATE = saved;
    }
  };

  // A ramp from -4 to just under 4 across one block, crossing 0 at sample 64.
  void FillRamp(YSE::DSP::buffer& b) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = (static_cast<float>(i) - 64.f) / 16.f;
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("reverseSubtract: right - in, with a number and with a signal (#1046)") {
    YSE::DSP::reverseSubtract op;
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    FillRamp(in);

    YSE::DSP::buffer& out = op(in, 1.f);
    REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      CHECK(out.getPtr()[i] == doctest::Approx(1.f - in.getPtr()[i]));
    }

    YSE::DSP::buffer right(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < right.getLength(); ++i)
      right.getPtr()[i] = static_cast<float>(i);
    YSE::DSP::buffer& out2 = op(in, right);
    for (unsigned int i = 0; i < out2.getLength(); ++i) {
      CAPTURE(i);
      CHECK(out2.getPtr()[i] == doctest::Approx(static_cast<float>(i) - in.getPtr()[i]));
    }
    CHECK(YSE::DSP::reverseSubtract::process(0.25f, 1.f) == doctest::Approx(0.75f));
  }

  TEST_CASE("reverseSubtract: 1 - x turns a sine upside down around 0.5 (#1046)") {
    YSE::DSP::reverseSubtract op;
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < in.getLength(); ++i)
      in.getPtr()[i] = 0.5f + 0.5f * std::sin(static_cast<float>(i) * 0.1f);
    YSE::DSP::buffer& out = op(in, 1.f);
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      CHECK(out.getPtr()[i] ==
            doctest::Approx(0.5f - 0.5f * std::sin(static_cast<float>(i) * 0.1f)).epsilon(1e-5));
    }
  }

  TEST_CASE("reverseDivide: right / in, and 0 where in is 0 (#1046)") {
    YSE::DSP::reverseDivide op;
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    FillRamp(in);
    REQUIRE(in.getPtr()[64] == 0.f);

    YSE::DSP::buffer& out = op(in, 2.f);
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      const float x = in.getPtr()[i];
      if (x == 0.f) {
        CHECK(out.getPtr()[i] == 0.f);
      } else {
        CHECK(out.getPtr()[i] == doctest::Approx(2.f / x));
      }
      CHECK(std::isfinite(out.getPtr()[i]));
    }

    YSE::DSP::buffer right(YSE::STANDARD_BUFFERSIZE);
    right = 3.f;
    YSE::DSP::buffer& out2 = op(in, right);
    CHECK(out2.getPtr()[0] == doctest::Approx(3.f / -4.f));
    CHECK(out2.getPtr()[64] == 0.f);
    CHECK(out2.getPtr()[80] == doctest::Approx(3.f));
    CHECK(YSE::DSP::reverseDivide::process(4.f, 1.f) == doctest::Approx(0.25f));
  }

  TEST_CASE("modulo: the remainder has the sign of the dividend, as fmod (#1046)") {
    using YSE::DSP::modulo;
    CHECK(modulo::process(5.5f, 2.f) == doctest::Approx(1.5f));
    CHECK(modulo::process(-5.5f, 2.f) == doctest::Approx(-1.5f));
    CHECK(modulo::process(5.5f, -2.f) == doctest::Approx(1.5f));
    CHECK(modulo::process(-5.5f, -2.f) == doctest::Approx(-1.5f));
    CHECK(modulo::process(1.25f, 1.f) == doctest::Approx(0.25f));
    CHECK(modulo::process(3.f, 0.f) == 0.f); // divisor 0: 0, not NaN

    modulo op;
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    FillRamp(in);
    YSE::DSP::buffer& out = op(in, 1.5f);
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      const float x = in.getPtr()[i];
      CHECK(out.getPtr()[i] == doctest::Approx(std::fmod(x, 1.5f)));
      CHECK(std::fabs(out.getPtr()[i]) < 1.5f);
      if (x > 0.f) CHECK(out.getPtr()[i] >= 0.f);
      if (x < 0.f) CHECK(out.getPtr()[i] <= 0.f);
    }

    YSE::DSP::buffer divisor(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < divisor.getLength(); ++i)
      divisor.getPtr()[i] = i < 64 ? 0.f : 0.75f;
    YSE::DSP::buffer& out2 = op(in, divisor);
    for (unsigned int i = 0; i < out2.getLength(); ++i) {
      CAPTURE(i);
      const float expected = i < 64 ? 0.f : std::fmod(in.getPtr()[i], 0.75f);
      CHECK(out2.getPtr()[i] == doctest::Approx(expected));
    }
  }

  TEST_CASE("arithmetic operators: block length follows the shorter input, capped (#1046)") {
    YSE::DSP::reverseSubtract sub;
    YSE::DSP::modulo mod;
    YSE::DSP::buffer shortIn(32);
    YSE::DSP::buffer longIn(YSE::STANDARD_BUFFERSIZE * 4);
    YSE::DSP::buffer full(YSE::STANDARD_BUFFERSIZE);
    shortIn = 1.f;
    longIn = 1.f;
    full = 2.f;
    CHECK(sub(shortIn, 0.f).getLength() == 32);
    CHECK(sub(longIn, 0.f).getLength() == YSE::STANDARD_BUFFERSIZE);
    CHECK(mod(shortIn, full).getLength() == 32);
    CHECK(mod(full, shortIn).getLength() == 32);
    CHECK(mod(longIn, full).getLength() == YSE::STANDARD_BUFFERSIZE);
  }

  TEST_CASE("arithmetic operators: no sample-rate state, same output at any rate (#1046)") {
    SampleRateGuard guard;
    YSE::DSP::reverseDivide op;
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    in = 4.f;
    op(in, 1.f);
    YSE::SAMPLERATE = 96000;
    CHECK(op(in, 1.f).getPtr()[10] == doctest::Approx(0.25f));
    YSE::SAMPLERATE = 22050;
    CHECK(op(in, 1.f).getPtr()[10] == doctest::Approx(0.25f));
  }

  TEST_CASE("arithmetic operators: blocks of any length allocate nothing (#1046)") {
    YSE::DSP::reverseSubtract sub;
    YSE::DSP::reverseDivide div;
    YSE::DSP::modulo mod;
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    YSE::DSP::buffer shortIn(32);
    YSE::DSP::buffer longIn(YSE::STANDARD_BUFFERSIZE * 4);
    in = 0.5f;
    shortIn = 0.5f;
    longIn = 0.5f;
    {
      TestHelpers::ProbeScope probe;
      sub(in, 1.f);
      sub(shortIn, in);
      sub(longIn, 1.f);
      div(in, 1.f);
      div(shortIn, in);
      div(in, in);
      mod(in, 0.3f);
      mod(longIn, in);
      mod(shortIn, 0.f);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
