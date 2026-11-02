// Tests for YSE::DSP::movingAverage (YseEngine/dsp/analysis/movingAverage.hpp),
// issue #1118. The reference is closed form: output n is the mean of f(x[k])
// for k in n-N+1 .. n, with x[k] = 0 before the start and for NaN / infinite
// samples, f the identity, |x| or x * x, and the square root taken in RMS mode.
// The accumulation since the last report (issue #1119) is the same mean over
// every sample since the last take().

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/analysis/movingAverage.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

using YSE::DSP::buffer;
using YSE::DSP::movingAverage;

namespace {

  constexpr UInt N = YSE::STANDARD_BUFFERSIZE;

  double F(float x, movingAverage::MODE mode) {
    const double d = std::isfinite(x) ? x : 0.0;
    if (mode == movingAverage::ABSOLUTE) return std::fabs(d);
    if (mode == movingAverage::RMS) return d * d;
    return d;
  }

  // The brute-force average at every sample of `x`, window `len`.
  std::vector<float> Reference(const std::vector<float>& x, UInt len, movingAverage::MODE mode) {
    std::vector<float> out(x.size());
    for (std::size_t n = 0; n < x.size(); ++n) {
      double total = 0.0;
      for (UInt k = 0; k < len && k <= n; ++k)
        total += F(x[n - k], mode);
      const double mean = total / len;
      out[n] = static_cast<float>(mode == movingAverage::RMS ? std::sqrt(mean) : mean);
    }
    return out;
  }

  // Runs `x` through `d` in blocks of `block` samples.
  std::vector<float> Blocks(movingAverage& d, const std::vector<float>& x, UInt block) {
    std::vector<float> out;
    for (std::size_t start = 0; start < x.size(); start += block) {
      const auto len = static_cast<UInt>(std::min<std::size_t>(block, x.size() - start));
      buffer b(len);
      for (UInt i = 0; i < len; ++i)
        b.getPtr()[i] = x[start + i];
      buffer& o = d(b);
      REQUIRE(o.getLength() == len);
      REQUIRE(&o == &d.output());
      for (UInt i = 0; i < len; ++i)
        out.push_back(o.getPtr()[i]);
    }
    return out;
  }

  // A deterministic signal with DC, two tones and a little noise-like jitter.
  std::vector<float> Signal(std::size_t length) {
    std::vector<float> x(length);
    unsigned int seed = 12345u;
    for (std::size_t n = 0; n < length; ++n) {
      seed = seed * 1103515245u + 12345u;
      const float jitter = static_cast<float>((seed >> 16) & 0x7fff) / 32768.f - 0.5f;
      x[n] = 0.1f + 0.6f * std::sin(0.031f * n) + 0.2f * std::sin(0.57f * n) + 0.1f * jitter;
    }
    return x;
  }

