// Tests for YSE::DSP::peakTracker (YseEngine/dsp/analysis/peakTracker.hpp),
// issue #1126. Reference output is closed form: with an interval of P samples
// (P >= 1), a report falls on sample n when floor((n + 1) / P) > floor(n / P),
// and gives max |x| over the samples since the previous report (NaN skipped),
// or in signed mode the first sample reaching that magnitude.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/analysis/peakTracker.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::DSP::buffer;
using YSE::DSP::peakTracker;

namespace {

  constexpr UInt N = YSE::STANDARD_BUFFERSIZE;
  constexpr UInt Quarter = N / 4;
  constexpr UInt Half = N / 2;

  struct Report {
    UInt at;
    float peak;
  };

  std::vector<Report> Reference(const std::vector<float>& x, double period, bool keepSign) {
    std::vector<Report> out;
    float best = 0.f;
    float bestAbs = 0.f;
    for (UInt n = 0; n < x.size(); ++n) {
      if (!std::isnan(x[n]) && std::fabs(x[n]) > bestAbs) {
        bestAbs = std::fabs(x[n]);
        best = x[n];
      }
      if (std::floor((n + 1.0) / period) > std::floor(n / period)) {
        out.push_back({n, keepSign ? best : bestAbs});
        best = 0.f;
        bestAbs = 0.f;
      }
    }
    return out;
  }

  // Runs `x` through `d` in blocks of `block` samples and collects every
  // interval report as a position in the whole signal.
  std::vector<Report> Blocks(peakTracker& d, const std::vector<float>& x, UInt block) {
    std::vector<Report> out;
    for (UInt start = 0; start < x.size(); start += block) {
      const UInt len = std::min<UInt>(block, static_cast<UInt>(x.size()) - start);
      buffer b(len);
      for (UInt i = 0; i < len; ++i)
        b.getPtr()[i] = x[start + i];
      const UInt count = d(b);
      CHECK(count == d.reports());
      for (UInt i = 0; i < d.reports(); ++i)
        out.push_back({start + d.report(i), d.value(i)});
    }
    return out;
  }

  std::vector<Report> PerSample(peakTracker& d, const std::vector<float>& x) {
    std::vector<Report> out;
    for (UInt n = 0; n < x.size(); ++n)
      if (d.process(x[n])) out.push_back({n, d.lastReport()});
    return out;
  }

  void Same(const std::vector<Report>& got, const std::vector<Report>& want) {
    REQUIRE(got.size() == want.size());
    for (std::size_t i = 0; i < got.size(); ++i) {
      CAPTURE(i);
      CHECK(got[i].at == want[i].at);
      CHECK(got[i].peak == want[i].peak);
    }
  }

  // A deterministic signal of mixed signs and magnitudes, with some NaN.
  std::vector<float> Mixed(UInt length) {
    std::vector<float> x(length);
    UInt state = 12345u;
    for (UInt n = 0; n < length; ++n) {
      state = state * 1103515245u + 12345u;
      x[n] = static_cast<float>(static_cast<int>((state >> 8) % 2001u) - 1000) / 1000.f;
      if (n % 37u == 5u) x[n] = std::numeric_limits<float>::quiet_NaN();
    }
    return x;
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("peakTracker: take() gives the largest magnitude and starts over (#1126)") {
    peakTracker d;
    CHECK(d.peak() == 0.f);
    CHECK(d.take() == 0.f); // an empty window
    CHECK(d.lastReport() == 0.f);
    CHECK_FALSE(d.process(0.25f));
    CHECK_FALSE(d.process(-0.75f));
    CHECK_FALSE(d.process(0.5f));
    CHECK(d.peak() == 0.75f); // a magnitude
    CHECK(d.take() == 0.75f);
    CHECK(d.lastReport() == 0.75f);
    CHECK(d.peak() == 0.f);
    d.process(0.125f);
    CHECK(d.take() == 0.125f); // the old peak is gone
    CHECK(d.take() == 0.f);
  }

  TEST_CASE("peakTracker: signed mode keeps the sign; the first of equal magnitudes wins "
            "(#1126)") {
    peakTracker d;
    d.signedPeak(true);
    CHECK(d.signedPeak());
    d.process(0.5f);
    d.process(-0.8f);
    d.process(0.3f);
    CHECK(d.peak() == -0.8f);
    CHECK(d.take() == -0.8f);
    d.process(-0.6f);
    d.process(0.6f);
    CHECK(d.take() == -0.6f);
    d.process(0.6f);
    d.process(-0.6f);
    CHECK(d.take() == 0.6f);

    // Switching modes reads the same window either way.
    d.process(-0.9f);
    d.signedPeak(false);
    CHECK(d.peak() == 0.9f);
    d.signedPeak(true);
    CHECK(d.peak() == -0.9f);
  }

  TEST_CASE("peakTracker: NaN is skipped, -0 is 0, an infinity is a peak (#1126)") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    peakTracker d;
    d.process(nan);
    CHECK(d.take() == 0.f);
    d.process(0.4f);
    d.process(nan);
    CHECK(d.take() == 0.4f);
    d.process(-0.f);
    CHECK(d.take() == 0.f);
    d.process(0.2f);
    d.process(-inf);
    CHECK(d.take() == inf);
    d.signedPeak(true);
    d.process(-inf);
    CHECK(d.take() == -inf);
    d.process(1e-40f); // a denormal is a peak too
    CHECK(d.take() == 1e-40f);
  }

