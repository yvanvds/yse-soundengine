// Tests for YSE::DSP::spikeDetector (YseEngine/dsp/analysis/spikeDetector.hpp),
// issue #1123. Reference output is closed form: sample n rises when x[n] != 0
// and x[n-1] == 0 (x[-1] = 0); a rise at n is reported when no refractory
// period is running or n - h >= R, where h is the start of the running one
// (the last reported rise or restart()) and R the period in samples; its
// interval is (n - p) * 1000 / rate ms, p the last reported rise, or 0 for
// the first.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/analysis/spikeDetector.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::DSP::buffer;
using YSE::DSP::spikeDetector;

namespace {

  constexpr UInt N = YSE::STANDARD_BUFFERSIZE;
  // Whole-sample fractions of a block, as counts (the divisions are exact).
  constexpr UInt Eighth = N / 8;
  constexpr UInt Quarter = N / 4;
  constexpr UInt Half = N / 2;

  struct Spike {
    UInt at;
    double ms;
  };

  std::vector<Spike> Reference(const std::vector<float>& x, double refractorySamples, double rate) {
    std::vector<Spike> out;
    bool before = false;
    bool holding = false;
    UInt hold = 0;
    UInt last = 0;
    for (UInt n = 0; n < x.size(); ++n) {
      const bool now = x[n] != 0.f;
      if (now && !before && (!holding || static_cast<double>(n - hold) >= refractorySamples)) {
        out.push_back({n, static_cast<double>(n - last) * 1000.0 / rate});
        last = n;
        hold = n;
        holding = true;
      }
      before = now;
    }
    return out;
  }

  // Runs `x` through `d` in blocks of `block` samples and collects every
  // reported rise as a position in the whole signal.
  std::vector<Spike> Blocks(spikeDetector& d, const std::vector<float>& x, UInt block) {
    std::vector<Spike> out;
    for (UInt start = 0; start < x.size(); start += block) {
      const UInt len = std::min<UInt>(block, static_cast<UInt>(x.size()) - start);
      buffer b(len);
      for (UInt i = 0; i < len; ++i)
        b.getPtr()[i] = x[start + i];
      const UInt count = d(b);
      CHECK(count == d.spikes());
      for (UInt i = 0; i < d.spikes(); ++i)
        out.push_back({start + d.spike(i), static_cast<double>(d.interval(i))});
    }
    return out;
  }

  std::vector<Spike> PerSample(spikeDetector& d, const std::vector<float>& x) {
    std::vector<Spike> out;
    for (UInt n = 0; n < x.size(); ++n)
      if (d.process(x[n])) out.push_back({n, static_cast<double>(d.lastInterval())});
    return out;
  }

  void Same(const std::vector<Spike>& got, const std::vector<Spike>& want) {
    REQUIRE(got.size() == want.size());
    for (std::size_t i = 0; i < got.size(); ++i) {
      CAPTURE(i);
      CHECK(got[i].at == want[i].at);
      CHECK(got[i].ms == doctest::Approx(want[i].ms).epsilon(1e-6));
    }
  }

