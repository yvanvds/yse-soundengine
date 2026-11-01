// Tests for YSE::DSP::zeroCrossing (YseEngine/dsp/analysis/zeroCrossing.hpp),
// issue #1124. Reference output is closed form: s(x) = 1 above 0, -1 below,
// 0 otherwise (0, -0, NaN); sample n crosses when s(x[n]) != 0 and the last
// non-zero side before it exists and differs. The count output is the number
// of crossings in the block on every sample, the click output the volume on
// each crossing and 0 elsewhere.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/analysis/zeroCrossing.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

using YSE::DSP::buffer;
using YSE::DSP::zeroCrossing;

namespace {

  constexpr UInt N = YSE::STANDARD_BUFFERSIZE;
  constexpr UInt Quarter = N / 4;
  constexpr UInt Half = N / 2;

  std::vector<UInt> Reference(const std::vector<float>& x) {
    std::vector<UInt> out;
    int side = 0;
    for (UInt n = 0; n < x.size(); ++n) {
      const int now = x[n] > 0.f ? 1 : (x[n] < 0.f ? -1 : 0);
      if (now == 0) continue;
      if (side != 0 && now != side) out.push_back(n);
      side = now;
    }
    return out;
  }

  // Runs `x` through `z` in blocks of `block` samples, checks each block's
  // outputs against its own report, and collects every crossing as a
  // position in the whole signal.
  std::vector<UInt> Blocks(zeroCrossing& z, const std::vector<float>& x, UInt block) {
    std::vector<UInt> out;
    for (UInt start = 0; start < x.size(); start += block) {
      const UInt len = std::min<UInt>(block, static_cast<UInt>(x.size()) - start);
      buffer b(len);
      std::copy(x.begin() + start, x.begin() + start + len, b.getPtr());
      const UInt c = z(b);
      REQUIRE(c == z.crossings());
      REQUIRE(z.counts().getLength() == len);
      REQUIRE(z.clicks().getLength() == len);
      UInt k = 0;
      for (UInt i = 0; i < len; ++i) {
        CHECK(z.counts().getPtr()[i] == static_cast<float>(c));
        const bool here = k < c && z.crossing(k) == i;
        CHECK(z.clicks().getPtr()[i] == (here ? z.volume() : 0.f));
        if (here) {
          out.push_back(start + i);
          ++k;
        }
      }
      CHECK(k == c);
    }
    return out;
  }

