// Tests for YSE::DSP::edgeDetector (YseEngine/dsp/analysis/edgeDetector.hpp),
// issue #1121. Reference output is closed form: sample n rises when x[n] != 0
// and x[n-1] == 0, falls when x[n] == 0 and x[n-1] != 0, with x[-1] = 0 after
// construction or reset().

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/analysis/edgeDetector.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

using YSE::DSP::buffer;
using YSE::DSP::edgeDetector;

namespace {

  constexpr UInt N = YSE::STANDARD_BUFFERSIZE;

  struct SampleRateGuard {
    UInt saved;
    SampleRateGuard() : saved(YSE::SAMPLERATE) {}
    ~SampleRateGuard() {
      YSE::SAMPLERATE = saved;
    }
  };

  buffer Constant(float value, UInt length = N) {
    buffer b(length);
    b = value;
    return b;
  }

  // The closed-form reference over a whole signal, split into blocks or not.
  struct Edges {
    std::vector<UInt> rises;
    std::vector<UInt> falls;
  };

  Edges Reference(const std::vector<float>& x) {
    Edges e;
    bool before = false;
    for (UInt i = 0; i < x.size(); ++i) {
      const bool now = x[i] != 0.f;
      if (now && !before) e.rises.push_back(i);
      if (!now && before) e.falls.push_back(i);
      before = now;
    }
    return e;
  }

  // Runs `x` through `d` in blocks of `block` samples and collects every
  // transition as a position in the whole signal.
  Edges Blocks(edgeDetector& d, const std::vector<float>& x, UInt block) {
    Edges e;
    for (UInt start = 0; start < x.size(); start += block) {
      const UInt len = std::min<UInt>(block, static_cast<UInt>(x.size()) - start);
      buffer b(len);
      for (UInt i = 0; i < len; ++i)
        b.getPtr()[i] = x[start + i];
      const UInt total = d(b);
      CHECK(total == d.rises() + d.falls());
      for (UInt i = 0; i < d.rises(); ++i)
        e.rises.push_back(start + d.rise(i));
      for (UInt i = 0; i < d.falls(); ++i)
        e.falls.push_back(start + d.fall(i));
    }
    return e;
  }

