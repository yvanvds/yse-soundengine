// Tests for YSE::DSP::fold (YseEngine/dsp/operators/fold.hpp), issue #1113.
// Reference output is closed form, in double: with the range lo..hi and its
// span s, fold is the triangle wave lo + s - |((x - lo) mod 2s) - s|, wrap is
// lo + ((x - lo) mod s), clip is min(max(x, lo), hi).

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/operators/fold.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::fold;
using MODE = YSE::DSP::fold::MODE;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr float INF = std::numeric_limits<float>::infinity();

  double Mod(double a, double b) {
    const double r = std::fmod(a, b);
    return r < 0.0 ? r + b : r;
  }

  double Reference(double x, double lo, double hi, MODE how) {
    const double s = hi - lo;
    switch (how) {
    case MODE::CLIP:
      return std::fmin(std::fmax(x, lo), hi);
    case MODE::WRAP:
      return lo + Mod(x - lo, s);
    case MODE::FOLD:
    default:
      return lo + s - std::fabs(Mod(x - lo, 2.0 * s) - s);
    }
  }

  float Wobble(unsigned int n) {
    const auto x = static_cast<float>(n);
    return 2.7f * std::sin(x * 0.031f) + 0.4f * std::sin(x * 1.7f);
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("fold: defaults fold into 0..1 (#1113)") {
    fold f;
    CHECK(f.mode() == MODE::FOLD);
    CHECK(f.low() == 0.f);
    CHECK(f.high() == 1.f);
    CHECK(f.process(0.25f) == 0.25f);
    CHECK(f.process(1.f) == 1.f);
    CHECK(f.process(0.f) == 0.f);
    CHECK(f.process(1.25f) == 0.75f);
    CHECK(f.process(-0.25f) == 0.25f);
    CHECK(f.process(2.25f) == 0.25f);
    CHECK(f.process(-1.5f) == 0.5f);
    CHECK(f.process(2.f) == 0.f);
    CHECK(f.process(3.f) == 1.f);
  }

  TEST_CASE("fold: values by hand in each mode (#1113)") {
    fold f;
    f.range(-1.f, 3.f);
    // Fold: reflected from -1 and 3, period 8.
    CHECK(f.process(4.f) == 2.f);
    CHECK(f.process(-2.f) == 0.f);
    CHECK(f.process(7.f) == -1.f);
    CHECK(f.process(8.f) == 0.f);
    CHECK(f.process(10.f) == 2.f);
    CHECK(f.process(-6.f) == 2.f);
    f.mode(MODE::WRAP);
    CHECK(f.mode() == MODE::WRAP);
    CHECK(f.process(3.f) == -1.f); // half-open at the top
    CHECK(f.process(4.f) == 0.f);
    CHECK(f.process(-2.f) == 2.f);
    CHECK(f.process(11.5f) == 3.5f - 4.f);
    CHECK(f.process(-1.f) == -1.f);
    CHECK(f.process(2.5f) == 2.5f);
    f.mode(MODE::CLIP);
    CHECK(f.process(4.f) == 3.f);
    CHECK(f.process(-200.f) == -1.f);
    CHECK(f.process(3.f) == 3.f);
    CHECK(f.process(0.125f) == 0.125f);
  }

  TEST_CASE("fold: samples inside the range pass bit for bit in every mode (#1113)") {
    for (const MODE how : {MODE::FOLD, MODE::WRAP, MODE::CLIP}) {
      CAPTURE(static_cast<int>(how));
      fold f;
      f.mode(how).range(-3.5f, 3.5f);
      for (unsigned int n = 0; n < 1024; ++n) {
        const float x = Wobble(n);
        CAPTURE(x);
        CHECK(f.process(x) == x);
      }
    }
  }

  TEST_CASE("fold: blocks against the closed form in each mode (#1113)") {
    const float ranges[][2] = {{0.f, 1.f}, {-1.f, 1.f}, {-0.3f, 0.7f}, {2.f, 5.f}, {-0.1f, 0.1f}};
    for (const MODE how : {MODE::FOLD, MODE::WRAP, MODE::CLIP}) {
      for (const auto& r : ranges) {
        CAPTURE(static_cast<int>(how));
        CAPTURE(r[0]);
        CAPTURE(r[1]);
        fold f;
        f.mode(how).range(r[0], r[1]);
        buffer in(N);
        for (unsigned int i = 0; i < N; ++i)
          in.getPtr()[i] = Wobble(i * 7) * 3.f;
        buffer& out = f(in);
        REQUIRE(out.getLength() == N);
        for (unsigned int i = 0; i < N; ++i) {
          CAPTURE(i);
          const float x = in.getPtr()[i];
          const float y = out.getPtr()[i];
          CHECK(y >= r[0]);
          CHECK(y <= r[1]);
          const double expected = Reference(x, r[0], r[1], how);
          // A wrapped sample that lands within rounding of the top may come
          // out at either end.
          const double span = static_cast<double>(r[1]) - r[0];
          if (how == MODE::WRAP && std::fabs(expected - r[1]) < 1e-5 * span)
            CHECK((std::fabs(y - r[0]) < 1e-5 * span || std::fabs(y - r[1]) < 1e-5 * span));
          else
            CHECK(y == doctest::Approx(expected).epsilon(1e-5).scale(span));
        }
      }
    }
  }

  TEST_CASE("fold: a fold is continuous and a wrap of a ramp is a sawtooth (#1113)") {
    fold f;
    f.range(-0.5f, 0.5f);
    float previous = f.process(-3.f);
    for (int n = 1; n <= 6000; ++n) {
      const float y = f.process(-3.f + static_cast<float>(n) * 0.001f);
      CHECK(std::fabs(y - previous) <= 0.0011f);
      previous = y;
    }
    f.mode(MODE::WRAP);
    int jumps = 0;
    previous = f.process(-3.f);
    for (int n = 1; n <= 6000; ++n) {
      const float y = f.process(-3.f + static_cast<float>(n) * 0.001f);
      if (y < previous) {
        ++jumps;
        CHECK(previous - y == doctest::Approx(1.f).epsilon(0.01));
      } else {
        CHECK(y - previous <= 0.0011f);
      }
      previous = y;
    }
    CHECK(jumps == 6);
  }

  TEST_CASE("fold: reversed and collapsed ranges, non-finite limits and input (#1113)") {
    fold f;
    f.range(1.f, -1.f);
    CHECK(f.low() == 1.f);
    CHECK(f.high() == -1.f);
    CHECK(f.process(1.5f) == 0.5f);
    CHECK(f.process(-1.5f) == -0.5f);
    CHECK(fold::Limit(1.5f, 1.f, -1.f, MODE::FOLD) == fold::Limit(1.5f, -1.f, 1.f, MODE::FOLD));

    f.range(0.5f, 0.5f);
    for (const MODE how : {MODE::FOLD, MODE::WRAP, MODE::CLIP}) {
      f.mode(how);
      CHECK(f.process(7.f) == 0.5f);
      CHECK(f.process(-7.f) == 0.5f);
      CHECK(f.process(0.5f) == 0.5f);
      CHECK(f.process(std::nanf("")) == 0.5f);
      CHECK(f.process(INF) == 0.5f);
    }

    f.mode(MODE::FOLD).range(-1.f, 1.f);
    f.low(std::nanf("")).high(INF);
    CHECK(f.low() == -1.f);
    CHECK(f.high() == 1.f);
    f.range(-INF, std::nanf(""));
    CHECK(f.low() == -1.f);
    CHECK(f.high() == 1.f);

    for (const MODE how : {MODE::FOLD, MODE::WRAP, MODE::CLIP}) {
      CAPTURE(static_cast<int>(how));
      f.mode(how).range(-1.f, 1.f);
      CHECK(f.process(std::nanf("")) == 0.f);
      CHECK(f.process(INF) == 1.f);
      CHECK(f.process(-INF) == -1.f);
      // NaN counts as 0, then is kept in range.
      f.range(2.f, 3.f);
      const float nanOut = f.process(std::nanf(""));
      CHECK(nanOut == static_cast<float>(Reference(0.0, 2.0, 3.0, how)));
      CHECK(f.process(1e30f) >= 2.f);
      CHECK(f.process(1e30f) <= 3.f);
      CHECK(f.process(-1e30f) >= 2.f);
      CHECK(f.process(-1e30f) <= 3.f);
    }
  }

  TEST_CASE("fold: the limits read per sample from signals (#1113)") {
    fold f;
    f.mode(MODE::WRAP);
    buffer in(N);
    buffer lo(N);
    buffer hi(N);
    for (unsigned int i = 0; i < N; ++i) {
      in.getPtr()[i] = Wobble(i);
      lo.getPtr()[i] = -0.5f - static_cast<float>(i % 8) * 0.1f;
      hi.getPtr()[i] = 0.25f + static_cast<float>(i % 5) * 0.2f;
    }
    lo.getPtr()[10] = std::nanf("");
    hi.getPtr()[11] = INF;
    buffer& out = f(in, lo, hi);
    REQUIRE(out.getLength() == N);
    fold reference;
    reference.mode(MODE::WRAP);
    for (unsigned int i = 0; i < N; ++i) {
      CAPTURE(i);
      reference.range(lo.getPtr()[i], hi.getPtr()[i]);
      CHECK(out.getPtr()[i] == reference.process(in.getPtr()[i]));
    }
    CHECK(f.low() == lo.getPtr()[N - 1]);
    CHECK(f.high() == hi.getPtr()[N - 1]);
    // Sample 10 keeps the low limit of sample 9, sample 11 the high of 10.
    CHECK(out.getPtr()[10] ==
          fold::Limit(in.getPtr()[10], lo.getPtr()[9], hi.getPtr()[10], MODE::WRAP));
    CHECK(out.getPtr()[11] ==
          fold::Limit(in.getPtr()[11], lo.getPtr()[11], hi.getPtr()[10], MODE::WRAP));
  }

  TEST_CASE("fold: per-sample, per-block and split-block calls agree (#1113)") {
    const unsigned int splits[] = {128, 64, 32, 13, 1};
    std::vector<float> input(4 * N);
    for (unsigned int n = 0; n < input.size(); ++n)
      input[n] = Wobble(n);
    std::vector<float> expected(input.size());
    {
      fold f;
      f.range(-0.6f, 0.9f);
      for (unsigned int n = 0; n < input.size(); ++n)
        expected[n] = f.process(input[n]);
    }
    for (unsigned int split : splits) {
      CAPTURE(split);
      fold f;
      f.range(-0.6f, 0.9f);
      buffer in(split);
      unsigned int n = 0;
      while (n + split <= input.size()) {
        for (unsigned int i = 0; i < split; ++i)
          in.getPtr()[i] = input[n + i];
        buffer& out = f(in);
        REQUIRE(out.getLength() == split);
        for (unsigned int i = 0; i < split; ++i) {
          CAPTURE(n + i);
          CHECK(out.getPtr()[i] == expected[n + i]);
        }
        n += split;
      }
    }
  }

  TEST_CASE("fold: the output does not depend on the sample rate (#1113)") {
    std::vector<float> first;
    for (const UInt rate : {44100u, 48000u, 96000u}) {
      CAPTURE(rate);
      TestHelpers::ScopedSampleRate scoped(rate);
      fold f;
      f.range(-0.4f, 0.4f);
      buffer in(N);
      for (unsigned int i = 0; i < N; ++i)
        in.getPtr()[i] = Wobble(i);
      buffer& out = f(in);
      if (first.empty()) {
        first.assign(out.getPtr(), out.getPtr() + N);
        continue;
      }
      for (unsigned int i = 0; i < N; ++i)
        CHECK(out.getPtr()[i] == first[i]);
    }
  }

  TEST_CASE("fold: a longer block is processed up to STANDARD_BUFFERSIZE (#1113)") {
    fold f;
    buffer shortIn(16);
    shortIn = 0.5f;
    CHECK(f(shortIn).getLength() == 16);
    buffer longIn(N * 2);
    longIn = 0.5f;
    CHECK(f(longIn).getLength() == N);
    buffer lo(8);
    buffer hi(N);
    lo = 0.f;
    hi = 1.f;
    CHECK(f(longIn, lo, hi).getLength() == 8);
  }

  TEST_CASE("fold: processing allocates nothing (#1113)") {
    fold f;
    buffer in(N);
    buffer shortIn(32);
    buffer lo(N);
    buffer hi(N);
    in = 1.3f;
    shortIn = 1.3f;
    lo = -1.f;
    hi = 1.f;
    f(in);
    f(in, lo, hi);
    {
      TestHelpers::ProbeScope probe;
      f(in);
      f(shortIn);
      f(in, lo, hi);
      f.mode(MODE::WRAP).range(0.f, 2.f);
      f(in);
      (void)f.process(5.f);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
