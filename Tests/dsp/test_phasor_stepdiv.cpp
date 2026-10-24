// Tests for YSE::DSP::phasorStepDiv (YseEngine/dsp/generators/phasorStepDiv.hpp),
// issue #1075. Driven, the input is a DSP::phasor at 375 Hz and 48 kHz: exactly
// 128 samples per cycle, so a pattern's steps fall on known samples. Free
// running at 48 kHz, 1 ms is exactly 48 samples.

#include <doctest/doctest.h>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>
#include "dsp/generators/phasor.hpp"
#include "dsp/generators/phasorStepDiv.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::phasorStepDiv;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // `samples` samples of a 375 Hz DSP::phasor (128 samples per cycle).
  std::vector<float> Ramp(unsigned int samples) {
    TestHelpers::ScopedSampleRate rate(48000);
    phasor source;
    std::vector<float> result;
    while (result.size() < samples) {
      buffer& out = source(375.f, N);
      for (unsigned int i = 0; i < N && result.size() < samples; ++i)
        result.push_back(out.getPtr()[i]);
    }
    return result;
  }

  struct run {
    std::vector<float> out;
    std::vector<float> index;
  };

  // Driven, sample by sample, calling `before(n)` ahead of sample n.
  run Process(phasorStepDiv& fn, const std::vector<float>& in,
              const std::function<void(unsigned int)>& before = nullptr) {
    run r;
    for (unsigned int n = 0; n < in.size(); ++n) {
      if (before) before(n);
      r.out.push_back(fn.process(in[n]));
      r.index.push_back(fn.lastIndex());
    }
    return r;
  }

  // Free running, sample by sample.
  run Free(phasorStepDiv& fn, unsigned int samples,
           const std::function<void(unsigned int)>& before = nullptr) {
    run r;
    for (unsigned int n = 0; n < samples; ++n) {
      if (before) before(n);
      r.out.push_back(fn.process());
      r.index.push_back(fn.lastIndex());
    }
    return r;
  }

  // The driven reference: step times `times`, input x. Steps of time 0 are
  // passed over; the ramp is the share of the step reached.
  void Reference(float x, const std::vector<float>& times, double& ramp, int& step) {
    double total = 0.0;
    for (const float t : times)
      total += t;
    const double p = static_cast<double>(x) * total;
    double start = 0.0;
    for (unsigned int j = 0; j < times.size(); ++j) {
      if (times[j] > 0.f && p < start + times[j]) {
        ramp = (p - start) / times[j];
        step = static_cast<int>(j);
        return;
      }
      start += times[j];
    }
    ramp = -1.0;
    step = -1;
  }

  void CheckDriven(phasorStepDiv& fn, const std::vector<float>& in,
                   const std::vector<float>& times) {
    const run r = Process(fn, in);
    for (unsigned int n = 0; n < in.size(); ++n) {
      CAPTURE(n);
      double ramp = 0.0;
      int step = 0;
      Reference(in[n], times, ramp, step);
      CHECK(r.out[n] >= 0.f);
      CHECK(r.out[n] < 1.f);
      CHECK(r.out[n] == doctest::Approx(ramp).epsilon(1e-6));
      CHECK(r.index[n] == static_cast<float>(step));
    }
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("phasorStepDiv: defaults and the three ways to give times (#1075)") {
    phasorStepDiv fn;
    CHECK(fn.patternLength() == 0);
    CHECK_FALSE(fn.loop());
    CHECK_FALSE(fn.syncUpdate());
    CHECK_FALSE(fn.running());
    CHECK(fn.time(0) == 0.f);

    // Times as they are; below 0 or not finite count as 0.
    const float times[] = {50.f, -3.f, std::numeric_limits<float>::quiet_NaN(), 25.f};
    fn.pattern(times, 4);
    REQUIRE(fn.patternLength() == 4);
    CHECK(fn.time(0) == 50.f);
    CHECK(fn.time(1) == 0.f);
    CHECK(fn.time(2) == 0.f);
    CHECK(fn.time(3) == 25.f);
    CHECK(fn.time(4) == 0.f);
    std::vector<float> many(100, 1.f);
    fn.pattern(many.data(), 100);
    CHECK(fn.patternLength() == phasorStepDiv::MAX_STEPS);
    fn.pattern(nullptr, 3);
    CHECK(fn.patternLength() == 0);

    // Line format: the times of value-time pairs or value-time-curve triples;
    // an incomplete last group is dropped.
    const float pairs[] = {0.f, 0.f, 1.f, 500.f, 0.f, 250.f, 7.f};
    fn.breakpoints(pairs, 7, false);
    REQUIRE(fn.patternLength() == 3);
    CHECK(fn.time(0) == 0.f);
    CHECK(fn.time(1) == 500.f);
    CHECK(fn.time(2) == 250.f);
    const float triples[] = {0.f, 0.f, 0.f, 1.f, 500.f, 0.5f, 0.f, 250.f};
    fn.breakpoints(triples, 8, true);
    REQUIRE(fn.patternLength() == 2);
    CHECK(fn.time(0) == 0.f);
    CHECK(fn.time(1) == 500.f);

    // Onsets: the moments the steps end, over a span of 1 or the largest.
    const float quarters[] = {0.25f, 0.5f, 0.75f};
    fn.onsets(quarters, 3);
    REQUIRE(fn.patternLength() == 4);
    for (unsigned int i = 0; i < 4; ++i)
      CHECK(fn.time(i) == doctest::Approx(0.25f));
    const float ms[] = {100.f, 300.f};
    fn.onsets(ms, 2);
    REQUIRE(fn.patternLength() == 2);
    CHECK(fn.time(0) == 100.f);
    CHECK(fn.time(1) == 200.f);
    const float back[] = {0.5f, 0.25f, 0.75f};
    fn.onsets(back, 3);
    REQUIRE(fn.patternLength() == 4);
    CHECK(fn.time(0) == doctest::Approx(0.5f));
    CHECK(fn.time(1) == 0.f);
    CHECK(fn.time(2) == doctest::Approx(0.25f));
    CHECK(fn.time(3) == doctest::Approx(0.25f));
    fn.onsets(many.data(), 100);
    CHECK(fn.patternLength() == phasorStepDiv::MAX_STEPS - 1); // all at 1: no last step
    fn.onsets(quarters, 0);
    CHECK(fn.patternLength() == 0);

    fn.loop(true).syncUpdate(true);
    CHECK(fn.loop());
    CHECK(fn.syncUpdate());
  }

  TEST_CASE("phasorStepDiv: driven, the steps share each input cycle by their times (#1075)") {
    const std::vector<float> in = Ramp(3 * N);

    SUBCASE("50 25: two thirds, then a third") {
      phasorStepDiv fn;
      const std::vector<float> times = {50.f, 25.f};
      fn.pattern(times.data(), 2);
      CheckDriven(fn, in, times);
      phasorStepDiv again;
      again.pattern(times.data(), 2);
      const run r = Process(again, in);
      // 128 * 2/3 = 85.3: sample 85 is the first step, 86 the second.
      CHECK(r.index[85] == 0.f);
      CHECK(r.index[86] == 1.f);
      CHECK(r.out[0] == 0.f);
      CHECK(r.out[64] == doctest::Approx(64.0 * 75.0 / 128.0 / 50.0));
      int falls = 0;
      for (unsigned int n = 1; n < in.size(); ++n)
        if (r.out[n] < r.out[n - 1]) falls++;
      CHECK(falls == 2 * 3 - 1);
    }

    SUBCASE("steps of time 0 never play but keep their number") {
      phasorStepDiv fn;
      const std::vector<float> times = {0.f, 32.f, 0.f, 64.f, 32.f, 0.f};
      fn.pattern(times.data(), 6);
      CheckDriven(fn, in, times);
      const run r = Process(fn, in);
      for (const float i : r.index)
        CHECK((i == 1.f || i == 3.f || i == 4.f));
    }

    SUBCASE("a breakpoint list: the ramp's index is the breakpoint it travels to") {
      phasorStepDiv fn;
      const float list[] = {0.f, 0.f, 1.f, 500.f, 0.f, 250.f};
      fn.breakpoints(list, 6, false);
      CheckDriven(fn, in, {0.f, 500.f, 250.f});
    }

    SUBCASE("onsets 0.25 0.5 0.75: four ramps of 32 samples") {
      phasorStepDiv fn;
      const float quarters[] = {0.25f, 0.5f, 0.75f};
      fn.onsets(quarters, 3);
      const run r = Process(fn, in);
      for (unsigned int n = 0; n < in.size(); ++n) {
        CAPTURE(n);
        CHECK(r.out[n] == doctest::Approx((n % 32) / 32.0).epsilon(1e-5));
        CHECK(r.index[n] == static_cast<float>(std::floor((n % 128) / 32.0)));
      }
    }

    SUBCASE("no pattern: the input passes through with index 0") {
      phasorStepDiv fn;
      const run r = Process(fn, in);
      for (unsigned int n = 0; n < in.size(); ++n) {
        CHECK(r.out[n] == in[n]);
        CHECK(r.index[n] == 0.f);
      }
      const float zeros[] = {0.f, 0.f};
      fn.pattern(zeros, 2);
      CHECK(fn.process(0.4f) == 0.4f);
      CHECK(fn.lastIndex() == 0.f);
    }

    SUBCASE("blocks give what samples give") {
      const std::vector<float> times = {3.f, 1.f, 4.f};
      phasorStepDiv bySample;
      bySample.pattern(times.data(), 3);
      const run r = Process(bySample, in);
      phasorStepDiv byBlock;
      byBlock.pattern(times.data(), 3);
      buffer block(N);
      for (unsigned int first = 0; first < in.size(); first += N) {
        for (unsigned int i = 0; i < N; ++i)
          block.getPtr()[i] = in[first + i];
        buffer& out = byBlock(block);
        REQUIRE(out.getLength() == N);
        REQUIRE(byBlock.index().getLength() == N);
        for (unsigned int i = 0; i < N; ++i) {
          CHECK(out.getPtr()[i] == r.out[first + i]);
          CHECK(byBlock.index().getPtr()[i] == r.index[first + i]);
        }
      }
    }
  }

  TEST_CASE("phasorStepDiv: driven, when a new pattern lands (#1075)") {
    const std::vector<float> in = Ramp(3 * N);
    const std::vector<float> first = {1.f, 1.f};
    const std::vector<float> second = {1.f, 3.f};

    SUBCASE("set before the first sample: applies from it, synced or not") {
      phasorStepDiv fn;
      fn.syncUpdate(true).pattern(second.data(), 2);
      CheckDriven(fn, in, second);
    }

    SUBCASE("syncupdate off: the next sample") {
      phasorStepDiv fn;
      fn.pattern(first.data(), 2);
      const run r = Process(fn, in, [&](unsigned int n) {
        if (n == 40) fn.pattern(second.data(), 2);
      });
      for (unsigned int n = 0; n < in.size(); ++n) {
        CAPTURE(n);
        double ramp = 0.0;
        int step = 0;
        Reference(in[n], n < 40 ? first : second, ramp, step);
        CHECK(r.out[n] == doctest::Approx(ramp).epsilon(1e-6));
        CHECK(r.index[n] == static_cast<float>(step));
      }
    }

    SUBCASE("syncupdate on: the next input wrap") {
      phasorStepDiv fn;
      fn.syncUpdate(true).pattern(first.data(), 2);
      const run r = Process(fn, in, [&](unsigned int n) {
        if (n == 40) fn.pattern(second.data(), 2);
      });
      for (unsigned int n = 0; n < in.size(); ++n) {
        CAPTURE(n);
        double ramp = 0.0;
        int step = 0;
        Reference(in[n], n < 128 ? first : second, ramp, step);
        CHECK(r.out[n] == doctest::Approx(ramp).epsilon(1e-6));
        CHECK(r.index[n] == static_cast<float>(step));
      }
    }
  }

  TEST_CASE("phasorStepDiv: driven input wrapped, falling and not a number (#1075)") {
    const float times[] = {1.f, 1.f};
    phasorStepDiv down;
    down.pattern(times, 2);
    for (int n = 0; n < 256; ++n) {
      const float x = static_cast<float>(1.0 - std::fmod(n, 128) / 128.0);
      const float wrapped = x >= 1.f ? 0.f : x;
      const float y = down.process(x);
      CAPTURE(n);
      CHECK(y == doctest::Approx(std::fmod(wrapped * 2.0, 1.0)).epsilon(1e-6));
      CHECK(down.lastIndex() == (wrapped < 0.5f ? 0.f : 1.f));
    }

    phasorStepDiv wrapped;
    wrapped.pattern(times, 2);
    CHECK(wrapped.process(1.25f) == doctest::Approx(0.5f));
    CHECK(wrapped.lastIndex() == 0.f);
    CHECK(wrapped.process(-0.25f) == doctest::Approx(0.5f));
    CHECK(wrapped.lastIndex() == 1.f);

    phasorStepDiv held;
    held.pattern(times, 2);
    const float before = held.process(0.3f);
    CHECK(held.process(std::numeric_limits<float>::quiet_NaN()) == before);
    CHECK(held.process(std::numeric_limits<float>::infinity()) == before);
    CHECK(held.lastIndex() == 0.f);
  }

  TEST_CASE("phasorStepDiv: driven output does not depend on the sample rate (#1075)") {
    const std::vector<float> in = Ramp(2 * N);
    const float times[] = {2.f, 5.f};
    const auto at = [&](unsigned int rate) {
      TestHelpers::ScopedSampleRate scoped(rate);
      phasorStepDiv fn;
      fn.pattern(times, 2);
      return Process(fn, in).out;
    };
    CHECK(at(44100) == at(96000));
  }

  TEST_CASE("phasorStepDiv: free running over times in milliseconds (#1075)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const float times[] = {1.f, 2.f}; // 48 and 96 samples

    SUBCASE("before a start: 0, index 0; once through, then 1 with the last index") {
      phasorStepDiv fn;
      fn.pattern(times, 2);
      run r = Free(fn, 10);
      for (unsigned int n = 0; n < 10; ++n) {
        CHECK(r.out[n] == 0.f);
        CHECK(r.index[n] == 0.f);
      }
      fn.start();
      CHECK(fn.running());
      r = Free(fn, 200);
      for (unsigned int k = 0; k < 200; ++k) {
        CAPTURE(k);
        if (k < 48) {
          CHECK(r.out[k] == doctest::Approx(k / 48.0).epsilon(1e-6));
          CHECK(r.index[k] == 0.f);
        } else if (k < 144) {
          CHECK(r.out[k] == doctest::Approx((k - 48) / 96.0).epsilon(1e-6));
          CHECK(r.index[k] == 1.f);
        } else {
          CHECK(r.out[k] == 1.f);
          CHECK(r.index[k] == 1.f);
        }
      }
      CHECK_FALSE(fn.running());
      // A second start plays it again.
      fn.start();
      CHECK(fn.process() == 0.f);
      CHECK(fn.lastIndex() == 0.f);
    }

    SUBCASE("loop: over and over") {
      phasorStepDiv fn;
      fn.pattern(times, 2).loop(true).start();
      const run r = Free(fn, 500);
      for (unsigned int k = 0; k < 500; ++k) {
        CAPTURE(k);
        const unsigned int m = k % 144;
        CHECK(r.out[k] == doctest::Approx(m < 48 ? m / 48.0 : (m - 48) / 96.0).epsilon(1e-6));
        CHECK(r.index[k] == (m < 48 ? 0.f : 1.f));
      }
      CHECK(fn.running());
    }

    SUBCASE("a time that is not a whole number of samples carries over") {
      TestHelpers::ScopedSampleRate odd(44100);
      const float half[] = {0.5f}; // 22.05 samples
      phasorStepDiv fn;
      fn.pattern(half, 1).loop(true).start();
      const run r = Free(fn, 300);
      for (unsigned int k = 0; k < 300; ++k) {
        CAPTURE(k);
        const double steps = k / 22.05;
        CHECK(r.out[k] == doctest::Approx(steps - std::floor(steps)).epsilon(1e-4));
      }
    }

    SUBCASE("steps of time 0 never play but keep their number") {
      const float zeros[] = {0.f, 1.f, 0.f, 1.f, 0.f};
      phasorStepDiv fn;
      fn.pattern(zeros, 5).start();
      const run r = Free(fn, 120);
      for (unsigned int k = 0; k < 120; ++k) {
        CAPTURE(k);
        CHECK(r.index[k] == (k < 48 ? 1.f : 3.f));
      }
      CHECK(r.out[100] == 1.f);
    }

    SUBCASE("step(n) jumps to the start of a step, clamped, started or not") {
      phasorStepDiv fn;
      fn.pattern(times, 2).step(1);
      CHECK(fn.running());
      run r = Free(fn, 96);
      CHECK(r.out[0] == 0.f);
      CHECK(r.index[0] == 1.f);
      CHECK(r.out[95] == doctest::Approx(95.0 / 96.0));
      CHECK(fn.process() == 1.f); // the end
      fn.step(99);
      CHECK(fn.process() == 0.f);
      CHECK(fn.lastIndex() == 1.f);
      fn.step(-3);
      CHECK(fn.process() == 0.f);
      CHECK(fn.lastIndex() == 0.f);
    }

    SUBCASE("nothing to play: a start does nothing") {
      phasorStepDiv fn;
      fn.start();
      CHECK_FALSE(fn.running());
      CHECK(fn.process() == 0.f);
      const float zeros[] = {0.f, 0.f};
      fn.pattern(zeros, 2).start();
      CHECK_FALSE(fn.running());
    }

    SUBCASE("follows the live sample rate") {
      const float one[] = {1.f};
      for (const unsigned int sr : {48000u, 96000u}) {
        CAPTURE(sr);
        TestHelpers::ScopedSampleRate scoped(sr);
        phasorStepDiv fn;
        fn.pattern(one, 1).start();
        const run r = Free(fn, sr / 1000 + 1);
        CHECK(r.out[sr / 1000 - 1] < 1.f);
        CHECK(r.out[sr / 1000] == 1.f);
      }
    }

    SUBCASE("blocks give what samples give") {
      phasorStepDiv bySample;
      bySample.pattern(times, 2).loop(true).start();
      const run r = Free(bySample, 3 * N);
      phasorStepDiv byBlock;
      byBlock.pattern(times, 2).loop(true).start();
      for (unsigned int first = 0; first < 3 * N; first += N) {
        buffer& out = byBlock();
        REQUIRE(out.getLength() == N);
        for (unsigned int i = 0; i < N; ++i) {
          CHECK(out.getPtr()[i] == r.out[first + i]);
          CHECK(byBlock.index().getPtr()[i] == r.index[first + i]);
        }
      }
    }
  }

  TEST_CASE("phasorStepDiv: free running, when a new pattern lands (#1075)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const float times[] = {1.f, 2.f};
    const float shorter[] = {0.5f, 0.5f}; // 24 and 24 samples

    SUBCASE("syncupdate off: the step playing keeps its number and time played") {
      phasorStepDiv fn;
      fn.pattern(times, 2).start();
      // At sample 60, step 1 has played 12 of 96; in the new pattern 12 of 24.
      const run r = Free(fn, 100, [&](unsigned int n) {
        if (n == 60) fn.pattern(shorter, 2);
      });
      CHECK(r.index[60] == 1.f);
      CHECK(r.out[60] == doctest::Approx(12.0 / 24.0));
      CHECK(r.out[71] == doctest::Approx(23.0 / 24.0));
      CHECK(r.out[72] == 1.f); // the end
      // Longer than the new step: it ends at once.
      phasorStepDiv late;
      late.pattern(times, 2).start();
      const run s = Free(late, 100, [&](unsigned int n) {
        if (n == 90) late.pattern(shorter, 2);
      });
      CHECK(s.out[90] == 1.f);
    }

    SUBCASE("syncupdate on: at the end of the sequence") {
      phasorStepDiv fn;
      fn.syncUpdate(true).loop(true).pattern(times, 2).start();
      const run r = Free(fn, 200, [&](unsigned int n) {
        if (n == 60) fn.pattern(shorter, 2);
      });
      CHECK(r.out[100] == doctest::Approx(52.0 / 96.0)); // old pattern still
      CHECK(r.index[144] == 0.f); // the new one from the loop
      CHECK(r.out[150] == doctest::Approx(6.0 / 24.0));
      CHECK(r.index[170] == 1.f);
      CHECK(r.out[192] == 0.f);
    }

    SUBCASE("syncupdate on: at a start, and at once while stopped") {
      phasorStepDiv fn;
      fn.syncUpdate(true).pattern(times, 2).start();
      Free(fn, 10);
      fn.pattern(shorter, 2).start();
      const run r = Free(fn, 50);
      CHECK(r.out[24] == 0.f);
      CHECK(r.index[24] == 1.f);
      CHECK(r.out[48] == 1.f);
      const float one[] = {1.f};
      fn.pattern(one, 1); // stopped: lands at once
      fn.process();
      fn.start();
      const run s = Free(fn, 49);
      CHECK(s.index[47] == 0.f);
      CHECK(s.out[48] == 1.f);
    }
  }

  TEST_CASE("phasorStepDiv: setters and blocks allocate nothing (#1075)") {
    phasorStepDiv fn;
    buffer in(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = static_cast<float>(i) / N;
    const float times[] = {2.f, 1.f, 1.f};
    const float list[] = {0.f, 0.f, 1.f, 5.f, 0.f, 3.f};
    const float onsets[] = {0.25f, 0.5f};
    fn(in);
    fn();
    {
      TestHelpers::ProbeScope probe;
      fn.pattern(times, 3).loop(true).syncUpdate(true);
      fn(in);
      fn.breakpoints(list, 6, false).start();
      fn();
      fn.onsets(onsets, 2).step(1);
      fn();
      fn(in);
      fn.process(0.5f);
      fn.process();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
