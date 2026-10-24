// Tests for YSE::DSP::updown (YseEngine/dsp/generators/updown.hpp), issue
// #1070. Reference output is closed form: for a 0..1 ramp of period P samples
// starting at 0, sample n of a cycle is
// level * min(n / upSamples, (P - 1 - n) / downSamples, 1), however long P is.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>
#include "dsp/generators/updown.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::updown;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // `samples` samples of `fn` run block by block, the input at sample n
  // being `input(n)`.
  std::vector<float> Run(updown& fn, unsigned int samples,
                         const std::function<float(unsigned int)>& input) {
    std::vector<float> result;
    buffer in(N);
    for (unsigned int start = 0; start < samples; start += N) {
      for (unsigned int i = 0; i < N; ++i)
        in.getPtr()[i] = input(start + i);
      buffer& out = fn(in);
      REQUIRE(out.getLength() == N);
      for (unsigned int i = 0; i < N && start + i < samples; ++i)
        result.push_back(out.getPtr()[i]);
    }
    return result;
  }

  // A 0..1 ramp of period `period`, exact in float for a power of two.
  std::function<float(unsigned int)> Ramp(unsigned int period) {
    return [period](unsigned int n) {
      return static_cast<float>(n % period) / static_cast<float>(period);
    };
  }

  float Trapezoid(unsigned int n, unsigned int period, float upSamples, float downSamples) {
    const float rise = static_cast<float>(n % period) / upSamples;
    const float fall = static_cast<float>(period - 1 - n % period) / downSamples;
    return std::min({rise, fall, 1.f});
  }

  // The largest change between two neighbouring samples.
  float LargestStep(const std::vector<float>& out) {
    float largest = 0.f;
    for (std::size_t i = 1; i < out.size(); ++i)
      largest = std::max(largest, std::fabs(out[i] - out[i - 1]));
    return largest;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("updown: defaults and setters (#1070)") {
    updown fn;
    CHECK(fn.up() == updown::DEFAULT_UP);
    CHECK(fn.down() == updown::DEFAULT_DOWN);
    CHECK(fn.level() == 1.f);
    CHECK(fn.resetThreshold() == 0.5f);
    CHECK(fn.envelope() == 0.f);

    fn.up(20.f).down(30.f).level(-2.f).resetThreshold(0.25f);
    CHECK(fn.up() == 20.f);
    CHECK(fn.down() == 30.f);
    CHECK(fn.level() == -2.f);
    CHECK(fn.resetThreshold() == 0.25f);

    // Not finite: ignored. Negative times and thresholds: 0.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    fn.up(nan).down(inf).level(nan).resetThreshold(-inf);
    CHECK(fn.up() == 20.f);
    CHECK(fn.down() == 30.f);
    CHECK(fn.level() == -2.f);
    CHECK(fn.resetThreshold() == 0.25f);
    fn.up(-1.f).down(-5.f).resetThreshold(-0.1f);
    CHECK(fn.up() == 0.f);
    CHECK(fn.down() == 0.f);
    CHECK(fn.resetThreshold() == 0.f);

    // A silent input gives silence.
    std::vector<float> out = Run(fn, N, [](unsigned int) { return 0.f; });
    for (float v : out)
      CHECK(v == 0.f);
  }

  TEST_CASE("updown: a rising ramp gives the closed-form trapezoid, whatever its period "
            "(#1070)") {
    TestHelpers::ScopedSampleRate rate(48000);
    // 2 ms up = 96 samples, 1 ms down = 48 samples.
    for (unsigned int period : {256u, 512u, 2048u}) {
      CAPTURE(period);
      updown fn;
      fn.up(2.f).down(1.f).level(0.5f);
      std::vector<float> out = Run(fn, period * 3, Ramp(period));
      for (unsigned int n = 0; n < out.size(); ++n) {
        CAPTURE(n);
        CHECK(out[n] == doctest::Approx(0.5f * Trapezoid(n, period, 96.f, 48.f)).epsilon(1e-5));
      }
    }
  }

  TEST_CASE("updown: a falling ramp rises after its reset and ends at 0 where it reaches 0 "
            "(#1070)") {
    TestHelpers::ScopedSampleRate rate(48000);
    constexpr unsigned int period = 512;
    updown fn;
    fn.up(2.f).down(1.f);
    // 1, (P-1)/P, ..., 1/P, then back to 1: a falling ramp never reads 0, so
    // the rise starts on the reset sample itself.
    std::vector<float> out = Run(fn, period * 3, [](unsigned int n) {
      return static_cast<float>(period - n % period) / static_cast<float>(period);
    });
    for (unsigned int n = 0; n < out.size(); ++n) {
      CAPTURE(n);
      const unsigned int k = n % period;
      const float expected = std::min(
          {static_cast<float>(k + 1) / 96.f, static_cast<float>(period - 1 - k) / 48.f, 1.f});
      CHECK(out[n] == doctest::Approx(expected).epsilon(1e-5));
    }
  }

  TEST_CASE("updown: a ramp that never reads 0 still falls to near 0 before each reset and "
            "never jumps (#1070)") {
    TestHelpers::ScopedSampleRate rate(48000);
    constexpr unsigned int period = 1024;
    updown fn;
    fn.up(2.f).down(1.f);
    std::vector<float> out = Run(fn, period * 4, [](unsigned int n) {
      return (static_cast<float>(n % period) + 0.5f) / static_cast<float>(period);
    });
    for (unsigned int cycle = 1; cycle < 4; ++cycle) {
      CAPTURE(cycle);
      CHECK(out[cycle * period - 1] <= 1.f / 48.f + 1e-6f);
      CHECK(out[cycle * period - 300] == doctest::Approx(1.f));
    }
    CHECK(LargestStep(out) <= 1.f / 48.f + 1e-6f);
  }

  TEST_CASE("updown: a gate rises and falls at constant rates, from wherever it is (#1070)") {
    TestHelpers::ScopedSampleRate rate(48000);
    updown fn;
    fn.up(2.f).down(1.f); // 96 and 48 samples
    // Open for 200 samples, closed for 30 (half the fall), open again.
    std::vector<float> out = Run(fn, 512, [](unsigned int n) {
      if (n < 200) return 1.f;
      if (n < 230) return 0.f;
      return 0.3f; // any non-zero value: a gate has no slope
    });
    for (unsigned int n = 0; n < 96; ++n) {
      CAPTURE(n);
      CHECK(out[n] == doctest::Approx(static_cast<float>(n + 1) / 96.f).epsilon(1e-5));
    }
    for (unsigned int n = 96; n < 200; ++n)
      CHECK(out[n] == 1.f);
    for (unsigned int n = 200; n < 230; ++n) {
      CAPTURE(n);
      CHECK(out[n] == doctest::Approx(1.f - static_cast<float>(n - 199) / 48.f).epsilon(1e-5));
    }
    // Reopened half-way down: it rises from there, no jump.
    const float from = 1.f - 30.f / 48.f;
    for (unsigned int n = 230; n < 260; ++n) {
      CAPTURE(n);
      const float expected = std::min(1.f, from + static_cast<float>(n - 229) / 96.f);
      CHECK(out[n] == doctest::Approx(expected).epsilon(1e-5));
    }
    CHECK(out[511] == 1.f);
    CHECK(LargestStep(out) <= 1.f / 48.f + 1e-6f);
  }

  TEST_CASE("updown: a ramp that resets early does not click (#1070)") {
    TestHelpers::ScopedSampleRate rate(48000);
    updown fn;
    fn.up(2.f).down(1.f);
    // A 0..1 ramp of 1024 samples, cut short at 0.5 and restarted.
    std::vector<float> out = Run(fn, 2048, [](unsigned int n) {
      const unsigned int k = n < 512 ? n : n - 512;
      return static_cast<float>(k % 1024) / 1024.f;
    });
    CHECK(out[511] == 1.f);
    CHECK(LargestStep(out) <= 1.f / 48.f + 1e-6f);
  }

  TEST_CASE("updown: restart drops the envelope so the rise begins again (#1070)") {
    TestHelpers::ScopedSampleRate rate(48000);
    updown fn;
    fn.up(2.f).down(1.f);
    std::vector<float> out = Run(fn, 256, [](unsigned int) { return 1.f; });
    CHECK(out.back() == 1.f);
    fn.restart();
    CHECK(fn.envelope() == 0.f);
    out = Run(fn, N, [](unsigned int) { return 1.f; });
    CHECK(out[0] == doctest::Approx(1.f / 96.f));
    CHECK(out[95] == doctest::Approx(1.f));
  }

  TEST_CASE("updown: times follow the live sample rate (#1070)") {
    TestHelpers::ScopedSampleRate rate(48000);
    updown fn;
    fn.up(2.f);
    std::vector<float> out = Run(fn, N, [](unsigned int) { return 1.f; });
    CHECK(out[0] == doctest::Approx(1.f / 96.f));
    fn.restart();
    YSE::SAMPLERATE = 96000;
    out = Run(fn, N, [](unsigned int) { return 1.f; });
    CHECK(out[0] == doctest::Approx(1.f / 192.f));
    CHECK(out[N - 1] == doctest::Approx(128.f / 192.f));
  }

  TEST_CASE("updown: zero times are instant edges (#1070)") {
    TestHelpers::ScopedSampleRate rate(48000);
    constexpr unsigned int period = 256;
    updown fn;
    fn.up(0.f).down(0.f).level(3.f);
    std::vector<float> out = Run(fn, period * 2, Ramp(period));
    for (unsigned int n = 0; n < out.size(); ++n) {
      CAPTURE(n);
      const unsigned int k = n % period;
      // 0 on the ramp's first (0) and last sample, the level in between.
      CHECK(out[n] == (k == 0 || k == period - 1 ? 0.f : 3.f));
    }
  }

  TEST_CASE("updown: the reset threshold decides which jumps restart the rise (#1070)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto input = [](unsigned int n) { return n < 200 ? 0.2f : 0.9f; };
    // A jump of 0.7 with the default threshold: a reset, the gate holds.
    updown fn;
    fn.up(2.f).down(1.f);
    std::vector<float> out = Run(fn, 256, input);
    CHECK(out[199] == 1.f);
    CHECK(out[200] == 1.f);
    // Above the threshold of 1 it is a slope: the ramp looks finished, so the
    // envelope starts to fall, then holds again once the input is flat.
    updown slow;
    slow.up(2.f).down(1.f).resetThreshold(1.f);
    out = Run(slow, 256, input);
    CHECK(out[199] == 1.f);
    CHECK(out[200] == doctest::Approx(1.f - 1.f / 48.f));
    CHECK(out[255] == 1.f);
  }

  TEST_CASE("updown: an input that is not a finite number counts as 0 (#1070)") {
    TestHelpers::ScopedSampleRate rate(48000);
    updown fn;
    fn.up(0.f).down(0.f);
    std::vector<float> out = Run(fn, N, [](unsigned int n) {
      if (n == 10) return std::numeric_limits<float>::quiet_NaN();
      if (n == 20) return std::numeric_limits<float>::infinity();
      return 1.f;
    });
    CHECK(out[9] == 1.f);
    CHECK(out[10] == 0.f);
    CHECK(out[11] == 1.f);
    CHECK(out[20] == 0.f);
    CHECK(out[21] == 1.f);
  }

  TEST_CASE("updown: processing and setters allocate nothing (#1070)") {
    updown fn;
    buffer in(N);
    in = 1.f;
    fn(in);
    TestHelpers::ProbeScope probe;
    fn.up(5.f).down(5.f).level(0.5f).resetThreshold(0.4f).restart();
    fn(in);
    CHECK(TestHelpers::g_alloc_count.load() == 0);
  }

} // TEST_SUITE("dsp")
