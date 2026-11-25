// Tests for YSE::DSP::scopeCapture (YseEngine/dsp/analysis/scopeCapture.hpp),
// issue #1169. Reference output is closed form: fed the sample index n as the
// signal, with no trigger, trace k starts at k * (size * period + delay) and
// its point j holds the minimum start + j * period and the maximum one period
// later minus one. Triggers are checked on a sawtooth of whole numbers, whose
// crossings fall on known samples, and the sine against its own samples.

#include <doctest/doctest.h>
#include <atomic>
#include <cmath>
#include <limits>
#include <thread>
#include <vector>
#include "dsp/analysis/scopeCapture.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::DSP::buffer;
using YSE::DSP::scopeCapture;

namespace {

  constexpr UInt N = YSE::STANDARD_BUFFERSIZE;

  // A trace copied out of the capture.
  struct Trace {
    scopeCapture::MODE mode = scopeCapture::WAVEFORM;
    UInt serial = 0;
    std::vector<float> first;
    std::vector<float> second;
  };

  Trace Copy(const scopeCapture::trace& t) {
    Trace out;
    out.mode = t.mode;
    out.serial = t.serial;
    out.first.assign(t.first, t.first + t.size);
    out.second.assign(t.second, t.second + t.size);
    return out;
  }

  // Feeds `total` samples of f(n) through the capture in blocks of `block`,
  // acquiring after every block, and returns every trace seen, in order. The
  // caller keeps at most one trace finishing per block, so none is skipped.
  template <typename F>
  std::vector<Trace> Run(scopeCapture& scope, F f, unsigned int total, unsigned int block,
                         bool xy = false) {
    std::vector<Trace> seen;
    unsigned int n = 0;
    while (n < total) {
      const unsigned int length = (total - n) < block ? (total - n) : block;
      buffer x(length);
      buffer y(length);
      for (unsigned int i = 0; i < length; ++i) {
        x.getPtr()[i] = f(n + i);
        y.getPtr()[i] = -f(n + i);
      }
      if (xy)
        scope(x, y);
      else
        scope(x);
      n += length;
      const scopeCapture::trace t = scope.acquire();
      if (t.serial != 0 && (seen.empty() || seen.back().serial != t.serial)) {
        REQUIRE(t.serial == seen.size() + 1); // none skipped
        seen.push_back(Copy(t));
      }
    }
    return seen;
  }

