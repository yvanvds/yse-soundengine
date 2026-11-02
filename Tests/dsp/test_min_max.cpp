// Tests for YSE::DSP::minMax (YseEngine/dsp/analysis/minMax.hpp), issue
// #1127. Reference output is closed form: after sample n the minimum and
// maximum are min / max of x over the samples since the last reset at or
// before n (NaN skipped), 0 while there are none.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/analysis/minMax.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::DSP::buffer;
using YSE::DSP::minMax;

namespace {

  constexpr UInt N = YSE::STANDARD_BUFFERSIZE;

  struct Pair {
    float low;
    float high;
  };

  std::vector<Pair> Reference(const std::vector<float>& x, const std::vector<float>& r) {
    std::vector<Pair> out;
    std::size_t start = 0;
    for (std::size_t n = 0; n < x.size(); ++n) {
      if (n < r.size() && r[n] != 0.f && !std::isnan(r[n])) start = n;
      bool any = false;
      float low = 0.f;
      float high = 0.f;
      for (std::size_t k = start; k <= n; ++k) {
        if (std::isnan(x[k])) continue;
        low = any ? std::min(low, x[k]) : x[k];
        high = any ? std::max(high, x[k]) : x[k];
        any = true;
      }
      out.push_back({low, high});
    }
    return out;
  }

  // Runs `x` (and the reset signal `r`, empty for none) through `d` in
  // blocks of `block` samples and collects the output signals.
  std::vector<Pair> Blocks(minMax& d, const std::vector<float>& x, const std::vector<float>& r,
                           UInt block) {
    std::vector<Pair> out;
    for (UInt start = 0; start < x.size(); start += block) {
      const UInt len = std::min<UInt>(block, static_cast<UInt>(x.size()) - start);
      buffer b(len);
      for (UInt i = 0; i < len; ++i)
        b.getPtr()[i] = x[start + i];
      if (r.empty()) {
        d(b);
      } else {
        buffer rb(len);
        for (UInt i = 0; i < len; ++i)
          rb.getPtr()[i] = r[start + i];
        d(b, rb);
      }
      REQUIRE(d.minimumSignal().getLength() == len);
      REQUIRE(d.maximumSignal().getLength() == len);
      for (UInt i = 0; i < len; ++i)
        out.push_back({d.minimumSignal().getPtr()[i], d.maximumSignal().getPtr()[i]});
      CHECK(d.minimum() == out.back().low);
      CHECK(d.maximum() == out.back().high);
    }
    return out;
  }

  void Same(const std::vector<Pair>& got, const std::vector<Pair>& want) {
    REQUIRE(got.size() == want.size());
    for (std::size_t i = 0; i < got.size(); ++i) {
      CAPTURE(i);
      CHECK(got[i].low == want[i].low);
      CHECK(got[i].high == want[i].high);
    }
  }