  // A pulse of `width` samples every `period` samples, the first at `first`.
  std::vector<float> Pulses(UInt length, UInt first, UInt period, UInt width = 1) {
    std::vector<float> x(length, 0.f);
    for (UInt i = first; i < length; ++i)
      if ((i - first) % period < width) x[i] = 1.f;
    return x;
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("spikeDetector: per sample, the ms between rises, the first since the start "
            "(#1123)") {
    ScopedSampleRate rate(1000); // one sample per millisecond
    spikeDetector d;
    CHECK(d.lastInterval() == 0.f);
    CHECK_FALSE(d.process(0.f));
    CHECK_FALSE(d.process(0.f));
    CHECK(d.process(0.5f)); // sample 2
    CHECK(d.lastInterval() == doctest::Approx(2.0));
    CHECK_FALSE(d.process(-1.f)); // still non-zero: no rise
    CHECK_FALSE(d.process(0.f));
    CHECK(d.process(3.f)); // sample 5
    CHECK(d.lastInterval() == doctest::Approx(3.0));
    CHECK_FALSE(d.process(0.f));
    CHECK_FALSE(d.process(-0.f)); // -0 is zero
    CHECK(d.lastInterval() == doctest::Approx(3.0)); // kept until the next
  }

  TEST_CASE("spikeDetector: a block reports each rise, its index and interval (#1123)") {
    ScopedSampleRate rate(48000);
    spikeDetector d;
    buffer b(N);
    b = 0.f;
    b.getPtr()[N / 4] = 1.f;
    b.getPtr()[N / 2] = 1.f;
    b.getPtr()[N / 2 + 1] = 1.f; // a two-sample pulse is one rise
    b.getPtr()[N - 1] = 1.f;
    REQUIRE(d(b) == 3);
    REQUIRE(d.spikes() == 3);
    CHECK(d.spike(0) == N / 4);
    CHECK(d.spike(1) == N / 2);
    CHECK(d.spike(2) == N - 1);
    CHECK(d.interval(0) == doctest::Approx(static_cast<double>(Quarter) * 1000.0 / 48000.0));
    CHECK(d.interval(1) == doctest::Approx(static_cast<double>(Quarter) * 1000.0 / 48000.0));
    CHECK(d.interval(2) == doctest::Approx(static_cast<double>(Half - 1) * 1000.0 / 48000.0));
    CHECK(d.lastInterval() == d.interval(2));

    // The next block only has its own: the carried non-zero sample makes a
    // non-zero first sample no rise, and the interval runs across the edge.
    b = 0.f;
    b.getPtr()[0] = 1.f;
    b.getPtr()[N / 8] = 1.f;
    REQUIRE(d(b) == 1);
    CHECK(d.spike(0) == N / 8);
    CHECK(d.interval(0) == doctest::Approx(static_cast<double>(Eighth + 1) * 1000.0 / 48000.0));

    b = 0.f;
    CHECK(d(b) == 0);
    CHECK(d.spikes() == 0);
  }

  TEST_CASE("spikeDetector: a pulse train gives its period across blocks (#1123)") {
    ScopedSampleRate rate(44100);
    spikeDetector d;
    // 441 samples: 10 ms, not a multiple of the block
    const std::vector<float> x = Pulses(N * 40, 7, 441, 3);
    const std::vector<Spike> got = Blocks(d, x, N);
    REQUIRE(got.size() == (N * 40 - 7 + 440) / 441);
    CHECK(got[0].ms == doctest::Approx(7 * 1000.0 / 44100.0));
    for (std::size_t i = 1; i < got.size(); ++i) {
      CAPTURE(i);
      CHECK(got[i].at == 7 + 441 * i);
      CHECK(got[i].ms == doctest::Approx(10.0));
    }
  }

  TEST_CASE("spikeDetector: the refractory period ignores rises sooner than it (#1123)") {
    ScopedSampleRate rate(1000);
    spikeDetector d;
    d.refractory(12.f);
    CHECK(d.refractory() == 12.f);
    // A rise every 5 ms from 5: reported at 5, then the first at least 12 ms
    // later, 20, then 35, 50 ...
    const std::vector<float> x = Pulses(100, 5, 5);
    const std::vector<Spike> got = PerSample(d, x);
    REQUIRE(got.size() == 7);
    CHECK(got[0].at == 5);
    CHECK(got[0].ms == doctest::Approx(5.0));
    for (std::size_t i = 1; i < got.size(); ++i) {
      CAPTURE(i);
      CHECK(got[i].at == 5 + 15 * i);
      CHECK(got[i].ms == doctest::Approx(15.0)); // the interval includes the period
    }

    // A rise exactly one period after the last is reported.
    spikeDetector e;
    e.refractory(15.f);
    const std::vector<Spike> exact = PerSample(e, x);
    REQUIRE(exact.size() == 7);
    CHECK(exact[1].at == 20);
    CHECK(exact[1].ms == doctest::Approx(15.0));
  }

  TEST_CASE("spikeDetector: restart() starts the refractory period again (#1123)") {
    ScopedSampleRate rate(1000);
    spikeDetector d;
    d.refractory(N / 4.f);
    buffer b(N);
    b = 0.f;
    b.getPtr()[1] = 1.f;
    REQUIRE(d(b) == 1); // no period was running
    b = 0.f;
    REQUIRE(d(b) == 0); // well past it now

    d.restart(); // from the first sample of the next block
    b = 0.f;
    b.getPtr()[3] = 1.f; // ignored
    b.getPtr()[N / 4 - 2] = 1.f; // ignored
    b.getPtr()[N / 4] = 1.f; // exactly one period after the restart
    REQUIRE(d(b) == 1);
    CHECK(d.spike(0) == N / 4);
    CHECK(d.interval(0) ==
          doctest::Approx(2.0 * N + static_cast<double>(Quarter - 1))); // since the last reported

    // With no refractory period restart() changes nothing.
    spikeDetector e;
    e.restart();
    CHECK(e.process(1.f));
  }

  TEST_CASE("spikeDetector: per sample, per block and split blocks agree with the reference "
            "(#1123)") {
    ScopedSampleRate rate(48000);
    std::vector<float> x(N * 64);
    for (UInt i = 0; i < x.size(); ++i)
      x[i] =
          std::sin(static_cast<float>(i) * 0.37f) + std::sin(static_cast<float>(i) * 0.011f) > 0.9f
              ? 1.f
              : 0.f;
    for (float ms : {0.f, 0.3f, 2.f, 7.5f}) {
      CAPTURE(ms);
      const std::vector<Spike> want = Reference(x, ms * 48.0, 48000.0);
      REQUIRE(want.size() > 4);
      spikeDetector a;
      a.refractory(ms);
      Same(PerSample(a, x), want);
      for (UInt block : {N, N / 2, 37u, 1u}) {
        CAPTURE(block);
        spikeDetector b;
        b.refractory(ms);
        Same(Blocks(b, x, block), want);
      }
    }
  }

  TEST_CASE("spikeDetector: follows the live sample rate (#1123)") {
    ScopedSampleRate rate(48000);
    spikeDetector d;
    d.refractory(5.f); // 240 samples at 48 kHz, 480 at 96 kHz
    std::vector<float> x = Pulses(N * 30, 0, 300);
    std::vector<Spike> got = Blocks(d, x, N);
    REQUIRE(got.size() == (N * 30 + 299) / 300);
    CHECK(got[1].ms == doctest::Approx(300 / 48.0));

    YSE::SAMPLERATE = 96000;
    spikeDetector e;
    e.refractory(5.f);
    got = Blocks(e, x, N);
    // 300 samples is now 3.125 ms, shorter than the period: every other rise
    REQUIRE(got.size() == ((N * 30 + 299) / 300 + 1) / 2);
    CHECK(got[1].at == 600);
    CHECK(got[1].ms == doctest::Approx(600 / 96.0));
  }

  TEST_CASE("spikeDetector: reset() and settings (#1123)") {
    ScopedSampleRate rate(1000);
    spikeDetector d;
    d.refractory(-3.f);
    CHECK(d.refractory() == 0.f);
    d.refractory(std::numeric_limits<float>::quiet_NaN());
    CHECK(d.refractory() == 0.f);
    d.refractory(4.f);

    CHECK(d.process(1.f));
    CHECK_FALSE(d.process(0.f));
    CHECK_FALSE(d.process(1.f)); // within the period
    d.reset();
    CHECK(d.refractory() == 4.f); // kept
    CHECK(d.lastInterval() == 0.f);
    CHECK_FALSE(d.process(0.f));
    CHECK(d.process(1.f)); // zero again, no period running, time from 0
    CHECK(d.lastInterval() == doctest::Approx(1.0));

    // An infinite period reports only the first rise.
    spikeDetector e;
    e.refractory(std::numeric_limits<float>::infinity());
    const std::vector<Spike> got = PerSample(e, Pulses(200, 3, 10));
    REQUIRE(got.size() == 1);
    CHECK(got[0].at == 3);

    // Denormals, infinities and NaN are non-zero.
    spikeDetector f;
    CHECK(f.process(std::numeric_limits<float>::denorm_min()));
    CHECK_FALSE(f.process(0.f));
    CHECK(f.process(std::numeric_limits<float>::infinity()));
    CHECK_FALSE(f.process(0.f));
    CHECK(f.process(std::numeric_limits<float>::quiet_NaN()));
  }

  TEST_CASE("spikeDetector: a block longer than the standard size is clamped (#1123)") {
    ScopedSampleRate rate(1000);
    spikeDetector d;
    buffer b(N * 2);
    b = 0.f;
    b.getPtr()[N - 1] = 1.f;
    b.getPtr()[N + 1] = 1.f; // past the clamp: never seen
    REQUIRE(d(b) == 1);
    CHECK(d.spike(0) == N - 1);
    buffer c(N);
    c = 0.f;
    c.getPtr()[2] = 1.f;
    REQUIRE(d(c) == 1);
    CHECK(d.interval(0) == doctest::Approx(3.0)); // time advanced by N, not 2N
  }

  TEST_CASE("spikeDetector: processing allocates nothing (#1123)") {
    spikeDetector d;
    d.refractory(1.f);
    buffer b(N);
    for (UInt i = 0; i < N; ++i)
      b.getPtr()[i] = (i % 3u) == 0 ? 1.f : 0.f;
    {
      TestHelpers::ProbeScope probe;
      d(b);
      d.process(0.f);
      d.process(1.f);
      d.restart();
      d.refractory(0.f);
      d.reset();
      d(b);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(d.spikes() > 0);
  }

} // TEST_SUITE("dsp")