  float Index(unsigned int n) {
    return static_cast<float>(n);
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("scopeCapture: defaults, and no trace before the first one finishes (#1169)") {
    scopeCapture scope;
    CHECK(scope.size() == 128);
    CHECK(scope.period() == 256);
    CHECK(scope.delay() == 0.f);
    CHECK(scope.trigger() == scopeCapture::NONE);
    CHECK(scope.level() == 0.f);
    CHECK(scope.capacity() == scopeCapture::MAX_SIZE);
    CHECK(scope.traces() == 0);

    scopeCapture::trace t = scope.acquire();
    CHECK(t.serial == 0);
    CHECK(t.size == 0);
    CHECK(t.first == nullptr);
    CHECK(t.second == nullptr);

    // 128 * 256 samples make the first trace; one sample short is none.
    buffer in(N);
    in = 0.25f;
    for (unsigned int b = 0; b < 255; ++b)
      scope(in);
    buffer last(N - 1);
    last = 0.25f;
    scope(last);
    CHECK(scope.traces() == 0);
    CHECK(scope.acquire().serial == 0);
    buffer one(1);
    one = 0.25f;
    scope(one);
    CHECK(scope.traces() == 1);
    t = scope.acquire();
    CHECK(t.serial == 1);
    CHECK(t.mode == scopeCapture::WAVEFORM);
    REQUIRE(t.size == 128);
    for (UInt i = 0; i < t.size; ++i) {
      CHECK(t.first[i] == 0.25f);
      CHECK(t.second[i] == 0.25f);
    }

    // A small capacity holds the size to it.
    scopeCapture small(16);
    CHECK(small.capacity() == 16);
    CHECK(small.size() == 16);
    CHECK(scopeCapture(0).capacity() == scopeCapture::MIN_SIZE);
    CHECK(scopeCapture(100000).capacity() == scopeCapture::MAX_SIZE);
  }

  TEST_CASE("scopeCapture: waveform points are the minimum and maximum of each period, "
            "sample-accurate across blocks (#1169)") {
    // Fed the index, point j of trace k is [start + j * P, start + j * P + P - 1].
    const UInt S = 4;
    const UInt P = 48; // 192 samples a trace: at most one per block
    for (unsigned int block : {128u, 64u, 13u, 1u}) {
      CAPTURE(block);
      scopeCapture scope;
      scope.size(S).period(P);
      const std::vector<Trace> seen = Run(scope, Index, 5 * S * P + 7, block);
      REQUIRE(seen.size() == 5);
      for (UInt k = 0; k < 5; ++k) {
        CAPTURE(k);
        CHECK(seen[k].mode == scopeCapture::WAVEFORM);
        REQUIRE(seen[k].first.size() == S);
        for (UInt j = 0; j < S; ++j) {
          const float start = static_cast<float>(k * S * P + j * P);
          CHECK(seen[k].first[j] == start);
          CHECK(seen[k].second[j] == start + static_cast<float>(P - 1));
        }
      }
    }

    // A falling signal: the minimum is the last sample of each period.
    scopeCapture down;
    down.size(2).period(3);
    const std::vector<Trace> seen =
        Run(down, [](unsigned int n) { return -static_cast<float>(n); }, 6, 1);
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].first == std::vector<float>{-2.f, -5.f});
    CHECK(seen[0].second == std::vector<float>{0.f, -3.f});
  }

  TEST_CASE("scopeCapture: a sine's points bracket its samples and reach its peaks (#1169)") {
    ScopedSampleRate rate(48000);
    const double f = 375.0; // 128 samples a cycle
    auto sine = [f](unsigned int n) {
      return static_cast<float>(0.8 * std::sin(2.0 * 3.14159265358979323846 * f * n / 48000.0));
    };
    scopeCapture scope;
    scope.size(16).period(32); // a trace is four cycles, two blocks at 256 samples
    const std::vector<Trace> seen = Run(scope, sine, 512, N);
    REQUIRE(seen.size() == 1);
    float top = -1.f;
    float bottom = 1.f;
    for (UInt j = 0; j < 16; ++j) {
      float low = sine(j * 32);
      float high = low;
      for (UInt i = 1; i < 32; ++i) {
        low = std::fmin(low, sine(j * 32 + i));
        high = std::fmax(high, sine(j * 32 + i));
      }
      CHECK(seen[0].first[j] == low);
      CHECK(seen[0].second[j] == high);
      top = std::fmax(top, high);
      bottom = std::fmin(bottom, low);
    }
    CHECK(top == doctest::Approx(0.8f).epsilon(1e-4));
    CHECK(bottom == doctest::Approx(-0.8f).epsilon(1e-4));
  }

  TEST_CASE("scopeCapture: X/Y points are the first X sample of each period and its Y "
            "(#1169)") {
    for (unsigned int block : {128u, 13u, 1u}) {
      CAPTURE(block);
      scopeCapture scope;
      scope.size(4).period(40);
      const std::vector<Trace> seen = Run(scope, Index, 3 * 160, block, true);
      REQUIRE(seen.size() == 3);
      for (UInt k = 0; k < 3; ++k) {
        CHECK(seen[k].mode == scopeCapture::XY);
        for (UInt j = 0; j < 4; ++j) {
          const float x = static_cast<float>(k * 160 + j * 40);
          CHECK(seen[k].first[j] == x);
          CHECK(seen[k].second[j] == -x);
        }
      }
    }
  }

  TEST_CASE("scopeCapture: the up and down triggers start a trace on the crossing sample "
            "(#1169)") {
    // A sawtooth of whole numbers 0..99: rising to 75 at n % 100 == 75, falling
    // to or below 50 at n % 100 == 0 (not at n = 0, which has no sample before
    // it).
    auto saw = [](unsigned int n) { return static_cast<float>(n % 100); };

    SUBCASE("up") {
      scopeCapture scope;
      scope.size(2).period(5).trigger(scopeCapture::UP).level(75.f);
      const std::vector<Trace> seen = Run(scope, saw, 400, 1, true);
      REQUIRE(seen.size() == 4);
      for (const Trace& t : seen) {
        CHECK(t.first == std::vector<float>{75.f, 80.f});
      }
    }
    SUBCASE("up, level exactly reached from below, not re-triggered above it") {
      scopeCapture scope;
      scope.size(2).period(2).trigger(scopeCapture::UP).level(10.f);
      // 0..99: 10 triggers once a cycle; the samples after the trace stay above.
      const std::vector<Trace> seen = Run(scope, saw, 300, 1);
      REQUIRE(seen.size() == 3);
      for (const Trace& t : seen) {
        CHECK(t.first == std::vector<float>{10.f, 12.f});
        CHECK(t.second == std::vector<float>{11.f, 13.f});
      }
    }
    SUBCASE("down") {
      scopeCapture scope;
      scope.size(3).period(2).trigger(scopeCapture::DOWN).level(50.f);
      const std::vector<Trace> seen = Run(scope, saw, 350, 1, true);
      REQUIRE(seen.size() == 3); // at 100, 200 and 300
      for (const Trace& t : seen) {
        CHECK(t.first == std::vector<float>{0.f, 2.f, 4.f});
      }
    }
    SUBCASE("no trigger: the next sample after a trace starts the next one") {
      scopeCapture scope;
      scope.size(2).period(5).level(75.f);
      const std::vector<Trace> seen = Run(scope, saw, 30, 1, true);
      REQUIRE(seen.size() == 3);
      CHECK(seen[0].first == std::vector<float>{0.f, 5.f});
      CHECK(seen[1].first == std::vector<float>{10.f, 15.f});
      CHECK(seen[2].first == std::vector<float>{20.f, 25.f});
    }
    SUBCASE("the first sample never triggers") {
      // A constant 0 sits past both levels from its first sample on, but no
      // sample before it was on the other side.
      auto zero = [](unsigned int) { return 0.f; };
      scopeCapture down;
      down.size(2).period(2).trigger(scopeCapture::DOWN).level(50.f);
      CHECK(Run(down, zero, 99, 1).empty());
      scopeCapture up;
      up.size(2).period(2).trigger(scopeCapture::UP).level(-1.f);
      CHECK(Run(up, zero, 99, 1).empty());
    }
  }

  TEST_CASE("scopeCapture: the delay between traces follows the live sample rate (#1169)") {
    // Trace k starts at k * (S * P + D): D samples of delay, then no trigger.
    struct Row {
      UInt rate;
      float ms;
      UInt samples;
    };
    for (const Row& row : {Row{1000, 10.f, 10}, Row{2000, 10.f, 20}, Row{3000, 0.5f, 2},
                           Row{44100, 1.f, 44}, Row{48000, 0.f, 0}}) {
      CAPTURE(row.rate);
      CAPTURE(row.ms);
      ScopedSampleRate rate(row.rate);
      scopeCapture scope;
      scope.size(2).period(30).delay(row.ms);
      const UInt length = 60 + row.samples;
      const std::vector<Trace> seen = Run(scope, Index, 4 * length, 1);
      REQUIRE(seen.size() >= 3);
      for (UInt k = 0; k < 3; ++k) {
        CHECK(seen[k].first[0] == static_cast<float>(k * length));
        CHECK(seen[k].first[1] == static_cast<float>(k * length + 30));
      }
    }

    // The rate is read when the wait begins, so a change applies from the next one.
    ScopedSampleRate rate(1000);
    scopeCapture scope;
    scope.size(2).period(2).delay(5.f);
    std::vector<Trace> seen = Run(scope, Index, 4, 1); // trace 1: 0..3, wait 5
    REQUIRE(seen.size() == 1);
    YSE::SAMPLERATE = 2000;
    buffer in(1);
    for (unsigned int n = 4; n < 40; ++n) {
      in.getPtr()[0] = static_cast<float>(n);
      scope(in);
    }
    // Trace 2 starts at 4 + 5 = 9 and ends at 12; the wait of 10 makes 23 the
    // start of trace 3.
    const scopeCapture::trace t = scope.acquire();
    REQUIRE(t.serial == 3);
    CHECK(t.first[0] == 23.f);
  }

  TEST_CASE("scopeCapture: settings are clamped, NaN is ignored or counts as 0 (#1169)") {
    scopeCapture scope(64);
    scope.size(1);
    CHECK(scope.size() == 2);
    scope.size(65);
    CHECK(scope.size() == 64);
    scope.period(0);
    CHECK(scope.period() == 2);
    scope.period(100000);
    CHECK(scope.period() == scopeCapture::MAX_PERIOD);
    scope.delay(-5.f);
    CHECK(scope.delay() == 0.f);
    scope.delay(7.f).delay(std::nanf(""));
    CHECK(scope.delay() == 7.f);
    scope.level(0.5f).level(std::nanf(""));
    CHECK(scope.level() == 0.5f);

    CHECK(scopeCapture::Size(std::nanf("")) == scopeCapture::DEFAULT_SIZE);
    CHECK(scopeCapture::Size(100.9f) == 100);
    CHECK(scopeCapture::Size(-3.f) == scopeCapture::MIN_SIZE);
    CHECK(scopeCapture::Size(1e9f) == scopeCapture::MAX_SIZE);
    CHECK(scopeCapture::Period(std::nanf("")) == scopeCapture::DEFAULT_PERIOD);
    CHECK(scopeCapture::Period(64.5f) == 64);
    CHECK(scopeCapture::Period(1.f) == scopeCapture::MIN_PERIOD);
    CHECK(scopeCapture::Period(1e9f) == scopeCapture::MAX_PERIOD);
    CHECK(scopeCapture::Trigger(0.f) == scopeCapture::NONE);
    CHECK(scopeCapture::Trigger(1.f) == scopeCapture::UP);
    CHECK(scopeCapture::Trigger(1.7f) == scopeCapture::UP);
    CHECK(scopeCapture::Trigger(2.f) == scopeCapture::DOWN);
    CHECK(scopeCapture::Trigger(3.f) == scopeCapture::NONE);
    CHECK(scopeCapture::Trigger(-1.f) == scopeCapture::NONE);
    CHECK(scopeCapture::Trigger(std::nanf("")) == scopeCapture::NONE);

    // NaN samples count as 0; infinities are kept.
    scopeCapture odd;
    odd.size(2).period(2);
    const float values[] = {std::nanf(""), 3.f, std::numeric_limits<float>::infinity(), -1.f};
    const std::vector<Trace> seen = Run(odd, [&values](unsigned int n) { return values[n]; }, 4, 1);
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].first[0] == 0.f);
    CHECK(seen[0].second[0] == 3.f);
    CHECK(seen[0].first[1] == -1.f);
    CHECK(std::isinf(seen[0].second[1]));
  }

  TEST_CASE("scopeCapture: changing the size, the period or the mode drops the trace in "
            "progress (#1169)") {
    buffer in(1);
    auto feed = [&in](scopeCapture& scope, unsigned int from, unsigned int to) {
      for (unsigned int n = from; n < to; ++n) {
        in.getPtr()[0] = static_cast<float>(n);
        scope(in);
      }
    };

    SUBCASE("size") {
      scopeCapture scope;
      scope.size(4).period(2);
      feed(scope, 0, 5);
      scope.size(4); // no change keeps it
      feed(scope, 5, 6);
      scope.size(3);
      feed(scope, 6, 12);
      const scopeCapture::trace t = scope.acquire();
      REQUIRE(t.serial == 1);
      CHECK(t.size == 3);
      CHECK(t.first[0] == 6.f);
      CHECK(t.first[2] == 10.f);
    }
    SUBCASE("period") {
      scopeCapture scope;
      scope.size(2).period(4);
      feed(scope, 0, 7);
      scope.period(2);
      feed(scope, 7, 11);
      const scopeCapture::trace t = scope.acquire();
      REQUIRE(t.serial == 1);
      CHECK(t.first[0] == 7.f);
      CHECK(t.second[1] == 10.f);
    }
    SUBCASE("mode") {
      scopeCapture scope;
      scope.size(2).period(2);
      feed(scope, 0, 3);
      buffer x(4);
      buffer y(4);
      for (unsigned int i = 0; i < 4; ++i) {
        x.getPtr()[i] = static_cast<float>(10 + i);
        y.getPtr()[i] = static_cast<float>(20 + i);
      }
      scope(x, y);
      const scopeCapture::trace t = scope.acquire();
      REQUIRE(t.serial == 1);
      CHECK(t.mode == scopeCapture::XY);
      CHECK(t.first[0] == 10.f);
      CHECK(t.second[0] == 20.f);
      CHECK(t.first[1] == 12.f);
      CHECK(t.second[1] == 22.f);
    }
    SUBCASE("restart, and it skips the delay") {
      ScopedSampleRate rate(1000);
      scopeCapture scope;
      scope.size(2).period(2).delay(100.f);
      feed(scope, 0, 4); // trace 1, then a wait of 100 samples
      scope.restart();
      feed(scope, 4, 8);
      const scopeCapture::trace t = scope.acquire();
      REQUIRE(t.serial == 2);
      CHECK(t.first[0] == 4.f);
    }
  }

  TEST_CASE("scopeCapture: the held trace stays put while newer ones finish; acquire takes "
            "the newest (#1169)") {
    scopeCapture scope;
    scope.size(2).period(2);
    buffer in(4);
    auto trace = [&in, &scope](float value) {
      in = value;
      scope(in); // one trace of `value`
    };
    trace(1.f);
    const scopeCapture::trace first = scope.acquire();
    REQUIRE(first.serial == 1);
    CHECK(scope.acquire().first == first.first); // nothing newer: the same one

    // Three more finish; the one held is never written.
    trace(2.f);
    trace(3.f);
    trace(4.f);
    CHECK(scope.traces() == 4);
    CHECK(first.first[0] == 1.f);
    CHECK(first.second[1] == 1.f);
    CHECK(scope.held().serial == 1);

    const scopeCapture::trace newest = scope.acquire();
    CHECK(newest.serial == 4);
    CHECK(newest.first[0] == 4.f);
    CHECK(newest.first != first.first);
    CHECK(scope.held().serial == 4);
  }

  TEST_CASE("scopeCapture: a reader on another thread only ever sees whole traces (#1169)") {
    // Every block is one trace of the block's number, so a trace whose
    // values are not all its serial was torn.
    scopeCapture scope;
    scope.size(4).period(32);
    std::atomic<bool> done{false};
    std::atomic<unsigned int> torn{0};
    std::atomic<unsigned int> read{0};
    // Capturing starts once the reader reads, and the reader reads once more
    // after capturing is done: it takes a trace at least once, however the
    // threads are scheduled (#1292).
    std::atomic<bool> reading{false};
    std::thread reader([&] {
      UInt last = 0;
      for (bool finished = false; !finished;) {
        finished = done.load(std::memory_order_acquire);
        reading.store(true, std::memory_order_release);
        const scopeCapture::trace t = scope.acquire();
        if (t.serial == 0) continue;
        if (t.serial < last) torn.fetch_add(1);
        last = t.serial;
        for (UInt i = 0; i < t.size; ++i) {
          if (t.first[i] != static_cast<float>(t.serial) ||
              t.second[i] != static_cast<float>(t.serial))
            torn.fetch_add(1);
        }
        read.fetch_add(1);
      }
    });
    buffer in(N);
    while (!reading.load(std::memory_order_acquire))
      std::this_thread::yield();
    for (unsigned int k = 1; k <= 20000; ++k) {
      in = static_cast<float>(k);
      scope(in);
    }
    done.store(true, std::memory_order_release);
    reader.join();
    CHECK(torn.load() == 0);
    CHECK(read.load() > 0);
    CHECK(scope.traces() == 20000);
    CHECK(scope.acquire().serial == 20000);
  }

  TEST_CASE("scopeCapture: capturing, the setters and reading allocate nothing (#1169)") {
    ScopedSampleRate rate(1000);
    scopeCapture scope;
    buffer in(N);
    in = 0.5f;
    buffer y(N);
    y = -0.5f;
    scope.size(4).period(8);
    scope(in);
    {
      TestHelpers::ProbeScope probe;
      scope.size(8).period(16).delay(3.f).trigger(scopeCapture::UP).level(0.25f);
      scope(in);
      scope.trigger(scopeCapture::NONE);
      scope(in);
      scope(in, y);
      scope.restart();
      scope(in);
      const scopeCapture::trace t = scope.acquire();
      (void)scope.held();
      (void)scope.traces();
      CHECK(t.serial > 0);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
