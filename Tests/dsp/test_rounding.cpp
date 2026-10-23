// Tests for YSE::DSP::round and trunc (YseEngine/dsp/operators/rounding.hpp),
// issue #1050. Reference output is closed form: std::round / std::floor /
// std::trunc of each sample, and the pass-through and finite edge values.

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include "dsp/operators/rounding.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

namespace {

  // -4 to just under 4 across one block, in sixteenths: hits halfway values.
  void FillRamp(YSE::DSP::buffer& b) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = (static_cast<float>(i) - 64.f) / 16.f;
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("round: nearest multiple of a step matches std::round, sample by sample (#1050)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    FillRamp(in);
    for (float step : {1.f, 0.5f, 3.f, -2.f}) {
      CAPTURE(step);
      YSE::DSP::round r(step);
      CHECK(r.getStep() == step);
      CHECK(r.getNearest());
      YSE::DSP::buffer& out = r(in);
      REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
      const float s = std::fabs(step);
      for (unsigned int i = 0; i < out.getLength(); ++i) {
        CAPTURE(i);
        CHECK(out.getPtr()[i] == std::round(in.getPtr()[i] / s) * s);
        CHECK(YSE::DSP::round::process(in.getPtr()[i], step) == out.getPtr()[i]);
      }
    }
  }

  TEST_CASE("round: floor mode rounds down to the multiple (#1050)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    FillRamp(in);
    YSE::DSP::round r;
    r.setNearest(false).setStep(0.5f);
    CHECK_FALSE(r.getNearest());
    YSE::DSP::buffer& out = r(in);
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      CHECK(out.getPtr()[i] == std::floor(in.getPtr()[i] / 0.5f) * 0.5f);
    }
    CHECK(YSE::DSP::round::process(0.9f, 1.f, false) == 0.f);
    CHECK(YSE::DSP::round::process(-0.1f, 1.f, false) == -1.f);
    CHECK(YSE::DSP::round::process(7.f, 3.f, false) == 6.f);
  }

  TEST_CASE("round: closed-form values and halfway cases (#1050)") {
    using YSE::DSP::round;
    CHECK(round::process(0.5f, 1.f) == 1.f);
    CHECK(round::process(-0.5f, 1.f) == -1.f);
    CHECK(round::process(1.49f, 1.f) == 1.f);
    CHECK(round::process(7.f, 3.f) == 6.f);
    CHECK(round::process(8.f, 3.f) == 9.f);
    CHECK(round::process(0.3f, 0.25f) == 0.25f);
  }

  TEST_CASE("round: a step of 0 passes through, and nothing non-finite leaves (#1050)") {
    using YSE::DSP::round;
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(round::process(1.37f, 0.f) == 1.37f);
    CHECK(round::process(-2.5f, 0.f, false) == -2.5f);
    // A step whose quotient overflows, or an infinite / NaN step: pass through.
    CHECK(round::process(3.f, 1e-40f) == 3.f);
    CHECK(round::process(3.f, inf) == 3.f);
    CHECK(round::process(3.f, nan) == 3.f);
    // A non-finite input: 0.
    CHECK(round::process(nan, 1.f) == 0.f);
    CHECK(round::process(inf, 1.f) == 0.f);
    CHECK(round::process(-inf, 0.f) == 0.f);
  }

  TEST_CASE("round: a step signal is read sample by sample (#1050)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE), step(YSE::STANDARD_BUFFERSIZE);
    FillRamp(in);
    for (unsigned int i = 0; i < step.getLength(); ++i)
      step.getPtr()[i] = (i % 3 == 0) ? 0.f : static_cast<float>(i % 3);
    YSE::DSP::round r(100.f); // ignored: the signal wins
    YSE::DSP::buffer& out = r(in, step);
    REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      CHECK(out.getPtr()[i] == YSE::DSP::round::process(in.getPtr()[i], step.getPtr()[i]));
    }
    // A shorter step signal shortens the block.
    YSE::DSP::buffer shortStep(10);
    shortStep = 1.f;
    CHECK(r(in, shortStep).getLength() == 10);
  }

  TEST_CASE("trunc: drops the fractional part toward zero (#1050)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    FillRamp(in);
    YSE::DSP::trunc t;
    YSE::DSP::buffer& out = t(in);
    REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      CHECK(out.getPtr()[i] == std::trunc(in.getPtr()[i]));
    }
    CHECK(YSE::DSP::trunc::process(1.7f) == 1.f);
    CHECK(YSE::DSP::trunc::process(-1.7f) == -1.f);
    CHECK(YSE::DSP::trunc::process(std::numeric_limits<float>::quiet_NaN()) == 0.f);
    CHECK(YSE::DSP::trunc::process(std::numeric_limits<float>::infinity()) == 0.f);
  }

  TEST_CASE("round / trunc: a sine through both, against closed form (#1050)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < in.getLength(); ++i)
      in.getPtr()[i] = 3.f * std::sin(static_cast<float>(i) * 0.1f);
    YSE::DSP::round r(1.f);
    YSE::DSP::trunc t;
    YSE::DSP::buffer& rounded = r(in);
    for (unsigned int i = 0; i < in.getLength(); ++i)
      CHECK(rounded.getPtr()[i] == std::round(in.getPtr()[i]));
    YSE::DSP::buffer& truncated = t(in);
    for (unsigned int i = 0; i < in.getLength(); ++i)
      CHECK(truncated.getPtr()[i] == std::trunc(in.getPtr()[i]));
  }

  TEST_CASE("round / trunc: processing allocates nothing (#1050)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE), step(YSE::STANDARD_BUFFERSIZE);
    FillRamp(in);
    step = 0.5f;
    YSE::DSP::buffer shortIn(17);
    shortIn = 1.5f;
    YSE::DSP::round r;
    YSE::DSP::trunc t;
    TestHelpers::ProbeScope probe;
    r(in);
    r.setNearest(false).setStep(2.f);
    r(in);
    r(in, step);
    r(shortIn);
    r(in);
    t(in);
    t(shortIn);
    t(in);
    CHECK(TestHelpers::g_alloc_count.load() == 0);
  }

} // TEST_SUITE("dsp")
