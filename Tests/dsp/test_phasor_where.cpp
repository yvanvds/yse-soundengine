// Tests for YSE::DSP::phasorWhere (YseEngine/dsp/generators/phasorWhere.hpp),
// issue #1078. The input is a DSP::phasor at 375 Hz and 48 kHz: exactly 128
// samples per cycle, so sample n reads (n mod 128) / 128 and, from the first
// step on, n mod 128 samples have elapsed and 128 - n mod 128 remain.

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/generators/phasor.hpp"
#include "dsp/generators/phasorWhere.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::phasorWhere;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr unsigned int PERIOD = 128;
  constexpr double MS = 1000.0 / 48000.0; // one sample at 48 kHz

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

  struct whereTimes {
    std::vector<float> elapsed;
    std::vector<float> remaining;
  };

  // Sample by sample through process().
  whereTimes Process(phasorWhere& fn, const std::vector<float>& in) {
    whereTimes t;
    for (float x : in) {
      t.elapsed.push_back(fn.process(x));
      t.remaining.push_back(fn.lastRemaining());
    }
    return t;
  }

  bool Near(float actual, double samples, double msPerSample = MS) {
    return std::fabs(static_cast<double>(actual) - samples * msPerSample) < 1e-4;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("phasorWhere: a rising phasor, elapsed and remaining in ms (#1078)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::vector<float> in = Ramp(4 * PERIOD);
    phasorWhere fn;
    const whereTimes t = Process(fn, in);

    // Before the input first moves there is nothing to measure.
    CHECK(t.elapsed[0] == 0.f);
    CHECK(t.remaining[0] == 0.f);
    bool elapsedOk = true;
    bool remainingOk = true;
    bool sumOk = true;
    for (unsigned int n = 1; n < in.size(); ++n) {
      const double k = n % PERIOD;
      if (!Near(t.elapsed[n], k)) elapsedOk = false;
      if (!Near(t.remaining[n], PERIOD - k)) remainingOk = false;
      if (!Near(t.elapsed[n] + t.remaining[n], PERIOD)) sumOk = false;
    }
    CHECK(elapsedOk);
    CHECK(remainingOk);
    CHECK(sumOk);
    // At each reset: 0 elapsed, a whole cycle (2.667 ms) to go.
    CHECK(t.elapsed[256] == doctest::Approx(0.0));
    CHECK(t.remaining[256] == doctest::Approx(PERIOD * MS));
    CHECK(t.remaining[255] == doctest::Approx(MS));
  }

  TEST_CASE("phasorWhere: elapsed counts from between two samples (#1078)") {
    TestHelpers::ScopedSampleRate rate(48000);
    // Steps of 0.01 from 0.005: the wrap from 0.995 to 0.005 crossed the
    // reset half a step back.
    std::vector<float> in;
    in.reserve(250);
    for (unsigned int n = 0; n < 250; ++n)
      in.push_back(static_cast<float>(std::fmod(0.005 + 0.01 * n, 1.0)));
    phasorWhere fn;
    const whereTimes t = Process(fn, in);
    for (unsigned int n = 100; n < 250; ++n) {
      CAPTURE(n);
      const double k = (n % 100) + 0.5;
      CHECK(t.elapsed[n] == doctest::Approx(k * MS).epsilon(1e-3));
      CHECK(t.remaining[n] == doctest::Approx((100.0 - k) * MS).epsilon(1e-3));
    }
  }

  TEST_CASE("phasorWhere: a falling ramp (#1078)") {
    TestHelpers::ScopedSampleRate rate(48000);
    // 0, 127/128, 126/128 ... 1/128, 0, 127/128 ...: a cycle starts at 1
    // (the backward wrap) and ends at 0.
    const std::vector<float> rising = Ramp(3 * PERIOD);
    std::vector<float> falling;
    falling.reserve(rising.size());
    for (unsigned int n = 0; n < rising.size(); ++n)
      falling.push_back(n % PERIOD == 0 ? 0.f : 1.f - rising[n]);
    phasorWhere fn;
    const whereTimes t = Process(fn, falling);
    CHECK(t.elapsed[0] == 0.f);
    for (unsigned int n = 1; n < falling.size(); ++n) {
      CAPTURE(n);
      const double k = n % PERIOD == 0 ? PERIOD : n % PERIOD;
      CHECK(Near(t.elapsed[n], k));
      CHECK(Near(t.remaining[n], PERIOD - k));
    }
    // So a DSP::phasor with a negative frequency reads the same.
    const std::vector<float> negative = Ramp(3 * PERIOD, -375.f);
    phasorWhere neg;
    const whereTimes u = Process(neg, negative);
    for (unsigned int n = 2 * PERIOD + 1; n < 3 * PERIOD - 1; ++n) {
      CAPTURE(n);
      CHECK(u.elapsed[n] + u.remaining[n] == doctest::Approx(PERIOD * MS).epsilon(1e-3));
    }
  }

  TEST_CASE("phasorWhere: speed changes, turns and stops (#1078)") {
    TestHelpers::ScopedSampleRate rate(48000);

    // 64 steps of 1/128 to 0.5, then steps of 1/64: elapsed counts on,
    // remaining follows the new speed at once.
    {
      std::vector<float> in;
      for (unsigned int n = 0; n <= 64; ++n)
        in.push_back(static_cast<float>(n) / 128.f);
      for (unsigned int n = 1; n < 32; ++n)
        in.push_back(0.5f + static_cast<float>(n) / 64.f);
      phasorWhere fn;
      const whereTimes t = Process(fn, in);
      CHECK(Near(t.elapsed[64], 64));
      CHECK(Near(t.remaining[64], 64));
      for (unsigned int n = 65; n < in.size(); ++n) {
        CAPTURE(n);
        CHECK(Near(t.elapsed[n], n));
        CHECK(Near(t.remaining[n], 32.0 - (n - 64)));
      }
    }

    // Rise to 0.5, then fall: the turn starts a cycle measured downwards;
    // with no reset to count from, elapsed is estimated from the speed.
    {
      std::vector<float> in;
      for (unsigned int n = 0; n <= 64; ++n)
        in.push_back(static_cast<float>(n) / 128.f);
      for (unsigned int n = 1; n < 32; ++n)
        in.push_back(0.5f - static_cast<float>(n) / 128.f);
      phasorWhere fn;
      const whereTimes t = Process(fn, in);
      for (unsigned int n = 65; n < in.size(); ++n) {
        CAPTURE(n);
        // Falling, n - 64 steps below 0.5: done (64 + n - 64) / 128.
        CHECK(Near(t.elapsed[n], n));
        CHECK(Near(t.remaining[n], 128.0 - n));
      }
    }

    // Held still for 20 samples at 0.25: elapsed counts on, remaining
    // keeps its last prediction; then it moves on as before.
    {
      std::vector<float> in;
      for (unsigned int n = 0; n <= 32; ++n)
        in.push_back(static_cast<float>(n) / 128.f);
      for (unsigned int n = 0; n < 20; ++n)
        in.push_back(0.25f);
      for (unsigned int n = 33; n < 128; ++n)
        in.push_back(static_cast<float>(n) / 128.f);
      phasorWhere fn;
      const whereTimes t = Process(fn, in);
      for (unsigned int n = 33; n < 53; ++n) {
        CAPTURE(n);
        CHECK(Near(t.elapsed[n], n));
        CHECK(Near(t.remaining[n], 96));
      }
      CHECK(Near(t.elapsed[53], 53));
      CHECK(Near(t.remaining[53], 95));
    }

    // Held from the start: nothing to measure yet; the first step anchors.
    {
      const std::vector<float> in = {0.3f, 0.3f, 0.3f, 0.31f, 0.32f};
      phasorWhere fn;
      const whereTimes t = Process(fn, in);
      for (unsigned int n = 0; n < 3; ++n) {
        CHECK(t.elapsed[n] == 0.f);
        CHECK(t.remaining[n] == 0.f);
      }
      CHECK(t.elapsed[3] == doctest::Approx(31.0 * MS).epsilon(1e-4));
      CHECK(t.remaining[3] == doctest::Approx(69.0 * MS).epsilon(1e-4));
      CHECK(t.elapsed[4] == doctest::Approx(32.0 * MS).epsilon(1e-4));
    }
  }

  TEST_CASE("phasorWhere: out-of-range and non-finite input (#1078)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::vector<float> rising = Ramp(3 * PERIOD);

    // Out of range: wrapped into 0..1.
    std::vector<float> shifted;
    shifted.reserve(rising.size());
    for (unsigned int n = 0; n < rising.size(); ++n)
      shifted.push_back(rising[n] + static_cast<float>(n % 3) * 2.f - 2.f);
    phasorWhere wrapped;
    const whereTimes w = Process(wrapped, shifted);
    for (unsigned int n = 1; n < shifted.size(); ++n) {
      CAPTURE(n);
      CHECK(w.elapsed[n] == doctest::Approx((n % PERIOD) * MS).epsilon(1e-3));
      CHECK(w.remaining[n] == doctest::Approx((PERIOD - n % PERIOD) * MS).epsilon(1e-3));
    }

    // Not a finite number: the previous input, read as holding still.
    std::vector<float> holes(rising);
    holes[0] = std::numeric_limits<float>::quiet_NaN();
    holes[70] = std::numeric_limits<float>::infinity();
    holes[71] = -std::numeric_limits<float>::infinity();
    holes[72] = std::numeric_limits<float>::quiet_NaN();
    phasorWhere holed;
    const whereTimes h = Process(holed, holes);
    CHECK(h.elapsed[0] == 0.f);
    CHECK(Near(h.elapsed[72], 72));
    CHECK(Near(h.remaining[72], PERIOD - 69));
    // Sample 73 jumps four steps at once: four times the speed.
    CHECK(Near(h.elapsed[73], 73));
    CHECK(Near(h.remaining[73], (PERIOD - 73) / 4.0));
    CHECK(Near(h.remaining[74], PERIOD - 74));
    for (unsigned int n = 0; n < holes.size(); ++n) {
      CHECK(std::isfinite(h.elapsed[n]));
      CHECK(std::isfinite(h.remaining[n]));
    }
  }

  TEST_CASE("phasorWhere: blocks match samples; follows the live sample rate (#1078)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::vector<float> in = Ramp(2 * N);
    phasorWhere samples;
    const whereTimes one = Process(samples, in);

    phasorWhere blocks;
    buffer chunk(N);
    whereTimes two;
    for (unsigned int b = 0; b < 2; ++b) {
      for (unsigned int i = 0; i < N; ++i)
        chunk.getPtr()[i] = in[b * N + i];
      buffer& e = blocks(chunk);
      REQUIRE(e.getLength() == N);
      REQUIRE(blocks.remaining().getLength() == N);
      for (unsigned int i = 0; i < N; ++i) {
        two.elapsed.push_back(e.getPtr()[i]);
        two.remaining.push_back(blocks.remaining().getPtr()[i]);
      }
      CHECK(blocks.lastRemaining() == two.remaining.back());
    }
    CHECK(one.elapsed == two.elapsed);
    CHECK(one.remaining == two.remaining);

    // The same input read at 96 kHz takes half the milliseconds; the rate
    // is read live, so a change between blocks applies from the next one.
    YSE::SAMPLERATE = 96000;
    phasorWhere fast;
    const whereTimes t = Process(fast, in);
    for (unsigned int n = 1; n < in.size(); ++n) {
      CAPTURE(n);
      CHECK(Near(t.elapsed[n], n % PERIOD, 1000.0 / 96000.0));
      CHECK(Near(t.remaining[n], PERIOD - n % PERIOD, 1000.0 / 96000.0));
    }
    phasorWhere switched;
    YSE::SAMPLERATE = 48000;
    for (unsigned int i = 0; i < N; ++i)
      chunk.getPtr()[i] = in[i];
    (void)switched(chunk);
    YSE::SAMPLERATE = 96000;
    for (unsigned int i = 0; i < N; ++i)
      chunk.getPtr()[i] = in[N + i];
    buffer& e = switched(chunk);
    CHECK(Near(e.getPtr()[5], (N + 5) % PERIOD, 1000.0 / 96000.0));
  }

  TEST_CASE("phasorWhere: processing allocates nothing (#1078)") {
    phasorWhere fn;
    buffer in(N);
    in = 0.25f;
    fn(in);
    TestHelpers::ProbeScope probe;
    fn(in);
    (void)fn.process(0.5f);
    (void)fn.remaining();
    CHECK(TestHelpers::g_alloc_count.load() == 0);
  }

} // TEST_SUITE("dsp")
