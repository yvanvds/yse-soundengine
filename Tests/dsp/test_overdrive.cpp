// Tests for YSE::DSP::overdrive (YseEngine/dsp/operators/overdrive.hpp), issue
// #1112. Reference output is closed form: with the drive d, a sample x with
// |x| < 1 gives sign(x) * (1 - (1 - |x|)^d), limited to -1..1, and a sample
// at or beyond full scale gives sign(x).

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/operators/overdrive.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::overdrive;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // The curve, in double.
  double Curve(double x, double d) {
    const double a = std::fabs(x);
    if (a >= 1.0) return x > 0.0 ? 1.0 : -1.0;
    const double y = std::fmax(1.0 - std::pow(1.0 - a, d), -1.0);
    return x < 0.0 ? -y : y;
  }

  float Wobble(unsigned int n) {
    const auto x = static_cast<float>(n);
    return 1.3f * std::sin(x * 0.031f) + 0.2f * std::sin(x * 1.7f);
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("overdrive: the default drive 1 passes -1..1 bit for bit and clips beyond (#1112)") {
    overdrive o;
    CHECK(o.drive() == overdrive::DEFAULT_DRIVE);
    for (unsigned int n = 0; n < 512; ++n) {
      CAPTURE(n);
      const float x = Wobble(n);
      const float expected = x >= 1.f ? 1.f : (x <= -1.f ? -1.f : x);
      CHECK(o.process(x) == expected);
    }
    CHECK(o.process(0.3f) == 0.3f);
    CHECK(o.process(-0.999999f) == -0.999999f);
    CHECK(o.process(1.f) == 1.f);
    CHECK(o.process(-1.f) == -1.f);
    CHECK(o.process(7.f) == 1.f);
    CHECK(o.process(-7.f) == -1.f);
  }

  TEST_CASE("overdrive: values by hand (#1112)") {
    overdrive o;
    o.drive(2.f);
    CHECK(o.process(0.5f) == doctest::Approx(0.75f).epsilon(1e-6)); // 1 - 0.5^2
    CHECK(o.process(-0.75f) == doctest::Approx(-0.9375f).epsilon(1e-6)); // -(1 - 0.25^2)
    CHECK(o.process(0.f) == 0.f);
    CHECK(o.process(1.5f) == 1.f);
    o.drive(3.f);
    CHECK(o.process(0.5f) == doctest::Approx(0.875f).epsilon(1e-6)); // 1 - 0.5^3
    o.drive(0.5f);
    CHECK(o.process(0.75f) == doctest::Approx(0.5f).epsilon(1e-6)); // 1 - sqrt(0.25)
    o.drive(0.f);
    CHECK(o.process(0.9f) == 0.f);
    CHECK(o.process(-0.2f) == 0.f);
    CHECK(o.process(1.f) == 1.f);
    o.drive(-1.f);
    CHECK(o.process(0.5f) == doctest::Approx(-1.f).epsilon(1e-6)); // 1 - 1/0.5
    CHECK(o.process(-0.5f) == doctest::Approx(1.f).epsilon(1e-6));
    CHECK(o.process(0.25f) == doctest::Approx(-1.f / 3.f).epsilon(1e-6)); // 1 - 1/0.75
    CHECK(o.process(0.9999f) == -1.f); // -inf limited
  }

  TEST_CASE("overdrive: blocks against the closed form at several drives (#1112)") {
    for (const float d : {0.f, 0.25f, 0.5f, 1.f, 1.5f, 2.f, 4.f, 10.f, -0.5f, -3.f}) {
      CAPTURE(d);
      overdrive o;
      o.drive(d);
      CHECK(o.drive() == d);
      buffer in(N);
      for (unsigned int i = 0; i < N; ++i)
        in.getPtr()[i] = Wobble(i);
      buffer& out = o(in);
      REQUIRE(out.getLength() == N);
      for (unsigned int i = 0; i < N; ++i) {
        CAPTURE(i);
        const float y = out.getPtr()[i];
        CHECK(y >= -1.f);
        CHECK(y <= 1.f);
        CHECK(y == doctest::Approx(Curve(in.getPtr()[i], d)).epsilon(2e-6));
      }
    }
  }

  TEST_CASE("overdrive: a positive drive is odd, rising, and amplifies small samples by the "
            "drive (#1112)") {
    for (const float d : {0.5f, 1.f, 2.f, 4.f, 10.f}) {
      CAPTURE(d);
      overdrive o;
      o.drive(d);
      float last = -1.f;
      for (int k = -1500; k <= 1500; ++k) {
        CAPTURE(k);
        const float x = static_cast<float>(k) / 1000.f;
        const float y = o.process(x);
        CHECK(y >= last);
        CHECK(o.process(-x) == -y);
        last = y;
      }
      // The slope at 0 is the drive.
      const float x = 1e-4f;
      CHECK(o.process(x) / x == doctest::Approx(d).epsilon(1e-2));
    }
    // A sine at full scale through drive 4: the peaks are rounded off to 1,
    // the quiet parts are about 4 times louder.
    overdrive o;
    o.drive(4.f);
    CHECK(o.process(0.9f) > 0.9998f);
    CHECK(o.process(0.9f) < 1.f);
    CHECK(o.process(0.05f) == doctest::Approx(Curve(0.05, 4.0)).epsilon(1e-6));
    CHECK(o.process(0.05f) > 0.18f);
  }

  TEST_CASE("overdrive: a non-finite drive is ignored, non-finite input gives 0 or full "
            "scale (#1112)") {
    overdrive o;
    o.drive(3.f);
    o.drive(std::nanf(""));
    CHECK(o.drive() == 3.f);
    o.drive(std::numeric_limits<float>::infinity());
    CHECK(o.drive() == 3.f);
    o.drive(-std::numeric_limits<float>::infinity());
    CHECK(o.drive() == 3.f);
    CHECK(o.process(std::nanf("")) == 0.f);
    CHECK(o.process(std::numeric_limits<float>::infinity()) == 1.f);
    CHECK(o.process(-std::numeric_limits<float>::infinity()) == -1.f);
    // A huge finite drive is a square wave inside -1..1.
    o.drive(1e30f);
    CHECK(o.process(0.01f) == 1.f);
    CHECK(o.process(-0.01f) == -1.f);
    o.drive(-1e30f);
    CHECK(o.process(0.01f) == -1.f);
    CHECK(o.process(-0.01f) == 1.f);
  }

  TEST_CASE("overdrive: the drive read per sample from a signal (#1112)") {
    overdrive o;
    overdrive reference;
    buffer in(N);
    buffer drive(N);
    for (unsigned int i = 0; i < N; ++i) {
      in.getPtr()[i] = Wobble(i);
      drive.getPtr()[i] = static_cast<float>(i % 40) * 0.25f - 1.f; // -1 .. 8.75
    }
    drive.getPtr()[10] = std::nanf("");
    drive.getPtr()[11] = std::numeric_limits<float>::infinity();
    buffer& out = o(in, drive);
    REQUIRE(out.getLength() == N);
    for (unsigned int i = 0; i < N; ++i) {
      CAPTURE(i);
      reference.drive(drive.getPtr()[i]);
      CHECK(out.getPtr()[i] == reference.process(in.getPtr()[i]));
    }
    CHECK(o.drive() == drive.getPtr()[N - 1]);
    // Samples 10 and 11 keep the drive of sample 9.
    overdrive at9;
    at9.drive(drive.getPtr()[9]);
    CHECK(out.getPtr()[10] == at9.process(in.getPtr()[10]));
    CHECK(out.getPtr()[11] == at9.process(in.getPtr()[11]));
  }

  TEST_CASE("overdrive: per-sample, per-block and split-block calls agree (#1112)") {
    const unsigned int splits[] = {128, 64, 32, 13, 1};
    std::vector<float> input(4 * N);
    for (unsigned int n = 0; n < input.size(); ++n)
      input[n] = Wobble(n);
    std::vector<float> expected(input.size());
    {
      overdrive o;
      o.drive(3.5f);
      for (unsigned int n = 0; n < input.size(); ++n)
        expected[n] = o.process(input[n]);
    }
    {
      overdrive o;
      o.drive(3.5f);
      buffer in(N);
      for (unsigned int block = 0; block < 4; ++block) {
        for (unsigned int i = 0; i < N; ++i)
          in.getPtr()[i] = input[block * N + i];
        buffer& out = o(in);
        for (unsigned int i = 0; i < N; ++i)
          CHECK(out.getPtr()[i] == expected[block * N + i]);
      }
    }
    for (unsigned int split : splits) {
      CAPTURE(split);
      overdrive o;
      o.drive(3.5f);
      buffer in(split);
      unsigned int n = 0;
      while (n + split <= input.size()) {
        for (unsigned int i = 0; i < split; ++i)
          in.getPtr()[i] = input[n + i];
        buffer& out = o(in);
        REQUIRE(out.getLength() == split);
        for (unsigned int i = 0; i < split; ++i) {
          CAPTURE(n + i);
          CHECK(out.getPtr()[i] == expected[n + i]);
        }
        n += split;
      }
    }
  }

  TEST_CASE("overdrive: the output does not depend on the sample rate (#1112)") {
    std::vector<float> first;
    for (const UInt rate : {44100u, 48000u, 96000u}) {
      CAPTURE(rate);
      TestHelpers::ScopedSampleRate scoped(rate);
      overdrive o;
      o.drive(5.f);
      buffer in(N);
      for (unsigned int i = 0; i < N; ++i)
        in.getPtr()[i] = Wobble(i);
      buffer& out = o(in);
      if (first.empty()) {
        first.assign(out.getPtr(), out.getPtr() + N);
        continue;
      }
      for (unsigned int i = 0; i < N; ++i)
        CHECK(out.getPtr()[i] == first[i]);
    }
  }

  TEST_CASE("overdrive: a longer block is processed up to STANDARD_BUFFERSIZE (#1112)") {
    overdrive o;
    buffer shortIn(16);
    shortIn = 0.5f;
    CHECK(o(shortIn).getLength() == 16);
    buffer longIn(N * 2);
    longIn = 0.5f;
    CHECK(o(longIn).getLength() == N);
    buffer d(8);
    d = 2.f;
    CHECK(o(longIn, d).getLength() == 8);
  }

  TEST_CASE("overdrive: processing allocates nothing (#1112)") {
    overdrive o;
    buffer in(N);
    buffer shortIn(32);
    buffer d(N);
    in = 0.3f;
    shortIn = 0.3f;
    d = 4.f;
    o(in);
    o(in, d);
    {
      TestHelpers::ProbeScope probe;
      o(in);
      o(shortIn);
      o(in, d);
      o(in);
      o.drive(2.f);
      (void)o.process(0.5f);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
