// Tests for YSE::DSP::triggeredRamp (YseEngine/dsp/generators/triggeredRamp.hpp),
// issue #1072. Reference output is closed form: a trigger at sample t0 gives
// start + (end - start) * p^e with p = (n - t0) / T, T the duration in
// samples and e = (1 + curve) / (1 - curve), reaching end at n - t0 >= T.

#include <doctest/doctest.h>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>
#include "dsp/generators/triggeredRamp.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::triggeredRamp;
using mode = YSE::DSP::triggeredRamp::durationMode;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  using Signal = std::function<float(unsigned int)>;

  // `samples` samples of `fn` run block by block, the trigger at sample n
  // being `trigger(n)`; with `duration`, `start` and `end` the four-buffer
  // call is used.
  std::vector<float> Run(triggeredRamp& fn, unsigned int samples, const Signal& trigger,
                         const Signal& duration = nullptr, const Signal& start = nullptr,
                         const Signal& end = nullptr) {
    std::vector<float> result;
    buffer in(N);
    buffer d(N);
    buffer s(N);
    buffer e(N);
    for (unsigned int first = 0; first < samples; first += N) {
      for (unsigned int i = 0; i < N; ++i) {
        const unsigned int n = first + i;
        in.getPtr()[i] = trigger(n);
        d.getPtr()[i] = duration ? duration(n) : fn.duration();
        s.getPtr()[i] = start ? start(n) : fn.start();
        e.getPtr()[i] = end ? end(n) : fn.end();
      }
      buffer& out = duration ? fn(in, d, s, e) : fn(in);
      REQUIRE(out.getLength() == N);
      for (unsigned int i = 0; i < N && first + i < samples; ++i)
        result.push_back(out.getPtr()[i]);
    }
    return result;
  }

  // 1 from sample `at` on (one trigger), 0 before.
  Signal StepAt(unsigned int at) {
    return [at](unsigned int n) { return n >= at ? 1.f : 0.f; };
  }

  // One-sample pulses at each of `at`.
  Signal Pulses(std::vector<unsigned int> at) {
    return [at](unsigned int n) {
      for (unsigned int a : at)
        if (n == a) return 1.f;
      return 0.f;
    };
  }

  float Linear(unsigned int n, unsigned int t0, float samples, float from, float to) {
    if (n < t0) return from;
    const float p = static_cast<float>(n - t0) / samples;
    return p >= 1.f ? to : from + (to - from) * p;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("triggeredRamp: defaults and setters (#1072)") {
    triggeredRamp fn;
    CHECK(fn.duration() == triggeredRamp::DEFAULT_DURATION);
    CHECK(fn.start() == 0.f);
    CHECK(fn.end() == 1.f);
    CHECK(fn.curve() == 0.f);
    CHECK(fn.mode() == mode::atStart);
    CHECK_FALSE(fn.resetToZero());
    CHECK_FALSE(fn.retrigger());
    CHECK_FALSE(fn.running());
    CHECK(fn.phase() == 0.f);

    fn.duration(250.f).start(-2.f).end(3.f).curve(0.5f).mode(mode::continuous);
    fn.resetToZero(true).retrigger(true);
    CHECK(fn.duration() == 250.f);
    CHECK(fn.start() == -2.f);
    CHECK(fn.end() == 3.f);
    CHECK(fn.curve() == 0.5f);
    CHECK(fn.mode() == mode::continuous);
    CHECK(fn.resetToZero());
    CHECK(fn.retrigger());

    // Not finite: ignored. Negative durations: 0. Curves clamp to -1..1.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    fn.duration(nan).start(inf).end(-inf).curve(nan);
    CHECK(fn.duration() == 250.f);
    CHECK(fn.start() == -2.f);
    CHECK(fn.end() == 3.f);
    CHECK(fn.curve() == 0.5f);
    fn.duration(-5.f).curve(3.f);
    CHECK(fn.duration() == 0.f);
    CHECK(fn.curve() == 1.f);
    fn.curve(-7.f);
    CHECK(fn.curve() == -1.f);
  }

  TEST_CASE("triggeredRamp: before a trigger it outputs start; a trigger gives the closed-form "
            "linear ramp, then holds end (#1072)") {
    TestHelpers::ScopedSampleRate rate(48000);
    for (float ms : {1.f, 5.f, 10.f, 12.345f}) {
      CAPTURE(ms);
      triggeredRamp fn;
      fn.duration(ms).start(0.25f).end(-0.75f);
      const float samples = ms * 48.f;
      constexpr unsigned int t0 = 100; // mid-block
      std::vector<float> out = Run(fn, 1024, StepAt(t0));
      for (unsigned int n = 0; n < out.size(); ++n) {
        CAPTURE(n);
        CHECK(out[n] == doctest::Approx(Linear(n, t0, samples, 0.25f, -0.75f)).epsilon(1e-5));
      }
      CHECK(out.back() == -0.75f);
      CHECK_FALSE(fn.running());
      CHECK(fn.phase() == 1.f);
    }
  }

  TEST_CASE("triggeredRamp: a trigger is a 0 to non-zero edge; held or repeated non-zero "
            "samples do not trigger again (#1072)") {
    TestHelpers::ScopedSampleRate rate(48000);
    triggeredRamp fn;
    fn.duration(1.f); // 48 samples
    // Non-zero from the very first sample counts (the input before it was 0),
    // a negative value counts, and a held value does not trigger again.
    std::vector<float> out = Run(fn, 256, [](unsigned int n) { return n < 60 ? -0.5f : 0.3f; });
    for (unsigned int n = 0; n < out.size(); ++n) {
      CAPTURE(n);
      CHECK(out[n] == doctest::Approx(Linear(n, 0, 48.f, 0.f, 1.f)).epsilon(1e-5));
    }
    // Back to 0 and up again: a second ramp.
    out = Run(fn, 256, [](unsigned int n) { return n < 10 ? 0.f : 1.f; });
    CHECK(out[0] == 1.f);
    for (unsigned int n = 10; n < out.size(); ++n) {
      CAPTURE(n);
      CHECK(out[n] == doctest::Approx(Linear(n, 10, 48.f, 0.f, 1.f)).epsilon(1e-5));
    }
  }

  TEST_CASE("triggeredRamp: retrigger off ignores triggers while a ramp runs; on restarts "
            "from start (#1072)") {
    TestHelpers::ScopedSampleRate rate(48000);
    // 2 ms = 96 samples; pulses at 0, 40 (during the ramp) and 200 (after).
    const Signal pulses = Pulses({0, 40, 200});
    triggeredRamp off;
    off.duration(2.f);
    std::vector<float> out = Run(off, 512, pulses);
    for (unsigned int n = 0; n < 200; ++n) {
      CAPTURE(n);
      CHECK(out[n] == doctest::Approx(Linear(n, 0, 96.f, 0.f, 1.f)).epsilon(1e-5));
    }
    for (unsigned int n = 200; n < out.size(); ++n) {
      CAPTURE(n);
      CHECK(out[n] == doctest::Approx(Linear(n, 200, 96.f, 0.f, 1.f)).epsilon(1e-5));
    }

    triggeredRamp on;
    on.duration(2.f).retrigger(true);
    out = Run(on, 512, pulses);
    for (unsigned int n = 0; n < 40; ++n)
      CHECK(out[n] == doctest::Approx(Linear(n, 0, 96.f, 0.f, 1.f)).epsilon(1e-5));
    for (unsigned int n = 40; n < 200; ++n) {
      CAPTURE(n);
      CHECK(out[n] == doctest::Approx(Linear(n, 40, 96.f, 0.f, 1.f)).epsilon(1e-5));
    }
    CHECK(out[200] == 0.f);
  }

  TEST_CASE("triggeredRamp: reset returns to 0 after the ramp and before the first trigger "
            "(#1072)") {
    TestHelpers::ScopedSampleRate rate(48000);
    triggeredRamp fn;
    fn.duration(1.f).start(0.5f).end(2.f).resetToZero(true);
    std::vector<float> out = Run(fn, 256, StepAt(20));
    for (unsigned int n = 0; n < 20; ++n)
      CHECK(out[n] == 0.f);
    for (unsigned int n = 20; n <= 20 + 48; ++n) {
      CAPTURE(n);
      CHECK(out[n] == doctest::Approx(Linear(n, 20, 48.f, 0.5f, 2.f)).epsilon(1e-5));
    }
    CHECK(out[68] == 2.f); // end is reached, for one sample
    for (unsigned int n = 69; n < out.size(); ++n)
      CHECK(out[n] == 0.f);
  }

  TEST_CASE("triggeredRamp: curve bends the phase as p^((1+c)/(1-c)) (#1072)") {
    TestHelpers::ScopedSampleRate rate(48000);
    for (float c : {-0.5f, 0.5f, 0.9f}) {
      CAPTURE(c);
      triggeredRamp fn;
      fn.duration(2.f).curve(c).end(4.f);
      const float exponent = (1.f + c) / (1.f - c);
      std::vector<float> out = Run(fn, 256, StepAt(0));
      for (unsigned int n = 0; n < 96; ++n) {
        CAPTURE(n);
        const float p = static_cast<float>(n) / 96.f;
        CHECK(out[n] == doctest::Approx(4.f * std::pow(p, exponent)).epsilon(1e-5));
      }
      CHECK(out[96] == 4.f);
      // Below 0 a rising ramp runs above the straight line, above 0 below it.
      if (c < 0.f) {
        CHECK(out[48] > 2.f);
      } else {
        CHECK(out[48] < 2.f);
      }
    }
    // The limits are steps: -1 jumps to end at once, +1 holds start.
    triggeredRamp jump;
    jump.duration(1.f).curve(-1.f);
    std::vector<float> out = Run(jump, 64, StepAt(0));
    CHECK(out[0] == 0.f);
    CHECK(out[1] == 1.f);
    triggeredRamp hold;
    hold.duration(1.f).curve(1.f);
    out = Run(hold, 64, StepAt(0));
    CHECK(out[47] == 0.f);
    CHECK(out[48] == 1.f);
  }

  TEST_CASE("triggeredRamp: a duration of 0 jumps to end on the trigger sample (#1072)") {
    TestHelpers::ScopedSampleRate rate(48000);
    triggeredRamp fn;
    fn.duration(0.f).start(3.f).end(7.f);
    std::vector<float> out = Run(fn, 64, StepAt(5));
    CHECK(out[4] == 3.f);
    CHECK(out[5] == 7.f);
    CHECK(out[63] == 7.f);
  }

  TEST_CASE("triggeredRamp: the duration follows the live sample rate (#1072)") {
    TestHelpers::ScopedSampleRate rate(48000);
    triggeredRamp fn;
    fn.duration(1.f);
    std::vector<float> out = Run(fn, N, StepAt(0));
    CHECK(out[24] == doctest::Approx(0.5f));
    YSE::SAMPLERATE = 96000;
    out = Run(fn, N, [](unsigned int n) { return n < 1 ? 0.f : 1.f; });
    CHECK(out[1 + 48] == doctest::Approx(0.5f));
    CHECK(out[1 + 96] == 1.f);
  }

  TEST_CASE("triggeredRamp: mode atStart latches the duration; continuous rescales what is "
            "left (#1072)") {
    TestHelpers::ScopedSampleRate rate(48000);
    // 10 ms (480 samples), changed to 15 ms (720 samples) after 4 ms (192):
    // Max's example scaled down. The atStart ramp ignores the change.
    const Signal duration = [](unsigned int n) { return n < 192 ? 10.f : 15.f; };
    triggeredRamp fixed;
    std::vector<float> out = Run(fixed, 1024, StepAt(0), duration);
    for (unsigned int n = 0; n < out.size(); ++n) {
      CAPTURE(n);
      CHECK(out[n] == doctest::Approx(Linear(n, 0, 480.f, 0.f, 1.f)).epsilon(1e-5));
    }

    triggeredRamp live;
    live.mode(mode::continuous);
    out = Run(live, 1024, StepAt(0), duration);
    for (unsigned int n = 0; n < 192; ++n) {
      CAPTURE(n);
      CHECK(out[n] == doctest::Approx(static_cast<float>(n) / 480.f).epsilon(1e-5));
    }
    // From 191's phase, the rest stretched to end at sample 720.
    const float p191 = 191.f / 480.f;
    for (unsigned int n = 192; n < 720; ++n) {
      CAPTURE(n);
      const float expected = p191 + (1.f - p191) * static_cast<float>(n - 191) / (720.f - 191.f);
      CHECK(out[n] == doctest::Approx(expected).epsilon(1e-5));
    }
    CHECK(out[719] < 1.f);
    CHECK(out[720] == 1.f);

    // A duration already past ends the ramp at once.
    triggeredRamp shrink;
    shrink.mode(mode::continuous);
    out = Run(shrink, 512, StepAt(0), [](unsigned int n) { return n < 100 ? 10.f : 1.f; });
    CHECK(out[99] == doctest::Approx(99.f / 480.f));
    CHECK(out[100] == 1.f);
    CHECK_FALSE(shrink.running());
  }

  TEST_CASE("triggeredRamp: start and end buffers are read at every sample (#1072)") {
    TestHelpers::ScopedSampleRate rate(48000);
    triggeredRamp fn;
    const Signal start = [](unsigned int n) { return 0.001f * static_cast<float>(n); };
    const Signal end = [](unsigned int n) { return n < 300 ? 1.f : -1.f; };
    std::vector<float> out =
        Run(fn, 512, StepAt(10), [](unsigned int) { return 5.f; }, start, end); // 240 samples
    for (unsigned int n = 0; n < out.size(); ++n) {
      CAPTURE(n);
      const float s = start(n);
      const float e = end(n);
      CHECK(out[n] == doctest::Approx(Linear(n, 10, 240.f, s, e)).epsilon(1e-5));
    }
    CHECK(out[400] == -1.f);
  }

  TEST_CASE("triggeredRamp: non-finite samples read as 0 (trigger) or the setter (the rest) "
            "(#1072)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    triggeredRamp fn;
    fn.duration(1.f);
    // NaN between two 1s is a 0: the second 1 triggers again... but retrigger
    // is off and the ramp runs, so only the first starts one.
    std::vector<float> out = Run(fn, 128, [nan](unsigned int n) {
      if (n == 0) return 1.f;
      if (n == 10) return nan;
      return 1.f;
    });
    for (unsigned int n = 0; n < out.size(); ++n)
      CHECK(out[n] == doctest::Approx(Linear(n, 0, 48.f, 0.f, 1.f)).epsilon(1e-5));
    fn.retrigger(true);
    out = Run(fn, 128, [nan](unsigned int n) { return n == 5 ? nan : 1.f; });
    // 0 -> 1 at 0 is no edge (the last block ended on 1); NaN at 5 is a 0,
    // so 6 triggers.
    CHECK(out[5] == 1.f);
    CHECK(out[6] == 0.f);
    CHECK(out[30] == doctest::Approx(24.f / 48.f));

    triggeredRamp setters;
    setters.duration(1.f).start(0.f).end(2.f);
    out = Run(
        setters, 128, StepAt(0), [nan](unsigned int n) { return n == 0 ? nan : 1.f; },
        [nan](unsigned int) { return nan; }, [nan](unsigned int n) { return n == 24 ? nan : 2.f; });
    CHECK(out[0] == 0.f);
    CHECK(out[24] == doctest::Approx(1.f));
  }

  TEST_CASE("triggeredRamp: processing and setters allocate nothing (#1072)") {
    triggeredRamp fn;
    buffer in(N);
    buffer d(N);
    in = 1.f;
    d = 3.f;
    fn(in);
    TestHelpers::ProbeScope probe;
    fn.duration(5.f).start(0.5f).end(0.f).curve(0.3f).mode(mode::continuous);
    fn.resetToZero(true).retrigger(true);
    fn(in);
    fn(in, d, d, d);
    CHECK(TestHelpers::g_alloc_count.load() == 0);
  }

} // TEST_SUITE("dsp")
