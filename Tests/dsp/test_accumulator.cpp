// Tests for YSE::DSP::accumulator (YseEngine/dsp/operators/accumulator.hpp),
// issue #1047. Reference output is closed form: from a sum s0, sample n of the
// running sum of a constant c is s0 + (n + 1) * c.

#include <doctest/doctest.h>
#include "dsp/operators/accumulator.hpp"
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

  TEST_CASE("accumulator: 1, 1, 1, 1 gives 1, 2, 3, 4 (#1047)") {
    YSE::DSP::accumulator sum;
    CHECK(sum.process(1.f) == 1.f);
    CHECK(sum.process(1.f) == 2.f);
    CHECK(sum.process(1.f) == 3.f);
    CHECK(sum.process(1.f) == 4.f);
    CHECK(sum.sum() == 4.0);
  }

  TEST_CASE("accumulator: a constant becomes a ramp across blocks (#1047)") {
    YSE::DSP::accumulator sum(10.0);
    YSE::DSP::buffer in = Constant(0.5f);
    unsigned int n = 0;
    for (int block = 0; block < 3; ++block) {
      YSE::DSP::buffer& out = sum(in);
      REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
      for (unsigned int i = 0; i < out.getLength(); ++i, ++n) {
        CAPTURE(n);
        CHECK(out.getPtr()[i] == doctest::Approx(10.0 + (n + 1) * 0.5));
      }
    }
  }

  TEST_CASE("accumulator: an impulse gives a step (#1047)") {
    YSE::DSP::accumulator sum;
    YSE::DSP::buffer in = Constant(0.f);
    in.getPtr()[5] = 2.f;
    YSE::DSP::buffer& out = sum(in);
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      CHECK(out.getPtr()[i] == (i < 5 ? 0.f : 2.f));
    }
  }

  TEST_CASE("accumulator: the sum is double precision and does not drift (#1047)") {
    // 0.1 is not exact in float; a float sum of 1e6 of them is off by
    // about 1%, a double one by far less than a float can show.
    YSE::DSP::accumulator sum;
    YSE::DSP::buffer in = Constant(0.1f);
    const unsigned int blocks = 1000000 / YSE::STANDARD_BUFFERSIZE;
    for (unsigned int b = 0; b < blocks; ++b)
      sum(in);
    const double expected = static_cast<double>(0.1f) * blocks * YSE::STANDARD_BUFFERSIZE;
    CHECK(sum.sum() == doctest::Approx(expected).epsilon(1e-9));
  }

  TEST_CASE("accumulator: a non-zero reset sample restarts the sum from that input (#1047)") {
    YSE::DSP::accumulator sum;
    YSE::DSP::buffer in = Constant(1.f);
    YSE::DSP::buffer reset = Constant(0.f);
    reset.getPtr()[10] = 1.f;
    reset.getPtr()[20] = -0.5f; // any non-zero value resets
    YSE::DSP::buffer& out = sum(in, reset);
    CHECK(out.getPtr()[9] == 10.f);
    CHECK(out.getPtr()[10] == 1.f);
    CHECK(out.getPtr()[19] == 10.f);
    CHECK(out.getPtr()[20] == 1.f);
    CHECK(out.getPtr()[21] == 2.f);
    CHECK(sum.process(0.f, 1.f) == 0.f);
    CHECK(sum.process(3.f, 0.f) == 3.f);
  }

  TEST_CASE("accumulator: set and reset (#1047)") {
    YSE::DSP::accumulator sum;
    sum.process(5.f);
    sum.set(100.0);
    CHECK(sum.sum() == 100.0);
    CHECK(sum.process(1.f) == 101.f); // the input is added after the set value
    sum.reset();
    CHECK(sum.sum() == 0.0);
    CHECK(sum.process(2.f) == 2.f);
  }

  TEST_CASE("accumulator: block length follows the shorter input, clamped (#1047)") {
    YSE::DSP::accumulator sum;
    YSE::DSP::buffer shortIn = Constant(1.f, 16);
    CHECK(sum(shortIn).getLength() == 16);
    YSE::DSP::buffer in = Constant(1.f);
    YSE::DSP::buffer shortReset = Constant(0.f, 8);
    CHECK(sum(in, shortReset).getLength() == 8);
    YSE::DSP::buffer longIn = Constant(1.f, YSE::STANDARD_BUFFERSIZE * 2);
    CHECK(sum(longIn).getLength() == YSE::STANDARD_BUFFERSIZE);
  }

  TEST_CASE("accumulator: output does not depend on the sample rate (#1047)") {
    SampleRateGuard guard;
    YSE::DSP::accumulator sum;
    YSE::DSP::buffer in = Constant(1.f);
    sum(in);
    YSE::SAMPLERATE = YSE::SAMPLERATE == 48000 ? 44100 : 48000;
    YSE::DSP::buffer& out = sum(in);
    CHECK(out.getPtr()[0] == static_cast<float>(YSE::STANDARD_BUFFERSIZE + 1));
  }

  TEST_CASE("accumulator: processing allocates nothing (#1047)") {
    YSE::DSP::accumulator sum;
    YSE::DSP::buffer in = Constant(1.f);
    YSE::DSP::buffer reset = Constant(0.f);
    YSE::DSP::buffer shortIn = Constant(1.f, 32);
    sum(in);
    {
      TestHelpers::ProbeScope probe;
      sum(in);
      sum(in, reset);
      sum(shortIn);
      sum(in);
      sum.set(3.0).reset();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
