// Tests for YSE::DSP::direction (YseEngine/dsp/analysis/direction.hpp), issue
// #1120. Reference output is closed form: sample n is the sign of
// x[n] - x[n-1], with x[-1] = 0 after construction or reset().

#include <doctest/doctest.h>
#include <cmath>
#include "dsp/analysis/direction.hpp"
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

  YSE::DSP::buffer Constant(float value, UInt length = YSE::STANDARD_BUFFERSIZE) {
    YSE::DSP::buffer b(length);
    b = value;
    return b;
  }

  float Sign(float now, float before) {
    if (now > before) return 1.f;
    if (now < before) return -1.f;
    return 0.f;
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("direction: 1 up, -1 down, 0 steady, from 0 (#1120)") {
    YSE::DSP::direction d;
    CHECK(d.process(1.f) == 1.f); // the sample before the first is 0
    CHECK(d.process(3.f) == 1.f);
    CHECK(d.process(3.f) == 0.f);
    CHECK(d.process(-1.f) == -1.f);
    CHECK(d.process(-1.f) == 0.f);
    CHECK(d.process(-0.5f) == 1.f);
    CHECK(d.previous() == -0.5f);

    YSE::DSP::direction neg;
    CHECK(neg.process(-0.25f) == -1.f);
    YSE::DSP::direction zero;
    CHECK(zero.process(0.f) == 0.f);
  }

  TEST_CASE("direction: the output is the size of no step, only its sign (#1120)") {
    YSE::DSP::direction d;
    CHECK(d.process(1000.f) == 1.f);
    CHECK(d.process(1000.0001f) == 1.f);
    // The smallest step float can take still counts.
    CHECK(d.process(std::nextafter(1000.0001f, 2000.f)) == 1.f);
    CHECK(d.process(-1e30f) == -1.f);
  }

  TEST_CASE("direction: an impulse gives 1 then -1, a step a single 1 (#1120)") {
    YSE::DSP::direction d;
    YSE::DSP::buffer in = Constant(0.f);
    in.getPtr()[5] = 0.5f;
    YSE::DSP::buffer& out = d(in);
    REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      CHECK(out.getPtr()[i] == (i == 5 ? 1.f : (i == 6 ? -1.f : 0.f)));
    }

    YSE::DSP::direction s;
    YSE::DSP::buffer step = Constant(0.f);
    for (unsigned int i = 10; i < step.getLength(); ++i)
      step.getPtr()[i] = -0.75f;
    YSE::DSP::buffer& edge = s(step);
    for (unsigned int i = 0; i < edge.getLength(); ++i) {
      CAPTURE(i);
      CHECK(edge.getPtr()[i] == (i == 10 ? -1.f : 0.f));
    }
  }

  TEST_CASE("direction: a constant gives its sign once, then 0 across blocks (#1120)") {
    YSE::DSP::direction d;
    YSE::DSP::buffer in = Constant(0.5f);
    YSE::DSP::buffer& first = d(in);
    CHECK(first.getPtr()[0] == 1.f);
    for (unsigned int i = 1; i < first.getLength(); ++i)
      CHECK(first.getPtr()[i] == 0.f);
    // The last sample carries over the block boundary.
    YSE::DSP::buffer& second = d(in);
    for (unsigned int i = 0; i < second.getLength(); ++i)
      CHECK(second.getPtr()[i] == 0.f);
  }

  TEST_CASE("direction: a wrapping ramp is 1 except one -1 at each wrap (#1120)") {
    YSE::DSP::direction d;
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < in.getLength(); ++i)
      in.getPtr()[i] = static_cast<float>((i + 1) % 16) / 16.f;
    YSE::DSP::buffer& out = d(in);
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      CHECK(out.getPtr()[i] == ((i + 1) % 16 == 0 ? -1.f : 1.f));
    }
  }

  TEST_CASE("direction: a sine flips at each peak and trough (#1120)") {
    // sin(w n) rises while cos(w (n - 1/2)) > 0, the sign of the exact
    // backward difference 2 sin(w/2) cos(w (n - 1/2)). Samples where that
    // cosine is near 0 are skipped: float rounding decides them.
    const double w = 2.0 * 3.14159265358979323846 * 750.0 / 48000.0; // 64 samples a cycle
    YSE::DSP::direction d;
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < in.getLength(); ++i)
      in.getPtr()[i] = static_cast<float>(std::sin(w * (i + 1)));
    d.process(0.f); // x[0] = sin(0) = 0
    YSE::DSP::buffer& out = d(in);
    int flips = 0;
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      const double c = std::cos(w * (i + 0.5));
      if (std::fabs(c) > 1e-3) CHECK(out.getPtr()[i] == (c > 0 ? 1.f : -1.f));
      if (i > 0 && out.getPtr()[i] != out.getPtr()[i - 1]) ++flips;
    }
    // Two flips per 64-sample cycle, the first after sample 15.
    CHECK(flips == static_cast<int>(YSE::STANDARD_BUFFERSIZE / 32));
  }

  TEST_CASE("direction: per-sample and per-block calls agree, across any block split (#1120)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < in.getLength(); ++i)
      in.getPtr()[i] = static_cast<float>((i * 37u) % 11u) - 5.f;

    YSE::DSP::direction perSample;
    YSE::DSP::direction perBlock;
    YSE::DSP::direction split;
    YSE::DSP::buffer& whole = perBlock(in);

    YSE::DSP::buffer head(17), tail(YSE::STANDARD_BUFFERSIZE - 17);
    for (unsigned int i = 0; i < 17; ++i)
      head.getPtr()[i] = in.getPtr()[i];
    for (unsigned int i = 17; i < in.getLength(); ++i)
      tail.getPtr()[i - 17] = in.getPtr()[i];
    float splitOut[YSE::STANDARD_BUFFERSIZE];
    YSE::DSP::buffer& a = split(head);
    REQUIRE(a.getLength() == 17);
    for (unsigned int i = 0; i < 17; ++i)
      splitOut[i] = a.getPtr()[i];
    YSE::DSP::buffer& b = split(tail);
    REQUIRE(b.getLength() == YSE::STANDARD_BUFFERSIZE - 17);
    for (unsigned int i = 17; i < in.getLength(); ++i)
      splitOut[i] = b.getPtr()[i - 17];

    for (unsigned int i = 0; i < in.getLength(); ++i) {
      CAPTURE(i);
      const float expected = Sign(in.getPtr()[i], i == 0 ? 0.f : in.getPtr()[i - 1]);
      CHECK(perSample.process(in.getPtr()[i]) == expected);
      CHECK(whole.getPtr()[i] == expected);
      CHECK(splitOut[i] == expected);
    }
  }

  TEST_CASE("direction: reset() and set() choose the sample before the next (#1120)") {
    YSE::DSP::direction d;
    d.process(5.f);
    d.reset();
    CHECK(d.previous() == 0.f);
    CHECK(d.process(2.f) == 1.f);
    d.set(10.f);
    CHECK(d.previous() == 10.f);
    CHECK(d.process(2.f) == -1.f);
    d.set(2.f);
    CHECK(d.process(2.f) == 0.f);
  }

  TEST_CASE("direction: NaN gives 0 for two samples, infinities compare as extremes (#1120)") {
    YSE::DSP::direction d;
    d.process(1.f);
    CHECK(d.process(NAN) == 0.f);
    CHECK(d.process(2.f) == 0.f); // compared with NaN
    CHECK(d.process(3.f) == 1.f); // recovered
    CHECK(d.process(INFINITY) == 1.f);
    CHECK(d.process(INFINITY) == 0.f);
    CHECK(d.process(0.f) == -1.f);
    CHECK(d.process(-INFINITY) == -1.f);
    CHECK(d.process(0.f) == 1.f);
  }

  TEST_CASE("direction: a longer block is processed up to STANDARD_BUFFERSIZE (#1120)") {
    YSE::DSP::direction d;
    YSE::DSP::buffer shortIn = Constant(1.f, 16);
    CHECK(d(shortIn).getLength() == 16);
    YSE::DSP::buffer longIn = Constant(1.f, YSE::STANDARD_BUFFERSIZE * 2);
    CHECK(d(longIn).getLength() == YSE::STANDARD_BUFFERSIZE);
  }

  TEST_CASE("direction: output does not depend on the sample rate (#1120)") {
    SampleRateGuard guard;
    YSE::DSP::direction d;
    YSE::DSP::buffer in = Constant(1.f);
    d(in);
    YSE::SAMPLERATE = YSE::SAMPLERATE == 48000 ? 44100 : 48000;
    YSE::DSP::buffer two = Constant(-3.f);
    YSE::DSP::buffer& out = d(two);
    CHECK(out.getPtr()[0] == -1.f);
    CHECK(out.getPtr()[1] == 0.f);
  }

  TEST_CASE("direction: processing allocates nothing (#1120)") {
    YSE::DSP::direction d;
    YSE::DSP::buffer in = Constant(1.f);
    YSE::DSP::buffer shortIn = Constant(1.f, 32);
    d(in);
    {
      TestHelpers::ProbeScope probe;
      d(in);
      d(shortIn);
      d(in);
      d.process(0.5f);
      d.set(2.f).reset();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
