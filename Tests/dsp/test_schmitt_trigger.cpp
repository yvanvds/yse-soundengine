// Tests for YSE::DSP::schmittTrigger (YseEngine/dsp/analysis/schmittTrigger.hpp),
// issue #1122. The reference is closed form, written independently of the
// class's state machine: with low < high, y[n] is 1 when some m <= n has
// x[m] >= high and no k in (m, n] has x[k] <= low; with low >= high, y[n] is
// x[n] >= high. The output before the first sample is 0.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/analysis/schmittTrigger.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

using YSE::DSP::buffer;
using YSE::DSP::schmittTrigger;

namespace {

  constexpr UInt N = YSE::STANDARD_BUFFERSIZE;

  struct SampleRateGuard {
    UInt saved;
    SampleRateGuard() : saved(YSE::SAMPLERATE) {}
    ~SampleRateGuard() {
      YSE::SAMPLERATE = saved;
    }
  };

  // The closed form above, by search: the last sample at or above `high` and
  // the last at or below `low`, up to and including n.
  std::vector<float> Reference(const std::vector<float>& x, float low, float high) {
    std::vector<float> y(x.size(), 0.f);
    if (low >= high) {
      for (size_t n = 0; n < x.size(); ++n)
        y[n] = x[n] >= high ? 1.f : 0.f;
      return y;
    }
    for (size_t n = 0; n < x.size(); ++n) {
      long lastSet = -1;
      long lastReset = -1;
      for (size_t m = 0; m <= n; ++m) {
        if (x[m] >= high) lastSet = static_cast<long>(m);
        if (x[m] <= low) lastReset = static_cast<long>(m);
      }
      y[n] = lastSet > lastReset ? 1.f : 0.f;
    }
    return y;
  }

  std::vector<float> Blocks(schmittTrigger& s, const std::vector<float>& x, UInt block) {
    std::vector<float> y;
    for (UInt start = 0; start < x.size(); start += block) {
      const UInt len = std::min<UInt>(block, static_cast<UInt>(x.size()) - start);
      buffer b(len);
      for (UInt i = 0; i < len; ++i)
        b.getPtr()[i] = x[start + i];
      buffer& out = s(b);
      REQUIRE(out.getLength() == len);
      for (UInt i = 0; i < len; ++i)
        y.push_back(out.getPtr()[i]);
    }
    return y;
  }

  // A sine with fast noise on top: it crosses each threshold many times near
  // it, which a plain comparison turns into chatter.
  std::vector<float> NoisySine(UInt length) {
    std::vector<float> x(length);
    for (UInt i = 0; i < length; ++i) {
      const auto n = static_cast<float>(i);
      x[i] = std::sin(n * 0.02f) + 0.15f * std::sin(n * 2.7f);
    }
    return x;
  }

