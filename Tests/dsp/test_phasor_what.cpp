// Tests for YSE::DSP::phasorWhat (YseEngine/dsp/generators/phasorWhat.hpp),
// issue #1079. The input is a DSP::phasor at 375 Hz and 48 kHz: exactly 128
// samples per cycle, so sample n reads (n mod 128) / 128. A value t is passed
// rising at the first sample above it, the one after the previous sample was
// at or below it.

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/generators/phasor.hpp"
#include "dsp/generators/phasorWhat.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::phasorWhat;

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

  // The same ramp falling: 0, 127/128, 126/128 ... 1/128, 0 ...
  std::vector<float> Falling(unsigned int samples) {
    const std::vector<float> rising = Ramp(samples);
    std::vector<float> falling;
    falling.reserve(rising.size());
    for (unsigned int n = 0; n < rising.size(); ++n)
      falling.push_back(n % PERIOD == 0 ? 0.f : 1.f - rising[n]);
    return falling;
  }

  // Sample by sample through process().
  std::vector<float> Process(phasorWhat& fn, const std::vector<float>& in) {
    std::vector<float> out;
    out.reserve(in.size());
    for (float x : in)
      out.push_back(fn.process(x));
    return out;
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

  // The closed form for the 128-sample ramps: the samples n >= first whose
  // position in the cycle is in `positions`.
  std::vector<unsigned int> Expected(unsigned int samples, std::vector<unsigned int> positions,
                                     unsigned int first = 1) {
    std::vector<unsigned int> at;
    for (unsigned int n = first; n < samples; ++n)
      for (unsigned int p : positions)
        if (n % PERIOD == p) at.push_back(n);
    return at;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("phasorWhat: four values on a rising phasor, against the closed form (#1079)") {
    const std::vector<float> in = Ramp(4 * PERIOD);
    phasorWhat fn;
    const float quarters[] = {0.f, 0.25f, 0.5f, 0.75f};
    fn.matches(quarters, 4);
    CHECK(fn.count() == 4);
    // Each value lands exactly on a sample, so it fires on the sample after:
    // 0 at 1, 0.25 (32/128) at 33 and so on. The reset passes 0: 127/128 to
    // 0 rises through 1 = 0 and fires one sample later, when 0 moves on.
    CHECK(Fired(Process(fn, in)) == Expected(in.size(), {1, 33, 65, 97}));
  }

  TEST_CASE("phasorWhat: values between samples, 1 is 0, a wrap is not a fall (#1079)") {
    const std::vector<float> in = Ramp(3 * PERIOD);
    {
      // 0.3 * 128 = 38.4: the first sample above it is 39.
      phasorWhat fn;
      const float v[] = {0.3f};
      fn.matches(v, 1);
      CHECK(Fired(Process(fn, in)) == Expected(in.size(), {39}));
    }
    {
      phasorWhat fn;
      const float one[] = {1.f};
      fn.matches(one, 1);
      CHECK(Fired(Process(fn, in)) == Expected(in.size(), {1}));
    }
    {
      // The reset from 127/128 to 0 jumps down past 0.5 but is a rise: one
      // impulse a cycle, not two, in every mode that matches rises.
      for (auto mode : {phasorWhat::ASCENDING, phasorWhat::BOTH}) {
        CAPTURE(mode);
        phasorWhat fn;
        const float half[] = {0.5f};
        fn.matches(half, 1).triggerMode(mode);
        CHECK(Fired(Process(fn, in)) == Expected(in.size(), {65}));
      }
      phasorWhat fn;
      const float half[] = {0.5f};
      fn.matches(half, 1).triggerMode(phasorWhat::DESCENDING);
      CHECK(Fired(Process(fn, in)).empty());
    }
    {
      // Wrapped values: -0.75 and 1.25 are 0.25.
      phasorWhat fn;
      const float v[] = {-0.75f, 1.25f};
      fn.matches(v, 2);
      CHECK(Fired(Process(fn, in)) == Expected(in.size(), {33}));
    }
  }

  TEST_CASE("phasorWhat: a falling ramp and the trigger modes (#1079)") {
    const std::vector<float> in = Falling(3 * PERIOD);
    const float v[] = {0.f, 0.25f};
    // Falling: 0.25 (32/128) is at sample 96 of a cycle and fires at 97; 0
    // at 0 and fires at 1, when the backward wrap moves on from it.
    for (auto mode : {phasorWhat::DESCENDING, phasorWhat::BOTH}) {
      CAPTURE(mode);
      phasorWhat fn;
      fn.matches(v, 2).triggerMode(mode);
      CHECK(Fired(Process(fn, in)) == Expected(in.size(), {1, 97}));
    }
    phasorWhat fn;
    fn.matches(v, 2).triggerMode(phasorWhat::ASCENDING);
    CHECK(Fired(Process(fn, in)).empty());
  }

  TEST_CASE("phasorWhat: equals mode fires where a sample is the value (#1079)") {
    const std::vector<float> in = Ramp(3 * PERIOD);
    phasorWhat fn;
    const float v[] = {0.f, 0.25f, 0.3f};
    fn.matches(v, 3).triggerMode(phasorWhat::EQUALS);
    // 0.3 is never hit; 0 fires at the very first sample too.
    CHECK(Fired(Process(fn, in)) == Expected(in.size(), {0, 32}, 0));

    // A held input fires at every sample.
    phasorWhat held;
    const float quarter[] = {0.25f};
    held.matches(quarter, 1).triggerMode(phasorWhat::EQUALS);
    CHECK(Fired(Process(held, {0.25f, 0.25f, 0.5f, 0.25f})) == std::vector<unsigned int>{0, 1, 3});
    // In the other modes a held input fires once, when it moves on.
    phasorWhat moving;
    moving.matches(quarter, 1);
    CHECK(Fired(Process(moving, {0.25f, 0.25f, 0.25f, 0.5f})) == std::vector<unsigned int>{3});
  }

  TEST_CASE("phasorWhat: the first sample, several values in one step, odd input (#1079)") {
    const float v[] = {0.25f, 0.3f, 0.35f};
    {
      // The first sample has no step to pass anything, even at 0.4.
      phasorWhat fn;
      fn.matches(v, 3);
      CHECK(Fired(Process(fn, {0.4f, 0.1f, 0.4f})) == std::vector<unsigned int>{1, 2});
    }
    {
      // Three values in one step: one impulse of 1.
      phasorWhat fn;
      fn.matches(v, 3);
      const std::vector<float> out = Process(fn, {0.2f, 0.4f});
      CHECK(out[1] == 1.f);
    }
    {
      // Not a number: no impulse, and the next step is from 0.2.
      phasorWhat fn;
      const float nan = std::numeric_limits<float>::quiet_NaN();
      const float inf = std::numeric_limits<float>::infinity();
      fn.matches(v, 1);
      CHECK(Fired(Process(fn, {0.2f, nan, inf, 0.26f})) == std::vector<unsigned int>{3});
    }
    {
      // Out-of-range input is wrapped: 2.2 to 3.26 rises from 0.2 to 0.26.
      phasorWhat fn;
      fn.matches(v, 1);
      CHECK(Fired(Process(fn, {2.2f, 3.26f})) == std::vector<unsigned int>{1});
      // -0.8 is 0.2 too.
      phasorWhat neg;
      neg.matches(v, 1);
      CHECK(Fired(Process(neg, {-0.8f, 0.26f})) == std::vector<unsigned int>{1});
    }
    {
      // No values, or values cleared: nothing fires.
      phasorWhat fn;
      CHECK(Fired(Process(fn, Ramp(PERIOD))).empty());
      fn.matches(v, 3).clear();
      CHECK(fn.count() == 0);
      CHECK(Fired(Process(fn, Ramp(PERIOD))).empty());
    }
  }

  TEST_CASE("phasorWhat: the list holds 64 finite values (#1079)") {
    std::vector<float> many;
    many.reserve(70);
    for (unsigned int i = 0; i < 70; ++i)
      many.push_back(static_cast<float>(i) / 128.f);
    phasorWhat fn;
    fn.matches(many.data(), static_cast<UInt>(many.size()));
    CHECK(fn.count() == phasorWhat::MAX_MATCHES);
    // 0 .. 63 / 128 kept: the samples after them, 1 .. 64, fire.
    std::vector<unsigned int> first64;
    for (unsigned int n = 1; n <= 64; ++n)
      first64.push_back(n);
    CHECK(Fired(Process(fn, Ramp(PERIOD))) == first64);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float odd[] = {nan, 0.5f, std::numeric_limits<float>::infinity()};
    fn.matches(odd, 3);
    CHECK(fn.count() == 1);
    fn.matches(nullptr, 5);
    CHECK(fn.count() == 0);
  }

  TEST_CASE("phasorWhat: syncUpdate holds a new list until the input wraps (#1079)") {
    const std::vector<float> in = Ramp(3 * PERIOD);
    const float half[] = {0.5f};
    const float quarter[] = {0.25f};
    const auto Run = [&](bool synced) {
      phasorWhat fn;
      fn.syncUpdate(synced).matches(half, 1); // before the first sample: lands at once
      std::vector<float> out;
      for (unsigned int n = 0; n < in.size(); ++n) {
        if (n == 20) fn.matches(quarter, 1); // a cycle's 0.25 is still ahead
        if (n == 140) fn.clear();
        out.push_back(fn.process(in[n]));
      }
      return Fired(out);
    };
    // Unsynced: 0.25 from sample 20 (33), cleared at 140.
    CHECK(Run(false) == std::vector<unsigned int>{33});
    // Synced: 0.5 plays out the first cycle (65), 0.25 starts at the wrap at
    // 128 (161), the clear waits for the wrap at 256.
    CHECK(Run(true) == std::vector<unsigned int>{65, 161});

    // A list waiting when sync goes off lands at the next sample.
    phasorWhat fn;
    fn.syncUpdate(true);
    for (unsigned int n = 0; n < 10; ++n)
      fn.process(in[n]);
    fn.matches(quarter, 1);
    CHECK(fn.count() == 0);
    fn.syncUpdate(false);
    std::vector<float> out;
    for (unsigned int n = 10; n < PERIOD; ++n)
      out.push_back(fn.process(in[n]));
    CHECK(fn.count() == 1);
    CHECK(Fired(out) == std::vector<unsigned int>{33 - 10});

    // A falling ramp's backward wrap lands a list too.
    const std::vector<float> falling = Falling(2 * PERIOD);
    phasorWhat back;
    back.syncUpdate(true);
    for (unsigned int n = 0; n < 10; ++n)
      back.process(falling[n]);
    back.matches(quarter, 1);
    std::vector<float> fallOut;
    for (unsigned int n = 10; n < falling.size(); ++n)
      fallOut.push_back(back.process(falling[n]));
    // Lands at 129 (the wrap from 0 back through 1), fires at 128 + 97.
    CHECK(Fired(fallOut) == std::vector<unsigned int>{225 - 10});
  }

  TEST_CASE("phasorWhat: block and sample processing agree, at any length (#1079)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const float v[] = {0.f, 0.3f, 0.61f, 0.9f};
    phasorWhat bySample;
    phasorWhat byBlock;
    bySample.matches(v, 4);
    byBlock.matches(v, 4);
    phasor source;
    for (unsigned int block = 0; block < 6; ++block) {
      CAPTURE(block);
      const unsigned int length = block % 2 == 0 ? N : 37;
      buffer& in = source(block < 3 ? 517.f : -211.f, length);
      std::vector<float> expected;
      expected.reserve(in.getLength());
      for (unsigned int i = 0; i < in.getLength(); ++i)
        expected.push_back(bySample.process(in.getPtr()[i]));
      buffer& out = byBlock(in);
      REQUIRE(out.getLength() == in.getLength());
      bool same = true;
      for (unsigned int i = 0; i < out.getLength(); ++i)
        if (out.getPtr()[i] != expected[i]) same = false;
      CHECK(same);
    }
  }

  TEST_CASE("phasorWhat: processing allocates nothing (#1079)") {
    TestHelpers::ScopedSampleRate rate(48000);
    phasorWhat fn;
    phasor source;
    const float v[] = {0.f, 0.5f};
    buffer& warm = source(375.f, N);
    fn(warm);
    {
      TestHelpers::ProbeScope probe;
      fn.matches(v, 2).triggerMode(phasorWhat::BOTH).syncUpdate(true);
      for (int block = 0; block < 4; ++block)
        fn(source(375.f, N));
      fn.clear();
      fn.process(0.25f);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