  std::vector<float> Sine(UInt length, double period) {
    std::vector<float> x(length);
    for (UInt n = 0; n < length; ++n)
      x[n] = static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * (n + 0.5) / period));
    return x;
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("zeroCrossing: hard-coded crossings, counts and clicks in one block (#1124)") {
    zeroCrossing z;
    buffer in(8);
    float* a = in.getPtr();
    // 0 has no side; 1 sets it; -1 crosses; 0 holds; -2 same side; 3 crosses;
    // 0 holds; -0.5 crosses.
    const float x[8] = {0.f, 1.f, -1.f, 0.f, -2.f, 3.f, 0.f, -0.5f};
    std::copy(x, x + 8, a);
    CHECK(z(in) == 3);
    REQUIRE(z.crossings() == 3);
    CHECK(z.crossing(0) == 2);
    CHECK(z.crossing(1) == 5);
    CHECK(z.crossing(2) == 7);
    const float clicks[8] = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 1.f};
    for (UInt i = 0; i < 8; ++i) {
      CAPTURE(i);
      CHECK(z.counts().getPtr()[i] == 3.f);
      CHECK(z.clicks().getPtr()[i] == clicks[i]);
    }
  }

  TEST_CASE("zeroCrossing: zero, -0 and NaN keep the side; touching zero is no crossing "
            "(#1124)") {
    zeroCrossing z;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const float denorm = std::numeric_limits<float>::denorm_min();
    CHECK_FALSE(z.process(-1.f)); // the first side is never a crossing
    CHECK_FALSE(z.process(0.f));
    CHECK_FALSE(z.process(-0.f));
    CHECK_FALSE(z.process(nan));
    CHECK_FALSE(z.process(-1.f)); // touched zero and went back
    CHECK(z.process(denorm)); // the smallest positive is on a side
    CHECK(z.process(-inf));
    CHECK(z.process(inf));
    CHECK_FALSE(z.process(nan));
    CHECK(z.process(-denorm));
  }

  TEST_CASE("zeroCrossing: silence and DC give nothing (#1124)") {
    zeroCrossing z;
    buffer in(N);
    in = 0.f;
    CHECK(z(in) == 0);
    in = 0.3f;
    CHECK(z(in) == 0);
    for (UInt i = 0; i < N; ++i) {
      CHECK(z.counts().getPtr()[i] == 0.f);
      CHECK(z.clicks().getPtr()[i] == 0.f);
    }
    in = -0.3f;
    CHECK(z(in) == 1); // DC flipping sign once, at sample 0
    CHECK(z.crossing(0) == 0);
  }

  TEST_CASE("zeroCrossing: a sine crosses twice per period (#1124)") {
    // Period 32 samples, offset half a sample so no sample is exactly 0:
    // crossings at 16, 32, 48 ... after the first half period.
    zeroCrossing z;
    const std::vector<float> x = Sine(N, 32.0);
    buffer in(N);
    std::copy(x.begin(), x.end(), in.getPtr());
    const UInt c = z(in);
    CHECK(c == N / 16 - 1);
    for (UInt i = 0; i < c; ++i)
      CHECK(z.crossing(i) == 16 * (i + 1));
    // The next block continues the same sine: crossings at 0, 16, 32 ...
    const std::vector<float> s = Sine(2 * N, 32.0);
    std::copy(s.begin() + N, s.end(), in.getPtr());
    CHECK(z(in) == N / 16);
    CHECK(z.crossing(0) == 0);
  }

  TEST_CASE("zeroCrossing: the block split does not change the crossings (#1124)") {
    // A sine with a smaller fast sine and exact zeros scattered in.
    std::vector<float> x(4 * N);
    for (UInt n = 0; n < x.size(); ++n) {
      const auto t = static_cast<double>(n);
      x[n] = static_cast<float>(std::sin(t * 0.11) + 0.4 * std::sin(t * 1.7));
      if (n % 7 == 3) x[n] = 0.f;
    }
    const std::vector<UInt> expected = Reference(x);
    REQUIRE(expected.size() > 10);
    for (UInt block : {N, Half, Quarter, UInt{1}, UInt{13}}) {
      CAPTURE(block);
      zeroCrossing z;
      CHECK(Blocks(z, x, block) == expected);
    }
    // Per sample.
    zeroCrossing z;
    std::vector<UInt> single;
    for (UInt n = 0; n < x.size(); ++n)
      if (z.process(x[n])) single.push_back(n);
    CHECK(single == expected);
  }

  TEST_CASE("zeroCrossing: the click volume, NaN, and reset (#1124)") {
    zeroCrossing z;
    CHECK(z.volume() == 1.f);
    z.volume(-0.25f);
    buffer in(4);
    float* a = in.getPtr();
    a[0] = 1.f;
    a[1] = -1.f;
    a[2] = 1.f;
    a[3] = 1.f;
    CHECK(z(in) == 2);
    CHECK(z.clicks().getPtr()[0] == 0.f);
    CHECK(z.clicks().getPtr()[1] == -0.25f);
    CHECK(z.clicks().getPtr()[2] == -0.25f);
    CHECK(z.clicks().getPtr()[3] == 0.f);
    z.volume(std::nanf(""));
    CHECK(z.volume() == 0.f);
    z.volume(2.f);
    // The side carries: the next block starting below 0 crosses on sample 0.
    a[0] = -1.f;
    CHECK(z(in) == 2);
    CHECK(z.crossing(0) == 0);
    CHECK(z.clicks().getPtr()[0] == 2.f);
    // reset() forgets it and the report; keeps the volume.
    z.reset();
    CHECK(z.crossings() == 0);
    CHECK(z.volume() == 2.f);
    a[0] = -1.f;
    a[1] = -1.f;
    CHECK(z(in) == 1);
    CHECK(z.crossing(0) == 2);
  }

  TEST_CASE("zeroCrossing: a block longer than STANDARD_BUFFERSIZE is clamped (#1124)") {
    zeroCrossing z;
    buffer in(N + 16);
    for (UInt i = 0; i < in.getLength(); ++i)
      in.getPtr()[i] = (i % 2 == 0) ? 1.f : -1.f;
    CHECK(z(in) == N - 1);
    CHECK(z.counts().getLength() == N);
    CHECK(z.clicks().getLength() == N);
  }

  TEST_CASE("zeroCrossing: processing allocates nothing (#1124)") {
    zeroCrossing z;
    buffer full(N);
    buffer shorter(Half);
    for (UInt i = 0; i < N; ++i)
      full.getPtr()[i] = (i % 3 == 0) ? 1.f : -1.f;
    shorter = -0.5f;
    z(full);
    {
      TestHelpers::ProbeScope probe;
      z(full);
      z(shorter); // shrinking and growing within the capacity
      z(full);
      z.process(0.5f);
      z.volume(0.5f);
      z.reset();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
