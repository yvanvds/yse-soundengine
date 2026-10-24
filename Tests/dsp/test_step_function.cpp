// Tests for YSE::DSP::stepFunction (YseEngine/dsp/generators/stepFunction.hpp),
// issue #1081. The input is a DSP::phasor at 375 Hz and 48 kHz: exactly 128
// samples per cycle, so its forward wraps are at samples 128, 256, ... and
// sample n is in cycle n / 128 at phase (n % 128) / 128.

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/generators/phasor.hpp"
#include "dsp/generators/phasorStepDiv.hpp"
#include "dsp/generators/stepFunction.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::phasorStepDiv;
using YSE::DSP::stepFunction;

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
    std::vector<unsigned int> step;
  };

  // Sample by sample through process(phase).
  outputs Process(stepFunction& fn, const std::vector<float>& in) {
    outputs o;
    for (float x : in) {
      o.out.push_back(fn.process(x));
      o.step.push_back(fn.lastStep());
    }
    return o;
  }

  // The closed form of step k at phase f: from value k - 1 (the last for
  // step 0) to value k, bent by the step's curve.
  float Segment(const std::vector<float>& values, const std::vector<float>& curves, unsigned int k,
                float f) {
    const std::size_t n = values.size();
    const float from = values[(k + n - 1) % n];
    const float to = values[k];
    const float c = k < curves.size() ? curves[k] : 0.f;
    const float e = (1.f + c) / (1.f - c);
    return from + (to - from) * std::pow(f, e);
  }

  // The output for a rising 128-sample phasor: sample n is in step
  // (first + n / 128) mod size.
  void CheckRising(const outputs& o, const std::vector<float>& in, const std::vector<float>& values,
                   const std::vector<float>& curves, unsigned int first = 0) {
    for (unsigned int n = 0; n < in.size(); ++n) {
      CAPTURE(n);
      const unsigned int k = (first + n / PERIOD) % values.size();
      REQUIRE(o.step[n] == k);
      REQUIRE(o.out[n] == doctest::Approx(Segment(values, curves, k, in[n])).epsilon(1e-5));
    }
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("stepFunction: each cycle of a rising phasor travels to the next value (#1081)") {
    const std::vector<float> clock = Ramp(PERIOD * 7);
    const std::vector<float> values = {0.f, 1.f, 0.5f};
    stepFunction fn;
    fn.pattern(values.data(), 3);
    CHECK(fn.length() == 3);
    const outputs o = Process(fn, clock);
    CheckRising(o, clock, values, {});

    // Step 0 starts at the last value, so the loop is continuous: each wrap
    // lands where the cycle before it was heading.
    CHECK(o.out[0] == 0.5f);
    CHECK(o.out[PERIOD] == 0.f);
    CHECK(o.out[PERIOD * 2] == 1.f);
    CHECK(o.out[PERIOD * 3] == 0.5f);
    CHECK(o.out[PERIOD / 2] == doctest::Approx(0.25f)); // halfway from 0.5 to 0
  }

  TEST_CASE("stepFunction: an empty pattern is 0, one value a constant (#1081)") {
    const std::vector<float> clock = Ramp(PERIOD * 3);
    stepFunction empty;
    CHECK(empty.length() == 0);
    for (float x : Process(empty, clock).out)
      REQUIRE(x == 0.f);

    stepFunction constant;
    const float one[] = {0.7f};
    constant.pattern(one, 1);
    const outputs o = Process(constant, clock);
    for (unsigned int n = 0; n < clock.size(); ++n) {
      REQUIRE(o.out[n] == 0.7f);
      REQUIRE(o.step[n] == 0);
    }
  }

  TEST_CASE("stepFunction: curves bend each step as .function does (#1081)") {
    const std::vector<float> clock = Ramp(PERIOD * 6);
    const std::vector<float> values = {0.f, 1.f, 0.5f};
    const std::vector<float> curves = {0.5f, 0.f, -0.5f};
    stepFunction fn;
    fn.pattern(values.data(), 3).curvePattern(curves.data(), 3);
    const outputs o = Process(fn, clock);
    CheckRising(o, clock, values, curves);
    // A positive curve moves late: below the straight line halfway.
    CHECK(o.out[PERIOD / 2] > 0.25f);

    // Curves past the list are linear; out of range is clamped, NaN is 0.
    stepFunction partial;
    const float some[] = {std::numeric_limits<float>::quiet_NaN(), 3.f};
    partial.pattern(values.data(), 3).curvePattern(some, 2);
    // Curve 1 clamps to +1: hold until the end of the step, then jump.
    const outputs p = Process(partial, clock);
    CHECK(p.out[PERIOD + PERIOD / 2] == 0.f);
    CHECK(p.out[PERIOD * 2] == 1.f);
    stepFunction linearRest;
    linearRest.pattern(values.data(), 3).curvePattern(curves.data(), 1);
    CheckRising(Process(linearRest, clock), clock, values, {0.5f});

    // Clearing the curves makes every step linear again. The next ramp's
    // first sample wraps from the last one, so step 0 plays from there.
    fn.curvePattern(nullptr, 0);
    const std::vector<float> more = Ramp(PERIOD * 3);
    CheckRising(Process(fn, more), more, values, {}, 0);
  }

  TEST_CASE("stepFunction: a falling phasor steps backward (#1081)") {
    const std::vector<float> clock = Ramp(PERIOD * 7, -375.f);
    const std::vector<float> values = {0.f, 1.f, 0.5f};
    stepFunction fn;
    fn.pattern(values.data(), 3);
    const outputs o = Process(fn, clock);
    // A model of the backward count: down by one at each rise of more than
    // half a cycle.
    unsigned int k = 0;
    for (unsigned int n = 0; n < clock.size(); ++n) {
      CAPTURE(n);
      if (n > 0 && clock[n] - clock[n - 1] > 0.5f) k = (k + 2) % 3;
      REQUIRE(o.step[n] == k);
      REQUIRE(o.out[n] == doctest::Approx(Segment(values, {}, k, clock[n])).epsilon(1e-5));
    }
    // It went round: from step 0 to 2, 1, 0 ...
    CHECK(o.step.back() != 0u);
  }

  TEST_CASE("stepFunction: step() and reset() (#1081)") {
    const std::vector<float> clock = Ramp(PERIOD * 4);
    const std::vector<float> values = {0.f, 1.f, 0.5f, 0.25f};
    stepFunction fn;
    fn.pattern(values.data(), 4);

    fn.step(2);
    CheckRising(Process(fn, std::vector<float>(clock.begin(), clock.begin() + PERIOD)),
                std::vector<float>(clock.begin(), clock.begin() + PERIOD), values, {}, 2);
    CHECK(fn.lastStep() == 2);

    // Wrapped into the pattern: -1 is the last step, 9 is step 1.
    fn.step(-1);
    CHECK(fn.lastStep() == 3);
    fn.step(9);
    CHECK(fn.lastStep() == 1);

    // reset: step 0, and the input's history is kept, so no wrap is invented.
    fn.reset();
    CHECK(fn.lastStep() == 0);
    const float mid = clock[PERIOD / 2];
    CHECK(fn.process(mid) == doctest::Approx(Segment(values, {}, 0, mid)));
    CHECK(fn.lastStep() == 0);
  }

  TEST_CASE("stepFunction: a step signal picks the step; ~stepdiv's index plays a function "
            "(#1081)") {
    const std::vector<float> values = {0.f, 1.f, 0.5f};
    stepFunction fn;
    fn.pattern(values.data(), 3);
    // Rounded down, wrapped; a NaN is the step before.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(fn.process(0.5f, 1.9f) == doctest::Approx(0.5f));
    CHECK(fn.lastStep() == 1);
    CHECK(fn.process(0.5f, -1.f) == doctest::Approx(0.75f)); // step 2: 1 to 0.5
    CHECK(fn.lastStep() == 2);
    CHECK(fn.process(0.5f, nan) == doctest::Approx(0.75f));
    CHECK(fn.lastStep() == 2);
    CHECK(fn.process(0.5f, 4.f) == doctest::Approx(0.5f)); // step 1
    // step() does not change what the step signal says.
    fn.step(0);
    CHECK(fn.process(0.5f, 4.f) == doctest::Approx(0.5f));

    // The pair of the issue: phasorStepDiv shares a phasor's cycle between
    // the times of a breakpoint function, and its ramp and index play the
    // function's values. 0 0 1 500 0 250: up from 0 to 1 over the first two
    // thirds of the cycle, down to 0 over the last third.
    TestHelpers::ScopedSampleRate rate(48000);
    const float list[] = {0.f, 0.f, 1.f, 500.f, 0.f, 250.f};
    phasorStepDiv div;
    div.breakpoints(list, 6, false);
    stepFunction function;
    function.breakpoints(list, 6, false);
    CHECK(function.length() == 3);
    phasor source;
    const unsigned int period = 384; // 125 Hz at 48 kHz
    unsigned int n = 0;
    for (int block = 0; block < 8; ++block) {
      buffer& ramp = div(source(125.f, N));
      buffer& out = function(ramp, div.index());
      REQUIRE(out.getLength() == N);
      for (unsigned int i = 0; i < N; ++i, ++n) {
        CAPTURE(n);
        const float p = static_cast<float>(n % period) / static_cast<float>(period);
        const float expected = p < 2.f / 3.f ? p * 1.5f : 1.f - (p - 2.f / 3.f) * 3.f;
        REQUIRE(out.getPtr()[i] == doctest::Approx(expected).epsilon(1e-3));
      }
    }
  }

  TEST_CASE("stepFunction: breakpoints() reads line format, with an initial value (#1081)") {
    const std::vector<float> clock = Ramp(PERIOD * 4);
    // Pairs: the times are ignored; an incomplete last pair is dropped.
    const float pairs[] = {0.2f, 0.f, 0.8f, 100.f, 0.4f};
    stepFunction fn;
    fn.breakpoints(pairs, 5, false);
    CHECK(fn.length() == 2);
    CheckRising(Process(fn, clock), clock, {0.2f, 0.8f}, {});

    // Triples set the curves; Max's initial value comes first, curve 0.
    const float triples[] = {1.f, 100.f, 0.5f, 0.f, 50.f, -0.5f};
    const float initial = 0.5f;
    stepFunction curved;
    curved.breakpoints(triples, 6, true, &initial);
    CHECK(curved.length() == 3);
    CheckRising(Process(curved, clock), clock, {0.5f, 1.f, 0.f}, {0.f, 0.5f, -0.5f});

    // Pairs after triples keep the curves; a NaN value drops its group.
    const float nanPairs[] = {std::numeric_limits<float>::quiet_NaN(), 1.f, 0.3f, 1.f, 0.6f, 1.f};
    curved.breakpoints(nanPairs, 6, false);
    CHECK(curved.length() == 2);
    // It landed on step 0 (four cycles of three steps), and the ramp's first
    // sample wraps from the last one: step 1.
    CheckRising(Process(curved, clock), clock, {0.3f, 0.6f}, {0.f, 0.5f}, 1);
    // Triples replace the whole curve pattern.
    const float flat[] = {0.3f, 1.f, 0.f, 0.6f, 1.f, 0.f};
    curved.breakpoints(flat, 6, true);
    CheckRising(Process(curved, clock), clock, {0.3f, 0.6f}, {}, 1);
  }

  TEST_CASE("stepFunction: values are finite, at most MAX_STEPS (#1081)") {
    const float messy[] = {0.1f, std::numeric_limits<float>::quiet_NaN(), 0.2f,
                           std::numeric_limits<float>::infinity(), 0.3f};
    stepFunction fn;
    fn.pattern(messy, 5);
    CHECK(fn.length() == 3);
    std::vector<float> many(stepFunction::MAX_STEPS + 10, 1.f);
    fn.pattern(many.data(), static_cast<unsigned int>(many.size()));
    CHECK(fn.length() == stepFunction::MAX_STEPS);
    fn.pattern(nullptr, 3);
    CHECK(fn.length() == 0);
  }

  TEST_CASE("stepFunction: syncUpdate lands a new pattern at once, at a wrap, or at the end "
            "of the sequence (#1081)") {
    const std::vector<float> clock = Ramp(PERIOD * 8);
    const float oldValues[] = {0.f, 1.f, 0.5f};
    const float newValues[] = {2.f, 3.f};

    // Before the first sample a pattern lands at once, whatever the mode.
    stepFunction fresh;
    fresh.syncUpdate(stepFunction::SEQUENCE_END).pattern(oldValues, 3);
    CHECK(fresh.length() == 3);

    // IMMEDIATE: the next sample, the step kept and wrapped (step 2 of 3 is
    // step 0 of 2).
    stepFunction now;
    now.pattern(oldValues, 3);
    for (unsigned int n = 0; n < PERIOD * 2 + 10; ++n)
      now.process(clock[n]);
    CHECK(now.lastStep() == 2);
    now.pattern(newValues, 2);
    CHECK(now.length() == 2);
    CHECK(now.lastStep() == 0);

    // INPUT_RESET: waits for the next wrap.
    stepFunction atWrap;
    atWrap.pattern(oldValues, 3).syncUpdate(stepFunction::INPUT_RESET);
    for (unsigned int n = 0; n < 10; ++n)
      atWrap.process(clock[n]);
    atWrap.pattern(newValues, 2);
    for (unsigned int n = 10; n < PERIOD; ++n) {
      atWrap.process(clock[n]);
      REQUIRE(atWrap.length() == 3);
    }
    CHECK(atWrap.process(clock[PERIOD]) == 2.f); // step 1 of the new pattern
    CHECK(atWrap.length() == 2);

    // SEQUENCE_END: waits for the step to go round from the last to the first.
    stepFunction atEnd;
    atEnd.pattern(oldValues, 3).syncUpdate(stepFunction::SEQUENCE_END);
    atEnd.process(clock[0]);
    atEnd.pattern(newValues, 2);
    for (unsigned int n = 1; n < PERIOD * 3; ++n) {
      atEnd.process(clock[n]);
      REQUIRE(atEnd.length() == 3);
    }
    CHECK(atEnd.process(clock[PERIOD * 3]) == 3.f); // step 0: from 3 to 2
    CHECK(atEnd.length() == 2);
    CHECK(atEnd.lastStep() == 0);

    // A waiting pattern lands at a reset, and when the mode turns IMMEDIATE.
    stepFunction byReset;
    byReset.syncUpdate(stepFunction::SEQUENCE_END);
    byReset.process(0.1f);
    byReset.pattern(newValues, 2);
    CHECK(byReset.length() == 0);
    byReset.reset();
    CHECK(byReset.length() == 2);
    byReset.pattern(oldValues, 3);
    CHECK(byReset.length() == 2);
    byReset.syncUpdate(stepFunction::IMMEDIATE);
    CHECK(byReset.length() == 3);

    // With a step signal, SEQUENCE_END waits for the step to fall, and
    // INPUT_RESET for the input to wrap.
    stepFunction signalEnd;
    signalEnd.pattern(oldValues, 3).syncUpdate(stepFunction::SEQUENCE_END);
    signalEnd.process(0.1f, 1.f);
    signalEnd.pattern(newValues, 2);
    signalEnd.process(0.95f, 2.f);
    signalEnd.process(0.05f, 2.f); // the input wraps, the step does not fall
    CHECK(signalEnd.length() == 3);
    signalEnd.process(0.1f, 1.f);
    CHECK(signalEnd.length() == 2);

    stepFunction signalWrap;
    signalWrap.pattern(oldValues, 3).syncUpdate(stepFunction::INPUT_RESET);
    signalWrap.process(0.1f, 2.f);
    signalWrap.pattern(newValues, 2);
    signalWrap.process(0.5f, 1.f); // the step falls, the input does not wrap
    signalWrap.process(0.95f, 1.f);
    CHECK(signalWrap.length() == 3);
    signalWrap.process(0.01f, 1.f);
    CHECK(signalWrap.length() == 2);
  }

  TEST_CASE("stepFunction: a sample that is not a number counts as the one before (#1081)") {
    std::vector<float> clock = Ramp(PERIOD * 3);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::vector<float> values = {0.f, 1.f, 0.5f};
    stepFunction fn;
    fn.pattern(values.data(), 3);
    clock[PERIOD] = nan; // the wrap sample: the wrap comes one sample late
    const outputs o = Process(fn, clock);
    CHECK(o.step[PERIOD] == 0);
    CHECK(o.out[PERIOD] == o.out[PERIOD - 1]);
    CHECK(o.step[PERIOD + 1] == 1);
    for (float x : o.out)
      REQUIRE(std::isfinite(x));
  }

  TEST_CASE("stepFunction: blocks match samples; nothing depends on the sample rate (#1081)") {
    const unsigned int blocks = 12;
    const std::vector<float> clock = Ramp(N * blocks, 250.f);
    const float values[] = {0.f, 1.f, 0.5f};
    const float curves[] = {0.3f, -0.2f, 0.f};
    stepFunction bySample;
    bySample.pattern(values, 3).curvePattern(curves, 3);
    const outputs ref = Process(bySample, clock);

    for (unsigned int rate : {44100u, 96000u}) {
      CAPTURE(rate);
      TestHelpers::ScopedSampleRate scoped(rate);
      stepFunction byBlock;
      byBlock.pattern(values, 3).curvePattern(curves, 3);
      buffer in(N);
      for (unsigned int b = 0; b < blocks; ++b) {
        for (unsigned int i = 0; i < N; ++i)
          in.getPtr()[i] = clock[b * N + i];
        buffer& out = byBlock(in);
        REQUIRE(out.getLength() == N);
        for (unsigned int i = 0; i < N; ++i)
          REQUIRE(out.getPtr()[i] == ref.out[b * N + i]);
      }
    }
  }

  TEST_CASE("stepFunction: processing allocates nothing (#1081)") {
    stepFunction fn;
    buffer in(N);
    buffer steps(N);
    for (unsigned int i = 0; i < N; ++i) {
      in.getPtr()[i] = static_cast<float>(i % 16) / 16.f;
      const unsigned int whole = i / 16; // whole steps of 16 samples
      steps.getPtr()[i] = static_cast<float>(whole);
    }
    const float values[] = {0.f, 1.f, 0.5f};
    const float list[] = {0.f, 0.f, 1.f, 10.f, 0.5f};
    fn(in);
    {
      TestHelpers::ProbeScope probe;
      fn.pattern(values, 3).curvePattern(values, 2).syncUpdate(stepFunction::INPUT_RESET);
      fn(in);
      fn.breakpoints(list, 5, false).step(2).reset().syncUpdate(stepFunction::SEQUENCE_END);
      fn(in, steps);
      fn.process(0.5f);
      fn.process(0.5f, 1.f);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
