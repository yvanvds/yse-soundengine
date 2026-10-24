// Tests for YSE::DSP::sampleHold (YseEngine/dsp/filters.hpp): the trigger
// modes, threshold and duration added for ~sah (issue #1082), and the
// class's original Pure Data style behaviour, still the default. The input
// is a counter (sample n reads n), so the output names the sample that was
// taken; the trigger is usually a DSP::phasor at 375 Hz and 48 kHz, exactly
// 128 samples per cycle, so sample n reads (n mod 128) / 128.

#include <doctest/doctest.h>
#include <algorithm>
#include <limits>
#include <vector>
#include "dsp/filters.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::sampleHold;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr unsigned int PERIOD = 128;

  // `samples` samples of a DSP::phasor at `hz` (375 Hz: 128 per cycle).
  std::vector<float> Ramp(unsigned int samples, float hz = 375.f) {
    TestHelpers::ScopedSampleRate rate(48000);
    phasor source;
    std::vector<float> result;
    while (result.size() < samples) {
      buffer& out = source(hz, N);
      for (unsigned int i = 0; i < N && result.size() < samples; ++i)
        result.push_back(out.getPtr()[i]);
    }
    return result;
  }

  std::vector<float> Counter(unsigned int samples) {
    std::vector<float> result(samples);
    for (unsigned int n = 0; n < samples; ++n)
      result[n] = static_cast<float>(n);
    return result;
  }

  // Runs `fn` over `in` and `trigger` in blocks of `block` samples.
  std::vector<float> Run(sampleHold& fn, const std::vector<float>& in,
                         const std::vector<float>& trigger, unsigned int block = N) {
    std::vector<float> out;
    for (std::size_t start = 0; start < in.size(); start += block) {
      const unsigned int length =
          static_cast<unsigned int>(std::min<std::size_t>(block, in.size() - start));
      buffer a(length);
      buffer b(length);
      for (unsigned int i = 0; i < length; ++i) {
        a.getPtr()[i] = in[start + i];
        b.getPtr()[i] = trigger[start + i];
      }
      buffer& result = fn(a, b);
      REQUIRE(result.getLength() == length);
      for (unsigned int i = 0; i < length; ++i)
        out.push_back(result.getPtr()[i]);
    }
    return out;
  }

  // The output a sample-and-hold of a counter gives when it triggers at the
  // samples `at` (sorted), holding `initial` before the first.
  std::vector<float> Held(unsigned int samples, const std::vector<unsigned int>& at,
                          float initial = 0.f) {
    std::vector<float> result(samples);
    float held = initial;
    std::size_t next = 0;
    for (unsigned int n = 0; n < samples; ++n) {
      if (next < at.size() && at[next] == n) {
        held = static_cast<float>(n);
        ++next;
      }
      result[n] = held;
    }
    return result;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("sampleHold: the default is the original falling-control trigger (#1082)") {
    // A rising ramp falls once per cycle, at its reset: n = 128, 256, ...
    const std::vector<float> ramp = Ramp(4 * PERIOD);
    sampleHold fn;
    CHECK(Run(fn, Counter(4 * PERIOD), ramp) == Held(4 * PERIOD, {128, 256, 384}));

    // reset() with its default makes the next sample trigger.
    sampleHold primed;
    primed.reset();
    CHECK(Run(primed, Counter(2 * PERIOD), ramp) == Held(2 * PERIOD, {0, 128}));

    // The threshold does not matter in this mode; set() is the held value.
    sampleHold held;
    held.threshold(0.5f).set(-3.f);
    CHECK(Run(held, Counter(2 * PERIOD), ramp) == Held(2 * PERIOD, {128}, -3.f));
  }

  TEST_CASE("sampleHold: ascending and descending against a phasor's closed form (#1082)") {
    const std::vector<float> rising = Ramp(4 * PERIOD);
    const std::vector<float> falling = Ramp(4 * PERIOD, -375.f);
    const std::vector<float> in = Counter(4 * PERIOD);

    // Ascending, threshold 0.5: sample 64 is exactly 0.5 (at, not above), so
    // the trigger is the next one, 65, then once per cycle.
    sampleHold up;
    up.triggerMode(sampleHold::ASCENDING).threshold(0.5f);
    CHECK(Run(up, in, rising) == Held(4 * PERIOD, {65, 193, 321, 449}));

    // Threshold 0 on a rising ramp: the first step above 0 in each cycle.
    sampleHold zero;
    zero.triggerMode(sampleHold::ASCENDING);
    CHECK(Run(zero, in, rising) == Held(4 * PERIOD, {1, 129, 257, 385}));

    // Ascending does not see a falling ramp cross 0.5; descending does, at
    // the first sample below it.
    sampleHold upOnFalling;
    upOnFalling.triggerMode(sampleHold::ASCENDING).threshold(0.5f);
    const std::vector<float> outUp = Run(upOnFalling, in, falling);
    sampleHold down;
    down.triggerMode(sampleHold::DESCENDING).threshold(0.5f);
    const std::vector<float> outDown = Run(down, in, falling);
    // The previous control starts at 0.
    std::vector<unsigned int> downAt;
    std::vector<unsigned int> upAt;
    float previous = 0.f;
    for (unsigned int n = 0; n < falling.size(); ++n) {
      if (previous >= 0.5f && falling[n] < 0.5f) downAt.push_back(n);
      if (previous <= 0.5f && falling[n] > 0.5f) upAt.push_back(n);
      previous = falling[n];
    }
    REQUIRE(downAt.size() == 4);
    REQUIRE(upAt.size() == 4);
    CHECK(outDown == Held(4 * PERIOD, downAt));
    // A falling ramp jumps up from near 0 to near 1 at its reset: that is an
    // ascending crossing of 0.5, as the reference's plain comparison reads it.
    CHECK(outUp == Held(4 * PERIOD, upAt));
  }

  TEST_CASE("sampleHold: both, equals, zero-to-nonzero and nonzero-to-zero (#1082)") {
    const std::vector<float> trigger = {0.f, 1.f, 1.f, 0.f, -1.f, 0.f, 2.f, 1.f, 1.f, 0.f};
    const std::vector<float> in = Counter(static_cast<unsigned int>(trigger.size()));
    const unsigned int n = static_cast<unsigned int>(trigger.size());

    sampleHold both;
    both.triggerMode(sampleHold::BOTH).threshold(0.5f);
    CHECK(Run(both, in, trigger) == Held(n, {1, 3, 6, 9}));

    // Equals takes every sample on the threshold, following the input there.
    sampleHold equals;
    equals.triggerMode(sampleHold::EQUALS).threshold(1.f);
    CHECK(Run(equals, in, trigger) == Held(n, {1, 2, 7, 8}));

    // The threshold is ignored by the zero modes. The previous control
    // starts at 0, so sample 1 leaves zero.
    sampleHold rise;
    rise.triggerMode(sampleHold::ZERO_TO_NONZERO).threshold(5.f);
    CHECK(Run(rise, in, trigger) == Held(n, {1, 4, 6}));
    sampleHold fall;
    fall.triggerMode(sampleHold::NONZERO_TO_ZERO).threshold(5.f);
    CHECK(Run(fall, in, trigger) == Held(n, {3, 5, 9}));
  }

  TEST_CASE("sampleHold: phase mode triggers where a ramp passes the threshold, either way "
            "(#1082)") {
    const std::vector<float> rising = Ramp(4 * PERIOD);
    const std::vector<float> falling = Ramp(4 * PERIOD, -375.f);
    const std::vector<float> in = Counter(4 * PERIOD);

    // Threshold 0: once per cycle at the reset. As for phasorWhat a rise
    // passes t when a <= t < b, so the reset sample, which lands exactly on
    // 0, fires on the sample after it: 129, 257, 385. The previous control
    // starts at 0, so sample 1 moves on from it too.
    sampleHold resets;
    resets.triggerMode(sampleHold::PHASE);
    const std::vector<float> outRising = Run(resets, in, rising);
    CHECK(outRising == Held(4 * PERIOD, {1, 129, 257, 385}));

    // 1 is the same phase as 0.
    sampleHold one;
    one.triggerMode(sampleHold::PHASE).threshold(1.f);
    CHECK(Run(one, in, rising) == outRising);

    // A falling ramp passes 0 at its reset too, where the plain descending
    // and ascending modes would not see one crossing a cycle.
    sampleHold back;
    back.triggerMode(sampleHold::PHASE);
    const std::vector<float> outFalling = Run(back, in, falling);
    // A fall passes t when b < t <= a: the step from 0 up to near 1 (a
    // backward wrap) fires at once. The previous control starts at 0.
    std::vector<unsigned int> fallingAt;
    float previous = 0.f;
    for (unsigned int n = 0; n < falling.size(); ++n) {
      if (falling[n] > previous + 0.5f) fallingAt.push_back(n);
      previous = falling[n];
    }
    REQUIRE(fallingAt.size() == 4);
    CHECK(outFalling == Held(4 * PERIOD, fallingAt));

    // An inner phase: rising through 0.25 at a <= 0.25 < b, sample 33.
    sampleHold quarter;
    quarter.triggerMode(sampleHold::PHASE).threshold(0.25f);
    CHECK(Run(quarter, in, rising) == Held(4 * PERIOD, {33, 161, 289, 417}));
  }

  TEST_CASE("sampleHold: duration holds the value that many samples, then 0 (#1082)") {
    const std::vector<float> rising = Ramp(3 * PERIOD);
    const std::vector<float> in = Counter(3 * PERIOD);
    sampleHold fn;
    fn.triggerMode(sampleHold::ASCENDING).threshold(0.5f).duration(3);
    const std::vector<float> out = Run(fn, in, rising);
    for (unsigned int n = 0; n < out.size(); ++n) {
      CAPTURE(n);
      const unsigned int k = n % PERIOD;
      const float expected = (k >= 65 && k < 68) ? static_cast<float>(n - k + 65) : 0.f;
      CHECK(out[n] == expected);
    }

    // Across a block edge: the trigger at 126 lasts into the next block.
    const std::vector<float> trigger = [] {
      std::vector<float> t(2 * N, 0.f);
      t[126] = 1.f;
      return t;
    }();
    sampleHold edge;
    edge.triggerMode(sampleHold::ZERO_TO_NONZERO).duration(5);
    const std::vector<float> outEdge = Run(edge, Counter(2 * N), trigger);
    for (unsigned int n = 0; n < outEdge.size(); ++n) {
      CAPTURE(n);
      CHECK(outEdge[n] == ((n >= 126 && n < 131) ? 126.f : 0.f));
    }

    // Back to 0: held until the next trigger again.
    fn.duration(0);
    const std::vector<float> outHeld = Run(fn, in, rising);
    CHECK(outHeld[300] == 321.f - 128.f);
  }

  TEST_CASE("sampleHold: a non-finite control sample is skipped (#1082)") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    // 0, NaN, 1: the 1 is compared with the 0 and rises; inf never triggers.
    const std::vector<float> trigger = {0.f, nan, 1.f, inf, 0.f, nan, 1.f};
    const unsigned int n = static_cast<unsigned int>(trigger.size());
    sampleHold fn;
    fn.triggerMode(sampleHold::ASCENDING).threshold(0.5f);
    CHECK(Run(fn, Counter(n), trigger) == Held(n, {2, 6}));
  }

  TEST_CASE("sampleHold: block size does not change the output; buffers are clamped (#1082)") {
    const std::vector<float> rising = Ramp(4 * PERIOD);
    const std::vector<float> in = Counter(4 * PERIOD);
    for (int mode = sampleHold::ASCENDING; mode <= sampleHold::DECREASE; ++mode) {
      CAPTURE(mode);
      sampleHold whole;
      whole.triggerMode(static_cast<sampleHold::TRIGGER_MODE>(mode)).threshold(0.25f).duration(7);
      sampleHold pieces;
      pieces.triggerMode(static_cast<sampleHold::TRIGGER_MODE>(mode)).threshold(0.25f).duration(7);
      CHECK(Run(whole, in, rising) == Run(pieces, in, rising, 17));
    }

    // A shorter trigger buffer bounds the block.
    sampleHold fn;
    buffer a(N);
    buffer b(N / 2);
    buffer& out = fn(a, b);
    CHECK(out.getLength() == N / 2);
  }

  TEST_CASE("sampleHold: processing allocates nothing (#1082)") {
    sampleHold fn;
    fn.triggerMode(sampleHold::PHASE).duration(4);
    buffer in(N);
    buffer trigger(N);
    buffer shorter(N / 4);
    for (unsigned int i = 0; i < N; ++i) {
      in.getPtr()[i] = static_cast<float>(i);
      trigger.getPtr()[i] = static_cast<float>(i % 32) / 32.f;
    }
    {
      TestHelpers::ProbeScope probe;
      for (int mode = sampleHold::ASCENDING; mode <= sampleHold::DECREASE; ++mode) {
        fn.triggerMode(static_cast<sampleHold::TRIGGER_MODE>(mode));
        fn(in, trigger);
        fn(shorter, trigger);
      }
      fn(in, trigger);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
