// Tests for YSE::DSP::lineSegments (YseEngine/dsp/generators/lineSegments.hpp),
// the class behind ~line (issue #1042). Most cases run at 1000 Hz so a
// millisecond is one sample: a segment of N ms reaches its target on its Nth
// sample, and sample k (0-based) of a segment from a to b is at
// a + (b - a) (k + 1) / N.

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/generators/lineSegments.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::DSP::buffer;
using YSE::DSP::lineSegments;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr float NaN = std::numeric_limits<float>::quiet_NaN();
  constexpr float INF = std::numeric_limits<float>::infinity();

  // Sample k (0-based) of a straight segment from `a` to `b` over `n`
  // samples, the closed form written out independently of the class.
  float Segment(float a, float b, unsigned int n, unsigned int k) {
    const double f = static_cast<double>(k + 1) / static_cast<double>(n);
    return static_cast<float>(a + (b - a) * f);
  }

  std::vector<float> Copy(buffer& b) {
    return std::vector<float>(b.getPtr(), b.getPtr() + b.getLength());
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("lineSegments: defaults hold 0 with no list and are not busy (#1042)") {
    ScopedSampleRate rate(1000);
    lineSegments fn;
    CHECK(fn.capacity() == lineSegments::DEFAULT_POINTS);
    CHECK(fn.value() == 0.f);
    CHECK_FALSE(fn.active());
    CHECK_FALSE(fn.paused());
    CHECK(fn.remaining() == 0);
    buffer& out = fn();
    REQUIRE(out.getLength() == N);
    REQUIRE(fn.activity().getLength() == N);
    for (unsigned int i = 0; i < N; ++i) {
      REQUIRE(out.getPtr()[i] == 0.f);
      REQUIRE(fn.activity().getPtr()[i] == 0.f);
    }
    CHECK_FALSE(fn.finished());
  }

  TEST_CASE("lineSegments: one segment against the closed form; activity until it arrives "
            "(#1042)") {
    ScopedSampleRate rate(1000);
    lineSegments fn;
    fn.set(1.f, 100.f);
    CHECK(fn.active());
    buffer& out = fn();
    const float* o = out.getPtr();
    const float* a = fn.activity().getPtr();
    for (unsigned int k = 0; k < 100; ++k)
      REQUIRE(o[k] == doctest::Approx(Segment(0.f, 1.f, 100, k)));
    CHECK(o[99] == 1.f); // exactly the target, on the 100th sample
    for (unsigned int k = 0; k < 99; ++k)
      REQUIRE(a[k] == 1.f);
    for (unsigned int k = 99; k < N; ++k) {
      REQUIRE(a[k] == 0.f);
      REQUIRE(o[k] == 1.f);
    }
    CHECK(fn.finished());
    fn();
    CHECK_FALSE(fn.finished()); // once
  }

  TEST_CASE("lineSegments: a list plays back to back from the current value (#1042)") {
    ScopedSampleRate rate(1000);
    lineSegments fn;
    fn.jump(2.f);
    const float targets[] = {10.f, 4.f, 4.f, 8.f};
    const float times[] = {20.f, 10.f, 5.f, 30.f};
    REQUIRE(fn.segments(targets, times, 4) == 4);
    CHECK(fn.remaining() == 4);
    buffer& out = fn();
    const float* o = out.getPtr();
    for (unsigned int k = 0; k < 20; ++k)
      REQUIRE(o[k] == doctest::Approx(Segment(2.f, 10.f, 20, k)));
    for (unsigned int k = 0; k < 10; ++k)
      REQUIRE(o[20 + k] == doctest::Approx(Segment(10.f, 4.f, 10, k)));
    for (unsigned int k = 0; k < 5; ++k)
      REQUIRE(o[30 + k] == 4.f); // flat
    for (unsigned int k = 0; k < 30; ++k)
      REQUIRE(o[35 + k] == doctest::Approx(Segment(4.f, 8.f, 30, k)));
    for (unsigned int k = 65; k < N; ++k)
      REQUIRE(o[k] == 8.f);
    // Busy through the flat segment too, until the last target.
    for (unsigned int k = 0; k < 64; ++k)
      REQUIRE(fn.activity().getPtr()[k] == 1.f);
    CHECK(fn.activity().getPtr()[64] == 0.f);
    CHECK(fn.finished());
    CHECK(fn.remaining() == 0);
  }

  TEST_CASE("lineSegments: a new list starts where a running one got to (#1042)") {
    ScopedSampleRate rate(1000);
    lineSegments fn;
    fn.set(100.f, 200.f);
    fn(50);
    CHECK(fn.value() == doctest::Approx(25.f));
    fn.set(0.f, 25.f);
    buffer& out = fn(30);
    CHECK(fn.finished());
    for (unsigned int k = 0; k < 25; ++k)
      REQUIRE(out.getPtr()[k] == doctest::Approx(Segment(25.f, 0.f, 25, k)));
  }

  TEST_CASE("lineSegments: a ramp spans blocks and finishes in the block it arrives in "
            "(#1042)") {
    ScopedSampleRate rate(1000);
    lineSegments fn;
    fn.set(-3.f, 300.f); // 300 samples: blocks of 128 end at 128, 256, 384
    fn();
    CHECK_FALSE(fn.finished());
    CHECK(fn.value() == doctest::Approx(Segment(0.f, -3.f, 300, N - 1)));
    fn();
    CHECK_FALSE(fn.finished());
    buffer& last = fn();
    CHECK(fn.finished());
    CHECK(last.getPtr()[300 - 2 * N - 1] == -3.f);
    CHECK(last.getPtr()[300 - 2 * N - 2] == doctest::Approx(Segment(0.f, -3.f, 300, 298)));
  }

  TEST_CASE("lineSegments: time 0 is a jump that takes no time (#1042)") {
    ScopedSampleRate rate(1000);
    lineSegments fn;
    const float targets[] = {5.f, 1.f, 3.f};
    const float times[] = {0.f, 0.f, 2.f};
    fn.segments(targets, times, 3);
    buffer& out = fn(4);
    CHECK(out.getPtr()[0] == doctest::Approx(2.f)); // jumps 5, 1 then halfway to 3
    CHECK(out.getPtr()[1] == 3.f);
    CHECK(out.getPtr()[3] == 3.f);
    CHECK(fn.finished());
  }

  TEST_CASE("lineSegments: set with time 0 jumps at once and finishes in the next block, "
            "jump does not finish (#1042)") {
    ScopedSampleRate rate(1000);
    lineSegments fn;
    fn.set(0.75f, 0.f);
    CHECK(fn.value() == 0.75f); // before any block
    CHECK_FALSE(fn.active());
    buffer& out = fn();
    for (unsigned int k = 0; k < N; ++k) {
      REQUIRE(out.getPtr()[k] == 0.75f);
      REQUIRE(fn.activity().getPtr()[k] == 0.f);
    }
    CHECK(fn.finished());
    fn();
    CHECK_FALSE(fn.finished());

    // A list given after the jump, before the block, starts from the jumped
    // value and replaces it: one finish, at the end of the list.
    fn.set(0.f, 0.f);
    fn.set(1.f, 10.f);
    buffer& ramp = fn(5);
    CHECK_FALSE(fn.finished());
    CHECK(ramp.getPtr()[0] == doctest::Approx(0.1f));
    fn(5);
    CHECK(fn.finished());

    // jump() is an initial value: no finish.
    fn.jump(-2.f);
    CHECK(fn.value() == -2.f);
    fn();
    CHECK_FALSE(fn.finished());

    // Times that are not a number or negative are 0.
    fn.set(4.f, NaN);
    CHECK(fn.value() == 4.f);
    fn.set(5.f, -10.f);
    CHECK(fn.value() == 5.f);
    fn.set(6.f, INF);
    CHECK(fn.value() == 6.f);
  }

  TEST_CASE("lineSegments: a fractional length carries over and does not drift (#1042)") {
    ScopedSampleRate rate(1000);
    lineSegments fn;
    // Twenty segments of 2.5 samples alternating 0 and 1: 50 samples.
    float targets[20];
    float times[20];
    for (unsigned int i = 0; i < 20; ++i) {
      targets[i] = (i % 2 == 0) ? 1.f : 0.f;
      times[i] = 2.5f;
    }
    fn.segments(targets, times, 20);
    buffer& out = fn(60);
    const float* o = out.getPtr();
    // Sample s (1-based) is at time s; segment j spans 2.5 j .. 2.5 (j + 1).
    for (unsigned int s = 1; s <= 50; ++s) {
      const double t = static_cast<double>(s);
      const unsigned int j = static_cast<unsigned int>(std::floor(t / 2.5 - 1e-9));
      const double f = (t - 2.5 * j) / 2.5;
      const double a = j == 0 ? 0.0 : ((j - 1) % 2 == 0 ? 1.0 : 0.0);
      const double b = j % 2 == 0 ? 1.0 : 0.0;
      CAPTURE(s);
      REQUIRE(o[s - 1] == doctest::Approx(a + (b - a) * f).epsilon(1e-5));
    }
    CHECK(o[49] == 0.f);
    CHECK(fn.finished());
  }

  TEST_CASE("lineSegments: stop holds and drops the list; pause and resume keep the place "
            "(#1042)") {
    ScopedSampleRate rate(1000);
    lineSegments fn;
    const float target[] = {100.f, 0.f};
    const float time[] = {100.f, 10.f};
    fn.segments(target, time, 2);
    fn(10);
    fn.pause();
    CHECK(fn.paused());
    CHECK_FALSE(fn.active());
    CHECK(fn.remaining() == 2);
    buffer& held = fn(20);
    for (unsigned int k = 0; k < 20; ++k) {
      REQUIRE(held.getPtr()[k] == doctest::Approx(10.f));
      REQUIRE(fn.activity().getPtr()[k] == 0.f);
    }
    fn.resume();
    CHECK(fn.active());
    buffer& more = fn(5);
    CHECK(more.getPtr()[0] == doctest::Approx(11.f));
    CHECK(more.getPtr()[4] == doctest::Approx(15.f));
    CHECK(fn.activity().getPtr()[4] == 1.f);

    fn.stop();
    CHECK_FALSE(fn.active());
    CHECK_FALSE(fn.paused());
    CHECK(fn.remaining() == 0); // the pending segment is gone too
    buffer& stopped = fn(200);
    CHECK_FALSE(fn.finished()); // a stop is not an end
    CHECK(stopped.getPtr()[N - 1] == doctest::Approx(15.f));
    fn.resume(); // nothing to resume
    fn(10);
    CHECK(fn.value() == doctest::Approx(15.f));

    // A new list, a set and a jump each end a pause.
    fn.segments(target, time, 2);
    fn.pause();
    fn.segments(target, time, 2);
    CHECK(fn.active());
    fn.pause();
    fn.set(3.f, 10.f);
    CHECK(fn.active());
    fn.pause();
    fn.jump(1.f);
    CHECK_FALSE(fn.paused());
  }

  TEST_CASE("lineSegments: targets that are not finite numbers are refused (#1042)") {
    ScopedSampleRate rate(1000);
    lineSegments fn;
    fn.set(1.f, 10.f);
    const float bad[] = {2.f, NaN};
    const float times[] = {1.f, 1.f};
    CHECK(fn.segments(bad, times, 2) == 0);
    CHECK(fn.segments(bad, times, 0) == 0);
    CHECK(fn.segments(nullptr, times, 1) == 0);
    fn.set(INF, 0.f);
    fn.jump(NaN);
    // The first list still plays.
    buffer& out = fn(10);
    CHECK(out.getPtr()[9] == 1.f);
    CHECK(fn.finished());
  }

  TEST_CASE("lineSegments: a list longer than the capacity is cut to it (#1042)") {
    ScopedSampleRate rate(1000);
    lineSegments fn(4);
    CHECK(fn.capacity() == 4);
    const float targets[] = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f};
    const float times[] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
    CHECK(fn.segments(targets, times, 6) == 4);
    buffer& out = fn(8);
    CHECK(out.getPtr()[3] == 4.f);
    CHECK(out.getPtr()[7] == 4.f);
    CHECK(lineSegments(0).capacity() == 1);
    CHECK(lineSegments(lineSegments::MAX_POINTS + 1).capacity() == lineSegments::MAX_POINTS);
  }

  TEST_CASE("lineSegments: follows the live sample rate (#1042)") {
    lineSegments fn;
    {
      ScopedSampleRate rate(1000);
      fn.set(1.f, 10.f);
      fn(5);
      CHECK(fn.value() == doctest::Approx(0.5f));
    }
    {
      ScopedSampleRate rate(2000); // 20 samples now; 6 of them gone
      fn(1);
      CHECK(fn.value() == doctest::Approx(0.3f));
      fn(13);
      CHECK(fn.active());
      fn(1);
      CHECK(fn.value() == 1.f);
      CHECK(fn.finished());
    }
    {
      // Built at one rate, used at another: nothing derived in the constructor.
      lineSegments later;
      ScopedSampleRate rate(48000);
      later.set(1.f, 1.f); // 48 samples
      buffer& out = later(48);
      CHECK(out.getPtr()[46] < 1.f);
      CHECK(out.getPtr()[47] == 1.f);
      CHECK(later.finished());
    }
  }

  TEST_CASE("lineSegments: a long list is the same split into any blocks (#1042)") {
    ScopedSampleRate rate(48000);
    const float targets[] = {1.f, -0.5f};
    const float times[] = {7.3f, 11.1f};
    lineSegments whole;
    whole.segments(targets, times, 2);
    std::vector<float> ref;
    for (int b = 0; b < 8; ++b) {
      const std::vector<float> part = Copy(whole());
      ref.insert(ref.end(), part.begin(), part.end());
    }
    const unsigned int splits[] = {64, 13, 1};
    for (unsigned int split : splits) {
      CAPTURE(split);
      lineSegments fn;
      fn.segments(targets, times, 2);
      std::vector<float> got;
      while (got.size() < ref.size()) {
        const std::vector<float> part = Copy(fn(split));
        got.insert(got.end(), part.begin(), part.end());
      }
      for (std::size_t i = 0; i < ref.size(); ++i)
        REQUIRE(got[i] == ref[i]);
    }
  }

  TEST_CASE("lineSegments: reset and the block length clamp (#1042)") {
    ScopedSampleRate rate(1000);
    lineSegments fn;
    fn.set(1.f, 10.f);
    fn(3);
    fn.reset();
    CHECK(fn.value() == 0.f);
    CHECK_FALSE(fn.active());
    CHECK(fn(N * 2).getLength() == N);
    CHECK(fn.activity().getLength() == N);
  }

  TEST_CASE("lineSegments: processing allocates nothing (#1042)") {
    ScopedSampleRate rate(1000);
    lineSegments fn;
    const float targets[] = {1.f, 0.f, 0.5f};
    const float times[] = {30.f, 0.f, 70.f};
    fn();
    {
      TestHelpers::ProbeScope probe;
      fn.segments(targets, times, 3);
      fn(N / 2);
      fn.pause();
      fn();
      fn.resume();
      fn();
      fn.set(0.3f, 0.f);
      fn.set(0.2f, 40.f);
      fn();
      fn.jump(0.2f);
      fn.stop();
      fn();
      fn.reset();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
