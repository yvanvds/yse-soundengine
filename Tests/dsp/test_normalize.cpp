// Tests for YSE::DSP::normalize (YseEngine/dsp/modules/normalize.hpp),
// issue #1111. Reference output is closed form: sample n comes out as
// x[n] / P(n) * a, with P(n) the largest |x[k]| for k <= n (at least the
// floor of 0.000001), computed here in double.

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/modules/normalize.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::normalize;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  // A swell: rises and falls, so the peak keeps rising for a while and then
  // holds.
  float Swell(unsigned int n) {
    const auto x = static_cast<float>(n);
    const float envelope = 0.05f + 0.6f * std::sin(x * 0.002f) * std::sin(x * 0.002f);
    return envelope * (0.8f * std::sin(x * 0.031f) + 0.2f * std::sin(x * 1.7f));
  }

  // The closed form for a fresh object at a constant amplitude.
  std::vector<double> Reference(const std::vector<float>& x, double amplitude) {
    std::vector<double> y(x.size());
    double peak = normalize::MIN_PEAK;
    for (std::size_t n = 0; n < x.size(); ++n) {
      peak = std::fmax(peak, std::fabs(static_cast<double>(x[n])));
      y[n] = static_cast<double>(x[n]) / peak * amplitude;
    }
    return y;
  }

  std::vector<float> Input(std::size_t size) {
    std::vector<float> x(size);
    for (std::size_t n = 0; n < size; ++n)
      x[n] = Swell(static_cast<unsigned int>(n));
    return x;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("normalize: defaults (#1111)") {
    normalize f;
    CHECK(f.amplitude() == normalize::DEFAULT_AMPLITUDE);
    CHECK(f.amplitude() == 1.f);
    CHECK(f.peak() == normalize::MIN_PEAK);
    CHECK(normalize::MIN_PEAK == 0.000001f);
    CHECK(f.process(0.f) == 0.f);
    CHECK(f.peak() == normalize::MIN_PEAK);
  }

  TEST_CASE("normalize: blocks against the closed form (#1111)") {
    for (const float amplitude : {1.f, 0.5f, 2.f, -0.25f}) {
      CAPTURE(amplitude);
      const std::vector<float> x = Input(6 * N);
      const std::vector<double> expected = Reference(x, amplitude);
      normalize f;
      f.amplitude(amplitude);
      buffer in(N);
      double largest = 0.0;
      for (unsigned int block = 0; block < 6; ++block) {
        CAPTURE(block);
        for (unsigned int i = 0; i < N; ++i)
          in.getPtr()[i] = x[block * N + i];
        buffer& out = f(in);
        REQUIRE(out.getLength() == N);
        for (unsigned int i = 0; i < N; ++i) {
          CAPTURE(i);
          const double y = out.getPtr()[i];
          CHECK(std::fabs(y - expected[block * N + i]) <= 1e-6);
          largest = std::fmax(largest, std::fabs(y));
        }
      }
      // Never above the amplitude, and the peak samples reach it exactly.
      CHECK(largest == std::fabs(amplitude));
    }
  }

  TEST_CASE("normalize: a new peak comes out at the amplitude, a quieter sample below it "
            "(#1111)") {
    normalize f;
    f.amplitude(0.8f);
    CHECK(f.process(0.25f) == 0.8f); // first sample: the peak
    CHECK(f.peak() == 0.25f);
    CHECK(f.process(-0.5f) == -0.8f); // new peak, with its sign
    CHECK(f.peak() == 0.5f);
    CHECK(f.process(0.25f) == 0.4f); // half the peak
    CHECK(f.process(-0.125f) == -0.2f);
    CHECK(f.process(0.f) == 0.f);
    CHECK(f.peak() == 0.5f); // the peak only rises
  }

  TEST_CASE("normalize: a sine of any level comes out at the amplitude (#1111)") {
    for (const float level : {0.001f, 0.1f, 1.f, 30.f}) {
      CAPTURE(level);
      normalize f;
      f.amplitude(0.5f);
      buffer in(N);
      // The first cycle (period 64) sets the peak; from then on the output
      // is the sine at 0.5.
      for (unsigned int block = 0; block < 2; ++block) {
        for (unsigned int i = 0; i < N; ++i) {
          const auto n = static_cast<float>(block * N + i);
          in.getPtr()[i] = level * std::sin(n * 2.f * static_cast<float>(PI) / 64.f);
        }
        buffer& out = f(in);
        if (block == 0) continue;
        for (unsigned int i = 0; i < N; ++i) {
          CAPTURE(i);
          const auto n = static_cast<double>(N + i);
          CHECK(std::fabs(out.getPtr()[i] - 0.5 * std::sin(n * 2.0 * PI / 64.0)) <= 1e-5);
        }
      }
      CHECK(std::fabs(f.peak() - level) <= level * 1e-6f);
    }
  }

  TEST_CASE("normalize: the floor of 0.000001 (#1111)") {
    normalize f;
    // Below the floor, the output is the input scaled by 1 / 0.000001.
    CHECK(f.process(0.0000005f) == doctest::Approx(0.5f).epsilon(1e-6));
    CHECK(f.peak() == normalize::MIN_PEAK);
    // A denormal neither moves the peak nor overflows.
    const float tiny = std::numeric_limits<float>::denorm_min();
    CHECK(std::isfinite(f.process(tiny)));
    CHECK(f.peak() == normalize::MIN_PEAK);
  }

  TEST_CASE("normalize: peak() sets the peak, reset() starts over (#1111)") {
    normalize f;
    f.peak(2.f);
    CHECK(f.peak() == 2.f);
    CHECK(f.process(1.f) == 0.5f);
    CHECK(f.process(4.f) == 1.f); // a higher sample raises it again
    CHECK(f.peak() == 4.f);
    f.peak(-0.5f); // the magnitude
    CHECK(f.peak() == 0.5f);
    CHECK(f.process(0.25f) == 0.5f);
    f.peak(0.f); // raised to the floor
    CHECK(f.peak() == normalize::MIN_PEAK);
    f.peak(std::numeric_limits<float>::quiet_NaN());
    f.peak(std::numeric_limits<float>::infinity());
    CHECK(f.peak() == normalize::MIN_PEAK);
    f.peak(3.f).amplitude(0.5f);
    f.reset();
    CHECK(f.peak() == normalize::MIN_PEAK);
    CHECK(f.amplitude() == 0.5f); // reset() keeps the amplitude
    CHECK(f.process(0.2f) == 0.5f);
  }

  TEST_CASE("normalize: non-finite settings are ignored, non-finite input gives 0 (#1111)") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    normalize f;
    f.amplitude(0.5f);
    f.amplitude(nan);
    f.amplitude(inf);
    f.amplitude(-inf);
    CHECK(f.amplitude() == 0.5f);
    CHECK(f.process(0.5f) == 0.5f);
    CHECK(f.process(nan) == 0.f);
    CHECK(f.process(inf) == 0.f);
    CHECK(f.process(-inf) == 0.f);
    CHECK(f.peak() == 0.5f); // untouched
    CHECK(f.process(0.25f) == 0.25f);
  }

  TEST_CASE("normalize: the amplitude read per sample from a signal (#1111)") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    normalize f;
    buffer in(N);
    buffer a(N);
    for (unsigned int i = 0; i < N; ++i) {
      in.getPtr()[i] = 0.5f;
      a.getPtr()[i] = static_cast<float>(i) / 64.f;
    }
    a.getPtr()[100] = nan; // keeps the value before
    buffer& out = f(in, a);
    REQUIRE(out.getLength() == N);
    for (unsigned int i = 0; i < N; ++i) {
      CAPTURE(i);
      const float expected = i == 100 ? 99.f / 64.f : static_cast<float>(i) / 64.f;
      CHECK(out.getPtr()[i] == expected);
    }
    CHECK(f.amplitude() == 127.f / 64.f);
    // Unequal lengths: as long as the shorter.
    buffer shortA(8);
    shortA = 2.f;
    CHECK(f(in, shortA).getLength() == 8);
  }

  TEST_CASE("normalize: per-sample, per-block and split-block calls agree (#1111)") {
    const unsigned int splits[] = {128, 64, 32, 13, 1};
    const std::vector<float> input = Input(4 * N);
    std::vector<float> expected(input.size());
    {
      normalize f;
      f.amplitude(0.7f);
      for (std::size_t n = 0; n < input.size(); ++n)
        expected[n] = f.process(input[n]);
    }
    {
      normalize f;
      f.amplitude(0.7f);
      buffer in(N);
      for (unsigned int block = 0; block < 4; ++block) {
        for (unsigned int i = 0; i < N; ++i)
          in.getPtr()[i] = input[block * N + i];
        buffer& out = f(in);
        for (unsigned int i = 0; i < N; ++i)
          CHECK(out.getPtr()[i] == expected[block * N + i]);
      }
    }
    for (unsigned int split : splits) {
      CAPTURE(split);
      normalize f;
      f.amplitude(0.7f);
      buffer in(split);
      unsigned int n = 0;
      while (n + split <= input.size()) {
        for (unsigned int i = 0; i < split; ++i)
          in.getPtr()[i] = input[n + i];
        buffer& out = f(in);
        REQUIRE(out.getLength() == split);
        for (unsigned int i = 0; i < split; ++i) {
          CAPTURE(n + i);
          CHECK(out.getPtr()[i] == expected[n + i]);
        }
        n += split;
      }
    }
  }

  TEST_CASE("normalize: the output does not depend on the sample rate (#1111)") {
    std::vector<float> first;
    for (const UInt rate : {44100u, 48000u, 96000u}) {
      CAPTURE(rate);
      TestHelpers::ScopedSampleRate scoped(rate);
      normalize f;
      buffer in(N);
      for (unsigned int i = 0; i < N; ++i)
        in.getPtr()[i] = Swell(i);
      buffer& out = f(in);
      if (first.empty()) {
        first.assign(out.getPtr(), out.getPtr() + N);
        continue;
      }
      for (unsigned int i = 0; i < N; ++i)
        CHECK(out.getPtr()[i] == first[i]);
    }
  }

  TEST_CASE("normalize: a longer block is processed up to STANDARD_BUFFERSIZE (#1111)") {
    normalize f;
    buffer shortIn(16);
    shortIn = 0.5f;
    CHECK(f(shortIn).getLength() == 16);
    buffer longIn(N * 2);
    longIn = 0.5f;
    CHECK(f(longIn).getLength() == N);
  }

  TEST_CASE("normalize: processing allocates nothing (#1111)") {
    normalize f;
    buffer in(N);
    buffer shortIn(32);
    buffer a(N);
    in = 0.3f;
    shortIn = 0.3f;
    a = 0.5f;
    f(in);
    f(in, a);
    {
      TestHelpers::ProbeScope probe;
      f(in);
      f(shortIn);
      f(in, a);
      f.process(0.5f);
      f.amplitude(2.f).peak(1.f).reset();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
