// Tests for YSE::DSP::levelMeter (YseEngine/dsp/analysis/levelMeter.hpp),
// issue #1166. Reference output is closed form: with an interval of P samples
// (P >= 1) a window ends on sample n when floor((n + 1) / P) > floor(n / P).
// Its value is max |x| (PEAK) or sqrt(mean x^2) (RMS) over its samples, NaN
// skipped. The level then moves toward it by 1 - exp(-T / time), T the window
// in ms, time the attack when rising and the release when falling (0 jumps);
// the held peak keeps the highest level for the hold time.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>
#include "dsp/analysis/levelMeter.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::DSP::buffer;
using YSE::DSP::levelMeter;

namespace {

  constexpr UInt N = YSE::STANDARD_BUFFERSIZE;

  struct Reading {
    UInt at;
    float level;
    float hold;
  };

  struct Settings {
    double period; // samples, at 1 kHz also ms
    bool rms;
    float attack;
    float release;
    float hold;
  };

  // The meter written out sample by sample, in double precision, at 1 kHz.
  std::vector<Reading> Reference(const std::vector<float>& x, const Settings& s) {
    std::vector<Reading> out;
    double peak = 0.0;
    double squares = 0.0;
    int count = 0;
    double level = 0.0;
    double held = 0.0;
    double left = 0.0;
    for (UInt n = 0; n < x.size(); ++n) {
      if (!std::isnan(x[n])) {
        peak = std::max(peak, static_cast<double>(std::fabs(x[n])));
        squares += static_cast<double>(x[n]) * x[n];
        count++;
      }
      if (std::floor((n + 1.0) / s.period) <= std::floor(n / s.period)) continue;
      const double value = s.rms ? (count > 0 ? std::sqrt(squares / count) : 0.0) : peak;
      peak = 0.0;
      squares = 0.0;
      count = 0;
      const double time = value > level ? s.attack : s.release;
      level = time <= 0.0 ? value : level + (value - level) * (1.0 - std::exp(-s.period / time));
      if (level >= held) {
        held = level;
        left = s.hold;
      } else {
        left -= s.period;
        if (left <= 0.0) {
          held = level;
          left = 0.0;
        }
      }
      out.push_back({n, static_cast<float>(level), static_cast<float>(held)});
    }
    return out;
  }

  void Configure(levelMeter& m, const Settings& s) {
    m.interval(static_cast<float>(s.period))
        .detector(s.rms ? levelMeter::RMS : levelMeter::PEAK)
        .attack(s.attack)
        .release(s.release)
        .hold(s.hold);
  }

  // Runs `x` through `m` in blocks of `block` samples; the readings at the
  // end of a window are read per sample, so a block holds one window at most.
  std::vector<Reading> PerSample(levelMeter& m, const std::vector<float>& x) {
    std::vector<Reading> out;
    for (UInt n = 0; n < x.size(); ++n)
      if (m.process(x[n])) out.push_back({n, m.level(), m.peakHold()});
    return out;
  }

  std::vector<UInt> BlockEnds(levelMeter& m, const std::vector<float>& x, UInt block,
                              std::vector<float>& levels) {
    std::vector<UInt> counts;
    for (UInt start = 0; start < x.size(); start += block) {
      const UInt len = std::min<UInt>(block, static_cast<UInt>(x.size()) - start);
      buffer b(len);
      for (UInt i = 0; i < len; ++i)
        b.getPtr()[i] = x[start + i];
      const UInt c = m(b);
      CHECK(c == m.reports());
      counts.push_back(c);
      levels.push_back(m.level());
    }
    return counts;
  }

  void Same(const std::vector<Reading>& got, const std::vector<Reading>& want) {
    REQUIRE(got.size() == want.size());
    for (std::size_t i = 0; i < got.size(); ++i) {
      CAPTURE(i);
      CHECK(got[i].at == want[i].at);
      CHECK(got[i].level == doctest::Approx(want[i].level).epsilon(1e-4));
      CHECK(got[i].hold == doctest::Approx(want[i].hold).epsilon(1e-4));
    }
  }