  UInt Transitions(const std::vector<float>& y) {
    UInt count = 0;
    float before = 0.f;
    for (const float v : y) {
      if (v != before) count++;
      before = v;
    }
    return count;
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("schmittTrigger: opens at or above high, holds, closes at or below low (#1122)") {
    schmittTrigger s;
    s.low(0.2f).high(0.8f);
    CHECK(s.low() == 0.2f);
    CHECK(s.high() == 0.8f);
    CHECK_FALSE(s.on());
    CHECK(s.process(0.5f) == 0.f); // between, from 0: holds 0
    CHECK(s.process(0.79f) == 0.f); // just below high
    CHECK(s.process(0.8f) == 1.f); // equal to high: opens
    CHECK(s.on());
    CHECK(s.process(0.5f) == 1.f); // between: holds 1
    CHECK(s.process(0.21f) == 1.f); // just above low
    CHECK(s.process(0.2f) == 0.f); // equal to low: closes
    CHECK(s.process(0.5f) == 0.f); // between: holds 0
    CHECK(s.process(5.f) == 1.f);
    CHECK(s.process(-5.f) == 0.f);
  }

  TEST_CASE("schmittTrigger: equal thresholds have no hysteresis, x >= t (#1122)") {
    schmittTrigger s; // both 0 by default
    CHECK(s.low() == 0.f);
    CHECK(s.high() == 0.f);
    CHECK(s.process(-1.f) == 0.f);
    CHECK(s.process(0.f) == 1.f);
    CHECK(s.process(0.f) == 1.f); // equal stays on: reset needs below
    CHECK(s.process(-1e-30f) == 0.f);
    s.low(0.5f).high(0.5f);
    CHECK(s.process(0.5f) == 1.f);
    CHECK(s.process(0.4999f) == 0.f);
  }

  TEST_CASE("schmittTrigger: a low threshold above the high one is the high one (#1122)") {
    schmittTrigger s;
    s.low(0.9f).high(0.3f);
    // Getters report the settings as made.
    CHECK(s.low() == 0.9f);
    CHECK(s.high() == 0.3f);
    // No toggling inside [0.3, 0.9]: 1 exactly while x >= 0.3.
    for (const float x : {0.5f, 0.5f, 0.5f, 0.3f, 0.29f, 0.95f, 0.6f, 0.1f})
      CHECK(s.process(x) == (x >= 0.3f ? 1.f : 0.f));
    const std::vector<float> x = NoisySine(3 * N);
    schmittTrigger t;
    t.low(0.9f).high(0.3f);
    CHECK(Blocks(t, x, N) == Reference(x, 0.9f, 0.3f));
  }

  TEST_CASE("schmittTrigger: a noisy sine against the closed form; hysteresis removes the "
            "chatter (#1122)") {
    const std::vector<float> x = NoisySine(4 * N);
    schmittTrigger gate;
    gate.low(-0.5f).high(0.5f);
    const std::vector<float> y = Blocks(gate, x, N);
    CHECK(y == Reference(x, -0.5f, 0.5f));
    // 4 * N samples at 0.02 rad/sample is 0.08 * N / (2 pi) periods: the
    // gate opens and closes once in each, give or take the one in progress.
    const auto periods = static_cast<UInt>(0.08f * static_cast<float>(N) / 6.2831853f);
    CHECK(Transitions(y) >= 2 * periods);
    CHECK(Transitions(y) <= 2 * periods + 2);
    // The same signal against one threshold chatters.
    schmittTrigger plain;
    plain.low(0.5f).high(0.5f);
    CHECK(Transitions(Blocks(plain, x, N)) > 2 * Transitions(y));
  }

  TEST_CASE("schmittTrigger: an impulse opens once and closes on the next sample; a step "
            "opens and stays (#1122)") {
    std::vector<float> impulse(N, 0.f);
    impulse[0] = 1.f;
    schmittTrigger a;
    a.low(0.f).high(0.5f);
    const std::vector<float> y = Blocks(a, impulse, N);
    CHECK(y[0] == 1.f);
    CHECK(y[1] == 0.f); // 0 is at the low threshold
    CHECK(std::count(y.begin(), y.end(), 1.f) == 1);

    std::vector<float> step(2 * N, 0.f);
    std::fill(step.begin() + 100, step.end(), 1.f);
    schmittTrigger b;
    b.low(0.f).high(0.5f);
    const std::vector<float> z = Blocks(b, step, N);
    CHECK(z == Reference(step, 0.f, 0.5f));
    CHECK(z[99] == 0.f);
    CHECK(z[100] == 1.f);
    CHECK(z.back() == 1.f);
  }

  TEST_CASE("schmittTrigger: the state carries across blocks; per-sample, per-block and "
            "split-block calls agree (#1122)") {
    const std::vector<float> x = NoisySine(3 * N);
    const std::vector<float> expected = Reference(x, -0.3f, 0.6f);

    schmittTrigger perSample;
    perSample.low(-0.3f).high(0.6f);
    std::vector<float> y;
    y.reserve(x.size());
    for (const float v : x)
      y.push_back(perSample.process(v));
    CHECK(y == expected);

    for (const UInt block : {N, 1u, 7u, N / 2 + 3, N - 1}) {
      CAPTURE(block);
      schmittTrigger s;
      s.low(-0.3f).high(0.6f);
      CHECK(Blocks(s, x, block) == expected);
    }
  }

  TEST_CASE("schmittTrigger: thresholds read per sample from signals (#1122)") {
    schmittTrigger s;
    buffer in(N);
    buffer lo(N);
    buffer hi(N);
    // A constant 0.5 against a high threshold that sweeps down from 1 and a
    // low one that sweeps up after: opens where high reaches 0.5, closes
    // where low reaches 0.5.
    for (UInt i = 0; i < N; ++i) {
      in.getPtr()[i] = 0.5f;
      hi.getPtr()[i] = i < N / 4 ? 1.f : 0.f;
      lo.getPtr()[i] = i < 3 * N / 4 ? -1.f : 0.5f;
    }
    buffer& out = s(in, lo, hi);
    REQUIRE(out.getLength() == N);
    CHECK(out.getPtr()[N / 4 - 1] == 0.f);
    CHECK(out.getPtr()[N / 4] == 1.f);
    CHECK(out.getPtr()[3 * N / 4 - 1] == 1.f);
    // low 0.5 and high 0: low is used as high, and 0.5 >= 0 keeps it on.
    CHECK(out.getPtr()[3 * N / 4] == 1.f);
    CHECK(out.getPtr()[N - 1] == 1.f);
    CHECK(s.low() == 0.5f);
    CHECK(s.high() == 0.f);

    // As long as the shortest buffer.
    buffer shortLow(10);
    shortLow = 0.f;
    CHECK(s(in, shortLow, hi).getLength() == 10);
  }

  TEST_CASE("schmittTrigger: set() and reset() choose the state (#1122)") {
    schmittTrigger s;
    s.low(-1.f).high(1.f);
    s.set(true);
    CHECK(s.on());
    CHECK(s.process(0.f) == 1.f); // between: holds the state set
    s.reset();
    CHECK_FALSE(s.on());
    CHECK(s.process(0.f) == 0.f);
  }

  TEST_CASE("schmittTrigger: NaN input holds; NaN thresholds are ignored; infinities are "
            "thresholds (#1122)") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    schmittTrigger s;
    s.low(-1.f).high(1.f);
    CHECK(s.process(nan) == 0.f);
    CHECK(s.process(2.f) == 1.f);
    CHECK(s.process(nan) == 1.f);
    CHECK(s.process(-2.f) == 0.f);
    s.low(nan).high(nan);
    CHECK(s.low() == -1.f);
    CHECK(s.high() == 1.f);
    // Infinite input crosses finite thresholds.
    CHECK(s.process(inf) == 1.f);
    CHECK(s.process(-inf) == 0.f);
    // A high threshold of +inf opens only on +inf.
    s.high(inf);
    CHECK(s.process(1e38f) == 0.f);
    CHECK(s.process(inf) == 1.f);
    // A low threshold of -inf closes only on -inf.
    s.low(-inf).high(0.f);
    CHECK(s.process(-1e38f) == 1.f);
    CHECK(s.process(-inf) == 0.f);
  }

