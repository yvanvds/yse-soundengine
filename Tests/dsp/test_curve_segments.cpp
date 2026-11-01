// Tests for YSE::DSP::curveSegments (YseEngine/dsp/generators/curveSegments.hpp),
// the class behind ~curve (issue #1065). Most cases run at 1000 Hz so a
// millisecond is one sample: a segment of N ms reaches its target on its Nth
// sample.

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/generators/curveSegments.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::DSP::buffer;
using YSE::DSP::curveSegments;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr float NaN = std::numeric_limits<float>::quiet_NaN();
  constexpr float INF = std::numeric_limits<float>::infinity();

  // The closed forms, written out independently of the class.
  double PowerShape(double f, double c) {
    return std::pow(f, (1.0 + c) / (1.0 - c));
  }

  double ExpShape(double f, double c) {
    const double k = 2.0 * std::log((1.0 + c) / (1.0 - c));
    if (k == 0.0) return f;
    return (std::exp(k * f) - 1.0) / (std::exp(k) - 1.0);
  }

  // Sample k (0-based) of a segment from `a` to `b` over `n` samples.
  float Segment(float a, float b, unsigned int n, unsigned int k, double c, bool exponential) {
    const double f = static_cast<double>(k + 1) / static_cast<double>(n);
    const double s = exponential ? ExpShape(f, c) : PowerShape(f, c);
    return static_cast<float>(a + (b - a) * s);
  }

  std::vector<float> Copy(buffer& b) {
    return std::vector<float>(b.getPtr(), b.getPtr() + b.getLength());
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("curveSegments: defaults hold 0 with no list (#1065)") {
    ScopedSampleRate rate(1000);
    curveSegments fn;
    CHECK(fn.capacity() == curveSegments::DEFAULT_POINTS);
    CHECK(fn.shape() == curveSegments::POWER);
    CHECK(fn.value() == 0.f);
    CHECK_FALSE(fn.active());
    CHECK_FALSE(fn.paused());
    CHECK(fn.remaining() == 0);
    buffer& out = fn();
    REQUIRE(out.getLength() == N);
    for (unsigned int i = 0; i < N; ++i)
      REQUIRE(out.getPtr()[i] == 0.f);
    CHECK_FALSE(fn.finished());
  }

  TEST_CASE("curveSegments: each curvature against the closed form, both shapes (#1065)") {
    ScopedSampleRate rate(1000);
    const float curves[] = {-0.9f, -0.5f, -0.2f, 0.f, 0.2f, 0.5f, 0.9f};
    for (int exponential = 0; exponential < 2; ++exponential) {
      for (float c : curves) {
        CAPTURE(exponential);
        CAPTURE(c);
        curveSegments fn;
        fn.shape(exponential != 0 ? curveSegments::EXPONENTIAL : curveSegments::POWER);
        fn.jump(-1.f);
        const float target[] = {3.f};
        const float time[] = {100.f};
        const float curve[] = {c};
        REQUIRE(fn.segments(target, time, curve, 1) == 1);
        CHECK(fn.active());
        buffer& out = fn();
        for (unsigned int k = 0; k < 100; ++k)
          REQUIRE(out.getPtr()[k] ==
                  doctest::Approx(Segment(-1.f, 3.f, 100, k, c, exponential != 0)).epsilon(1e-5));
        CHECK(out.getPtr()[99] == 3.f); // the target exactly, on the 100th sample
        for (unsigned int k = 100; k < N; ++k)
          REQUIRE(out.getPtr()[k] == 3.f);
        CHECK(fn.finished());
        CHECK_FALSE(fn.active());
      }
    }
  }

  TEST_CASE("curveSegments: c and -c mirror each other; positive leaves slowly (#1065)") {
    // The power curves of c and -c are reflections across the diagonal
    // (inverse functions), the exponential ones half-turns about the middle.
    ScopedSampleRate rate(1000);
    for (int exponential = 0; exponential < 2; ++exponential) {
      CAPTURE(exponential);
      const auto mode = exponential != 0 ? curveSegments::EXPONENTIAL : curveSegments::POWER;
      curveSegments up;
      curveSegments down;
      up.shape(mode);
      down.shape(mode);
      const float target[] = {1.f};
      const float time[] = {64.f};
      const float plus[] = {0.6f};
      const float minus[] = {-0.6f};
      up.segments(target, time, plus, 1);
      down.segments(target, time, minus, 1);
      const std::vector<float> a = Copy(up());
      const std::vector<float> b = Copy(down());
      CHECK(a[31] < 0.5f);
      CHECK(b[31] > 0.5f);
      for (unsigned int k = 0; k < 63; ++k) {
        const double f = static_cast<double>(k + 1) / 64.0;
        if (exponential != 0) {
          // shape_c(f) = 1 - shape_-c(1 - f): sample k of one against sample
          // 62 - k of the other.
          REQUIRE(a[k] == doctest::Approx(1.f - b[62 - k]).epsilon(1e-4));
        } else {
          // shape_-c(shape_c(f)) = f.
          REQUIRE(PowerShape(a[k], -0.6) == doctest::Approx(f).epsilon(1e-4));
        }
      }
    }
  }

  TEST_CASE("curveSegments: a list plays back to back from the current value (#1065)") {
    ScopedSampleRate rate(1000);
    curveSegments fn;
    fn.jump(2.f);
    const float targets[] = {10.f, 4.f, 4.f, 8.f};
    const float times[] = {20.f, 10.f, 5.f, 30.f};
    const float curves[] = {0.f, 0.5f, 0.f, -0.5f};
    REQUIRE(fn.segments(targets, times, curves, 4) == 4);
    CHECK(fn.remaining() == 4);
    buffer& out = fn();
    const float* o = out.getPtr();
    for (unsigned int k = 0; k < 20; ++k)
      REQUIRE(o[k] == doctest::Approx(Segment(2.f, 10.f, 20, k, 0.0, false)));
    for (unsigned int k = 0; k < 10; ++k)
      REQUIRE(o[20 + k] == doctest::Approx(Segment(10.f, 4.f, 10, k, 0.5, false)));
    for (unsigned int k = 0; k < 5; ++k)
      REQUIRE(o[30 + k] == 4.f); // flat
    for (unsigned int k = 0; k < 30; ++k)
      REQUIRE(o[35 + k] == doctest::Approx(Segment(4.f, 8.f, 30, k, -0.5, false)));
    for (unsigned int k = 65; k < N; ++k)
      REQUIRE(o[k] == 8.f);
    CHECK(fn.finished());
    CHECK(fn.remaining() == 0);
  }

  TEST_CASE("curveSegments: a new list starts where a running one got to (#1065)") {
    ScopedSampleRate rate(1000);
    curveSegments fn;
    const float up[] = {100.f};
    const float t200[] = {200.f};
    fn.segments(up, t200, nullptr, 1);
    fn(50);
    CHECK(fn.value() == doctest::Approx(25.f));
    const float down[] = {0.f};
    const float t25[] = {25.f};
    fn.segments(down, t25, nullptr, 1);
    buffer& out = fn(30);
    CHECK(fn.finished());
    for (unsigned int k = 0; k < 25; ++k)
      REQUIRE(out.getPtr()[k] == doctest::Approx(Segment(25.f, 0.f, 25, k, 0.0, false)));
  }

  TEST_CASE("curveSegments: time 0 is a jump that takes no time (#1065)") {
    ScopedSampleRate rate(1000);
    curveSegments fn;
    const float targets[] = {5.f, 1.f, 3.f};
    const float times[] = {0.f, 0.f, 2.f};
    fn.segments(targets, times, nullptr, 3);
    buffer& out = fn(4);
    CHECK(out.getPtr()[0] == doctest::Approx(2.f)); // jumps 5, 1 then halfway to 3
    CHECK(out.getPtr()[1] == 3.f);
    CHECK(out.getPtr()[3] == 3.f);
    CHECK(fn.finished());

    // A single jump finishes in the block it starts.
    const float seven[] = {7.f};
    const float zero[] = {0.f};
    fn.segments(seven, zero, nullptr, 1);
    CHECK(fn.active());
    fn(1);
    CHECK(fn.value() == 7.f);
    CHECK(fn.finished());
  }

  TEST_CASE("curveSegments: a fractional length carries over and does not drift (#1065)") {
    ScopedSampleRate rate(1000);
    curveSegments fn;
    // Twenty segments of 2.5 samples alternating 0 and 1: 50 samples.
    float targets[20];
    float times[20];
    for (unsigned int i = 0; i < 20; ++i) {
      targets[i] = (i % 2 == 0) ? 1.f : 0.f;
      times[i] = 2.5f;
    }
    fn.segments(targets, times, nullptr, 20);
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

  TEST_CASE("curveSegments: stop holds and drops the list; pause and resume keep the place "
            "(#1065)") {
    ScopedSampleRate rate(1000);
    curveSegments fn;
    const float target[] = {100.f};
    const float time[] = {100.f};
    fn.segments(target, time, nullptr, 1);
    fn(10);
    fn.pause();
    CHECK(fn.paused());
    CHECK_FALSE(fn.active());
    buffer& held = fn(20);
    for (unsigned int k = 0; k < 20; ++k)
      REQUIRE(held.getPtr()[k] == doctest::Approx(10.f));
    fn.resume();
    CHECK(fn.active());
    buffer& more = fn(5);
    CHECK(more.getPtr()[0] == doctest::Approx(11.f));
    CHECK(more.getPtr()[4] == doctest::Approx(15.f));

    fn.stop();
    CHECK_FALSE(fn.active());
    CHECK_FALSE(fn.paused());
    buffer& stopped = fn(100);
    CHECK_FALSE(fn.finished()); // a stop is not an end
    CHECK(stopped.getPtr()[99] == doctest::Approx(15.f));
    fn.resume(); // nothing to resume
    fn(10);
    CHECK(fn.value() == doctest::Approx(15.f));

    // A new list ends a pause.
    fn.segments(target, time, nullptr, 1);
    fn.pause();
    fn.segments(target, time, nullptr, 1);
    CHECK(fn.active());
  }

  TEST_CASE("curveSegments: jump sets the value at once and drops the list (#1065)") {
    ScopedSampleRate rate(1000);
    curveSegments fn;
    const float target[] = {100.f};
    const float time[] = {100.f};
    fn.segments(target, time, nullptr, 1);
    fn(10);
    fn.jump(-3.f);
    CHECK(fn.value() == -3.f);
    CHECK_FALSE(fn.active());
    buffer& out = fn(10);
    CHECK(out.getPtr()[9] == -3.f);
    CHECK_FALSE(fn.finished());
    fn.jump(NaN);
    fn.jump(INF);
    CHECK(fn.value() == -3.f);
  }

  TEST_CASE("curveSegments: cleaning of times, curves and targets (#1065)") {
    ScopedSampleRate rate(1000);
    curveSegments fn;
    const float bad[] = {1.f, NaN};
    const float times[] = {10.f, 10.f};
    CHECK(fn.segments(bad, times, nullptr, 2) == 0); // refused whole
    CHECK_FALSE(fn.active());
    CHECK(fn.segments(bad, times, nullptr, 0) == 0);
    CHECK(fn.segments(nullptr, times, nullptr, 1) == 0);

    // Negative and NaN times are jumps; curves past 1 are 1 (hold), NaN 0.
    const float targets[] = {1.f, 2.f, 3.f, 4.f};
    const float t[] = {-5.f, NaN, 4.f, 4.f};
    const float c[] = {0.f, 0.f, 7.f, NaN};
    REQUIRE(fn.segments(targets, t, c, 4) == 4);
    buffer& out = fn(8);
    const float* o = out.getPtr();
    CHECK(o[0] == 2.f); // two jumps, then a hold toward 3
    CHECK(o[2] == 2.f);
    CHECK(o[3] == 3.f); // arrives at the end
    CHECK(o[4] == doctest::Approx(3.25f)); // straight to 4
    CHECK(o[7] == 4.f);

    // -1 jumps at the start.
    const float five[] = {5.f};
    const float ten[] = {10.f};
    const float minus[] = {-3.f};
    fn.segments(five, ten, minus, 1);
    CHECK(fn(1).getPtr()[0] == 5.f);
    fn.shape(curveSegments::EXPONENTIAL);
    const float plus[] = {1.f};
    fn.segments(ten, ten, plus, 1);
    buffer& e = fn(10);
    CHECK(e.getPtr()[8] == 5.f);
    CHECK(e.getPtr()[9] == 10.f);
  }

  TEST_CASE("curveSegments: a list longer than the capacity is cut to it (#1065)") {
    ScopedSampleRate rate(1000);
    curveSegments fn(4);
    CHECK(fn.capacity() == 4);
    const float targets[] = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f};
    const float times[] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
    CHECK(fn.segments(targets, times, nullptr, 6) == 4);
    buffer& out = fn(8);
    CHECK(out.getPtr()[3] == 4.f);
    CHECK(out.getPtr()[7] == 4.f);
    CHECK(curveSegments(0).capacity() == 1);
    CHECK(curveSegments(curveSegments::MAX_POINTS + 1).capacity() == curveSegments::MAX_POINTS);
  }

  TEST_CASE("curveSegments: the shape can change mid segment without a jump in place (#1065)") {
    ScopedSampleRate rate(1000);
    curveSegments fn;
    const float target[] = {1.f};
    const float time[] = {100.f};
    const float curve[] = {0.5f};
    fn.segments(target, time, curve, 1);
    fn(40);
    fn.shape(curveSegments::EXPONENTIAL);
    buffer& out = fn(60);
    CHECK(out.getPtr()[0] == doctest::Approx(Segment(0.f, 1.f, 100, 40, 0.5, true)));
    CHECK(out.getPtr()[59] == 1.f);
  }

  TEST_CASE("curveSegments: follows the live sample rate (#1065)") {
    curveSegments fn;
    const float target[] = {1.f};
    const float time[] = {10.f};
    {
      ScopedSampleRate rate(1000);
      fn.segments(target, time, nullptr, 1);
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
  }

  TEST_CASE("curveSegments: a long segment is the same split into any blocks (#1065)") {
    ScopedSampleRate rate(48000);
    const float targets[] = {1.f, -0.5f};
    const float times[] = {7.3f, 11.1f};
    const float curves[] = {0.7f, -0.4f};
    curveSegments whole;
    whole.segments(targets, times, curves, 2);
    std::vector<float> ref;
    for (int b = 0; b < 8; ++b) {
      const std::vector<float> part = Copy(whole());
      ref.insert(ref.end(), part.begin(), part.end());
    }
    const unsigned int splits[] = {64, 13, 1};
    for (unsigned int split : splits) {
      CAPTURE(split);
      curveSegments fn;
      fn.segments(targets, times, curves, 2);
      std::vector<float> got;
      while (got.size() < ref.size()) {
        const std::vector<float> part = Copy(fn(split));
        got.insert(got.end(), part.begin(), part.end());
      }
      for (std::size_t i = 0; i < ref.size(); ++i)
        REQUIRE(got[i] == ref[i]);
    }
  }

  TEST_CASE("curveSegments: reset and the block length clamp (#1065)") {
    ScopedSampleRate rate(1000);
    curveSegments fn;
    fn.shape(curveSegments::EXPONENTIAL);
    const float target[] = {1.f};
    const float time[] = {10.f};
    fn.segments(target, time, nullptr, 1);
    fn(3);
    fn.reset();
    CHECK(fn.value() == 0.f);
    CHECK_FALSE(fn.active());
    CHECK(fn.shape() == curveSegments::EXPONENTIAL);
    CHECK(fn(N * 2).getLength() == N);
  }

  TEST_CASE("curveSegments: processing allocates nothing (#1065)") {
    ScopedSampleRate rate(1000);
    curveSegments fn;
    const float targets[] = {1.f, 0.f, 0.5f};
    const float times[] = {30.f, 0.f, 70.f};
    const float curves[] = {0.3f, 0.f, -0.8f};
    fn();
    {
      TestHelpers::ProbeScope probe;
      fn.segments(targets, times, curves, 3);
      fn(N / 2);
      fn.pause();
      fn();
      fn.resume();
      fn.shape(curveSegments::EXPONENTIAL);
      fn();
      fn.jump(0.2f);
      fn.stop();
      fn();
      fn.reset();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
