// Tests for YSE::DSP::compare, minimum and maximum
// (YseEngine/dsp/operators/compare.hpp), issue #1048. Reference output is
// closed form: the C++ comparison as 0.0 / 1.0, and min / max per sample.

#include <doctest/doctest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include "dsp/operators/compare.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

using OP = YSE::DSP::compare::OP;

namespace {

  // A ramp from -4 to just under 4 across one block, crossing 0 at sample 64.
  void FillRamp(YSE::DSP::buffer& b) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = (static_cast<float>(i) - 64.f) / 16.f;
  }

  float Expected(OP op, float a, float b) {
    bool r = false;
    switch (op) {
    case OP::EQUAL:
      r = a == b;
      break;
    case OP::NOT_EQUAL:
      r = a != b;
      break;
    case OP::GREATER:
      r = a > b;
      break;
    case OP::GREATER_EQUAL:
      r = a >= b;
      break;
    case OP::LESS:
      r = a < b;
      break;
    case OP::LESS_EQUAL:
      r = a <= b;
      break;
    }
    return r ? 1.f : 0.f;
  }

  const OP kAll[] = {OP::EQUAL,         OP::NOT_EQUAL, OP::GREATER,
                     OP::GREATER_EQUAL, OP::LESS,      OP::LESS_EQUAL};

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("compare: every operator against a number gives exactly 0 or 1 (#1048)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    FillRamp(in);
    for (OP op : kAll) {
      CAPTURE(static_cast<int>(op));
      YSE::DSP::compare cmp(op);
      CHECK(cmp.getOperator() == op);
      YSE::DSP::buffer& out = cmp(in, 1.f); // the ramp hits 1.0 exactly at sample 80
      REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
      for (unsigned int i = 0; i < out.getLength(); ++i) {
        CAPTURE(i);
        CHECK(out.getPtr()[i] == Expected(op, in.getPtr()[i], 1.f));
      }
    }
  }

  TEST_CASE("compare: every operator against a signal, sample by sample (#1048)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE), right(YSE::STANDARD_BUFFERSIZE);
    FillRamp(in);
    // A falling ramp: crosses the rising one at sample 64, where both are 0.
    for (unsigned int i = 0; i < right.getLength(); ++i)
      right.getPtr()[i] = -in.getPtr()[i];
    YSE::DSP::compare cmp;
    for (OP op : kAll) {
      CAPTURE(static_cast<int>(op));
      YSE::DSP::buffer& out = cmp.setOperator(op)(in, right);
      for (unsigned int i = 0; i < out.getLength(); ++i) {
        CAPTURE(i);
        CHECK(out.getPtr()[i] == Expected(op, in.getPtr()[i], right.getPtr()[i]));
      }
      CHECK(YSE::DSP::compare::process(op, 0.5f, 0.25f) == Expected(op, 0.5f, 0.25f));
    }
  }

  TEST_CASE("compare: > a threshold turns a sine into a square gate (#1048)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < in.getLength(); ++i)
      in.getPtr()[i] = std::sin(static_cast<float>(i) * 0.1f);
    YSE::DSP::compare cmp(OP::GREATER);
    YSE::DSP::buffer& out = cmp(in, 0.f);
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      CHECK(out.getPtr()[i] == (std::sin(static_cast<float>(i) * 0.1f) > 0.f ? 1.f : 0.f));
    }
  }

  TEST_CASE("compare: fuzzy widens equality, not the strict operators (#1048)") {
    YSE::DSP::compare cmp;
    cmp.setFuzzy(0.1f);
    CHECK(cmp.getFuzzy() == doctest::Approx(0.1f));
    YSE::DSP::buffer in(4), right(4);
    // in - right: -0.2, -0.05, 0.05, 0.2
    in.getPtr()[0] = 0.8f;
    in.getPtr()[1] = 0.95f;
    in.getPtr()[2] = 1.05f;
    in.getPtr()[3] = 1.2f;
    right = 1.f;

    auto run = [&](OP op) {
      YSE::DSP::buffer& out = cmp.setOperator(op)(in, right);
      REQUIRE(out.getLength() == 4);
      return std::array<float, 4>{out.getPtr()[0], out.getPtr()[1], out.getPtr()[2],
                                  out.getPtr()[3]};
    };
    CHECK(run(OP::EQUAL) == std::array<float, 4>{0.f, 1.f, 1.f, 0.f});
    CHECK(run(OP::NOT_EQUAL) == std::array<float, 4>{1.f, 0.f, 0.f, 1.f});
    CHECK(run(OP::GREATER_EQUAL) == std::array<float, 4>{0.f, 1.f, 1.f, 1.f});
    CHECK(run(OP::LESS_EQUAL) == std::array<float, 4>{1.f, 1.f, 1.f, 0.f});
    CHECK(run(OP::GREATER) == std::array<float, 4>{0.f, 0.f, 1.f, 1.f});
    CHECK(run(OP::LESS) == std::array<float, 4>{1.f, 1.f, 0.f, 0.f});

    // The scalar path agrees.
    CHECK(YSE::DSP::compare::process(OP::EQUAL, 0.95f, 1.f, 0.1f) == 1.f);
    CHECK(YSE::DSP::compare::process(OP::EQUAL, 0.95f, 1.f) == 0.f);

    // A negative or NaN tolerance means exact.
    cmp.setFuzzy(-1.f);
    CHECK(cmp.getFuzzy() == 0.f);
    cmp.setFuzzy(std::numeric_limits<float>::quiet_NaN());
    CHECK(cmp.getFuzzy() == 0.f);
    CHECK(YSE::DSP::compare::process(OP::LESS_EQUAL, 1.5f, 1.f, -1.f) == 0.f);
  }

  TEST_CASE("compare: NaN is false except for !=, infinities compare equal (#1048)") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (OP op : kAll) {
      CAPTURE(static_cast<int>(op));
      CHECK(YSE::DSP::compare::process(op, nan, 1.f, 0.5f) == (op == OP::NOT_EQUAL ? 1.f : 0.f));
    }
    CHECK(YSE::DSP::compare::process(OP::EQUAL, inf, inf) == 1.f);
    CHECK(YSE::DSP::compare::process(OP::EQUAL, inf, inf, 0.5f) == 1.f);
  }

  TEST_CASE("minimum / maximum: closed form against a number and a signal (#1048)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE), right(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < in.getLength(); ++i) {
      in.getPtr()[i] = std::sin(static_cast<float>(i) * 0.1f);
      right.getPtr()[i] = std::cos(static_cast<float>(i) * 0.07f);
    }
    YSE::DSP::minimum lo;
    YSE::DSP::maximum hi;

    YSE::DSP::buffer& clipped = lo(in, 0.5f);
    for (unsigned int i = 0; i < clipped.getLength(); ++i) {
      CAPTURE(i);
      CHECK(clipped.getPtr()[i] == std::min(in.getPtr()[i], 0.5f));
    }
    YSE::DSP::buffer& rectified = hi(in, 0.f);
    for (unsigned int i = 0; i < rectified.getLength(); ++i) {
      CAPTURE(i);
      CHECK(rectified.getPtr()[i] == std::max(in.getPtr()[i], 0.f));
    }
    YSE::DSP::buffer& lower = lo(in, right);
    YSE::DSP::buffer& upper = hi(in, right);
    for (unsigned int i = 0; i < lower.getLength(); ++i) {
      CAPTURE(i);
      CHECK(lower.getPtr()[i] == std::min(in.getPtr()[i], right.getPtr()[i]));
      CHECK(upper.getPtr()[i] == std::max(in.getPtr()[i], right.getPtr()[i]));
    }
    CHECK(YSE::DSP::minimum::process(-2.f, 3.f) == -2.f);
    CHECK(YSE::DSP::maximum::process(-2.f, 3.f) == 3.f);
  }

  TEST_CASE("minimum / maximum: a NaN on one side gives the other side (#1048)") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(YSE::DSP::minimum::process(nan, 2.f) == 2.f);
    CHECK(YSE::DSP::minimum::process(2.f, nan) == 2.f);
    CHECK(YSE::DSP::maximum::process(nan, 2.f) == 2.f);
    CHECK(YSE::DSP::maximum::process(2.f, nan) == 2.f);
  }

  TEST_CASE("compare / minimum / maximum: block length is the shorter input (#1048)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE), shortRight(16);
    FillRamp(in);
    shortRight = 0.f;
    YSE::DSP::compare cmp(OP::GREATER);
    YSE::DSP::minimum lo;
    YSE::DSP::maximum hi;
    CHECK(cmp(in, shortRight).getLength() == 16);
    CHECK(lo(in, shortRight).getLength() == 16);
    CHECK(hi(in, shortRight).getLength() == 16);
    // and back to a full block
    CHECK(cmp(in, 0.f).getLength() == YSE::STANDARD_BUFFERSIZE);
    CHECK(lo(in, 0.f).getLength() == YSE::STANDARD_BUFFERSIZE);
    CHECK(hi(in, 0.f).getLength() == YSE::STANDARD_BUFFERSIZE);
  }

  TEST_CASE("compare / minimum / maximum: processing allocates nothing (#1048)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE), right(YSE::STANDARD_BUFFERSIZE), shortRight(16);
    FillRamp(in);
    right = 0.5f;
    shortRight = 0.5f;
    YSE::DSP::compare cmp;
    YSE::DSP::minimum lo;
    YSE::DSP::maximum hi;
    {
      TestHelpers::ProbeScope probe;
      for (OP op : kAll) {
        cmp.setOperator(op).setFuzzy(0.01f);
        cmp(in, 0.f);
        cmp(in, right);
        cmp(in, shortRight);
      }
      lo(in, 0.f);
      lo(in, right);
      lo(in, shortRight);
      hi(in, 0.f);
      hi(in, right);
      hi(in, shortRight);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