  TEST_CASE("schmittTrigger: a block longer than STANDARD_BUFFERSIZE is clamped (#1122)") {
    schmittTrigger s;
    buffer in(N + 64);
    in = 1.f;
    CHECK(s(in).getLength() == N);
  }

  TEST_CASE("schmittTrigger: the output does not depend on the sample rate (#1122)") {
    SampleRateGuard guard;
    const std::vector<float> x = NoisySine(2 * N);
    YSE::SAMPLERATE = 44100;
    schmittTrigger a;
    a.low(-0.2f).high(0.4f);
    const std::vector<float> at44 = Blocks(a, x, N);
    YSE::SAMPLERATE = 96000;
    schmittTrigger b;
    b.low(-0.2f).high(0.4f);
    CHECK(Blocks(b, x, N) == at44);
  }

  TEST_CASE("schmittTrigger: processing allocates nothing (#1122)") {
    schmittTrigger s;
    buffer in(N);
    buffer lo(N);
    buffer hi(N);
    for (UInt i = 0; i < N; ++i)
      in.getPtr()[i] = std::sin(static_cast<float>(i) * 0.1f);
    lo = -0.5f;
    hi = 0.5f;
    buffer shortIn(32);
    shortIn = 1.f;
    {
      TestHelpers::ProbeScope probe;
      s(in);
      s(shortIn);
      s(in, lo, hi);
      s.process(0.f);
      s.low(-0.1f).high(0.1f).set(true).reset();
      s(in);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
