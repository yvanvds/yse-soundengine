// Tests for YSE::DSP::degrade (YseEngine/dsp/operators/degrade.hpp), issue
// #1106. Reference output is closed form: at a ratio p/q the k-th take (from
// 0) is at sample ceil(k q / p), so sample n holds input
// ceil(floor(n p / q) q / p); the output is floor(held * 2^(b-1)) / 2^(b-1).

#include <doctest/doctest.h>
#include <cmath>
#include <vector>
#include "dsp/operators/degrade.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::degrade;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // The quantization, in double: exact for every value used here.
  float Quantize(double held, int bits) {
    const double q = std::ldexp(1.0, bits - 1);
    return static_cast<float>(std::floor(held * q) / q);
  }

  // The input sample held at sample n, for a ratio p / q (p <= q), from a
  // fresh object.
  unsigned int HeldIndex(unsigned int n, unsigned int p, unsigned int q) {
    if (p == 0) return 0;
    const unsigned int k = n * p / q; // takes before and at n, minus one
    return (k * q + p - 1) / p; // ceil(k q / p)
  }

  // Distinct, exactly quantizable at 24 bits: (n + 1) / 1024.
  float Ramp(unsigned int n) {
    return static_cast<float>(n + 1) / 1024.f;
  }

  float Wobble(unsigned int n) {
    const auto x = static_cast<float>(n);
    return 0.8f * std::sin(x * 0.031f) + 0.15f * std::sin(x * 1.7f);
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("degrade: defaults take every sample at 24 bits (#1106)") {
    degrade d;
    CHECK(d.ratio() == degrade::DEFAULT_RATIO);
    CHECK(d.bits() == degrade::DEFAULT_BITS);
    CHECK(d.held() == 0.f);
    // Multiples of 2^-23 pass bit for bit.
    for (unsigned int n = 0; n < 64; ++n) {
      CAPTURE(n);
      const float x = Ramp(n) - 0.5f;
      CHECK(d.process(x) == x);
    }
    // Anything else moves down by less than one step of 2^-23.
    for (unsigned int n = 0; n < 256; ++n) {
      CAPTURE(n);
      const float x = Wobble(n);
      const float y = d.process(x);
      CHECK(y <= x);
      CHECK(x - y < std::ldexp(1.f, -23));
      CHECK(y == Quantize(x, 24));
    }
  }

  TEST_CASE("degrade: each number of bits against the closed form (#1106)") {
    const float inputs[] = {0.f, 0.7f, -0.3f, 0.999f, -1.f, 1.f, -0.01f, 0.5f, -0.5f, 1.75f, -2.2f};
    for (int bits = 1; bits <= 24; ++bits) {
      CAPTURE(bits);
      degrade d;
      d.bits(bits);
      CHECK(d.bits() == bits);
      for (float x : inputs) {
        CAPTURE(x);
        CHECK(d.process(x) == Quantize(x, bits));
      }
    }
    // A few by hand: 1 bit is -1 or 0 on -1..1, 2 bits steps of 0.5.
    degrade one;
    one.bits(1);
    CHECK(one.process(0.7f) == 0.f);
    CHECK(one.process(-0.3f) == -1.f);
    CHECK(one.process(0.f) == 0.f);
    CHECK(one.process(1.f) == 1.f); // nothing is clipped
    degrade two;
    two.bits(2);
    CHECK(two.process(0.7f) == 0.5f);
    CHECK(two.process(-0.3f) == -0.5f);
    CHECK(two.process(-0.5f) == -0.5f);
    degrade three;
    three.bits(3);
    CHECK(three.process(0.6f) == 0.5f);
    CHECK(three.process(-0.1f) == -0.25f);
  }

  TEST_CASE("degrade: ratios hold the input against the closed form (#1106)") {
    struct Ratio {
      unsigned int p;
      unsigned int q;
    };
    const Ratio ratios[] = {{1, 1}, {1, 2}, {1, 4}, {1, 8}, {3, 8}, {5, 16}, {7, 8}};
    for (const Ratio r : ratios) {
      CAPTURE(r.p);
      CAPTURE(r.q);
      degrade d;
      d.ratio(static_cast<float>(r.p) / static_cast<float>(r.q));
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
          CHECK(out.getPtr()[i] == Ramp(HeldIndex(n, r.p, r.q)));
        }
      }
    }
    // 0.25 by hand: inputs 0, 4, 8 ... each held four samples.
    degrade quarter;
    quarter.ratio(0.25f);
    const float expected[] = {1.f, 1.f, 1.f, 1.f, 5.f, 5.f, 5.f, 5.f, 9.f};
    for (unsigned int n = 0; n < 9; ++n) {
      CAPTURE(n);
      CHECK(quarter.process(static_cast<float>(n + 1)) == expected[n]);
    }
  }

  TEST_CASE("degrade: a sine at a quarter of the rate and 4 bits (#1106)") {
    const double w = 2.0 * 3.14159265358979323846 * 440.0 / 48000.0;
    degrade d;
    d.ratio(0.25f).bits(4);
    buffer in(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = static_cast<float>(std::sin(w * i));
    buffer& out = d(in);
    for (unsigned int i = 0; i < N; ++i) {
      CAPTURE(i);
      CHECK(out.getPtr()[i] == Quantize(in.getPtr()[i - i % 4], 4));
      // 4 bits: every output a multiple of 1/8.
      const float scaled = out.getPtr()[i] * 8.f;
      CHECK(scaled == std::floor(scaled));
    }
  }

  TEST_CASE("degrade: a fractional period does not drift (#1106)") {
    // 0.3 is not exact in binary; over 40 blocks the takes stay within one
    // of n * 0.3.
    degrade d;
    d.ratio(0.3f);
    const double r = static_cast<double>(0.3f);
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
    const double expected = std::floor((40.0 * N - 1.0) * r) + 1.0;
    CHECK(std::fabs(static_cast<double>(takes) - expected) <= 1.0);
  }

  TEST_CASE("degrade: ratio 0 holds the first sample, reset() takes again (#1106)") {
    degrade d;
    d.ratio(0.f);
    CHECK(d.process(0.25f) == 0.25f);
    for (int n = 0; n < 1000; ++n)
      CHECK(d.process(0.75f) == 0.25f);
    CHECK(d.held() == 0.25f);
    // Back to 1: the next sample is taken.
    d.ratio(1.f);
    CHECK(d.process(0.5f) == 0.25f); // the phase reaches 1 on this one
    CHECK(d.process(0.125f) == 0.125f);
    d.ratio(0.f);
    d.reset();
    CHECK(d.held() == 0.f);
    CHECK(d.ratio() == 0.f); // settings kept
    CHECK(d.process(-0.5f) == -0.5f);
    CHECK(d.process(0.5f) == -0.5f);
  }

  TEST_CASE("degrade: settings are clamped, NaN ignored, non-finite input not taken (#1106)") {
    degrade d;
    d.ratio(2.f);
    CHECK(d.ratio() == 1.f);
    d.ratio(-1.f);
    CHECK(d.ratio() == 0.f);
    d.ratio(0.5f).ratio(std::nanf(""));
    CHECK(d.ratio() == 0.5f);
    d.bits(0);
    CHECK(d.bits() == 1);
    d.bits(100);
    CHECK(d.bits() == 24);
    d.bits(-5);
    CHECK(d.bits() == 1);

    degrade e;
    CHECK(e.process(0.5f) == 0.5f);
    CHECK(e.process(std::nanf("")) == 0.5f);
    CHECK(e.process(INFINITY) == 0.5f);
    CHECK(e.process(-INFINITY) == 0.5f);
    CHECK(e.process(0.25f) == 0.25f);
  }

  TEST_CASE("degrade: ratio and bits read per sample from signals (#1106)") {
    degrade d;
    degrade reference;
    buffer in(N);
    buffer ratio(N);
    buffer bits(N);
    for (unsigned int i = 0; i < N; ++i) {
      in.getPtr()[i] = Wobble(i);
      ratio.getPtr()[i] = i < 40 ? 0.5f : (i < 80 ? 0.125f : 1.f);
      bits.getPtr()[i] = 1.f + static_cast<float>(i % 40) * 0.6f; // 1 .. 24.4
    }
    // Out of range and NaN values among them.
    ratio.getPtr()[10] = -3.f;
    ratio.getPtr()[11] = std::nanf("");
    bits.getPtr()[20] = 1e30f;
    bits.getPtr()[21] = -1e30f;
    bits.getPtr()[22] = std::nanf("");
    buffer& out = d(in, ratio, bits);
    REQUIRE(out.getLength() == N);
    for (unsigned int i = 0; i < N; ++i) {
      CAPTURE(i);
      reference.ratio(ratio.getPtr()[i]);
      const float b = bits.getPtr()[i];
      if (!std::isnan(b)) reference.bits(b > 24.f ? 24 : (b < 1.f ? 1 : static_cast<int>(b)));
      CHECK(out.getPtr()[i] == reference.process(in.getPtr()[i]));
    }
    CHECK(d.ratio() == 1.f);
    CHECK(d.bits() == static_cast<int>(bits.getPtr()[N - 1]));
    // The values above by hand: a fraction of a bit is truncated.
    degrade f;
    buffer one(1);
    buffer r(1);
    buffer b(1);
    one = 0.6f;
    r = 1.f;
    b = 3.9f;
    CHECK(f(one, r, b).getPtr()[0] == 0.5f); // 3 bits
    CHECK(f.bits() == 3);
    b = 1e30f;
    f(one, r, b);
    CHECK(f.bits() == 24);
  }

  TEST_CASE("degrade: per-sample, per-block and split-block calls agree (#1106)") {
    const unsigned int splits[] = {128, 64, 32, 13, 1};
    std::vector<float> input(4 * N);
    for (unsigned int n = 0; n < input.size(); ++n)
      input[n] = Wobble(n);
    std::vector<float> expected(input.size());
    {
      degrade d;
      d.ratio(0.3f).bits(6);
      for (unsigned int n = 0; n < input.size(); ++n)
        expected[n] = d.process(input[n]);
    }
    {
      degrade d;
      d.ratio(0.3f).bits(6);
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
      degrade d;
      d.ratio(0.3f).bits(6);
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

  TEST_CASE("degrade: the output does not depend on the sample rate (#1106)") {
    std::vector<float> first;
    for (const UInt rate : {44100u, 48000u, 96000u}) {
      CAPTURE(rate);
      TestHelpers::ScopedSampleRate scoped(rate);
      degrade d;
      d.ratio(0.25f).bits(5);
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

  TEST_CASE("degrade: a longer block is processed up to STANDARD_BUFFERSIZE (#1106)") {
    degrade d;
    buffer shortIn(16);
    shortIn = 0.5f;
    CHECK(d(shortIn).getLength() == 16);
    buffer longIn(N * 2);
    longIn = 0.5f;
    CHECK(d(longIn).getLength() == N);
    buffer r(8);
    buffer b(N);
    r = 1.f;
    b = 8.f;
    CHECK(d(longIn, r, b).getLength() == 8);
  }

  TEST_CASE("degrade: processing allocates nothing (#1106)") {
    degrade d;
    buffer in(N);
    buffer shortIn(32);
    buffer r(N);
    buffer b(N);
    in = 0.3f;
    shortIn = 0.3f;
    r = 0.5f;
    b = 8.f;
    d(in);
    d(in, r, b);
    {
      TestHelpers::ProbeScope probe;
      d(in);
      d(shortIn);
      d(in, r, b);
      d(in);
      d.process(0.5f);
      d.ratio(0.25f).bits(4).reset();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