  void CheckClose(const std::vector<float>& got, const std::vector<float>& want, float eps) {
    REQUIRE(got.size() == want.size());
    for (std::size_t i = 0; i < got.size(); ++i) {
      if (std::fabs(got[i] - want[i]) > eps) {
        INFO("sample " << i << ": " << got[i] << " vs " << want[i]);
        CHECK(std::fabs(got[i] - want[i]) <= eps);
        return;
      }
    }
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("movingAverage: defaults, capacity clamp and setters (#1118)") {
    movingAverage d;
    CHECK(d.capacity() == movingAverage::DEFAULT_CAPACITY);
    CHECK(d.length() == movingAverage::DEFAULT_CAPACITY);
    CHECK(d.mode() == movingAverage::BIPOLAR);
    CHECK(d.value() == 0.f);

    CHECK(movingAverage(0).capacity() == 1);
    CHECK(movingAverage(movingAverage::MAXIMUM_CAPACITY + 5).capacity() ==
          movingAverage::MAXIMUM_CAPACITY);

    movingAverage e(10);
    e.length(0);
    CHECK(e.length() == 1);
    e.length(11);
    CHECK(e.length() == 10);
    e.length(4);
    CHECK(e.length() == 4);
    e.mode(movingAverage::RMS);
    CHECK(e.mode() == movingAverage::RMS);
    e.mode(static_cast<movingAverage::MODE>(7));
    CHECK(e.mode() == movingAverage::BIPOLAR);
  }

  TEST_CASE("movingAverage: an impulse is a box of height 1/N (#1118)") {
    movingAverage d(4);
    CHECK(d.process(1.f) == 0.25f);
    CHECK(d.process(0.f) == 0.25f);
    CHECK(d.process(0.f) == 0.25f);
    CHECK(d.process(0.f) == 0.25f);
    CHECK(d.process(0.f) == 0.f);
    CHECK(d.value() == 0.f);
  }

  TEST_CASE("movingAverage: a step ramps up over N samples, the missing ones count as 0 (#1118)") {
    movingAverage d(8);
    for (UInt i = 1; i <= 8; ++i)
      CHECK(d.process(1.f) == doctest::Approx(i / 8.0));
    CHECK(d.process(1.f) == 1.f);
  }

  TEST_CASE("movingAverage: the three modes on a +-1 square and a sine (#1118)") {
    // A square of +-0.5: bipolar 0, absolute 0.5, rms 0.5 over an even window.
    for (auto mode : {movingAverage::BIPOLAR, movingAverage::ABSOLUTE, movingAverage::RMS}) {
      movingAverage d(64);
      d.mode(mode);
      float last = 0.f;
      for (int i = 0; i < 256; ++i)
        last = d.process((i % 2 == 0) ? 0.5f : -0.5f);
      CHECK(last == doctest::Approx(mode == movingAverage::BIPOLAR ? 0.0 : 0.5).epsilon(1e-6));
    }
    // A sine with a whole number of cycles in the window: mean 0, mean
    // magnitude 2A/pi, rms A/sqrt(2).
    const double pi = 3.14159265358979323846;
    const UInt period = 50;
    const UInt window = 400; // eight cycles
    const double amplitude = 0.8;
    for (auto mode : {movingAverage::BIPOLAR, movingAverage::ABSOLUTE, movingAverage::RMS}) {
      movingAverage d(window);
      d.mode(mode);
      float last = 0.f;
      for (UInt n = 0; n < 3 * window; ++n)
        last = d.process(static_cast<float>(amplitude * std::sin(2.0 * pi * n / period)));
      double want = 0.0;
      if (mode == movingAverage::ABSOLUTE) want = 2.0 * amplitude / pi;
      if (mode == movingAverage::RMS) want = amplitude / std::sqrt(2.0);
      CHECK(last == doctest::Approx(want).epsilon(2e-3));
      if (mode == movingAverage::BIPOLAR) CHECK(std::fabs(last) < 1e-5f);
    }
  }

  TEST_CASE("movingAverage: matches the brute-force reference across block splits (#1118)") {
    const std::vector<float> x = Signal(5 * N + 37);
    for (auto mode : {movingAverage::BIPOLAR, movingAverage::ABSOLUTE, movingAverage::RMS}) {
      for (UInt len : {1u, 3u, 77u, 100u, 300u}) {
        const std::vector<float> want = Reference(x, len, mode);
        for (UInt block : {N, 64u, 32u, 13u, 1u}) {
          INFO("mode " << mode << " length " << len << " block " << block);
          movingAverage d(300);
          d.mode(mode).length(len);
          CheckClose(Blocks(d, x, block), want, 1e-5f);
        }
        movingAverage single(300);
        single.mode(mode).length(len);
        std::vector<float> got;
        got.reserve(x.size());
        for (float s : x)
          got.push_back(single.process(s));
        CheckClose(got, want, 1e-5f);
      }
    }
  }

  TEST_CASE("movingAverage: NaN and infinities count as 0 and do not poison the sum (#1118)") {
    std::vector<float> x = Signal(3 * N);
    x[10] = std::numeric_limits<float>::quiet_NaN();
    x[200] = std::numeric_limits<float>::infinity();
    x[201] = -std::numeric_limits<float>::infinity();
    for (auto mode : {movingAverage::BIPOLAR, movingAverage::ABSOLUTE, movingAverage::RMS}) {
      movingAverage d(50);
      d.mode(mode);
      const std::vector<float> got = Blocks(d, x, N);
      for (float v : got)
        CHECK(std::isfinite(v));
      CheckClose(got, Reference(x, 50, mode), 1e-5f);
    }
  }

  TEST_CASE("movingAverage: rounding does not drift over a long run (#1118)") {
    // Samples from 1e6 down to tiny ones: each add and remove on the running sum
    // rounds, but the output stays on the reference after a million samples,
    // and a return to silence reads exactly 0 rather than a rounding residue.
    for (auto mode : {movingAverage::BIPOLAR, movingAverage::ABSOLUTE, movingAverage::RMS}) {
      CAPTURE(mode);
      movingAverage d(1000);
      d.mode(mode);
      unsigned int seed = 777u;
      for (int n = 0; n < 1000000; ++n) {
        seed = seed * 1103515245u + 12345u;
        const float jitter = static_cast<float>((seed >> 8) & 0xffff) / 65536.f - 0.5f;
        d.process(1.0e7f * jitter * jitter * jitter);
      }
      float last = 0.f;
      for (int n = 0; n < 1000; ++n)
        last = d.process(0.f);
      CHECK(last == 0.f);
    }
  }

  TEST_CASE("movingAverage: a new length or mode covers the samples already seen (#1118)") {
    constexpr UInt H = 2 * N; // more than the capacity
    const std::vector<float> x = Signal(4 * N);
    movingAverage d(200);
    std::vector<float> head(x.begin(), x.begin() + H);
    Blocks(d, head, N);
    // Shorter window: the last 20 samples.
    d.length(20);
    double total = 0.0;
    for (UInt k = 0; k < 20; ++k)
      total += x[H - 1 - k];
    CHECK(d.value() == doctest::Approx(total / 20.0).epsilon(1e-5));
    // Back up to 150: the window reaches back past the shorter one.
    d.length(150);
    total = 0.0;
    for (UInt k = 0; k < 150; ++k)
      total += x[H - 1 - k];
    CHECK(d.value() == doctest::Approx(total / 150.0).epsilon(1e-5));
    // RMS of the same 150.
    d.mode(movingAverage::RMS);
    total = 0.0;
    for (UInt k = 0; k < 150; ++k)
      total += static_cast<double>(x[H - 1 - k]) * x[H - 1 - k];
    CHECK(d.value() == doctest::Approx(std::sqrt(total / 150.0)).epsilon(1e-5));
    // And processing goes on against the reference for the full signal.
    std::vector<float> tail(x.begin() + H, x.end());
    const std::vector<float> got = Blocks(d, tail, 17);
    const std::vector<float> want = Reference(x, 150, movingAverage::RMS);
    CheckClose(got, std::vector<float>(want.begin() + H, want.end()), 1e-5f);
  }

  TEST_CASE("movingAverage: reset() forgets every sample and keeps the settings (#1118)") {
    movingAverage d(16);
    d.mode(movingAverage::ABSOLUTE).length(8);
    for (int i = 0; i < 40; ++i)
      d.process(-1.f);
    CHECK(d.value() == 1.f);
    d.reset();
    CHECK(d.value() == 0.f);
    CHECK(d.length() == 8);
    CHECK(d.mode() == movingAverage::ABSOLUTE);
    CHECK(d.process(-1.f) == 0.125f);
  }

  TEST_CASE("movingAverage: a block longer than STANDARD_BUFFERSIZE is clamped (#1118)") {
    movingAverage d(4);
    buffer b(N + 8);
    b = 1.f;
    buffer& o = d(b);
    CHECK(o.getLength() == N);
    CHECK(o.getPtr()[N - 1] == 1.f);
  }

  TEST_CASE("movingAverage: take() reports the average since the last report, in each mode "
            "(#1119)") {
    const std::vector<float> x = Signal(1000);
    const movingAverage::MODE modes[] = {movingAverage::BIPOLAR, movingAverage::ABSOLUTE,
                                         movingAverage::RMS};
    for (const movingAverage::MODE mode : modes) {
      CAPTURE(mode);
      movingAverage d(10);
      d.mode(mode);
      CHECK(d.accumulated() == 0.f); // empty: 0
      CHECK(d.take() == 0.f);
      // Reports after 0, 1, 2 .. samples: windows of growing length.
      std::size_t at = 0;
      for (std::size_t span = 1; at + span <= x.size(); at += span, ++span) {
        double total = 0.0;
        for (std::size_t k = at; k < at + span; ++k) {
          d.process(x[k]);
          total += F(x[k], mode);
        }
        const double mean = total / static_cast<double>(span);
        const auto want = static_cast<float>(mode == movingAverage::RMS ? std::sqrt(mean) : mean);
        CHECK(d.accumulatedSamples() == static_cast<double>(span));
        CHECK(d.accumulated() == doctest::Approx(want).epsilon(1e-6));
        CHECK(d.take() == doctest::Approx(want).epsilon(1e-6));
        CHECK(d.accumulatedSamples() == 0.0);
      }
    }
  }

  TEST_CASE("movingAverage: accumulate() feeds only the accumulation, the same as process() "
            "(#1119)") {
    const std::vector<float> x = Signal(5 * N);
    movingAverage full(64);
    movingAverage only(64);
    full.mode(movingAverage::ABSOLUTE);
    only.mode(movingAverage::ABSOLUTE);
    const UInt splits[] = {N, 64, 13, 1};
    for (const UInt block : splits) {
      CAPTURE(block);
      Blocks(full, x, block);
      for (std::size_t start = 0; start < x.size(); start += block) {
        const auto len = static_cast<UInt>(std::min<std::size_t>(block, x.size() - start));
        buffer b(len);
        for (UInt i = 0; i < len; ++i)
          b.getPtr()[i] = x[start + i];
        only.accumulate(b);
      }
      CHECK(only.accumulatedSamples() == full.accumulatedSamples());
      CHECK(only.take() == doctest::Approx(full.take()).epsilon(1e-6));
    }
    // The window never saw a sample.
    CHECK(only.value() == 0.f);
    CHECK(full.value() > 0.f);
  }

  TEST_CASE("movingAverage: the accumulation skips NaN and infinities, and survives a long run "
            "(#1119)") {
    movingAverage d(1);
    d.mode(movingAverage::ABSOLUTE);
    buffer b(4);
    b.getPtr()[0] = std::numeric_limits<float>::quiet_NaN();
    b.getPtr()[1] = std::numeric_limits<float>::infinity();
    b.getPtr()[2] = -std::numeric_limits<float>::infinity();
    b.getPtr()[3] = -2.f;
    d.accumulate(b);
    CHECK(d.take() == 0.5f); // 2 over four samples

    // Ten million samples of 0.1 and one of 1000: the mean stays exact to
    // float precision.
    buffer tenth(N);
    tenth = 0.1f;
    const UInt blocks = 10000000 / N;
    for (UInt i = 0; i < blocks; ++i)
      d.accumulate(tenth);
    d.process(1000.f);
    const double n = static_cast<double>(blocks) * N + 1.0;
    const double want = (static_cast<double>(blocks) * N * static_cast<double>(0.1f) + 1000.0) / n;
    CHECK(d.take() == doctest::Approx(want).epsilon(1e-6));
  }

  TEST_CASE("movingAverage: a new mode and reset() start the accumulation over; a new length "
            "does not (#1119)") {
    movingAverage d(8);
    d.process(-1.f);
    d.process(3.f);
    d.length(2);
    CHECK(d.accumulated() == 1.f); // bipolar: (-1 + 3) / 2
    d.mode(movingAverage::ABSOLUTE);
    CHECK(d.accumulatedSamples() == 0.0);
    CHECK(d.accumulated() == 0.f);
    d.process(-1.f);
    d.process(3.f);
    CHECK(d.accumulated() == 2.f);
    d.mode(movingAverage::ABSOLUTE); // the same mode: nothing changes
    CHECK(d.accumulated() == 2.f);
    d.reset();
    CHECK(d.accumulatedSamples() == 0.0);
    CHECK(d.take() == 0.f);
  }

  TEST_CASE("movingAverage: processing and the setters allocate nothing (#1118)") {
    movingAverage d(1000);
    buffer b(N);
    for (UInt i = 0; i < N; ++i)
      b.getPtr()[i] = static_cast<float>(i) / N;
    buffer shortBlock(13);
    shortBlock = 0.5f;
    {
      TestHelpers::ProbeScope probe;
      d(b);
      d(shortBlock);
      d(b);
      d.process(0.25f);
      d.length(10);
      d.mode(movingAverage::RMS);
      d.length(1000);
      d.length(5000);
      d.mode(movingAverage::ABSOLUTE);
      d(b);
      d.reset();
      d(b);
      d.accumulate(b);
      d.accumulate(shortBlock);
      d.accumulated();
      d.take();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(d.value() > 0.f);
  }

} // TEST_SUITE("dsp")
