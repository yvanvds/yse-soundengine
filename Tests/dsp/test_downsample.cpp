// Tests for YSE::DSP::downsample (YseEngine/dsp/operators/downsample.hpp),
// issue #1109. Reference output is closed form: at a period q/p (q >= p) the
// k-th take (from 0) is at sample ceil(k q / p), so sample n holds input
// ceil(floor(n p / q) q / p).

#include <doctest/doctest.h>
#include <cmath>
#include <vector>
#include "dsp/operators/downsample.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::downsample;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // The input sample held at sample n, for a period q / p (q >= p), from a
  // fresh object.
  unsigned int HeldIndex(unsigned int n, unsigned int q, unsigned int p) {
    const unsigned int k = n * p / q; // takes before and at n, minus one
    return (k * q + p - 1) / p; // ceil(k q / p)
  }

  // Distinct for every n.
  float Ramp(unsigned int n) {
    return static_cast<float>(n + 1) / 1024.f;
  }

  float Wobble(unsigned int n) {
    const auto x = static_cast<float>(n);
    return 0.8f * std::sin(x * 0.031f) + 0.15f * std::sin(x * 1.7f);
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("downsample: the default takes every sample bit for bit (#1109)") {
    downsample d;
    CHECK(d.period() == downsample::DEFAULT_PERIOD);
    CHECK(d.held() == 0.f);
    for (unsigned int n = 0; n < 256; ++n) {
      CAPTURE(n);
      const float x = Wobble(n);
      CHECK(d.process(x) == x);
    }
  }

  TEST_CASE("downsample: periods hold the input against the closed form (#1109)") {
    struct Period {
      unsigned int q;
      unsigned int p;
    };
    const Period periods[] = {{1, 1}, {2, 1}, {3, 1}, {7, 1}, {100, 1}, {5, 2}, {7, 4}, {33, 32}};
    for (const Period r : periods) {
      CAPTURE(r.q);
      CAPTURE(r.p);
      downsample d;
      d.period(static_cast<float>(r.q) / static_cast<float>(r.p));
      buffer in(N);
      for (unsigned int block = 0; block < 3; ++block) {
        CAPTURE(block);
        for (unsigned int i = 0; i < N; ++i)
          in.getPtr()[i] = Ramp(block * N + i);
        buffer& out = d(in);
        REQUIRE(out.getLength() == N);
        for (unsigned int i = 0; i < N; ++i) {
          CAPTURE(i);
          const unsigned int n = block * N + i;
          CHECK(out.getPtr()[i] == Ramp(HeldIndex(n, r.q, r.p)));
        }
      }
    }
    // 4 by hand: inputs 0, 4, 8 ... each held four samples.
    downsample four;
    four.period(4.f);
    const float expected[] = {1.f, 1.f, 1.f, 1.f, 5.f, 5.f, 5.f, 5.f, 9.f};
    for (unsigned int n = 0; n < 9; ++n) {
      CAPTURE(n);
      CHECK(four.process(static_cast<float>(n + 1)) == expected[n]);
    }
    // 2.5 by hand: takes at 0, 3, 5, 8, 10.
    downsample half;
    half.period(2.5f);
    const float uneven[] = {0.f, 0.f, 0.f, 3.f, 3.f, 5.f, 5.f, 5.f, 8.f, 8.f, 10.f};
    for (unsigned int n = 0; n < 11; ++n) {
      CAPTURE(n);
      CHECK(half.process(static_cast<float>(n)) == uneven[n]);
    }
  }

  TEST_CASE("downsample: a sine held every 6 samples (#1109)") {
    const double w = 2.0 * 3.14159265358979323846 * 440.0 / 48000.0;
    downsample d;
    d.period(6.f);
    buffer in(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = static_cast<float>(std::sin(w * i));
    buffer& out = d(in);
    for (unsigned int i = 0; i < N; ++i) {
      CAPTURE(i);
      CHECK(out.getPtr()[i] == in.getPtr()[i - i % 6]);
    }
  }

  TEST_CASE("downsample: a fractional period does not drift (#1109)") {
    // 3.3 is not exact in binary; over 40 blocks the takes stay within one
    // of n / 3.3.
    downsample d;
    d.period(3.3f);
    const double q = static_cast<double>(3.3f);
    buffer in(N);
    unsigned int takes = 0;
    float last = -1.f;
    for (unsigned int block = 0; block < 40; ++block) {
      for (unsigned int i = 0; i < N; ++i)
        in.getPtr()[i] = static_cast<float>(block * N + i);
      buffer& out = d(in);
      for (unsigned int i = 0; i < N; ++i) {
        if (out.getPtr()[i] != last) ++takes;
        last = out.getPtr()[i];
      }
    }
    const double expected = std::floor((40.0 * N - 1.0) / q) + 1.0;
    CHECK(std::fabs(static_cast<double>(takes) - expected) <= 1.0);
  }

  TEST_CASE("downsample: a shorter period takes at once, without a burst (#1109)") {
    downsample d;
    d.period(1000.f);
    CHECK(d.process(1.f) == 1.f);
    for (int n = 2; n <= 100; ++n)
      CHECK(d.process(static_cast<float>(n)) == 1.f);
    // 100 samples into a hold of 1000: down to 4, the next sample is taken
    // and the count starts again from it.
    d.period(4.f);
    CHECK(d.process(101.f) == 101.f);
    CHECK(d.process(102.f) == 101.f);
    CHECK(d.process(103.f) == 101.f);
    CHECK(d.process(104.f) == 101.f);
    CHECK(d.process(105.f) == 105.f);
    // A longer period counts from the last take.
    d.period(6.f);
    CHECK(d.process(106.f) == 105.f);
    CHECK(d.process(107.f) == 105.f);
    CHECK(d.process(108.f) == 105.f);
    CHECK(d.process(109.f) == 105.f);
    CHECK(d.process(110.f) == 105.f);
    CHECK(d.process(111.f) == 111.f);
  }

  TEST_CASE("downsample: an infinite period holds, reset() takes again (#1109)") {
    downsample d;
    d.period(INFINITY);
    CHECK(d.period() == INFINITY);
    CHECK(d.process(0.25f) == 0.25f);
    for (int n = 0; n < 1000; ++n)
      CHECK(d.process(0.75f) == 0.25f);
    CHECK(d.held() == 0.25f);
    // Back to 1: the next sample is taken.
    d.period(1.f);
    CHECK(d.process(0.5f) == 0.5f);
    CHECK(d.process(0.125f) == 0.125f);
    d.period(8.f);
    CHECK(d.process(0.375f) == 0.125f);
    d.reset();
    CHECK(d.held() == 0.f);
    CHECK(d.period() == 8.f); // the period is kept
    CHECK(d.process(-0.5f) == -0.5f);
    CHECK(d.process(0.5f) == -0.5f);
  }

  TEST_CASE("downsample: the period is at least 1, NaN ignored, non-finite input not taken "
            "(#1109)") {
    downsample d;
    d.period(0.5f);
    CHECK(d.period() == 1.f);
    d.period(0.f);
    CHECK(d.period() == 1.f);
    d.period(-7.f);
    CHECK(d.period() == 1.f);
    d.period(-INFINITY);
    CHECK(d.period() == 1.f);
    d.period(3.f).period(std::nanf(""));
    CHECK(d.period() == 3.f);

    downsample e;
    CHECK(e.process(0.5f) == 0.5f);
    CHECK(e.process(std::nanf("")) == 0.5f);
    CHECK(e.process(INFINITY) == 0.5f);
    CHECK(e.process(-INFINITY) == 0.5f);
    CHECK(e.process(0.25f) == 0.25f);
    // A non-finite sample due for a take is skipped, not postponed.
    downsample f;
    f.period(2.f);
    CHECK(f.process(1.f) == 1.f);
    CHECK(f.process(2.f) == 1.f);
    CHECK(f.process(std::nanf("")) == 1.f);
    CHECK(f.process(4.f) == 1.f);
    CHECK(f.process(5.f) == 5.f);
  }

  TEST_CASE("downsample: the period read per sample from a signal (#1109)") {
    downsample d;
    downsample reference;
    buffer in(N);
    buffer period(N);
    for (unsigned int i = 0; i < N; ++i) {
      in.getPtr()[i] = Wobble(i);
      period.getPtr()[i] = i < 40 ? 2.f : (i < 80 ? 7.5f : 1.f + static_cast<float>(i % 5));
    }
    // Out of range and NaN values among them.
    period.getPtr()[10] = -3.f;
    period.getPtr()[11] = std::nanf("");
    period.getPtr()[50] = 1e30f;
    buffer& out = d(in, period);
    REQUIRE(out.getLength() == N);
    for (unsigned int i = 0; i < N; ++i) {
      CAPTURE(i);
      reference.period(period.getPtr()[i]);
      CHECK(out.getPtr()[i] == reference.process(in.getPtr()[i]));
    }
    CHECK(d.period() == period.getPtr()[N - 1]);
    // By hand: a period signal of 3 holds each take three samples.
    downsample g;
    buffer x(9);
    buffer p(9);
    for (unsigned int i = 0; i < 9; ++i)
      x.getPtr()[i] = static_cast<float>(i);
    p = 3.f;
    buffer& y = g(x, p);
    const float expected[] = {0.f, 0.f, 0.f, 3.f, 3.f, 3.f, 6.f, 6.f, 6.f};
    for (unsigned int i = 0; i < 9; ++i) {
      CAPTURE(i);
      CHECK(y.getPtr()[i] == expected[i]);
    }
  }

  TEST_CASE("downsample: per-sample, per-block and split-block calls agree (#1109)") {
    const unsigned int splits[] = {128, 64, 32, 13, 1};
    std::vector<float> input(4 * N);
    for (unsigned int n = 0; n < input.size(); ++n)
      input[n] = Wobble(n);
    std::vector<float> expected(input.size());
    {
      downsample d;
      d.period(3.3f);
      for (unsigned int n = 0; n < input.size(); ++n)
        expected[n] = d.process(input[n]);
    }
    {
      downsample d;
      d.period(3.3f);
      buffer in(N);
      for (unsigned int block = 0; block < 4; ++block) {
        for (unsigned int i = 0; i < N; ++i)
          in.getPtr()[i] = input[block * N + i];
        buffer& out = d(in);
        for (unsigned int i = 0; i < N; ++i)
          CHECK(out.getPtr()[i] == expected[block * N + i]);
      }
    }
    for (unsigned int split : splits) {
      CAPTURE(split);
      downsample d;
      d.period(3.3f);
      buffer in(split);
      unsigned int n = 0;
      while (n + split <= input.size()) {
        for (unsigned int i = 0; i < split; ++i)
          in.getPtr()[i] = input[n + i];
        buffer& out = d(in);
        REQUIRE(out.getLength() == split);
        for (unsigned int i = 0; i < split; ++i) {
          CAPTURE(n + i);
          CHECK(out.getPtr()[i] == expected[n + i]);
        }
        n += split;
      }
    }
  }

  TEST_CASE("downsample: the output does not depend on the sample rate (#1109)") {
    std::vector<float> first;
    for (const UInt rate : {44100u, 48000u, 96000u}) {
      CAPTURE(rate);
      TestHelpers::ScopedSampleRate scoped(rate);
      downsample d;
      d.period(5.f);
      buffer in(N);
      for (unsigned int i = 0; i < N; ++i)
        in.getPtr()[i] = Wobble(i);
      buffer& out = d(in);
      if (first.empty()) {
        first.assign(out.getPtr(), out.getPtr() + N);
        continue;
      }
      for (unsigned int i = 0; i < N; ++i)
        CHECK(out.getPtr()[i] == first[i]);
    }
  }

  TEST_CASE("downsample: a longer block is processed up to STANDARD_BUFFERSIZE (#1109)") {
    downsample d;
    buffer shortIn(16);
    shortIn = 0.5f;
    CHECK(d(shortIn).getLength() == 16);
    buffer longIn(N * 2);
    longIn = 0.5f;
    CHECK(d(longIn).getLength() == N);
    buffer p(8);
    p = 2.f;
    CHECK(d(longIn, p).getLength() == 8);
  }

  TEST_CASE("downsample: processing allocates nothing (#1109)") {
    downsample d;
    buffer in(N);
    buffer shortIn(32);
    buffer p(N);
    in = 0.3f;
    shortIn = 0.3f;
    p = 4.f;
    d(in);
    d(in, p);
    {
      TestHelpers::ProbeScope probe;
      d(in);
      d(shortIn);
      d(in, p);
      d(in);
      d.process(0.5f);
      d.period(2.5f).reset();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
