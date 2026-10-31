// Tests for YSE::DSP::delta (YseEngine/dsp/operators/delta.hpp), issue #1108.
// Reference output is closed form: sample n is x[n] - x[n-1], with x[-1] = 0
// after construction or reset().

#include <doctest/doctest.h>
#include <cmath>
#include "dsp/operators/delta.hpp"
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

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("delta: each sample minus the one before, from 0 (#1108)") {
    YSE::DSP::delta d;
    CHECK(d.process(1.f) == 1.f); // the sample before the first is 0
    CHECK(d.process(3.f) == 2.f);
    CHECK(d.process(3.f) == 0.f);
    CHECK(d.process(-1.f) == -4.f);
    CHECK(d.previous() == -1.f);
  }

  TEST_CASE("delta: an impulse gives a doublet, a step an impulse (#1108)") {
    YSE::DSP::delta d;
    YSE::DSP::buffer in = Constant(0.f);
    in.getPtr()[5] = 1.f;
    YSE::DSP::buffer& out = d(in);
    REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      CHECK(out.getPtr()[i] == (i == 5 ? 1.f : (i == 6 ? -1.f : 0.f)));
    }

    YSE::DSP::delta s;
    YSE::DSP::buffer step = Constant(0.f);
    for (unsigned int i = 10; i < step.getLength(); ++i)
      step.getPtr()[i] = 0.75f;
    YSE::DSP::buffer& edge = s(step);
    for (unsigned int i = 0; i < edge.getLength(); ++i) {
      CAPTURE(i);
      CHECK(edge.getPtr()[i] == (i == 10 ? 0.75f : 0.f));
    }
  }

  TEST_CASE("delta: a constant gives its value once, then 0 (#1108)") {
    YSE::DSP::delta d;
    YSE::DSP::buffer in = Constant(0.5f);
    YSE::DSP::buffer& first = d(in);
    CHECK(first.getPtr()[0] == 0.5f);
    for (unsigned int i = 1; i < first.getLength(); ++i)
      CHECK(first.getPtr()[i] == 0.f);
    // The last sample carries over the block boundary.
    YSE::DSP::buffer& second = d(in);
    for (unsigned int i = 0; i < second.getLength(); ++i)
      CHECK(second.getPtr()[i] == 0.f);
  }

  TEST_CASE("delta: a ramp gives its slope, the wrap of a phasor a negative spike (#1108)") {
    // A phase ramp rising 1/16 per sample and wrapping every 16 samples: the
    // difference is 1/16 everywhere except at the wrap, where it is
    // 1/16 - 1. Exact in float: every value is a multiple of 1/16.
    YSE::DSP::delta d;
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < in.getLength(); ++i)
      in.getPtr()[i] = static_cast<float>((i + 1) % 16) / 16.f;
    YSE::DSP::buffer& out = d(in);
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      const bool wrap = (i + 1) % 16 == 0;
      CHECK(out.getPtr()[i] == (wrap ? 1.f / 16.f - 1.f : 1.f / 16.f));
    }
  }

  TEST_CASE("delta: a sine gives its scaled, quarter-turn shifted derivative (#1108)") {
    // d/dn sin(w n) ~ w cos(w n); the backward difference is exactly
    // 2 sin(w/2) cos(w (n - 1/2)).
    const double w = 2.0 * 3.14159265358979323846 * 440.0 / 48000.0;
    YSE::DSP::delta d;
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < in.getLength(); ++i)
      in.getPtr()[i] = static_cast<float>(std::sin(w * (i + 1)));
    d.process(0.f); // x[0] = sin(0) = 0
    YSE::DSP::buffer& out = d(in);
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      const double n = i + 1;
      const double expected = 2.0 * std::sin(w / 2.0) * std::cos(w * (n - 0.5));
      CHECK(out.getPtr()[i] == doctest::Approx(expected).epsilon(1e-6).scale(1.0));
    }
  }

  TEST_CASE("delta: per-sample and per-block calls agree, across any block split (#1108)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < in.getLength(); ++i)
      in.getPtr()[i] = static_cast<float>((i * 37u) % 11u) - 5.f;

    YSE::DSP::delta perSample;
    YSE::DSP::delta perBlock;
    YSE::DSP::delta split;
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
      const float expected = in.getPtr()[i] - (i == 0 ? 0.f : in.getPtr()[i - 1]);
      CHECK(perSample.process(in.getPtr()[i]) == expected);
      CHECK(whole.getPtr()[i] == expected);
      CHECK(splitOut[i] == expected);
    }
  }

  TEST_CASE("delta: reset() and set() choose the sample before the next (#1108)") {
    YSE::DSP::delta d;
    d.process(5.f);
    d.reset();
    CHECK(d.previous() == 0.f);
    CHECK(d.process(2.f) == 2.f);
    d.set(10.f);
    CHECK(d.previous() == 10.f);
    CHECK(d.process(12.f) == 2.f);
  }

  TEST_CASE("delta: a non-finite sample spoils only its own two outputs (#1108)") {
    YSE::DSP::delta d;
    d.process(1.f);
    CHECK(std::isnan(d.process(NAN)));
    CHECK(std::isnan(d.process(2.f))); // 2 - NaN
    CHECK(d.process(3.f) == 1.f); // recovered
    CHECK(std::isinf(d.process(INFINITY)));
    CHECK(std::isinf(d.process(0.f)));
    CHECK(d.process(0.f) == 0.f);
  }

  TEST_CASE("delta: a longer block is processed up to STANDARD_BUFFERSIZE (#1108)") {
    YSE::DSP::delta d;
    YSE::DSP::buffer shortIn = Constant(1.f, 16);
    CHECK(d(shortIn).getLength() == 16);
    YSE::DSP::buffer longIn = Constant(1.f, YSE::STANDARD_BUFFERSIZE * 2);
    CHECK(d(longIn).getLength() == YSE::STANDARD_BUFFERSIZE);
  }

  TEST_CASE("delta: output does not depend on the sample rate (#1108)") {
    SampleRateGuard guard;
    YSE::DSP::delta d;
    YSE::DSP::buffer in = Constant(1.f);
    d(in);
    YSE::SAMPLERATE = YSE::SAMPLERATE == 48000 ? 44100 : 48000;
    YSE::DSP::buffer two = Constant(3.f);
    YSE::DSP::buffer& out = d(two);
    CHECK(out.getPtr()[0] == 2.f);
    CHECK(out.getPtr()[1] == 0.f);
  }

  TEST_CASE("delta: processing allocates nothing (#1108)") {
    YSE::DSP::delta d;
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