  std::vector<float> Noise(UInt length, unsigned seed) {
    std::mt19937 gen(seed);
    std::uniform_real_distribution<float> dist(-1.f, 1.f);
    std::vector<float> x(length);
    // Bursts and gaps, so the ballistics rise and fall.
    for (UInt n = 0; n < length; ++n)
      x[n] = ((n / 200) % 2 == 0) ? dist(gen) : 0.05f * dist(gen);
    return x;
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("levelMeter: defaults (#1166)") {
    levelMeter m;
    CHECK(m.detector() == levelMeter::PEAK);
    CHECK(m.interval() == 20.f);
    CHECK(m.attack() == 0.f);
    CHECK(m.release() == 0.f);
    CHECK(m.hold() == 0.f);
    CHECK(m.offset() == 0.f);
    CHECK(m.level() == 0.f);
    CHECK(m.peakHold() == 0.f);
    CHECK(m.levelDb() == levelMeter::SILENCE);
    CHECK(m.peakHoldDb() == levelMeter::SILENCE);
    CHECK_FALSE(m.clipped());
    CHECK(m.reports() == 0);
  }

  TEST_CASE("levelMeter: peak windows end sample-accurately across blocks (#1166)") {
    ScopedSampleRate rate(1000); // one sample per millisecond
    levelMeter m;
    m.interval(50.f);
    buffer b(N);
    b = 0.f;
    b.getPtr()[10] = -0.25f; // window 0-49
    b.getPtr()[49] = 0.125f;
    b.getPtr()[50] = 0.5f; // window 50-99
    b.getPtr()[127] = 0.75f; // window 100-149, ends in the next block
    REQUIRE(m(b) == 2);
    CHECK(m.level() == 0.5f); // the last window that ended
    b = 0.f;
    REQUIRE(m(b) == 3); // 149, 199, 249
    CHECK(m.level() == 0.f);
    CHECK(m.reports() == 3);

    levelMeter n;
    n.interval(50.f);
    for (UInt i = 0; i < 49; ++i)
      CHECK_FALSE(n.process(i == 20 ? -0.75f : 0.f));
    CHECK(n.level() == 0.f); // nothing until the window ends
    REQUIRE(n.process(0.f));
    CHECK(n.level() == 0.75f);
  }

  TEST_CASE("levelMeter: RMS of a sine, a square and a DC level (#1166)") {
    ScopedSampleRate rate(48000);
    levelMeter m;
    m.detector(levelMeter::RMS).interval(10.f); // 480 samples: 10 cycles of 1 kHz
    std::vector<float> x(480 * 4);
    const double pi = std::acos(-1.0);
    for (UInt n = 0; n < x.size(); ++n)
      x[n] = static_cast<float>(0.8 * std::sin(2.0 * pi * 1000.0 * n / 48000.0));
    PerSample(m, x);
    CHECK(m.level() == doctest::Approx(0.8 / std::sqrt(2.0)).epsilon(1e-4));
    CHECK(m.levelDb() == doctest::Approx(20.0 * std::log10(0.8 / std::sqrt(2.0))).epsilon(1e-4));

    for (UInt n = 0; n < x.size(); ++n)
      x[n] = (n / 24) % 2 == 0 ? 0.5f : -0.5f;
    PerSample(m, x);
    CHECK(m.level() == doctest::Approx(0.5f).epsilon(1e-6));

    // The peak detector reads the sine's amplitude instead.
    m.detector(levelMeter::PEAK);
    for (UInt n = 0; n < x.size(); ++n)
      x[n] = static_cast<float>(0.8 * std::sin(2.0 * pi * 1000.0 * n / 48000.0));
    PerSample(m, x);
    CHECK(m.level() == doctest::Approx(0.8f).epsilon(1e-4));
  }

  TEST_CASE("levelMeter: attack and release are one-pole time constants (#1166)") {
    ScopedSampleRate rate(1000);
    levelMeter m;
    m.interval(10.f).attack(100.f).release(400.f);
    // A step to 1: after k windows of 10 ms, 1 - exp(-10 k / 100).
    for (int k = 1; k <= 20; ++k) {
      for (int i = 0; i < 10; ++i)
        m.process(1.f);
      CAPTURE(k);
      CHECK(m.level() == doctest::Approx(1.0 - std::exp(-10.0 * k / 100.0)).epsilon(1e-5));
    }
    const double top = 1.0 - std::exp(-200.0 / 100.0);
    // Then silence: the release from there.
    for (int k = 1; k <= 20; ++k) {
      for (int i = 0; i < 10; ++i)
        m.process(0.f);
      CAPTURE(k);
      CHECK(m.level() == doctest::Approx(top * std::exp(-10.0 * k / 400.0)).epsilon(1e-5));
    }
    // After 100 ms of a steady value the attack has covered 63 %.
    levelMeter n;
    n.interval(100.f).attack(100.f);
    for (int i = 0; i < 100; ++i)
      n.process(0.5f);
    CHECK(n.level() == doctest::Approx(0.5 * (1.0 - std::exp(-1.0))).epsilon(1e-6));
  }

  TEST_CASE("levelMeter: the standard ballistics of Max's levelmeter~ modes (#1166)") {
    struct Row {
      levelMeter::BALLISTICS mode;
      float attack;
      float release;
      float offset;
    };
    const Row rows[] = {
        {levelMeter::FAST, 0.f, 0.f, 0.f},        {levelMeter::VU, 330.f, 330.f, -20.f},
        {levelMeter::DIN, 10.f, 1500.f, -14.f},   {levelMeter::BBC, 10.f, 3000.f, -14.f},
        {levelMeter::NORDIC, 5.f, 2000.f, -14.f}, {levelMeter::EBU, 0.f, 1500.f, -14.f},
    };
    for (const Row& row : rows) {
      CAPTURE(static_cast<int>(row.mode));
      const levelMeter::ballisticsSettings s = levelMeter::settings(row.mode);
      CHECK(s.attack == row.attack);
      CHECK(s.release == row.release);
      CHECK(s.offset == row.offset);
      levelMeter m;
      m.ballistics(row.mode);
      CHECK(m.attack() == row.attack);
      CHECK(m.release() == row.release);
      CHECK(m.offset() == row.offset);
    }
    CHECK(static_cast<int>(levelMeter::EBU) == 5);
  }

  TEST_CASE("levelMeter: the held peak stays for the hold time, then follows the level "
            "(#1166)") {
    ScopedSampleRate rate(1000);
    levelMeter m;
    m.interval(10.f).hold(100.f);
    for (int i = 0; i < 10; ++i)
      m.process(i == 3 ? 0.8f : 0.1f);
    CHECK(m.peakHold() == 0.8f);
    // Nine more windows hold it; the tenth, 100 ms after, drops it.
    for (int k = 1; k <= 9; ++k) {
      for (int i = 0; i < 10; ++i)
        m.process(0.2f);
      CAPTURE(k);
      CHECK(m.level() == 0.2f);
      CHECK(m.peakHold() == 0.8f);
    }
    for (int i = 0; i < 10; ++i)
      m.process(0.2f);
    CHECK(m.peakHold() == 0.2f);
    // Falling, it follows; a rise holds again.
    for (int i = 0; i < 10; ++i)
      m.process(0.1f);
    CHECK(m.peakHold() == 0.1f);
    for (int i = 0; i < 10; ++i)
      m.process(0.4f);
    for (int i = 0; i < 10; ++i)
      m.process(0.f);
    CHECK(m.peakHold() == 0.4f);
    CHECK(m.peakHoldDb() == doctest::Approx(20.0 * std::log10(0.4)).epsilon(1e-5));

    // clear() drops it to the level; hold 0 makes it the level.
    m.clear();
    CHECK(m.peakHold() == 0.f);
    m.hold(0.f);
    for (int i = 0; i < 10; ++i)
      m.process(0.6f);
    for (int i = 0; i < 10; ++i)
      m.process(0.3f);
    CHECK(m.peakHold() == 0.3f);
  }

  TEST_CASE("levelMeter: the clip indicator latches above 1 until clear() (#1166)") {
    ScopedSampleRate rate(1000);
    levelMeter m;
    m.detector(levelMeter::RMS);
    buffer b(N);
    b = 0.f;
    b.getPtr()[5] = 1.f; // 0 dBFS is not a clip
    b.getPtr()[6] = -1.f;
    m(b);
    CHECK_FALSE(m.clipped());
    b.getPtr()[7] = -1.0001f; // whatever the detector
    m(b);
    CHECK(m.clipped());
    b = 0.f;
    m(b);
    CHECK(m.clipped()); // latched
    m.clear();
    CHECK_FALSE(m.clipped());
    m.set(0.5f);
    CHECK_FALSE(m.clipped());
    m.set(-1.5f);
    CHECK(m.clipped());
    m.reset();
    CHECK_FALSE(m.clipped());
  }

  TEST_CASE("levelMeter: set() and setDb() show a value at once (#1166)") {
    ScopedSampleRate rate(1000);
    levelMeter m;
    m.set(-0.5f);
    CHECK(m.level() == 0.5f);
    CHECK(m.peakHold() == 0.5f);
    m.set(std::numeric_limits<float>::quiet_NaN()); // ignored
    CHECK(m.level() == 0.5f);
    m.set(0.f);
    CHECK(m.level() == 0.f);

    // The ballistics apply, over one interval.
    m.interval(10.f).attack(100.f);
    m.set(1.f);
    CHECK(m.level() == doctest::Approx(1.0 - std::exp(-0.1)).epsilon(1e-6));

    // setDb() counts the offset in: -20 is the dBFS that reads 0.
    levelMeter n;
    n.offset(-20.f);
    n.setDb(-6.f); // -26 dBFS
    CHECK(n.level() == doctest::Approx(std::pow(10.0, -26.0 / 20.0)).epsilon(1e-6));
    CHECK(n.levelDb() == doctest::Approx(-6.f).epsilon(1e-5));
    n.setDb(std::numeric_limits<float>::quiet_NaN());
    CHECK(n.levelDb() == doctest::Approx(-6.f).epsilon(1e-5));
    n.setDb(-1000.f);
    CHECK(n.level() == 0.f); // below -200 dB
    CHECK(n.levelDb() == levelMeter::SILENCE);
  }

  TEST_CASE("levelMeter: decibels, the offset, the floor and position() (#1166)") {
    levelMeter m;
    m.set(0.5f);
    CHECK(m.levelDb() == doctest::Approx(20.0 * std::log10(0.5)).epsilon(1e-6));
    m.offset(3.f); // the reference level: subtracted
    CHECK(m.levelDb() == doctest::Approx(20.0 * std::log10(0.5) - 3.0).epsilon(1e-6));
    m.offset(std::numeric_limits<float>::quiet_NaN());
    CHECK(m.offset() == 0.f);
    m.offset(500.f);
    CHECK(m.levelDb() == levelMeter::SILENCE); // never below the floor
    m.set(1e-11f); // below 1e-10: 0
    CHECK(m.level() == 0.f);

    CHECK(levelMeter::position(-30.f, -60.f, 0.f) == 0.5f);
    CHECK(levelMeter::position(0.f, -60.f, 0.f) == 1.f);
    CHECK(levelMeter::position(6.f, -60.f, 0.f) == doctest::Approx(1.1f));
    CHECK(levelMeter::position(-60.f, -60.f, 0.f) == 0.f);
    CHECK(levelMeter::position(-200.f, -60.f, 0.f) == 0.f);
    CHECK(levelMeter::position(-10.f, -40.f, 20.f) == 0.5f);
    CHECK(levelMeter::position(std::numeric_limits<float>::quiet_NaN(), -60.f, 0.f) == 0.f);
    CHECK(levelMeter::position(-30.f, 0.f, 0.f) == 0.f); // an empty scale
    CHECK(levelMeter::position(-30.f, 0.f, -60.f) == 0.f);
  }

  TEST_CASE("levelMeter: NaN is skipped, an infinity jumps (#1166)") {
    ScopedSampleRate rate(1000);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    levelMeter m;
    m.interval(4.f).detector(levelMeter::RMS).release(100.f);
    m.process(nan);
    m.process(0.5f);
    m.process(nan);
    REQUIRE(m.process(0.5f));
    CHECK(m.level() == 0.5f); // the mean of the two numbers
    for (int i = 0; i < 4; ++i)
      m.process(nan);
    // An empty window is 0, reached through the release.
    CHECK(m.level() == doctest::Approx(0.5 * std::exp(-4.0 / 100.0)).epsilon(1e-6));

    m.detector(levelMeter::PEAK);
    for (int i = 0; i < 4; ++i)
      m.process(i == 1 ? -inf : 0.f);
    CHECK(std::isinf(m.level()));
    CHECK(m.clipped());
    for (int i = 0; i < 4; ++i)
      m.process(0.25f);
    CHECK(m.level() == 0.25f); // no distance from infinity to cover: it jumps
    CHECK_FALSE(std::isnan(m.peakHold()));
  }

  TEST_CASE("levelMeter: blocks and samples match a closed-form reference (#1166)") {
    ScopedSampleRate rate(1000);
    const std::vector<float> x = Noise(3000, 1166u);
    const Settings cases[] = {
        {50.0, false, 0.f, 0.f, 0.f},      {20.0, true, 0.f, 0.f, 0.f},
        {20.0, true, 330.f, 330.f, 0.f},   {10.0, false, 10.f, 1500.f, 300.f},
        {12.5, true, 5.f, 2000.f, 1000.f}, {1.0, false, 0.f, 50.f, 25.f},
        {7.25, false, 30.f, 120.f, 100.f},
    };
    for (const Settings& s : cases) {
      CAPTURE(s.period);
      CAPTURE(s.rms);
      CAPTURE(s.attack);
      CAPTURE(s.release);
      CAPTURE(s.hold);
      const std::vector<Reading> want = Reference(x, s);
      levelMeter one;
      Configure(one, s);
      Same(PerSample(one, x), want);

      // Block splits: the windows that end in each block, and the level
      // after it, are the reference's.
      for (UInt block : {N, N / 2, 13u, 1u}) {
        CAPTURE(block);
        levelMeter m;
        Configure(m, s);
        std::vector<float> levels;
        const std::vector<UInt> counts = BlockEnds(m, x, block, levels);
        std::size_t seen = 0;
        for (std::size_t k = 0; k < counts.size(); ++k) {
          seen += counts[k];
          const UInt end = std::min<UInt>(static_cast<UInt>((k + 1) * block), 3000u);
          std::size_t expected = 0;
          while (expected < want.size() && want[expected].at < end)
            expected++;
          REQUIRE(seen == expected);
          const float level = expected == 0 ? 0.f : want[expected - 1].level;
          CHECK(levels[k] == doctest::Approx(level).epsilon(1e-4));
        }
      }
    }
  }

  TEST_CASE("levelMeter: follows the live sample rate (#1166)") {
    levelMeter m;
    m.interval(1.f);
    buffer b(N);
    b = 0.5f;
    {
      ScopedSampleRate rate(32000); // 32 samples
      CHECK(m(b) == 4);
    }
    m.reset();
    {
      ScopedSampleRate rate(64000); // 64 samples
      CHECK(m(b) == 2);
    }
    // The window length in ms, not samples, sets the ballistics.
    levelMeter a;
    a.interval(1.f).attack(10.f);
    {
      ScopedSampleRate rate(64000);
      a(b);
    }
    CHECK(a.level() == doctest::Approx(0.5 * (1.0 - std::exp(-0.2))).epsilon(1e-5));
  }

  TEST_CASE("levelMeter: settings and reset() (#1166)") {
    ScopedSampleRate rate(1000);
    levelMeter m;
    m.interval(-3.f); // below one sample: every sample
    CHECK(m.interval() == 0.f);
    buffer b(N);
    b = 0.25f;
    CHECK(m(b) == N);
    m.interval(std::numeric_limits<float>::quiet_NaN()); // ignored
    CHECK(m.interval() == 0.f);
    m.attack(-1.f).release(std::numeric_limits<float>::quiet_NaN()).hold(-5.f);
    CHECK(m.attack() == 0.f);
    CHECK(m.release() == 0.f);
    CHECK(m.hold() == 0.f);
    m.detector(static_cast<levelMeter::DETECTOR>(7)); // not a detector: PEAK
    CHECK(m.detector() == levelMeter::PEAK);

    // A new interval counts from here; the same value does not restart.
    m.interval(10.f);
    for (int i = 0; i < 3; ++i)
      m.process(0.f);
    m.interval(10.f);
    UInt at = 0;
    while (!m.process(0.f))
      at++;
    CHECK(at == 6);
    m.interval(20.f);
    at = 0;
    while (!m.process(0.f))
      at++;
    CHECK(at == 19);

    // reset() keeps the settings and clears the rest.
    m.detector(levelMeter::RMS).attack(5.f).release(6.f).hold(7.f).offset(-3.f);
    b = 2.f;
    m(b);
    m.reset();
    CHECK(m.level() == 0.f);
    CHECK(m.peakHold() == 0.f);
    CHECK_FALSE(m.clipped());
    CHECK(m.reports() == 0);
    CHECK(m.detector() == levelMeter::RMS);
    CHECK(m.interval() == 20.f);
    CHECK(m.attack() == 5.f);
    CHECK(m.release() == 6.f);
    CHECK(m.hold() == 7.f);
    CHECK(m.offset() == -3.f);
    at = 0;
    while (!m.process(0.f))
      at++;
    CHECK(at == 19); // the window starts over
  }

  TEST_CASE("levelMeter: processing allocates nothing (#1166)") {
    ScopedSampleRate rate(1000);
    levelMeter m;
    m.interval(3.f).attack(10.f).release(20.f).hold(30.f);
    buffer b(N);
    for (UInt i = 0; i < N; ++i)
      b.getPtr()[i] = static_cast<float>(i) / N;
    {
      TestHelpers::ProbeScope probe;
      m(b);
      m.process(0.5f);
      m.set(0.25f);
      m.setDb(-6.f);
      m.detector(levelMeter::RMS);
      m.ballistics(levelMeter::VU);
      m.clear();
      m(b);
      m.reset();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(m.reports() == 0);
  }

} // TEST_SUITE("dsp")