  // A gate: on for `on` samples, off for `off`, starting on.
  std::vector<float> Gate(UInt length, UInt on, UInt off) {
    std::vector<float> x(length);
    for (UInt i = 0; i < length; ++i)
      x[i] = (i % (on + off)) < on ? 1.f : 0.f;
    return x;
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("edgeDetector: per sample, 1 on a rise, -1 on a fall, from zero (#1121)") {
    edgeDetector d;
    CHECK_FALSE(d.high());
    CHECK(d.process(0.f) == 0);
    CHECK(d.process(0.5f) == 1);
    CHECK(d.high());
    CHECK(d.process(-3.f) == 0); // non-zero to non-zero: no edge, sign does not matter
    CHECK(d.process(0.f) == -1);
    CHECK_FALSE(d.high());
    CHECK(d.process(0.f) == 0);
    CHECK(d.process(-1.f) == 1);
  }

  TEST_CASE("edgeDetector: a block reports each rise and fall at its sample index (#1121)") {
    edgeDetector d;
    buffer b = Constant(0.f);
    b.getPtr()[3] = 1.f;
    b.getPtr()[4] = 2.f;
    b.getPtr()[5] = 0.f;
    b.getPtr()[10] = -0.25f;
    b.getPtr()[N - 1] = 7.f;
    CHECK(d(b) == 5);
    REQUIRE(d.rises() == 3);
    CHECK(d.rise(0) == 3);
    CHECK(d.rise(1) == 10);
    CHECK(d.rise(2) == N - 1);
    REQUIRE(d.falls() == 2);
    CHECK(d.fall(0) == 5);
    CHECK(d.fall(1) == 11); // the single non-zero sample at 10 closes again at 11
    CHECK(d.high()); // the last sample is non-zero

    // The next block reports only its own transitions: the carried state makes
    // a zero first sample a fall at index 0.
    buffer z = Constant(0.f);
    CHECK(d(z) == 1);
    CHECK(d.rises() == 0);
    REQUIRE(d.falls() == 1);
    CHECK(d.fall(0) == 0);
  }

  TEST_CASE("edgeDetector: an impulse rises then falls; a constant rises once (#1121)") {
    edgeDetector d;
    buffer impulse = Constant(0.f);
    impulse.getPtr()[0] = 1.f;
    CHECK(d(impulse) == 2);
    CHECK(d.rise(0) == 0);
    CHECK(d.fall(0) == 1);

    edgeDetector c;
    buffer one = Constant(0.1f);
    CHECK(c(one) == 1); // the sample before the first is zero
    CHECK(c.rise(0) == 0);
    for (int block = 0; block < 3; ++block) {
      CAPTURE(block);
      CHECK(c(one) == 0);
    }
    buffer zero = Constant(0.f);
    CHECK(c(zero) == 1);
    CHECK(c.falls() == 1);
    CHECK(c(zero) == 0);
  }

  TEST_CASE("edgeDetector: a gate gives a rise per period and a fall per period (#1121)") {
    const std::vector<float> x = Gate(4 * N, 20, 13);
    edgeDetector d;
    const Edges got = Blocks(d, x, N);
    const Edges ref = Reference(x);
    CHECK(got.rises == ref.rises);
    CHECK(got.falls == ref.falls);
    // Closed form: rises at k*33, falls at k*33 + 20.
    for (UInt k = 0; k < got.rises.size(); ++k)
      CHECK(got.rises[k] == k * 33);
    for (UInt k = 0; k < got.falls.size(); ++k)
      CHECK(got.falls[k] == k * 33 + 20);
    CHECK(got.rises.size() == (4 * N + 32) / 33);
  }

  TEST_CASE("edgeDetector: a sine thresholded at 0 opens and closes on each half cycle (#1121)") {
    // A sine of period 64 samples, offset half a sample so no sample is 0.
    std::vector<float> x(4 * N);
    for (UInt i = 0; i < x.size(); ++i) {
      const double s = std::sin(2.0 * 3.14159265358979323846 * (i + 0.5) / 64.0);
      x[i] = s > 0.0 ? 1.f : 0.f; // what ~> 0 feeds it
    }
    edgeDetector d;
    const Edges got = Blocks(d, x, N);
    // Positive for i in [64k, 64k + 31], so rises at 64k and falls at 64k + 32.
    REQUIRE(got.rises.size() == 8);
    REQUIRE(got.falls.size() == 8);
    for (UInt k = 0; k < 8; ++k) {
      CHECK(got.rises[k] == 64 * k);
      CHECK(got.falls[k] == 64 * k + 32);
    }
  }

  TEST_CASE("edgeDetector: per-sample, per-block and split-block calls agree (#1121)") {
    std::vector<float> x(3 * N);
    for (UInt i = 0; i < x.size(); ++i)
      x[i] = ((i * 7919u) % 11u) < 5u ? 0.f : static_cast<float>(i % 3u) - 1.f;
    const Edges ref = Reference(x);

    edgeDetector single;
    Edges perSample;
    for (UInt i = 0; i < x.size(); ++i) {
      const Int e = single.process(x[i]);
      if (e == 1) perSample.rises.push_back(i);
      if (e == -1) perSample.falls.push_back(i);
    }
    CHECK(perSample.rises == ref.rises);
    CHECK(perSample.falls == ref.falls);

    for (UInt block : {1u, 7u, 64u, N}) {
      CAPTURE(block);
      edgeDetector d;
      const Edges got = Blocks(d, x, block);
      CHECK(got.rises == ref.rises);
      CHECK(got.falls == ref.falls);
    }
  }

  TEST_CASE("edgeDetector: every other sample on is the most a block can hold (#1121)") {
    edgeDetector d;
    buffer b(N);
    for (UInt i = 0; i < N; ++i)
      b.getPtr()[i] = (i % 2u) == 0 ? 1.f : 0.f;
    CHECK(d(b) == N);
    REQUIRE(d.rises() == N / 2);
    REQUIRE(d.falls() == N / 2);
    for (UInt k = 0; k < N / 2; ++k) {
      CHECK(d.rise(k) == 2 * k);
      CHECK(d.fall(k) == 2 * k + 1);
    }
  }

  TEST_CASE("edgeDetector: set() and reset() choose the remembered state (#1121)") {
    edgeDetector d;
    buffer one = Constant(1.f);
    buffer zero = Constant(0.f);
    d.set(true);
    CHECK(d.high());
    CHECK(d(one) == 0); // already on: no rise
    d.set(false);
    CHECK(d(one) == 1);
    d.set(true);
    CHECK(d.rises() == 0); // set() clears the block report
    CHECK(d(zero) == 1);
    CHECK(d.falls() == 1);
    d.set(true).reset();
    CHECK_FALSE(d.high());
    CHECK(d.falls() == 0);
    CHECK(d(zero) == 0);
  }

  TEST_CASE("edgeDetector: -0 is zero; denormals, infinities and NaN are non-zero (#1121)") {
    edgeDetector d;
    CHECK(d.process(-0.f) == 0);
    CHECK(d.process(std::numeric_limits<float>::denorm_min()) == 1);
    CHECK(d.process(-0.f) == -1);
    CHECK(d.process(std::numeric_limits<float>::infinity()) == 1);
    CHECK(d.process(-std::numeric_limits<float>::infinity()) == 0);
    CHECK(d.process(0.f) == -1);
    CHECK(d.process(std::numeric_limits<float>::quiet_NaN()) == 1);
    CHECK(d.process(0.f) == -1);
  }

  TEST_CASE("edgeDetector: a block longer than STANDARD_BUFFERSIZE is clamped (#1121)") {
    edgeDetector d;
    buffer b = Constant(0.f, 2 * N);
    b.getPtr()[N - 1] = 1.f;
    b.getPtr()[N + 4] = 1.f; // past the clamp: not seen
    CHECK(d(b) == 1);
    CHECK(d.rise(0) == N - 1);
    CHECK(d.high());

    buffer shortBlock = Constant(0.f, 5);
    CHECK(d(shortBlock) == 1);
    CHECK(d.fall(0) == 0);
  }

  TEST_CASE("edgeDetector: the transitions do not depend on the sample rate (#1121)") {
    SampleRateGuard guard;
    const std::vector<float> x = Gate(2 * N, 9, 4);
    YSE::SAMPLERATE = 44100;
    edgeDetector a;
    const Edges at44 = Blocks(a, x, N);
    YSE::SAMPLERATE = 96000;
    edgeDetector b;
    const Edges at96 = Blocks(b, x, N);
    CHECK(at44.rises == at96.rises);
    CHECK(at44.falls == at96.falls);
  }

  TEST_CASE("edgeDetector: processing allocates nothing (#1121)") {
    edgeDetector d;
    buffer in(N);
    for (UInt i = 0; i < N; ++i)
      in.getPtr()[i] = (i % 3u) == 0 ? 1.f : 0.f;
    buffer shortIn = Constant(1.f, 32);
    {
      TestHelpers::ProbeScope probe;
      d(in);
      d(shortIn);
      d.process(0.f);
      d.set(true).reset();
      d(in);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(d.rises() > 0);
  }

} // TEST_SUITE("dsp")
