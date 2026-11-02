// Tests for YSE::DSP::scale (YseEngine/dsp/operators/scale.hpp), issue #1114.
// Reference output is closed form, in double: modern is
// outLow + (outHigh - outLow) * sign(n) |n|^e with n the normalized input,
// classic MSP's printed linedrive formula, inverse modern with 1/e when the
// output range runs down.

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/operators/scale.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::scale;
using MODE = YSE::DSP::scale::MODE;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr float INF = std::numeric_limits<float>::infinity();

  double Reference(double x, double il, double ih, double ol, double oh, double e, MODE how) {
    if (how == MODE::CLASSIC) {
      const double r = oh - ol;
      const double y =
          ol + r * (r * std::exp(-(ih - il) * std::log(e)) * std::exp(x * std::log(e)));
      return r >= 0.0 ? y : -y;
    }
    if (how == MODE::MODERN_INVERSE && ol > oh) e = 1.0 / e;
    const double n = (x - il) / (ih - il);
    const double c = n >= 0.0 ? std::pow(n, e) : -std::pow(-n, e);
    return ol + (oh - ol) * c;
  }

  float Wobble(unsigned int n) {
    const auto x = static_cast<float>(n);
    return 1.3f * std::sin(x * 0.031f) + 0.2f * std::sin(x * 1.7f);
  }

  struct Setting {
    float il, ih, ol, oh, e;
  };
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("scale: the defaults map 0..1 onto 0..1, passing the signal bit for bit (#1114)") {
    scale s;
    CHECK(s.mode() == MODE::MODERN);
    CHECK(s.inLow() == 0.f);
    CHECK(s.inHigh() == 1.f);
    CHECK(s.outLow() == 0.f);
    CHECK(s.outHigh() == 1.f);
    CHECK(s.exponent() == 1.f);
    for (unsigned int n = 0; n < 200; ++n) {
      const float x = Wobble(n);
      CHECK(s.process(x) == x);
    }
  }

  TEST_CASE("scale: values by hand in the modern mode (#1114)") {
    scale s;
    s.inRange(-1.f, 1.f).outRange(200.f, 800.f);
    CHECK(s.process(-1.f) == 200.f);
    CHECK(s.process(1.f) == 800.f);
    CHECK(s.process(0.f) == 500.f);
    CHECK(s.process(0.5f) == 650.f);
    // Not limited: past the input range, past the output range.
    CHECK(s.process(2.f) == 1100.f);
    CHECK(s.process(-2.f) == -100.f);

    s.inRange(0.f, 1.f).outRange(0.f, 1.f).exponent(2.f);
    CHECK(s.process(0.5f) == 0.25f);
    CHECK(s.process(2.f) == 4.f);
    // Mirrored below the input range.
    CHECK(s.process(-0.5f) == -0.25f);
    s.exponent(0.5f);
    CHECK(s.process(0.25f) == 0.5f);
    CHECK(s.process(-0.25f) == -0.5f);

    // Either range may run down.
    s.inRange(1.f, 0.f).outRange(0.f, 10.f).exponent(1.f);
    CHECK(s.process(0.25f) == 7.5f);
    s.inRange(0.f, 1.f).outRange(10.f, 0.f);
    CHECK(s.process(0.25f) == 7.5f);
    s.exponent(2.f);
    CHECK(s.process(0.5f) == 7.5f);
  }

  TEST_CASE("scale: the inverse mode inverts the exponent for a downward output range "
            "(#1114)") {
    scale s;
    s.mode(MODE::MODERN_INVERSE).exponent(2.f);
    CHECK(s.mode() == MODE::MODERN_INVERSE);
    // Upward: as the modern mode.
    CHECK(s.process(0.5f) == 0.25f);
    // Downward: 1 - 0.25^(1/2) rather than 1 - 0.25^2.
    s.outRange(1.f, 0.f);
    CHECK(s.process(0.25f) == 0.5f);
    s.mode(MODE::MODERN);
    CHECK(s.process(0.25f) == 0.9375f);
  }

  TEST_CASE("scale: values by hand in the classic mode (#1114)") {
    scale s;
    s.mode(MODE::CLASSIC).exponent(2.f);
    // 0..1 onto 0..1 with base 2: 2^(x - 1).
    CHECK(s.process(1.f) == doctest::Approx(1.f));
    CHECK(s.process(0.f) == doctest::Approx(0.5f));
    CHECK(s.process(-1.f) == doctest::Approx(0.25f));
    // Onto 0..2: 2 * 2 * 2^(x - 1).
    s.outRange(0.f, 2.f);
    CHECK(s.process(0.f) == doctest::Approx(2.f));
    CHECK(s.process(1.f) == doctest::Approx(4.f));
    // Onto 2..0: negated as a whole, -(2 + (-2) * (-2) * 2^(x - 1)).
    s.outRange(2.f, 0.f);
    CHECK(s.process(0.f) == doctest::Approx(-4.f));
    // A base of 1 is a constant.
    s.outRange(0.f, 1.f).exponent(1.f);
    CHECK(s.process(0.3f) == 1.f);
    CHECK(s.process(-7.f) == 1.f);
  }

  TEST_CASE("scale: blocks against the closed form in each mode (#1114)") {
    const Setting settings[] = {
        {0.f, 1.f, 0.f, 1.f, 1.f},     {-1.f, 1.f, 200.f, 800.f, 1.f},
        {0.f, 1.f, 0.f, 1.f, 2.f},     {-1.f, 1.f, 1.f, -1.f, 3.f},
        {0.f, 2.f, 10.f, 0.f, 0.5f},   {1.f, -1.f, -5.f, 5.f, 1.06f},
        {-0.5f, 0.5f, 0.f, 4.f, 1.5f},
    };
    const MODE modes[] = {MODE::MODERN, MODE::CLASSIC, MODE::MODERN_INVERSE};
    buffer in(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = Wobble(i);
    for (const Setting& c : settings) {
      for (MODE how : modes) {
        CAPTURE(c.il);
        CAPTURE(c.ih);
        CAPTURE(c.ol);
        CAPTURE(c.oh);
        CAPTURE(c.e);
        CAPTURE(static_cast<int>(how));
        scale s;
        s.mode(how).inRange(c.il, c.ih).outRange(c.ol, c.oh).exponent(c.e);
        buffer& out = s(in);
        REQUIRE(out.getLength() == N);
        for (unsigned int i = 0; i < N; ++i) {
          CAPTURE(i);
          const double expected = Reference(in.getPtr()[i], c.il, c.ih, c.ol, c.oh, c.e, how);
          CHECK(out.getPtr()[i] ==
                doctest::Approx(expected).epsilon(1e-5).scale(std::fabs(c.oh - c.ol)));
          CHECK(out.getPtr()[i] == scale::Map(in.getPtr()[i], c.il, c.ih, c.ol, c.oh, c.e, how));
        }
      }
    }
  }

  TEST_CASE("scale: collapsed range, non-finite settings, input and results (#1114)") {
    scale s;
    // A collapsed input range gives outLow in the modern modes.
    s.inRange(0.5f, 0.5f).outRange(3.f, 7.f);
    CHECK(s.process(0.2f) == 3.f);
    CHECK(s.process(0.5f) == 3.f);
    s.mode(MODE::MODERN_INVERSE);
    CHECK(s.process(9.f) == 3.f);

    // A setting that is not finite is ignored.
    s.mode(MODE::MODERN).inRange(0.f, 1.f).outRange(0.f, 1.f);
    s.inLow(std::nanf("")).inHigh(INF).outLow(-INF).outHigh(std::nanf("")).exponent(INF);
    CHECK(s.inLow() == 0.f);
    CHECK(s.inHigh() == 1.f);
    CHECK(s.outLow() == 0.f);
    CHECK(s.outHigh() == 1.f);
    CHECK(s.exponent() == 1.f);

    // A result that is not finite gives 0.
    CHECK(s.process(std::nanf("")) == 0.f);
    CHECK(s.process(INF) == 0.f);
    CHECK(s.process(-INF) == 0.f);
    s.exponent(-1.f);
    CHECK(s.process(0.f) == 0.f); // 0^-1
    s.mode(MODE::CLASSIC).exponent(0.f);
    CHECK(s.process(0.5f) == 0.f); // log(0)
    s.exponent(-2.f);
    CHECK(s.process(0.5f) == 0.f); // log of a negative base
    s.exponent(1.06f);
    CHECK(s.process(1e6f) == 0.f); // overflow
  }

  TEST_CASE("scale: the settings read per sample from signals (#1114)") {
    scale s;
    s.mode(MODE::MODERN_INVERSE);
    buffer in(N);
    buffer il(N);
    buffer ih(N);
    buffer ol(N);
    buffer oh(N);
    buffer ex(N);
    for (unsigned int i = 0; i < N; ++i) {
      in.getPtr()[i] = Wobble(i);
      il.getPtr()[i] = -1.f - static_cast<float>(i % 3) * 0.1f;
      ih.getPtr()[i] = 1.f + static_cast<float>(i % 5) * 0.2f;
      ol.getPtr()[i] = (i % 2) != 0u ? 4.f : -4.f;
      oh.getPtr()[i] = static_cast<float>(i % 7);
      ex.getPtr()[i] = 0.5f + static_cast<float>(i % 4) * 0.5f;
    }
    il.getPtr()[10] = std::nanf("");
    ex.getPtr()[11] = INF;
    buffer& out = s(in, il, ih, ol, oh, ex);
    REQUIRE(out.getLength() == N);
    for (unsigned int i = 0; i < N; ++i) {
      CAPTURE(i);
      const float lowIn = i == 10 ? il.getPtr()[9] : il.getPtr()[i];
      const float e = i == 11 ? ex.getPtr()[10] : ex.getPtr()[i];
      CHECK(out.getPtr()[i] == scale::Map(in.getPtr()[i], lowIn, ih.getPtr()[i], ol.getPtr()[i],
                                          oh.getPtr()[i], e, MODE::MODERN_INVERSE));
    }
    CHECK(s.inLow() == il.getPtr()[N - 1]);
    CHECK(s.inHigh() == ih.getPtr()[N - 1]);
    CHECK(s.outLow() == ol.getPtr()[N - 1]);
    CHECK(s.outHigh() == oh.getPtr()[N - 1]);
    CHECK(s.exponent() == ex.getPtr()[N - 1]);
  }

  TEST_CASE("scale: per-sample, per-block and split-block calls agree (#1114)") {
    const unsigned int splits[] = {128, 64, 32, 13, 1};
    std::vector<float> input(4 * N);
    for (unsigned int n = 0; n < input.size(); ++n)
      input[n] = Wobble(n);
    const auto configure = [](scale& s) {
      s.inRange(-1.f, 1.f).outRange(100.f, 10.f).exponent(2.f);
    };
    std::vector<float> expected(input.size());
    {
      scale s;
      configure(s);
      for (unsigned int n = 0; n < input.size(); ++n)
        expected[n] = s.process(input[n]);
    }
    for (unsigned int split : splits) {
      CAPTURE(split);
      scale s;
      configure(s);
      buffer in(split);
      unsigned int n = 0;
      while (n + split <= input.size()) {
        for (unsigned int i = 0; i < split; ++i)
          in.getPtr()[i] = input[n + i];
        buffer& out = s(in);
        REQUIRE(out.getLength() == split);
        for (unsigned int i = 0; i < split; ++i) {
          CAPTURE(n + i);
          CHECK(out.getPtr()[i] == expected[n + i]);
        }
        n += split;
      }
    }
  }

  TEST_CASE("scale: the output does not depend on the sample rate (#1114)") {
    std::vector<float> first;
    for (const UInt rate : {44100u, 48000u, 96000u}) {
      CAPTURE(rate);
      TestHelpers::ScopedSampleRate scoped(rate);
      scale s;
      s.inRange(-1.f, 1.f).outRange(0.f, 5.f).exponent(3.f);
      buffer in(N);
      for (unsigned int i = 0; i < N; ++i)
        in.getPtr()[i] = Wobble(i);
      buffer& out = s(in);
      if (first.empty()) {
        first.assign(out.getPtr(), out.getPtr() + N);
        continue;
      }
      for (unsigned int i = 0; i < N; ++i)
        CHECK(out.getPtr()[i] == first[i]);
    }
  }

  TEST_CASE("scale: a longer block is processed up to STANDARD_BUFFERSIZE (#1114)") {
    scale s;
    buffer shortIn(16);
    shortIn = 0.5f;
    CHECK(s(shortIn).getLength() == 16);
    buffer longIn(N * 2);
    longIn = 0.5f;
    CHECK(s(longIn).getLength() == N);
    buffer eight(8);
    buffer full(N);
    eight = 1.f;
    full = 0.f;
    CHECK(s(longIn, full, eight, full, eight, eight).getLength() == 8);
  }

  TEST_CASE("scale: processing allocates nothing (#1114)") {
    scale s;
    buffer in(N);
    buffer shortIn(32);
    buffer lo(N);
    buffer hi(N);
    in = 0.3f;
    shortIn = 0.3f;
    lo = -1.f;
    hi = 2.f;
    s(in);
    s(in, lo, hi, lo, hi, hi);
    {
      TestHelpers::ProbeScope probe;
      s(in);
      s(shortIn);
      s(in, lo, hi, lo, hi, hi);
      s.mode(MODE::CLASSIC).exponent(1.06f);
      s(in);
      (void)s.process(5.f);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
