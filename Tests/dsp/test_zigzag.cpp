// Tests for YSE::DSP::zigzag (YseEngine/dsp/generators/zigzag.hpp), the class
// behind ~zigzag (issue #1067). Most cases run at 1000 Hz so a millisecond is
// one sample: a ramp of N ms reaches its point on its Nth sample.

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/generators/zigzag.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::DSP::buffer;
using YSE::DSP::zigzag;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr float NaN = std::numeric_limits<float>::quiet_NaN();
  constexpr float INF = std::numeric_limits<float>::infinity();

  // `n` samples of the function in blocks of at most `block`, with the index
  // of each in `index` when it is given.
  std::vector<float> Run(zigzag& fn, unsigned int n, unsigned int block = N,
                         std::vector<float>* index = nullptr) {
    std::vector<float> out;
    while (out.size() < n) {
      const unsigned int left = n - static_cast<unsigned int>(out.size());
      const unsigned int length = left < block ? left : block;
      buffer& b = fn(length);
      for (unsigned int i = 0; i < length; ++i) {
        out.push_back(b.getPtr()[i]);
        if (index != nullptr) index->push_back(fn.indices().getPtr()[i]);
      }
    }
    return out;
  }

  // Replaces the list with value/delta pairs and plays it.
  void Line(zigzag& fn, std::vector<float> pairs, bool play = true) {
    std::vector<float> values;
    std::vector<float> deltas;
    for (std::size_t i = 0; i < pairs.size(); i += 2) {
      values.push_back(pairs[i]);
      deltas.push_back(i + 1 < pairs.size() ? pairs[i + 1] : 0.f);
    }
    const unsigned int n = static_cast<unsigned int>(values.size());
    if (play) {
      REQUIRE(fn.line(values.data(), deltas.data(), n) == n);
    } else {
      REQUIRE(fn.setLine(values.data(), deltas.data(), n) == n);
    }
  }

  void Expect(const std::vector<float>& got, const std::vector<float>& want) {
    REQUIRE(got.size() >= want.size());
    for (std::size_t i = 0; i < want.size(); ++i) {
      CAPTURE(i);
      REQUIRE(got[i] == doctest::Approx(want[i]));
    }
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("zigzag: defaults (#1067)") {
    ScopedSampleRate rate(1000);
    zigzag fn;
    CHECK(fn.capacity() == zigzag::DEFAULT_POINTS);
    CHECK(fn.size() == 0);
    CHECK(fn.speed() == 1.f);
    CHECK(fn.loopMode() == zigzag::LOOP_OFF);
    CHECK(fn.mode() == zigzag::START);
    CHECK(fn.bangDelta() == 0.f);
    CHECK_FALSE(fn.active());
    std::vector<float> index;
    Expect(Run(fn, N, N, &index), std::vector<float>(N, 0.f));
    Expect(index, std::vector<float>(N, 0.f));
    CHECK_FALSE(fn.finished());
    fn.play(); // nothing to play
    fn.bang();
    CHECK_FALSE(fn.active());
    fn.hold(0.5f);
    Expect(Run(fn, 4), {0.5f, 0.5f, 0.5f, 0.5f});
  }

  TEST_CASE("zigzag: a line plays straight ramps point to point and stops at the end "
            "(#1067)") {
    ScopedSampleRate rate(1000);
    zigzag fn;
    Line(fn, {0, 0, 1, 4, -1, 2});
    CHECK(fn.active());
    std::vector<float> index;
    const std::vector<float> out = Run(fn, 10, N, &index);
    // Point 0 has no delta: the ramp to point 1 starts at once.
    Expect(out, {0.25f, 0.5f, 0.75f, 1.f, 0.f, -1.f, -1.f, -1.f, -1.f, -1.f});
    // The index moves on in the sample a point is reached.
    Expect(index, {1, 1, 1, 2, 2, 2, 2, 2, 2, 2});
    CHECK(fn.ends() == 1);
    CHECK_FALSE(fn.active());
    CHECK(fn.index() == 2);
    Run(fn, 4);
    CHECK_FALSE(fn.finished());

    // Point 0's delta is the time from wherever the output is.
    Line(fn, {0, 4, 1, 4});
    Expect(Run(fn, 8), {-0.75f, -0.5f, -0.25f, 0.f, 0.25f, 0.5f, 0.75f, 1.f});

    // setLine stores without playing.
    Line(fn, {5, 0, 6, 1}, false);
    CHECK_FALSE(fn.active());
    Expect(Run(fn, 2), {1.f, 1.f});
    CHECK(fn.size() == 2);
    CHECK(fn.pointValue(1) == 6.f);
    CHECK(fn.pointDelta(1) == 1.f);
    CHECK(fn.pointValue(2) == 0.f);
  }

  TEST_CASE("zigzag: speed scales time, 0 holds, a negative speed plays backward (#1067)") {
    ScopedSampleRate rate(1000);
    zigzag fn;
    fn.speed(0.5f);
    Line(fn, {0, 0, 1, 4});
    Expect(Run(fn, 9), {0.125f, 0.25f, 0.375f, 0.5f, 0.625f, 0.75f, 0.875f, 1.f, 1.f});

    fn.speed(2.f);
    Line(fn, {0, 0, 1, 4});
    Expect(Run(fn, 3), {0.5f, 1.f, 1.f});

    fn.speed(1.f);
    Line(fn, {0, 0, 1, 4});
    Run(fn, 1);
    fn.speed(0.f);
    Expect(Run(fn, 3), {0.25f, 0.25f, 0.25f});
    CHECK(fn.active());
    fn.speed(1.f);
    Expect(Run(fn, 1), {0.5f});

    // Backward: from the last point down, over each reached point's delta.
    fn.speed(-1.f);
    fn.hold(0.f);
    Line(fn, {0, 0, 1, 2, 3, 2});
    std::vector<float> index;
    Expect(Run(fn, 5, N, &index), {2.f, 1.f, 0.5f, 0.f, 0.f});
    Expect(index, {1, 0, 0, 0, 0});
    CHECK(fn.ends() == 1);
  }

  TEST_CASE("zigzag: reversing in the middle of a ramp turns back at the same rate "
            "(#1067)") {
    ScopedSampleRate rate(1000);
    zigzag fn;
    Line(fn, {0, 0, 10, 10, 20, 10});
    Expect(Run(fn, 5), {1, 2, 3, 4, 5});
    fn.speed(-1.f);
    Expect(Run(fn, 7), {4, 3, 2, 1, 0, 0, 0});
    CHECK(fn.ends() == 1);
    CHECK_FALSE(fn.active());

    // Turning forward again on the way back, at half speed.
    fn.speed(1.f);
    Line(fn, {0, 0, 10, 10, 20, 10});
    Run(fn, 15); // 15, heading for point 2
    fn.speed(-1.f);
    Expect(Run(fn, 2), {14, 13});
    fn.speed(0.5f);
    Expect(Run(fn, 4), {13.5f, 14.f, 14.5f, 15.f});
  }

  TEST_CASE("zigzag: loop modes start again or turn round, counting each end (#1067)") {
    ScopedSampleRate rate(1000);
    zigzag fn;
    fn.loopMode(zigzag::LOOP_ON);
    Line(fn, {0, 0, 1, 4});
    // Round again over point 0's delta, 0: a sawtooth with a period of 4.
    Expect(Run(fn, 9), {0.25f, 0.5f, 0.75f, 0.f, 0.25f, 0.5f, 0.75f, 0.f, 0.25f});
    CHECK(fn.ends() == 2);
    CHECK(fn.active());

    fn.loopMode(zigzag::LOOP_PENDULUM);
    fn.hold(0.f);
    Line(fn, {0, 0, 1, 4});
    Expect(Run(fn, 12),
           {0.25f, 0.5f, 0.75f, 1.f, 0.75f, 0.5f, 0.25f, 0.f, 0.25f, 0.5f, 0.75f, 1.f});
    CHECK(fn.ends() == 3);

    // Backward loops wrap to the end of the range.
    fn.loopMode(zigzag::LOOP_ON);
    fn.speed(-1.f);
    fn.hold(0.f);
    Line(fn, {0, 4, 1, 4});
    Expect(Run(fn, 10), {0.75f, 0.5f, 0.25f, 0.f, 0.25f, 0.5f, 0.75f, 1.f, 0.75f, 0.5f});

    // Leaving the pendulum plays in the speed's direction again; a range of
    // one point does not loop.
    fn.loopMode(zigzag::LOOP_OFF);
    CHECK(fn.loopMode() == zigzag::LOOP_OFF);
    fn.loopMode(static_cast<zigzag::LOOP>(7));
    CHECK(fn.loopMode() == zigzag::LOOP_OFF);
    fn.loopMode(zigzag::LOOP_ON);
    fn.speed(1.f);
    Line(fn, {3, 2}); // from 0.5
    Expect(Run(fn, 4), {1.75f, 3.f, 3.f, 3.f});
    CHECK_FALSE(fn.active());
  }

  TEST_CASE("zigzag: start, end and bound limit play; bang jumps there over the bang delta "
            "(#1067)") {
    ScopedSampleRate rate(1000);
    zigzag fn;
    Line(fn, {0, 4, 1, 4, 2, 4, 3, 4, 4, 4}, false);
    fn.bound(1, 3);
    fn.bang(); // to point 1 at once, then on to 3
    Expect(Run(fn, 9), {1.25f, 1.5f, 1.75f, 2.f, 2.25f, 2.5f, 2.75f, 3.f, 3.f});
    CHECK(fn.ends() == 1);

    fn.bangDelta(2.f);
    CHECK(fn.bangDelta() == 2.f);
    fn.bang(); // 3 -> 1 over 2 ms
    Expect(Run(fn, 3), {2.f, 1.f, 1.25f});

    // Backward, a bang goes to the end of the range.
    fn.speed(-1.f);
    fn.bangDelta(0.f);
    fn.bang();
    Expect(Run(fn, 2), {2.75f, 2.5f});

    // The range follows the list: an end past it is the last point, a start
    // past the end is swapped round.
    fn.speed(1.f);
    fn.bound(0, 99);
    fn.jumpEnd();
    Expect(Run(fn, 1), {4.f});
    fn.end(-1).start(3);
    fn.jumpStart(1.f);
    Expect(Run(fn, 1), {3.f});
    fn.bound(4, 2); // the range 2..4
    fn.stop();
    fn.jumpStart();
    Expect(Run(fn, 1), {2.25f});
  }

  TEST_CASE("zigzag: jump, next, prev and skip move relative to the index and play on "
            "(#1067)") {
    ScopedSampleRate rate(1000);
    zigzag fn;
    Line(fn, {0, 0, 10, 2, 20, 2, 30, 2}, false);
    fn.jump(2, 4.f); // 0 -> 20 over 4 ms, then on to 30
    std::vector<float> index;
    Expect(Run(fn, 7, N, &index), {5, 10, 15, 20, 25, 30, 30});
    Expect(index, {2, 2, 2, 3, 3, 3, 3});
    CHECK(fn.index() == 3);

    fn.prev(); // to point 2 at once, then on to 3
    Expect(Run(fn, 1), {25});
    fn.stop();
    CHECK(fn.index() == 2); // the last point reached
    fn.skip(-2, 1.f);
    CHECK(fn.index() == 0);
    Expect(Run(fn, 2), {0, 5});
    fn.stop();
    fn.next(2.f); // from 5 towards point 1
    Expect(Run(fn, 2), {7.5f, 10.f});
    // Clamped to the list.
    fn.jump(-5);
    CHECK(fn.index() == 0);
    fn.jump(50);
    CHECK(fn.index() == 3);
    Expect(Run(fn, 1), {30});
    CHECK(fn.finished());

    // STEP mode: a bang is next (prev, backward).
    fn.mode(zigzag::STEP);
    CHECK(fn.mode() == zigzag::STEP);
    fn.stop();
    fn.jump(0);
    Run(fn, 10);
    CHECK(fn.index() == 3);
    fn.speed(-1.f);
    fn.bang();
    CHECK(fn.index() == 2);
    fn.mode(static_cast<zigzag::MODE>(-3));
    CHECK(fn.mode() == zigzag::START);
  }

  TEST_CASE("zigzag: edits while playing follow the running ramp's point (#1067)") {
    ScopedSampleRate rate(1000);
    zigzag fn;
    Line(fn, {0, 0, 10, 10, 0, 10});
    Expect(Run(fn, 3), {1, 2, 3});
    // The target's value is read live.
    REQUIRE(fn.set(1, 20.f, 10.f));
    Expect(Run(fn, 1), {8});
    // An insert before the target moves it along.
    REQUIRE(fn.insert(0, 5.f, 0.f));
    CHECK(fn.index() == 2);
    CHECK(fn.pointValue(0) == 5.f);
    CHECK(fn.pointValue(2) == 20.f);
    Expect(Run(fn, 1), {10});
    // So does a remove before it.
    REQUIRE(fn.remove(0));
    CHECK(fn.index() == 1);
    Expect(Run(fn, 1), {12});
    // A removed target is replaced by the point that took its place.
    REQUIRE(fn.remove(1));
    CHECK(fn.index() == 1);
    CHECK(fn.size() == 2);
    CHECK(fn.pointValue(1) == 0.f);
    // A remove past the target leaves it.
    REQUIRE(fn.append(7.f, 3.f, false));
    REQUIRE(fn.remove(2));
    CHECK(fn.index() == 1);
    // An insert past the end appends.
    REQUIRE(fn.insert(99, 9.f, 1.f));
    CHECK(fn.size() == 3);
    CHECK(fn.pointValue(2) == 9.f);

    // append plays the new point, as Max does.
    fn.stop();
    fn.hold(0.f);
    REQUIRE(fn.append(3.f, 3.f));
    CHECK(fn.index() == 3);
    Expect(Run(fn, 4), {1, 2, 3, 3});

    // Removing everything stops play.
    fn.jump(0, 100.f);
    while (fn.size() > 0)
      REQUIRE(fn.remove(0));
    CHECK_FALSE(fn.active());
    CHECK(fn.index() == 0);
    fn.clear();
    CHECK(fn.size() == 0);
  }

  TEST_CASE("zigzag: refused and cleaned values (#1067)") {
    ScopedSampleRate rate(1000);
    zigzag fn(4);
    CHECK(fn.capacity() == 4);
    CHECK(zigzag(0).capacity() == 1);
    CHECK(zigzag(zigzag::MAX_POINTS + 1).capacity() == zigzag::MAX_POINTS);

    const float values[] = {1.f, 2.f, 3.f, 4.f, 5.f};
    const float deltas[] = {-1.f, NaN, INF, 2.f, 1.f};
    CHECK(fn.setLine(values, deltas, 5) == 4); // cut to the capacity
    CHECK(fn.pointDelta(0) == 0.f);
    CHECK(fn.pointDelta(1) == 0.f);
    CHECK(fn.pointDelta(2) == 0.f);
    CHECK(fn.pointDelta(3) == 2.f);
    const float bad[] = {1.f, NaN};
    CHECK(fn.setLine(bad, deltas, 2) == 0);
    CHECK(fn.setLine(nullptr, deltas, 2) == 0);
    CHECK(fn.setLine(values, deltas, 0) == 0);
    CHECK(fn.size() == 4);

    CHECK_FALSE(fn.append(1.f, 1.f)); // full
    CHECK_FALSE(fn.insert(0, 1.f, 1.f));
    CHECK_FALSE(fn.remove(4));
    CHECK_FALSE(fn.set(4, 1.f, 1.f));
    CHECK_FALSE(fn.set(0, NaN, 1.f));
    REQUIRE(fn.remove(3));
    CHECK_FALSE(fn.append(INF, 1.f));
    CHECK_FALSE(fn.insert(0, NaN, 1.f));
    REQUIRE(fn.set(0, 9.f, -4.f));
    CHECK(fn.pointDelta(0) == 0.f);

    fn.hold(NaN);
    CHECK(fn.value() == 0.f);
    fn.speed(NaN);
    fn.speed(INF);
    CHECK(fn.speed() == 1.f);
    fn.bangDelta(-3.f);
    CHECK(fn.bangDelta() == 0.f);
  }

  TEST_CASE("zigzag: in START mode an input signal reads the list at that index (#1067)") {
    ScopedSampleRate rate(1000);
    zigzag fn;
    buffer in(N);
    in = 0.f;
    // Nothing to read: the output holds.
    fn.hold(0.5f);
    CHECK(fn(&in, nullptr).getPtr()[0] == 0.5f);

    Line(fn, {0, 0, 10, 100, 20, 100});
    const float x[] = {0.f, 0.5f, 1.5f, 2.f, 5.f, -1.f, NaN, 1.25f};
    for (unsigned int i = 0; i < 8; ++i)
      in.getPtr()[i] = x[i];
    buffer& out = fn(&in, nullptr);
    const float want[] = {0.f, 5.f, 15.f, 20.f, 20.f, 0.f, 0.f, 12.5f};
    const float index[] = {0.f, 0.5f, 1.5f, 2.f, 2.f, 0.f, 0.f, 1.25f};
    for (unsigned int i = 0; i < 8; ++i) {
      CAPTURE(i);
      CHECK(out.getPtr()[i] == doctest::Approx(want[i]));
      CHECK(fn.indices().getPtr()[i] == doctest::Approx(index[i]));
    }
    // Without the signal, play goes on where it was: reading did not move it.
    CHECK(fn(4).getPtr()[0] == doctest::Approx(0.1f));
  }

  TEST_CASE("zigzag: in TRIGGER and STEP modes a falling input is a bang (#1067)") {
    ScopedSampleRate rate(1000);
    zigzag fn;
    fn.mode(zigzag::TRIGGER);
    Line(fn, {0, 0, 1, 100}, false);
    buffer in(N);
    // A ramp that wraps at samples 4 and 8; it starts below 0, which is not
    // a fall.
    const float x[] = {-1.f, 0.25f, 0.5f, 0.75f, 0.f, 0.25f, 0.5f, 0.75f, 0.f, 0.25f};
    in = 0.25f;
    for (unsigned int i = 0; i < 10; ++i)
      in.getPtr()[i] = x[i];
    buffer& out = fn(&in, nullptr);
    Expect(std::vector<float>(out.getPtr(), out.getPtr() + 10),
           {0.f, 0.f, 0.f, 0.f, 0.01f, 0.02f, 0.03f, 0.04f, 0.01f, 0.02f});
    // A flat or rising input is not a fall, nor is a second falling sample.
    in = 0.f;
    in.getPtr()[0] = 1.f;
    in.getPtr()[1] = 0.5f;
    in.getPtr()[2] = 0.25f;
    out = fn(&in, nullptr);
    CHECK(out.getPtr()[0] == 1.f); // the ramp from sample 8 ended at 107
    CHECK(out.getPtr()[1] == doctest::Approx(0.01f));
    CHECK(out.getPtr()[2] == doctest::Approx(0.02f));

    fn.mode(zigzag::STEP);
    Line(fn, {0, 0, 1, 0, 2, 0, 3, 0}, false);
    fn.jump(0);
    fn.stop();
    in = 1.f;
    in.getPtr()[1] = 0.f;
    in.getPtr()[2] = 1.f;
    in.getPtr()[3] = 0.f;
    in.getPtr()[3] = 0.f;
    fn(&in, nullptr);
    // Each fall steps to the next point, and play goes on from there.
    CHECK(fn.index() == 3);
  }

  TEST_CASE("zigzag: a speed signal's first sample sets the speed (#1067)") {
    ScopedSampleRate rate(1000);
    zigzag fn;
    Line(fn, {0, 0, 1, 8});
    buffer speed(N);
    speed = 0.f;
    speed.getPtr()[0] = 2.f;
    buffer& out = fn(nullptr, &speed);
    CHECK(out.getPtr()[0] == doctest::Approx(0.25f));
    CHECK(out.getPtr()[3] == doctest::Approx(1.f));
    CHECK(fn.speed() == 2.f);
  }

  TEST_CASE("zigzag: follows the live sample rate (#1067)") {
    ScopedSampleRate rate(2000);
    zigzag fn;
    Line(fn, {0, 0, 1, 4});
    Expect(Run(fn, 4), {0.125f, 0.25f, 0.375f, 0.5f});
    YSE::SAMPLERATE = 1000;
    Expect(Run(fn, 3), {0.75f, 1.f, 1.f});
  }

  TEST_CASE("zigzag: a list plays the same split into any blocks (#1067)") {
    ScopedSampleRate rate(44100);
    zigzag whole;
    zigzag split;
    whole.loopMode(zigzag::LOOP_PENDULUM);
    split.loopMode(zigzag::LOOP_PENDULUM);
    Line(whole, {0, 1.3f, 1, 2.9f, -0.5f, 3.7f});
    Line(split, {0, 1.3f, 1, 2.9f, -0.5f, 3.7f});
    const std::vector<float> a = Run(whole, 2000);
    std::vector<float> b;
    const unsigned int blocks[] = {64, 13, 1};
    for (unsigned int block : blocks) {
      const std::vector<float> part = Run(split, 700, block);
      b.insert(b.end(), part.begin(), part.end());
    }
    REQUIRE(b.size() == 2100);
    for (unsigned int i = 0; i < 2000; ++i)
      REQUIRE(a[i] == b[i]);
  }

  TEST_CASE("zigzag: reset and the block length clamp (#1067)") {
    ScopedSampleRate rate(1000);
    zigzag fn;
    Line(fn, {0, 0, 1, 1000});
    fn.speed(-2.f).loopMode(zigzag::LOOP_ON).mode(zigzag::STEP).bangDelta(3.f).bound(1, 1);
    CHECK(fn(N * 4).getLength() == N);
    fn.reset();
    CHECK(fn.size() == 0);
    CHECK(fn.capacity() == zigzag::DEFAULT_POINTS);
    CHECK(fn.value() == 0.f);
    CHECK(fn.speed() == 1.f);
    CHECK(fn.loopMode() == zigzag::LOOP_OFF);
    CHECK(fn.mode() == zigzag::START);
    CHECK(fn.bangDelta() == 0.f);
    CHECK_FALSE(fn.active());
    CHECK(fn.indices().getLength() == N);
  }

  TEST_CASE("zigzag: editing and processing allocate nothing (#1067)") {
    ScopedSampleRate rate(1000);
    zigzag fn;
    const float values[] = {0.f, 1.f, 0.5f};
    const float deltas[] = {0.f, 30.f, 70.f};
    buffer in(N);
    in = 0.5f;
    fn();
    {
      TestHelpers::ProbeScope probe;
      fn.line(values, deltas, 3);
      fn(N / 2);
      (void)fn.append(2.f, 10.f);
      (void)fn.insert(1, 3.f, 5.f);
      (void)fn.remove(0);
      (void)fn.set(0, 1.f, 1.f);
      fn.speed(-0.5f).loopMode(zigzag::LOOP_PENDULUM);
      fn();
      fn.bang();
      fn.next(3.f).prev().skip(2).jumpStart().jumpEnd(1.f);
      fn(&in, &in);
      fn.mode(zigzag::TRIGGER);
      fn(&in, nullptr);
      fn.stop();
      fn.reset();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
