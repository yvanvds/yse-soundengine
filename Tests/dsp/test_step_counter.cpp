// Tests for YSE::DSP::stepCounter (YseEngine/dsp/generators/stepCounter.hpp),
// issue #1080. The clock is a DSP::phasor at 375 Hz and 48 kHz: exactly 128
// samples per cycle, so its resets, the jumps, are at samples 128, 256, ...
// Jump j (from 0) is at sample 128 (j + 1).

#include <doctest/doctest.h>
#include <limits>
#include <vector>
#include "dsp/generators/phasor.hpp"
#include "dsp/generators/stepCounter.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::stepCounter;

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

  struct outputs {
    std::vector<float> out;
    std::vector<float> count;
    std::vector<float> step;
  };

  // Sample by sample through process().
  outputs Process(stepCounter& fn, const std::vector<float>& in) {
    outputs o;
    for (float x : in) {
      o.out.push_back(fn.process(x));
      o.count.push_back(fn.lastCount());
      o.step.push_back(fn.lastStep());
    }
    return o;
  }

  // The samples that fired, and whether every output was exactly 0 or 1.
  std::vector<unsigned int> Fired(const std::vector<float>& out) {
    std::vector<unsigned int> at;
    for (unsigned int n = 0; n < out.size(); ++n) {
      REQUIRE((out[n] == 0.f || out[n] == 1.f));
      if (out[n] == 1.f) at.push_back(n);
    }
    return at;
  }

  // The closed form: the jumps (from 0) that fire for the sequence `seq`
  // repeated, below `jumps`. Start mode on: jump 0, then the running sums of
  // the counts from there. Off: the running sums, counted from 1.
  std::vector<unsigned int> Expected(const std::vector<int>& seq, bool startMode,
                                     unsigned int jumps) {
    std::vector<unsigned int> at;
    if (startMode) at.push_back(0);
    unsigned int sum = 0;
    for (std::size_t m = 0;; ++m) {
      sum += static_cast<unsigned int>(seq[m % seq.size()]);
      const unsigned int j = startMode ? sum : sum - 1; // off: the first jump counts 1
      if (j >= jumps) break;
      at.push_back(j);
    }
    return at;
  }

  // Jump numbers to phasor sample positions.
  std::vector<unsigned int> AtSamples(const std::vector<unsigned int>& jumps) {
    std::vector<unsigned int> at;
    at.reserve(jumps.size());
    for (unsigned int j : jumps)
      at.push_back(PERIOD * (j + 1));
    return at;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("stepCounter: a phasor clock through 2 1 3, against the closed form (#1080)") {
    const unsigned int samples = PERIOD * 40;
    const std::vector<float> clock = Ramp(samples);
    const float seq[] = {2.f, 1.f, 3.f};

    for (bool start : {true, false}) {
      CAPTURE(start);
      stepCounter fn;
      fn.sequence(seq, 3).startMode(start);
      CHECK(fn.length() == 3);
      const outputs o = Process(fn, clock);
      CHECK(Fired(o.out) == AtSamples(Expected({2, 1, 3}, start, 39)));

      // The count and step outputs between the impulses.
      if (start) {
        // Jump 0 fires and starts step 0, count 0; jump 1 counts 1; jump 2
        // ends step 0.
        CHECK(o.count[PERIOD * 1] == 0.f);
        CHECK(o.step[PERIOD * 1] == 0.f);
        CHECK(o.count[PERIOD * 2] == 1.f);
        CHECK(o.count[PERIOD * 2 + 5] == 1.f);
        CHECK(o.step[PERIOD * 3] == 1.f);
        CHECK(o.count[PERIOD * 3] == 0.f);
        CHECK(o.step[PERIOD * 4] == 2.f);
        CHECK(o.count[PERIOD * 6] == 2.f);
        CHECK(o.step[PERIOD * 7] == 0.f);
      } else {
        // Before the first jump: step 0, count 0. Jump 0 counts 1, jump 1
        // ends step 0.
        CHECK(o.count[5] == 0.f);
        CHECK(o.step[5] == 0.f);
        CHECK(o.count[PERIOD * 1] == 1.f);
        CHECK(o.step[PERIOD * 2] == 1.f);
        CHECK(o.count[PERIOD * 2] == 0.f);
        CHECK(o.step[PERIOD * 3] == 2.f);
      }
      // Never more than the step allows.
      for (unsigned int n = 0; n < samples; ++n) {
        const int step = static_cast<int>(o.step[n]);
        REQUIRE(step >= 0);
        REQUIRE(step < 3);
        REQUIRE(o.count[n] < seq[step]);
      }
    }
  }

  TEST_CASE("stepCounter: an empty sequence makes every jump a step (#1080)") {
    const std::vector<float> clock = Ramp(PERIOD * 6);
    stepCounter on;
    CHECK(on.length() == 0);
    CHECK(Fired(Process(on, clock).out) == AtSamples({0, 1, 2, 3, 4}));
    stepCounter off;
    off.startMode(false);
    const outputs o = Process(off, clock);
    CHECK(Fired(o.out) == AtSamples({0, 1, 2, 3, 4}));
    for (float s : o.step)
      CHECK(s == 0.f);
  }

  TEST_CASE("stepCounter: direction, against a falling ramp and impulses (#1080)") {
    // A phasor at -375 Hz jumps up at every reset: samples 1, 129, ... (8
    // jumps).
    const std::vector<float> falling = Ramp(PERIOD * 8, -375.f);
    const float two[] = {2.f};
    std::vector<unsigned int> rises;
    {
      stepCounter both;
      both.sequence(two, 1);
      rises = Fired(Process(both, falling).out);
      CHECK(rises.size() == 4); // jumps 0, 2, 4, 6
    }
    stepCounter up;
    up.sequence(two, 1).direction(stepCounter::UP);
    CHECK(Fired(Process(up, falling).out) == rises);
    stepCounter down;
    down.sequence(two, 1).direction(stepCounter::DOWN);
    CHECK(Fired(Process(down, falling).out).empty());

    // A rising phasor jumps down: the other way round.
    const std::vector<float> rising = Ramp(PERIOD * 8);
    stepCounter upOnRising;
    upOnRising.direction(stepCounter::UP);
    CHECK(Fired(Process(upOnRising, rising).out).empty());
    stepCounter downOnRising;
    downOnRising.direction(stepCounter::DOWN);
    CHECK(Fired(Process(downOnRising, rising).out) == AtSamples({0, 1, 2, 3, 4, 5, 6}));

    // Impulses, one every 10 samples from sample 0: the first counts, as the
    // input reads 0 before it. Each rises and falls: two jumps in BOTH, one
    // in UP.
    std::vector<float> clicks(100, 0.f);
    for (unsigned int n = 0; n < clicks.size(); n += 10)
      clicks[n] = 1.f;
    stepCounter bothClicks;
    const std::vector<unsigned int> b = Fired(Process(bothClicks, clicks).out);
    CHECK(b.size() == 20);
    CHECK(b[0] == 0);
    CHECK(b[1] == 1);
    stepCounter upClicks;
    upClicks.direction(stepCounter::UP).sequence(two, 1);
    CHECK(Fired(Process(upClicks, clicks).out) == std::vector<unsigned int>{0, 20, 40, 60, 80});
  }

  TEST_CASE("stepCounter: the threshold (#1080)") {
    // Steps of 0.5: no jump at the default 0.75, every one at 0.5 or 0.
    std::vector<float> stairs(8, 0.f);
    for (unsigned int n = 0; n < 8; n += 2)
      stairs[n] = 0.5f;
    stepCounter coarse;
    CHECK(Fired(Process(coarse, stairs).out).empty());
    stepCounter fine;
    fine.threshold(0.5f);
    CHECK(Fired(Process(fine, stairs).out).size() == 8);
    stepCounter any;
    any.threshold(0.f);
    CHECK(Fired(Process(any, stairs).out).size() == 8);
    // A held input never jumps, even at threshold 0 or less.
    stepCounter held;
    held.threshold(-1.f);
    CHECK(Fired(Process(held, std::vector<float>(10, 0.f)).out).empty());
    // A threshold that is not a number is ignored.
    stepCounter kept;
    kept.threshold(std::numeric_limits<float>::quiet_NaN());
    CHECK(Fired(Process(kept, stairs).out).empty());
    // A phasor's reset (127/128) is a jump; a threshold above it is not.
    stepCounter high;
    high.threshold(0.995f);
    CHECK(Fired(Process(high, Ramp(PERIOD * 4)).out).empty());
  }

  TEST_CASE("stepCounter: reset starts over and waits for a first jump (#1080)") {
    const std::vector<float> clock = Ramp(PERIOD * 20);
    const float seq[] = {3.f, 2.f};
    stepCounter fn;
    fn.sequence(seq, 2);
    // Jumps 0 .. 4 (fires at 0, 3), reset after jump 4 is in.
    std::vector<float> first(clock.begin(), clock.begin() + PERIOD * 5 + 10);
    CHECK(Fired(Process(fn, first).out) == AtSamples({0, 3}));
    CHECK(fn.lastStep() == 1.f);
    CHECK(fn.lastCount() == 1.f);
    fn.reset();
    CHECK(fn.lastStep() == 0.f);
    CHECK(fn.lastCount() == 0.f);
    // From there the next jump (5) is jump 0 again: fires at 5, 8, 10, ...
    // The reset did not invent a jump at the next sample.
    std::vector<float> rest(clock.begin() + PERIOD * 5 + 10, clock.end());
    std::vector<unsigned int> at;
    for (unsigned int n : Fired(Process(fn, rest).out))
      at.push_back(n + PERIOD * 5 + 10);
    CHECK(at == AtSamples({5, 8, 10, 13, 15, 18}));
  }

  TEST_CASE("stepCounter: syncUpdate says when a new sequence lands (#1080)") {
    const std::vector<float> clock = Ramp(PERIOD * 30);
    const float oldSeq[] = {2.f, 3.f};
    const float newSeq[] = {1.f};
    // After jump 3 (step 1, count 1): change to `1`.
    const unsigned int cut = PERIOD * 4 + 10;
    const auto run = [&](stepCounter::SYNC mode) {
      stepCounter fn;
      fn.sequence(oldSeq, 2).syncUpdate(mode);
      std::vector<float> a(clock.begin(), clock.begin() + cut);
      std::vector<unsigned int> at = Fired(Process(fn, a).out);
      fn.sequence(newSeq, 1);
      std::vector<float> b(clock.begin() + cut, clock.end());
      for (unsigned int n : Fired(Process(fn, b).out))
        at.push_back(n + cut);
      return at;
    };
    // Old: fires at jumps 0, 2, 5, 7, 10 ...
    // Immediate: the step index 1 wraps to 0 in a one-step sequence and the
    // count of 1 is already reached: the next jump, 4, fires, then every one.
    CHECK(run(stepCounter::IMMEDIATE) ==
          AtSamples({0,  2,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                     16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28}));
    // After the step: step 1 of the old one ends at jump 5, then every jump.
    CHECK(run(stepCounter::AFTER_STEP) ==
          AtSamples({0,  2,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                     16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28}));

    // After the sequence: with 2 3 2 3 ..., the sequence that is playing at
    // the cut ends at jump 5 too; cut in step 0 of the second round instead.
    stepCounter fn;
    fn.sequence(oldSeq, 2).syncUpdate(stepCounter::AFTER_SEQUENCE);
    const unsigned int late = PERIOD * 7 + 10; // after jump 6, step 0
    std::vector<float> a(clock.begin(), clock.begin() + late);
    std::vector<unsigned int> at = Fired(Process(fn, a).out);
    fn.sequence(newSeq, 1);
    CHECK(fn.length() == 2); // still waiting
    std::vector<float> b(clock.begin() + late, clock.end());
    for (unsigned int n : Fired(Process(fn, b).out))
      at.push_back(n + late);
    CHECK(fn.length() == 1);
    // Old 0, 2, 5, 7 (step 1 now), 10 (end of the sequence), then every jump.
    CHECK(at == AtSamples({0,  2,  5,  7,  10, 11, 12, 13, 14, 15, 16, 17,
                           18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28}));

    // Before the first jump nothing has begun: a waiting sequence lands at
    // once; so does it at a reset, and when sync is switched off.
    stepCounter fresh;
    fresh.syncUpdate(stepCounter::AFTER_SEQUENCE).sequence(oldSeq, 2);
    CHECK(fresh.length() == 2);
    Process(fresh, std::vector<float>(clock.begin(), clock.begin() + PERIOD * 2 + 1));
    fresh.sequence(newSeq, 1);
    CHECK(fresh.length() == 2);
    fresh.reset();
    CHECK(fresh.length() == 1);
    Process(fresh, std::vector<float>(clock.begin(), clock.begin() + PERIOD * 2 + 1));
    fresh.sequence(oldSeq, 2);
    CHECK(fresh.length() == 1);
    fresh.syncUpdate(stepCounter::IMMEDIATE);
    CHECK(fresh.length() == 2);
  }

  TEST_CASE("stepCounter: counts are whole numbers of at least 1; capacity (#1080)") {
    const std::vector<float> clock = Ramp(PERIOD * 12);
    const float messy[] = {1.6f, std::numeric_limits<float>::quiet_NaN(), 0.f,
                           -3.f, std::numeric_limits<float>::infinity(),  2.4f};
    stepCounter fn;
    fn.sequence(messy, 6);
    CHECK(fn.length() == 4); // 2 1 1 2
    CHECK(Fired(Process(fn, clock).out) == AtSamples(Expected({2, 1, 1, 2}, true, 11)));

    std::vector<float> many(stepCounter::MAX_STEPS + 10, 1.f);
    stepCounter full;
    full.sequence(many.data(), static_cast<unsigned int>(many.size()));
    CHECK(full.length() == stepCounter::MAX_STEPS);
    full.sequence(nullptr, 3);
    CHECK(full.length() == 0);
  }

  TEST_CASE("stepCounter: a sample that is not a number is skipped (#1080)") {
    std::vector<float> clock = Ramp(PERIOD * 4);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    // A NaN on the reset sample: the jump is measured from the sample before
    // it to the one after, so it still counts, one sample late.
    clock[PERIOD * 2] = nan;
    stepCounter fn;
    const outputs o = Process(fn, clock);
    CHECK(Fired(o.out) == std::vector<unsigned int>{PERIOD, PERIOD * 2 + 1, PERIOD * 3});
    CHECK(o.out[PERIOD * 2] == 0.f);
  }

  TEST_CASE("stepCounter: blocks match samples; nothing depends on the sample rate (#1080)") {
    const unsigned int blocks = 12;
    const std::vector<float> clock = Ramp(N * blocks, 250.f);
    const float seq[] = {2.f, 1.f, 3.f};
    stepCounter bySample;
    bySample.sequence(seq, 3);
    const outputs ref = Process(bySample, clock);

    for (unsigned int rate : {44100u, 96000u}) {
      CAPTURE(rate);
      TestHelpers::ScopedSampleRate scoped(rate);
      stepCounter byBlock;
      byBlock.sequence(seq, 3);
      buffer in(N);
      for (unsigned int b = 0; b < blocks; ++b) {
        for (unsigned int i = 0; i < N; ++i)
          in.getPtr()[i] = clock[b * N + i];
        buffer& out = byBlock(in);
        REQUIRE(out.getLength() == N);
        for (unsigned int i = 0; i < N; ++i) {
          REQUIRE(out.getPtr()[i] == ref.out[b * N + i]);
          REQUIRE(byBlock.counts().getPtr()[i] == ref.count[b * N + i]);
          REQUIRE(byBlock.steps().getPtr()[i] == ref.step[b * N + i]);
        }
      }
    }
  }

  TEST_CASE("stepCounter: processing allocates nothing (#1080)") {
    stepCounter fn;
    buffer in(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = static_cast<float>(i % 16) / 16.f;
    const float seq[] = {2.f, 3.f};
    fn(in);
    {
      TestHelpers::ProbeScope probe;
      fn.sequence(seq, 2).syncUpdate(stepCounter::AFTER_STEP).direction(stepCounter::DOWN);
      fn(in);
      fn.sequence(seq, 1).reset().threshold(0.5f).startMode(false);
      fn(in);
      fn.process(0.5f);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