  TEST_CASE("peakTracker: an interval reports the peak of each window, sample-accurate "
            "(#1126)") {
    ScopedSampleRate rate(1000); // one sample per millisecond
    peakTracker d;
    d.interval(10.f);
    CHECK(d.interval() == 10.f);
    buffer b(N);
    for (UInt i = 0; i < N; ++i)
      b.getPtr()[i] = static_cast<float>(i % 10u) / 10.f * ((i % 2u) == 0 ? 1.f : -1.f);
    b.getPtr()[3] = -2.f;
    b.getPtr()[10] = 1.5f; // the first sample of the second window
    REQUIRE(d(b) == N / 10);
    CHECK(d.report(0) == 9);
    CHECK(d.value(0) == 2.f);
    CHECK(d.report(1) == 19);
    CHECK(d.value(1) == 1.5f);
    CHECK(d.report(2) == 29);
    CHECK(d.value(2) == 0.9f);
    CHECK(d.lastReport() == d.value(N / 10 - 1));
    // 128 = 12 * 10 + 8: the last 8 samples are in the running window.
    CHECK(d.peak() == 0.7f);

    // The window runs across the block edge: two more samples end it.
    buffer c(N);
    c = 0.f;
    c.getPtr()[2] = 5.f; // after the report at 1: the next window
    REQUIRE(d(c) == 13);
    CHECK(d.report(0) == 1);
    CHECK(d.value(0) == 0.7f);
    CHECK(d.report(1) == 11);
    CHECK(d.value(1) == 5.f);
    CHECK(d.value(2) == 0.f);
  }

  TEST_CASE("peakTracker: a fractional period keeps to the interval on average (#1126)") {
    ScopedSampleRate rate(1000);
    peakTracker d;
    d.interval(2.5f);
    buffer b(N);
    b = 1.f;
    REQUIRE(d(b) == 51); // floor(128 / 2.5)
    const UInt want[] = {2, 4, 7, 9, 12, 14, 17, 19};
    for (UInt i = 0; i < 8; ++i) {
      CAPTURE(i);
      CHECK(d.report(i) == want[i]);
    }
  }

  TEST_CASE("peakTracker: block splits and per sample match the reference (#1126)") {
    ScopedSampleRate rate(1000);
    const std::vector<float> x = Mixed(N * 8);
    for (const float ms : {1.f, 2.5f, 7.f, 10.f, 64.f, 300.f}) {
      for (const bool keepSign : {false, true}) {
        CAPTURE(ms);
        CAPTURE(keepSign);
        const std::vector<Report> want = Reference(x, ms, keepSign);
        for (const UInt block : {N, Half, Quarter, 13u, 1u}) {
          CAPTURE(block);
          peakTracker d;
          d.interval(ms).signedPeak(keepSign);
          Same(Blocks(d, x, block), want);
        }
        peakTracker s;
        s.interval(ms).signedPeak(keepSign);
        Same(PerSample(s, x), want);
      }
    }
  }

