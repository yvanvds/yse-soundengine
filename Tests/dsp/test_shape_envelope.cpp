// Tests for YSE::DSP::shapeEnvelope (YseEngine/dsp/generators/shapeEnvelope.hpp),
// issue #1071. Reference output is closed form: for a ramp at position p the
// envelope is read at time p * total, so the same breakpoints give the same
// shape for ramps of any length; constant segments are counted in samples at
// the live rate instead.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>
#include "dsp/generators/shapeEnvelope.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::shapeEnvelope;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // `samples` samples of `fn` run block by block, the input at sample n
  // being `input(n)`.
  std::vector<float> Run(shapeEnvelope& fn, unsigned int samples,
                         const std::function<float(unsigned int)>& input) {
    std::vector<float> result;
    buffer in(N);
    for (unsigned int start = 0; start < samples; start += N) {
      for (unsigned int i = 0; i < N; ++i)
        in.getPtr()[i] = input(start + i);
      buffer& out = fn(in);
      REQUIRE(out.getLength() == N);
      for (unsigned int i = 0; i < N && start + i < samples; ++i)
        result.push_back(out.getPtr()[i]);
    }
    return result;
  }

  std::function<float(unsigned int)> Ramp(unsigned int period) {
    return [period](unsigned int n) {
      return static_cast<float>(n % period) / static_cast<float>(period);
    };
  }

  // A falling ramp, 1 at the start of each cycle.
  std::function<float(unsigned int)> FallingRamp(unsigned int period) {
    return [period](unsigned int n) {
      return 1.f - static_cast<float>(n % period) / static_cast<float>(period);
    };
  }

  // The envelope 0 -> 1 over 100 ms -> 0 over 300 ms at time t (ms).
  float AttackRelease(float t) {
    if (t < 100.f) return t / 100.f;
    if (t < 400.f) return 1.f - (t - 100.f) / 300.f;
    return 0.f;
  }

  void SetAttackRelease(shapeEnvelope& fn) {
    const float values[] = {1.f, 0.f};
    const float times[] = {100.f, 300.f};
    REQUIRE(fn.breakpoints(0.f, values, times, nullptr, 2));
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("shapeEnvelope: defaults, capacity and the breakpoint store (#1071)") {
    shapeEnvelope fn;
    CHECK(fn.capacity() == shapeEnvelope::DEFAULT_POINTS);
    CHECK(fn.count() == 0);
    CHECK(fn.initial() == 0.f);
    CHECK(fn.constant() == 0);
    CHECK_FALSE(fn.curved());
    CHECK_FALSE(fn.syncUpdate());
    CHECK_FALSE(fn.pending());
    // No breakpoints: the initial value, whatever the ramp.
    for (float v : Run(fn, 2 * N, Ramp(100)))
      CHECK(v == 0.f);
    const float none[] = {0.f};
    REQUIRE(fn.breakpoints(0.25f, none, none, nullptr, 0));
    for (float v : Run(fn, N, Ramp(100)))
      CHECK(v == 0.25f);

    CHECK(shapeEnvelope(0).capacity() == 1);
    CHECK(shapeEnvelope(1u << 20).capacity() == shapeEnvelope::MAX_POINTS);

    // Negative times are 0; curves clamp to -1..1.
    const float values[] = {1.f, 2.f, 3.f};
    const float times[] = {10.f, -5.f, 20.f};
    const float curves[] = {0.5f, 3.f, -7.f};
    REQUIRE(fn.breakpoints(0.5f, values, times, curves, 3));
    CHECK(fn.count() == 3);
    CHECK(fn.initial() == 0.5f);
    CHECK(fn.value(1) == 2.f);
    CHECK(fn.time(1) == 0.f);
    CHECK(fn.curve(0) == 0.5f);
    CHECK(fn.curve(1) == 1.f);
    CHECK(fn.curve(2) == -1.f);
    CHECK(fn.value(3) == 0.f);

    // Refused whole: too many points, or a value that is not finite.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    shapeEnvelope small(2);
    CHECK_FALSE(small.breakpoints(0.f, values, times, nullptr, 3));
    const float bad[] = {1.f, nan};
    CHECK_FALSE(fn.breakpoints(0.f, bad, times, nullptr, 2));
    CHECK_FALSE(fn.breakpoints(0.f, values, bad, nullptr, 2));
    CHECK_FALSE(fn.breakpoints(0.f, values, times, bad, 2));
    CHECK_FALSE(fn.breakpoints(nan, values, times, nullptr, 2));
    CHECK(fn.count() == 3);
    CHECK(fn.initial() == 0.5f);
  }

  TEST_CASE("shapeEnvelope: the envelope scales to rising ramps of any length (#1071)") {
    for (unsigned int period : {256u, 1000u, 4096u}) {
      CAPTURE(period);
      shapeEnvelope fn;
      SetAttackRelease(fn);
      const std::vector<float> out = Run(fn, 3 * period, Ramp(period));
      for (unsigned int n = 0; n < out.size(); ++n) {
        CAPTURE(n);
        const float position = static_cast<float>(n % period) / static_cast<float>(period);
        CHECK(out[n] == doctest::Approx(AttackRelease(position * 400.f)).epsilon(1e-4));
      }
    }
  }

  TEST_CASE("shapeEnvelope: a falling ramp plays the envelope forward (#1071)") {
    const unsigned int period = 1000;
    shapeEnvelope fn;
    SetAttackRelease(fn);
    const std::vector<float> out = Run(fn, 3 * period, FallingRamp(period));
    for (unsigned int n = 0; n < out.size(); ++n) {
      CAPTURE(n);
      const float position = static_cast<float>(n % period) / static_cast<float>(period);
      CHECK(out[n] == doctest::Approx(AttackRelease(position * 400.f)).epsilon(1e-4));
    }
  }

  TEST_CASE("shapeEnvelope: a zero time jumps, the last value holds past the end (#1071)") {
    // .function's ramp list: `y0 0 y1 dt ...`.
    shapeEnvelope fn;
    const float values[] = {0.5f, 1.f};
    const float times[] = {0.f, 100.f};
    REQUIRE(fn.breakpoints(0.f, values, times, nullptr, 2));
    buffer in(N);
    in = 0.f;
    CHECK(fn(in).getPtr()[0] == 0.5f);
    in = 0.5f;
    CHECK(fn(in).getPtr()[N - 1] == doctest::Approx(0.75f));
    in = 1.f;
    CHECK(fn(in).getPtr()[N - 1] == 1.f);

    // All times 0: the last value.
    const float zeros[] = {0.f, 0.f};
    REQUIRE(fn.breakpoints(0.f, values, zeros, nullptr, 2));
    in = 0.25f;
    CHECK(fn(in).getPtr()[N - 1] == 1.f);
  }

  TEST_CASE("shapeEnvelope: curvature bends a segment when curves are on (#1071)") {
    const float values[] = {1.f};
    const float times[] = {100.f};
    const unsigned int period = 1024;
    for (float c : {0.5f, -0.5f, 0.f}) {
      CAPTURE(c);
      const float curves[] = {c};
      shapeEnvelope fn;
      REQUIRE(fn.breakpoints(0.f, values, times, curves, 1));
      // Off (Max's curvemode 0): linear whatever the curve.
      std::vector<float> out = Run(fn, period, Ramp(period));
      for (unsigned int n = 0; n < period; ++n)
        CHECK(out[n] == doctest::Approx(static_cast<float>(n) / period).epsilon(1e-5));
      fn.curved(true);
      out = Run(fn, period, [&](unsigned int n) { return Ramp(period)(n + period); });
      const float exponent = (1.f + c) / (1.f - c);
      for (unsigned int n = 0; n < period; ++n) {
        CAPTURE(n);
        const float f = static_cast<float>(n) / period;
        CHECK(out[n] == doctest::Approx(std::pow(f, exponent)).epsilon(1e-4));
      }
    }

    // +1 holds the start until the end, -1 jumps at once.
    shapeEnvelope hold;
    hold.curved(true);
    const float up[] = {1.f};
    REQUIRE(hold.breakpoints(0.f, values, times, up, 1));
    buffer in(N);
    in = 0.9f;
    CHECK(hold(in).getPtr()[N - 1] == 0.f);
    const float down[] = {-1.f};
    REQUIRE(hold.breakpoints(0.f, values, times, down, 1));
    in = 0.1f;
    CHECK(hold(in).getPtr()[N - 1] == 1.f);
  }

  TEST_CASE("shapeEnvelope: constant segments keep their time in ms at the live rate "
            "(#1071)") {
    for (unsigned int rate : {48000u, 96000u}) {
      for (unsigned int period : {4800u, 9600u}) {
        CAPTURE(rate);
        CAPTURE(period);
        TestHelpers::ScopedSampleRate scoped(rate);
        shapeEnvelope fn;
        const float values[] = {1.f, 0.f};
        const float times[] = {10.f, 90.f};
        REQUIRE(fn.breakpoints(0.f, values, times, nullptr, 2));
        fn.constant(1);
        const std::vector<float> out = Run(fn, 3 * period, Ramp(period));
        const float msPerSample = 1000.f / static_cast<float>(rate);
        // The attack is 10 ms: 480 samples at 48 kHz, 960 at 96 kHz, however
        // long the ramp. Cycles after the first start at their wrap sample.
        unsigned int attackEnd = 0;
        while (static_cast<float>(attackEnd) * msPerSample < 10.f)
          attackEnd++;
        const float from = static_cast<float>(attackEnd) / static_cast<float>(period);
        for (unsigned int n = period; n < out.size(); ++n) {
          CAPTURE(n);
          const unsigned int e = n % period;
          const float ms = static_cast<float>(e) * msPerSample;
          float expected = 0.f;
          if (e < attackEnd) {
            expected = ms / 10.f;
          } else {
            const float x = static_cast<float>(e) / static_cast<float>(period);
            expected = 1.f - (x - from) / (1.f - from);
          }
          CHECK(out[n] == doctest::Approx(expected).epsilon(1e-4));
        }
        CHECK(attackEnd >= rate / 100);
        CHECK(attackEnd <= rate / 100 + 1);
      }
    }
  }

  TEST_CASE("shapeEnvelope: constant segments wait while the ramp is off and only apply to "
            "rising ramps (#1071)") {
    TestHelpers::ScopedSampleRate scoped(48000);
    shapeEnvelope fn;
    const float values[] = {1.f, 0.f};
    const float times[] = {10.f, 90.f};
    REQUIRE(fn.breakpoints(0.f, values, times, nullptr, 2));
    fn.constant(1);
    // Off for a second: the clock waits at the start.
    for (float v : Run(fn, 48000, [](unsigned int) { return 0.f; }))
      CHECK(v == 0.f);
    // Turning on starts the attack: 480 samples to the top.
    const std::vector<float> on = Run(fn, 1024, [](unsigned int n) { return (n + 1) * 1e-5f; });
    CHECK(on[0] == 0.f);
    CHECK(on[240] == doctest::Approx(0.5f).epsilon(1e-4));
    CHECK(on[479] == doctest::Approx(479.f / 480.f).epsilon(1e-4));

    // A falling ramp scales the whole envelope: constant is ignored.
    shapeEnvelope falling;
    REQUIRE(falling.breakpoints(0.f, values, times, nullptr, 2));
    falling.constant(1);
    const unsigned int period = 4800;
    const std::vector<float> out = Run(falling, 2 * period, FallingRamp(period));
    for (unsigned int n = 0; n < out.size(); ++n) {
      const float position = static_cast<float>(n % period) / static_cast<float>(period);
      const float t = position * 100.f;
      const float expected = t < 10.f ? t / 10.f : 1.f - (t - 10.f) / 90.f;
      CHECK(out[n] == doctest::Approx(expected).epsilon(1e-4));
    }

    // More constant segments than there are: all play in time, then hold.
    shapeEnvelope all;
    REQUIRE(all.breakpoints(0.f, values, times, nullptr, 2));
    all.constant(5);
    const std::vector<float> slow = Run(all, 9600, Ramp(96000));
    CHECK(slow[2400] == doctest::Approx(1.f - 40.f / 90.f).epsilon(1e-3));
    CHECK(slow[9599] == 0.f);
  }

  TEST_CASE("shapeEnvelope: syncUpdate holds a new envelope until the ramp starts again "
            "(#1071)") {
    const unsigned int period = 1024;
    shapeEnvelope fn;
    SetAttackRelease(fn);
    fn.syncUpdate(true);
    (void)Run(fn, 384, Ramp(period));
    // Mid-cycle: the old envelope plays on.
    const float values[] = {0.5f};
    const float times[] = {1.f};
    REQUIRE(fn.breakpoints(0.5f, values, times, nullptr, 1));
    CHECK(fn.pending());
    CHECK(fn.count() == 2);
    const std::vector<float> rest =
        Run(fn, period - 384, [&](unsigned int n) { return Ramp(period)(n + 384); });
    for (unsigned int n = 0; n < rest.size(); ++n) {
      const float position = static_cast<float>(n + 384) / period;
      CHECK(rest[n] == doctest::Approx(AttackRelease(position * 400.f)).epsilon(1e-4));
    }
    // The wrap applies it.
    const std::vector<float> next = Run(fn, N, Ramp(period));
    CHECK_FALSE(fn.pending());
    CHECK(fn.count() == 1);
    for (float v : next)
      CHECK(v == 0.5f);

    // Of two waiting envelopes the last one wins; turning sync off applies it.
    const float one[] = {1.f};
    const float two[] = {2.f};
    REQUIRE(fn.breakpoints(0.f, one, times, nullptr, 1));
    REQUIRE(fn.breakpoints(2.f, two, times, nullptr, 1));
    CHECK(fn.pending());
    fn.syncUpdate(false);
    CHECK_FALSE(fn.pending());
    CHECK(fn.initial() == 2.f);
    // Without sync an envelope applies at once.
    REQUIRE(fn.breakpoints(0.f, one, times, nullptr, 1));
    CHECK(fn.value(0) == 1.f);
  }

  TEST_CASE("shapeEnvelope: input that is not a number counts as 0 (#1071)") {
    shapeEnvelope fn;
    SetAttackRelease(fn);
    buffer in(N);
    in = std::numeric_limits<float>::quiet_NaN();
    buffer& out = fn(in);
    for (unsigned int i = 0; i < N; ++i)
      CHECK(out.getPtr()[i] == 0.f);
    in = std::numeric_limits<float>::infinity();
    CHECK(fn(in).getPtr()[0] == 0.f);
  }

  TEST_CASE("shapeEnvelope: processing and setters allocate nothing (#1071)") {
    shapeEnvelope fn;
    buffer in(N);
    const float values[] = {1.f, 0.f};
    const float times[] = {10.f, 90.f};
    const float curves[] = {0.5f, -0.5f};
    {
      TestHelpers::ProbeScope probe;
      REQUIRE(fn.breakpoints(0.f, values, times, curves, 2));
      fn.constant(1).curved(true).syncUpdate(true);
      REQUIRE(fn.breakpoints(0.2f, values, times, curves, 2));
      for (unsigned int block = 0; block < 8; ++block) {
        for (unsigned int i = 0; i < N; ++i)
          in.getPtr()[i] = static_cast<float>((block * N + i) % 300) / 300.f;
        (void)fn(in);
      }
      fn.syncUpdate(false);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