  // A sine with a slowly growing amplitude and an offset: new extremes keep
  // arriving on both sides.
  std::vector<float> Signal(UInt length) {
    std::vector<float> x(length);
    for (UInt n = 0; n < length; ++n)
      x[n] =
          0.1f + (0.2f + 0.001f * static_cast<float>(n)) * std::sin(0.05f * static_cast<float>(n));
    return x;
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("minMax: before any sample both are 0 (#1127)") {
    minMax d;
    CHECK(d.empty());
    CHECK(d.minimum() == 0.f);
    CHECK(d.maximum() == 0.f);
  }

  TEST_CASE("minMax: the first sample sets both, with its sign (#1127)") {
    minMax d;
    d.process(-0.25f);
    CHECK_FALSE(d.empty());
    CHECK(d.minimum() == -0.25f);
    CHECK(d.maximum() == -0.25f);
    d.process(-0.5f);
    d.process(-0.125f);
    CHECK(d.minimum() == -0.5f);
    CHECK(d.maximum() == -0.125f);
  }

  TEST_CASE("minMax: block output matches the closed form across block splits (#1127)") {
    const std::vector<float> x = Signal(3 * N + 17);
    const std::vector<Pair> want = Reference(x, {});
    for (const UInt block : {N, UInt{64}, UInt{13}, UInt{1}}) {
      CAPTURE(block);
      minMax d;
      Same(Blocks(d, x, {}, block), want);
    }
    minMax d;
    for (const float v : x)
      d.process(v);
    CHECK(d.minimum() == want.back().low);
    CHECK(d.maximum() == want.back().high);
  }

  TEST_CASE("minMax: a reset signal starts over at every sample where it is not 0 (#1127)") {
    const std::vector<float> x = Signal(2 * N + 5);
    std::vector<float> r(x.size(), 0.f);
    r[0] = 1.f;
    r[40] = -2.f; // any non-zero
    r[N] = 1.f;
    r[N + 1] = 1.f;
    r[N + 1 + 50] = std::numeric_limits<float>::quiet_NaN(); // NaN is not a reset
    const std::vector<Pair> want = Reference(x, r);
    for (const UInt block : {N, UInt{64}, UInt{13}, UInt{1}}) {
      CAPTURE(block);
      minMax d;
      Same(Blocks(d, x, r, block), want);
    }
    // A reset sample is the first of the new run.
    minMax d;
    d.process(5.f);
    d.process(0.25f, 1.f);
    CHECK(d.minimum() == 0.25f);
    CHECK(d.maximum() == 0.25f);
    d.process(9.f, 0.f);
    CHECK(d.maximum() == 9.f);
    d.process(1.f, std::numeric_limits<float>::quiet_NaN());
    CHECK(d.minimum() == 0.25f);
  }

  TEST_CASE("minMax: a reset signal shorter than the input reads as 0 past its end (#1127)") {
    minMax d;
    buffer in(8);
    buffer r(2);
    for (UInt i = 0; i < 8; ++i)
      in.getPtr()[i] = static_cast<float>(i);
    r = 1.f;
    d(in, r);
    CHECK(d.minimum() == 1.f); // reset at 0 and 1, then tracked
    CHECK(d.maximum() == 7.f);
    CHECK(d.minimumSignal().getPtr()[0] == 0.f);
    CHECK(d.minimumSignal().getPtr()[7] == 1.f);
  }

  TEST_CASE("minMax: NaN is skipped, infinities count (#1127)") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    minMax d;
    d.process(nan);
    CHECK(d.empty());
    CHECK(d.minimum() == 0.f);
    d.process(0.5f);
    d.process(nan);
    CHECK(d.minimum() == 0.5f);
    CHECK(d.maximum() == 0.5f);
    d.process(-inf);
    d.process(inf);
    CHECK(d.minimum() == -inf);
    CHECK(d.maximum() == inf);
    // A reset on a NaN sample leaves nothing tracked.
    d.process(nan, 1.f);
    CHECK(d.empty());
    CHECK(d.minimum() == 0.f);
    CHECK(d.maximum() == 0.f);
  }

  TEST_CASE("minMax: reset() starts over and keeps the output signals (#1127)") {
    minMax d;
    buffer b(N);
    b = -0.75f;
    b.getPtr()[3] = 0.5f;
    d(b);
    d.reset();
    CHECK(d.empty());
    CHECK(d.minimum() == 0.f);
    CHECK(d.maximum() == 0.f);
    CHECK(d.minimumSignal().getPtr()[N - 1] == -0.75f);
    CHECK(d.maximumSignal().getPtr()[N - 1] == 0.5f);
    d.process(0.125f);
    CHECK(d.minimum() == 0.125f);
    CHECK(d.maximum() == 0.125f);
  }

  TEST_CASE("minMax: a block longer than STANDARD_BUFFERSIZE is processed up to it (#1127)") {
    minMax d;
    buffer b(N + 4);
    b = 0.f;
    b.getPtr()[N + 1] = 1.f; // past the clamp: never seen
    d(b);
    CHECK(d.maximum() == 0.f);
    CHECK(d.maximumSignal().getLength() == N);
  }

  TEST_CASE("minMax: no state depends on the sample rate (#1127)") {
    const std::vector<float> x = Signal(N);
    std::vector<Pair> first;
    {
      ScopedSampleRate rate(44100);
      minMax d;
      first = Blocks(d, x, {}, 64);
    }
    ScopedSampleRate rate(96000);
    minMax d;
    Same(Blocks(d, x, {}, 64), first);
  }

  TEST_CASE("minMax: processing allocates nothing (#1127)") {
    minMax d;
    buffer b(N);
    buffer r(N);
    buffer shortBlock(N / 2);
    for (UInt i = 0; i < N; ++i)
      b.getPtr()[i] = static_cast<float>(i) / N - 0.5f;
    r = 0.f;
    r.getPtr()[N / 2] = 1.f;
    shortBlock = 0.25f;
    {
      TestHelpers::ProbeScope probe;
      d(b);
      d(shortBlock);
      d(b, r); // back to the full length: within capacity
      d.process(0.5f);
      d.process(0.5f, 1.f);
      d.reset();
      d(b, r);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(d.maximum() > 0.f);
  }

} // TEST_SUITE("dsp")