  TEST_CASE("peakTracker: a sine reports its amplitude every window (#1126)") {
    ScopedSampleRate rate(48000);
    peakTracker d;
    d.interval(1.f); // 48 samples: four cycles of 12 samples
    std::vector<float> x(N * 4);
    for (UInt n = 0; n < x.size(); ++n)
      x[n] = 0.7f * static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * n / 12.0));
    const std::vector<Report> got = Blocks(d, x, N);
    REQUIRE(got.size() == N * 4 / 48);
    for (std::size_t i = 0; i < got.size(); ++i) {
      CAPTURE(i);
      CHECK(got[i].at == 47 + 48 * i);
      CHECK(got[i].peak == doctest::Approx(0.7).epsilon(1e-6));
    }
  }

  TEST_CASE("peakTracker: follows the live sample rate (#1126)") {
    peakTracker d;
    d.interval(1.f);
    buffer b(N);
    b = 0.5f;
    {
      ScopedSampleRate rate(32000); // 32 samples
      REQUIRE(d(b) == 4);
      CHECK(d.report(0) == 31);
    }
    d.reset();
    {
      ScopedSampleRate rate(64000); // 64 samples
      REQUIRE(d(b) == 2);
      CHECK(d.report(0) == 63);
    }
  }

  TEST_CASE("peakTracker: interval settings, take() and reset() (#1126)") {
    ScopedSampleRate rate(1000);
    peakTracker d;
    buffer b(N);
    b = 0.5f;
    CHECK(d(b) == 0); // no interval: only take() reports
    CHECK(d.reports() == 0);
    CHECK(d.peak() == 0.5f);

    d.interval(-3.f);
    CHECK(d.interval() == 0.f);
    d.interval(std::numeric_limits<float>::quiet_NaN());
    CHECK(d.interval() == 0.f);
    d.interval(0.25f); // below one sample: every sample
    CHECK(d(b) == N);
    CHECK(d.value(N - 1) == 0.5f);

    // take() counts the interval from here.
    d.interval(10.f);
    d.process(0.f);
    d.process(0.f);
    d.process(0.f);
    d.take();
    REQUIRE(d(b) == 12);
    CHECK(d.report(0) == 9);

    // A new interval counts from here; the same value does not restart.
    d.interval(10.f);
    REQUIRE(d(b) == 13);
    CHECK(d.report(0) == 1); // 8 samples were carried
    d.interval(20.f);
    REQUIRE(d(b) == 6);
    CHECK(d.report(0) == 19);

    // reset() keeps the interval and the mode, and clears the rest.
    d.signedPeak(true);
    d.process(-0.9f);
    d.reset();
    CHECK(d.peak() == 0.f);
    CHECK(d.lastReport() == 0.f);
    CHECK(d.reports() == 0);
    CHECK(d.interval() == 20.f);
    CHECK(d.signedPeak());
    b = -0.25f;
    REQUIRE(d(b) == 6);
    CHECK(d.report(0) == 19);
    CHECK(d.value(0) == -0.25f);
  }

  TEST_CASE("peakTracker: a longer block is processed up to STANDARD_BUFFERSIZE (#1126)") {
    ScopedSampleRate rate(1000);
    peakTracker d;
    buffer b(N * 2);
    b = 0.f;
    b.getPtr()[N - 1] = 0.5f;
    b.getPtr()[N + 1] = 1.f; // past the clamp: never seen
    d(b);
    CHECK(d.peak() == 0.5f);
    d.interval(static_cast<float>(N));
    REQUIRE(d(b) == 1);
    CHECK(d.report(0) == N - 1);
  }

  TEST_CASE("peakTracker: processing allocates nothing (#1126)") {
    ScopedSampleRate rate(1000);
    peakTracker d;
    d.interval(3.f);
    buffer b(N);
    for (UInt i = 0; i < N; ++i)
      b.getPtr()[i] = static_cast<float>(i) / N;
    {
      TestHelpers::ProbeScope probe;
      d(b);
      d.process(0.5f);
      d.take();
      d.interval(5.f);
      d.signedPeak(true);
      d.reset();
      d(b);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(d.reports() > 0);
  }

} // TEST_SUITE("dsp")
