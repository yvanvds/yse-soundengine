// Tests for YSE::DSP::phasorSubdiv (YseEngine/dsp/generators/phasorSubdiv.hpp),
// issue #1074. The input is a DSP::phasor at 375 Hz and 48 kHz: exactly 128
// samples per cycle, so sample n reads (n mod 128) / 128 and the plain
// subdivision by d is the closed form frac(d n / 128).

#include <doctest/doctest.h>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>
#include "dsp/generators/phasor.hpp"
#include "dsp/generators/phasorSubdiv.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::phasorSubdiv;
using silent = YSE::DSP::phasorSubdiv::silent;
using update = YSE::DSP::phasorSubdiv::update;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PERIOD = 128.0;

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

  double Frac(double x) {
    return x - std::floor(x);
  }

  // Distance on the 0..1 circle, so 0.9999999 and 0 are neighbours.
  double Circular(double a, double b) {
    const double d = Frac(a - b);
    return d < 0.5 ? d : 1.0 - d;
  }

  struct run {
    std::vector<float> out;
    std::vector<float> index;
  };

  // Sample by sample through process(), calling `before(n)` ahead of
  // sample n so a test can change the class at an exact sample.
  run Process(phasorSubdiv& fn, const std::vector<float>& in,
              const std::function<void(unsigned int)>& before = nullptr) {
    run r;
    for (unsigned int n = 0; n < in.size(); ++n) {
      if (before) before(n);
      r.out.push_back(fn.process(in[n]));
      r.index.push_back(fn.lastIndex());
    }
    return r;
  }

  // The duration-pattern reference: `units` the (padded) step lengths, `d`
  // the subdivisions. Position in the pattern P = (cycle mod M) d + d x.
  void PatternReference(const std::vector<float>& in, int d, const std::vector<int>& units,
                        std::vector<double>& ramp, std::vector<int>& step) {
    int total = 0;
    for (const int u : units)
      total += u;
    const int cycles = total >= d ? total / d : 1; // callers pass padded patterns
    int wraps = 0;
    for (unsigned int n = 0; n < in.size(); ++n) {
      if (n > 0 && in[n] < in[n - 1] - 0.5f) wraps++;
      const double p = (wraps % cycles) * d + static_cast<double>(in[n]) * d;
      int start = 0;
      unsigned int j = 0;
      while (j + 1 < units.size() && start + units[j] <= p) {
        start += units[j];
        ++j;
      }
      ramp.push_back((p - start) / units[j]);
      step.push_back(static_cast<int>(j));
    }
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("phasorSubdiv: defaults and setters (#1074)") {
    phasorSubdiv fn;
    CHECK(fn.div() == 1);
    CHECK(fn.patternLength() == 0);
    CHECK(fn.probLength() == 0);
    CHECK_FALSE(fn.lockProb());
    CHECK(fn.silentMode() == silent::hold);
    CHECK(fn.syncUpdate() == update::now);
    fn.div(0);
    CHECK(fn.div() == 1);
    fn.div(-5);
    CHECK(fn.div() == 1);
    fn.div(20000);
    CHECK(fn.div() == phasorSubdiv::MAX_DIV);
    fn.div(7).lockProb(true).silentMode(silent::minusOne).syncUpdate(update::input);
    CHECK(fn.div() == 7);
    CHECK(fn.lockProb());
    CHECK(fn.silentMode() == silent::minusOne);
    CHECK(fn.syncUpdate() == update::input);
    std::vector<int> longPattern(100, 1);
    fn.pattern(longPattern.data(), 100);
    CHECK(fn.patternLength() == phasorSubdiv::MAX_PATTERN);
    fn.pattern(nullptr, 3);
    CHECK(fn.patternLength() == 0);
    const float chances[] = {0.5f, 2.f, -1.f};
    fn.prob(chances, 3);
    CHECK(fn.probLength() == 3);
    fn.prob(nullptr, 0);
    CHECK(fn.probLength() == 0);
    CHECK(static_cast<int>(silent::hold) == 0);
    CHECK(static_cast<int>(silent::minusOne) == 1);
    CHECK(static_cast<int>(update::now) == 0);
    CHECK(static_cast<int>(update::output) == 1);
    CHECK(static_cast<int>(update::input) == 2);
  }

  TEST_CASE("phasorSubdiv: div d gives frac(d n / 128) and the step floor(d x), in phase, per "
            "block (#1074)") {
    const std::vector<float> in = Ramp(4 * N);
    for (const int d : {1, 2, 3, 4, 5, 7, 16, 64}) {
      CAPTURE(d);
      phasorSubdiv fn;
      fn.div(d);
      buffer block(N);
      int falls = 0;
      float last = 0.f;
      for (unsigned int first = 0; first < in.size(); first += N) {
        for (unsigned int i = 0; i < N; ++i)
          block.getPtr()[i] = in[first + i];
        buffer& out = fn(block);
        buffer& index = fn.index();
        REQUIRE(out.getLength() == N);
        REQUIRE(index.getLength() == N);
        for (unsigned int i = 0; i < N; ++i) {
          const unsigned int n = first + i;
          CAPTURE(n);
          const float y = out.getPtr()[i];
          CHECK(y >= 0.f);
          CHECK(y < 1.f);
          CHECK(Circular(y, Frac(d * static_cast<double>(n) / PERIOD)) < 1e-5);
          // Exactly the reference from the input sample read.
          const double u = static_cast<double>(in[n]) * d;
          CHECK(y == doctest::Approx(Frac(u)).epsilon(1e-6));
          CHECK(index.getPtr()[i] == static_cast<float>(std::floor(u)));
          if (n > 0 && y < last) falls++;
          last = y;
        }
      }
      // d ramps per input cycle: d * 4 starts, the first without a fall.
      CHECK(falls == 4 * d - 1);
    }
  }

  TEST_CASE("phasorSubdiv: a duration pattern splits the cycle unevenly (#1074)") {
    const std::vector<float> in = Ramp(3 * N);
    // div 3, 1 2: a ramp of a third, then one of two thirds.
    phasorSubdiv fn;
    const int steps[] = {1, 2};
    fn.div(3).pattern(steps, 2);
    const run r = Process(fn, in);
    for (unsigned int n = 0; n < in.size(); ++n) {
      CAPTURE(n);
      const double u = static_cast<double>(in[n]) * 3.0;
      if (u < 1.0) {
        CHECK(r.index[n] == 0.f);
        CHECK(r.out[n] == doctest::Approx(u).epsilon(1e-6));
      } else {
        CHECK(r.index[n] == 1.f);
        CHECK(r.out[n] == doctest::Approx((u - 1.0) / 2.0).epsilon(1e-6));
      }
    }
  }

  TEST_CASE("phasorSubdiv: a pattern short of the cycle is padded with one last step (#1074)") {
    const std::vector<float> in = Ramp(4 * N);
    struct padCase {
      int d;
      std::vector<int> given;
      std::vector<int> played;
    };
    for (const padCase& c : {padCase{5, {1, 2}, {1, 2, 2}}, padCase{4, {1, 2}, {1, 2, 1}},
                             padCase{4, {1, 3}, {1, 3}}, padCase{2, {3}, {3, 1}}}) {
      CAPTURE(c.d);
      phasorSubdiv fn;
      fn.div(c.d).pattern(c.given.data(), static_cast<UInt>(c.given.size()));
      const run r = Process(fn, in);
      std::vector<double> ramp;
      std::vector<int> step;
      PatternReference(in, c.d, c.played, ramp, step);
      for (unsigned int n = 0; n < in.size(); ++n) {
        CAPTURE(n);
        CHECK(r.index[n] == static_cast<float>(step[n]));
        CHECK(Circular(r.out[n], ramp[n]) < 1e-6);
      }
    }
  }

  TEST_CASE("phasorSubdiv: a pattern longer than the cycle spans several input cycles, a step "
            "across an input wrap runs on (#1074)") {
    const std::vector<float> in = Ramp(12 * N);
    for (const std::vector<int>& steps :
         {std::vector<int>{2, 1, 1, 2, 1, 1, 1}, std::vector<int>{2, 2, 2}}) {
      CAPTURE(steps.size());
      phasorSubdiv fn;
      fn.div(3).pattern(steps.data(), static_cast<UInt>(steps.size()));
      const run r = Process(fn, in);
      std::vector<double> ramp;
      std::vector<int> step;
      PatternReference(in, 3, steps, ramp, step);
      for (unsigned int n = 0; n < in.size(); ++n) {
        CAPTURE(n);
        CHECK(r.index[n] == static_cast<float>(step[n]));
        CHECK(Circular(r.out[n], ramp[n]) < 1e-6);
      }
    }
    // 2 2 2 over div 3: the middle step rises without a jump through the
    // input wrap at sample 128.
    phasorSubdiv fn;
    const int steps[] = {2, 2, 2};
    fn.div(3).pattern(steps, 3);
    const run r = Process(fn, in);
    CHECK(r.index[127] == 1.f);
    CHECK(r.index[128] == 1.f);
    CHECK(r.out[128] > r.out[127]);
    CHECK(r.out[128] == doctest::Approx(0.5f));
  }

  TEST_CASE("phasorSubdiv: prob 0 silences a step, the index holds or reads -1 (#1074)") {
    const std::vector<float> in = Ramp(2 * N);
    for (const silent mode : {silent::hold, silent::minusOne}) {
      CAPTURE(static_cast<int>(mode));
      phasorSubdiv fn;
      const float chances[] = {1.f, 0.f};
      fn.div(4).prob(chances, 2).silentMode(mode);
      const run r = Process(fn, in);
      for (unsigned int n = 0; n < in.size(); ++n) {
        CAPTURE(n);
        const double u = static_cast<double>(in[n]) * 4.0;
        const int k = static_cast<int>(std::floor(u));
        if (k % 2 == 0) {
          CHECK(r.out[n] == doctest::Approx(Frac(u)).epsilon(1e-6));
          CHECK(r.index[n] == static_cast<float>(k));
        } else {
          CHECK(r.out[n] == 0.f);
          CHECK(r.index[n] == (mode == silent::hold ? static_cast<float>(k - 1) : -1.f));
        }
      }
    }
    // A silent first step holds index 0.
    phasorSubdiv fn;
    const float never[] = {0.f};
    fn.div(2).prob(never, 1);
    const run r = Process(fn, in);
    for (unsigned int n = 0; n < in.size(); ++n) {
      CHECK(r.out[n] == 0.f);
      CHECK(r.index[n] == 0.f);
    }
    // Cleared: every step plays again.
    fn.prob(nullptr, 0);
    const run again = Process(fn, in);
    CHECK(again.out[N / 2 - 1] > 0.9f);
  }

  TEST_CASE("phasorSubdiv: prob between 0 and 1 is a seeded draw per step (#1074)") {
    const std::vector<float> in = Ramp(64 * N); // 64 cycles of 16 steps
    const float half[] = {0.5f};
    const auto played = [&](std::uint32_t seed, std::vector<float>& out) {
      phasorSubdiv fn;
      fn.div(16).prob(half, 1).seed(seed);
      out = Process(fn, in).out;
      int steps = 0;
      int heard = 0;
      // Each step is 8 samples; its middle sample is 0.5 when it plays.
      for (unsigned int n = 4; n < in.size(); n += 8) {
        steps++;
        if (out[n] > 0.f) heard++;
      }
      CHECK(steps == 1024);
      return heard;
    };
    std::vector<float> a;
    std::vector<float> b;
    std::vector<float> c;
    const int heardA = played(1234u, a);
    const int heardB = played(1234u, b);
    played(98765u, c);
    CHECK(heardA > 1024 * 4 / 10);
    CHECK(heardA < 1024 * 6 / 10);
    CHECK(heardA == heardB);
    CHECK(a == b);
    CHECK(a != c);
    // A step plays whole or not at all: decided once, at its start.
    for (unsigned int n = 0; n < in.size(); n += 8) {
      const bool on = a[n + 4] > 0.f;
      for (unsigned int i = 1; i < 8; ++i)
        CHECK((a[n + i] > 0.f) == on);
    }
  }

  TEST_CASE("phasorSubdiv: lockprob counts duration steps, off counts subdivisions (#1074)") {
    const std::vector<float> in = Ramp(2 * N);
    // div 4, pattern 2 1 1 starts its steps on units 0, 2, 3 (then 4, 6, 7);
    // prob 0 1.
    const int steps[] = {2, 1, 1};
    const float chances[] = {0.f, 1.f};
    for (const bool lock : {false, true}) {
      CAPTURE(lock);
      phasorSubdiv fn;
      fn.div(4).pattern(steps, 3).prob(chances, 2).lockProb(lock);
      const run r = Process(fn, in);
      // Middle samples of the six steps over two cycles: units 0-1, 2, 3.
      const unsigned int middles[] = {32, 80, 112, 160, 208, 240};
      // Off: units 0, 2, 3, 4, 6, 7 -> chances 0 0 1 0 0 1.
      // On: steps 0..5 -> chances 0 1 0 1 0 1.
      const bool expectOff[] = {false, false, true, false, false, true};
      const bool expectOn[] = {false, true, false, true, false, true};
      for (unsigned int s = 0; s < 6; ++s) {
        CAPTURE(s);
        CHECK((r.out[middles[s]] > 0.f) == (lock ? expectOn[s] : expectOff[s]));
      }
    }
  }

  TEST_CASE("phasorSubdiv: syncupdate lands a new div now, at the next output step or at the "
            "next input wrap (#1074)") {
    const std::vector<float> in = Ramp(2 * N);
    // div 2, changed to 4 ahead of sample 40 (x = 0.3125, inside step 0).
    const auto change = [&](update mode) {
      phasorSubdiv fn;
      fn.div(2).syncUpdate(mode);
      return Process(fn, in, [&](unsigned int n) {
        if (n == 40) fn.div(4);
      });
    };
    const auto divided = [&](unsigned int n, double d) {
      return Frac(static_cast<double>(in[n]) * d);
    };

    const run now = change(update::now);
    CHECK(now.out[39] == doctest::Approx(divided(39, 2)));
    CHECK(now.out[40] == doctest::Approx(divided(40, 4)));
    CHECK(now.index[40] == 1.f);

    const run output = change(update::output);
    CHECK(output.out[63] == doctest::Approx(divided(63, 2)));
    CHECK(output.index[63] == 0.f);
    CHECK(output.out[64] == doctest::Approx(divided(64, 4)));
    CHECK(output.index[64] == 2.f);

    const run input = change(update::input);
    CHECK(input.out[127] == doctest::Approx(divided(127, 2)));
    CHECK(input.index[127] == 1.f);
    CHECK(input.out[128] == 0.f);
    CHECK(input.out[160] == doctest::Approx(divided(160, 4)));
    CHECK(input.index[160] == 1.f);

    // A pattern lands the same way.
    phasorSubdiv fn;
    fn.div(4).syncUpdate(update::input);
    const int steps[] = {3, 1};
    const run pattern = Process(fn, in, [&](unsigned int n) {
      if (n == 10) fn.pattern(steps, 2);
    });
    CHECK(pattern.index[40] == 1.f); // still four even steps
    CHECK(pattern.index[128 + 40] == 0.f); // then 3 1
    CHECK(pattern.out[128 + 48] == doctest::Approx(0.5f));
  }

  TEST_CASE("phasorSubdiv: a falling, out-of-range or non-finite input (#1074)") {
    // A falling ramp subdivides too, its steps counting down.
    std::vector<float> falling;
    falling.reserve(256);
    for (unsigned int n = 0; n < 256; ++n)
      falling.push_back(1.f - static_cast<float>((n % 128) + 1) / 128.f);
    phasorSubdiv down;
    down.div(4);
    const run r = Process(down, falling);
    for (unsigned int n = 0; n < falling.size(); ++n) {
      CAPTURE(n);
      const double u = static_cast<double>(falling[n]) * 4.0;
      CHECK(r.out[n] == doctest::Approx(Frac(u)).epsilon(1e-6));
      CHECK(r.index[n] == static_cast<float>(std::floor(u)));
    }

    // Out of range: wrapped into 0..1.
    phasorSubdiv wrapped;
    wrapped.div(2);
    CHECK(wrapped.process(1.25f) == doctest::Approx(0.5f));
    CHECK(wrapped.lastIndex() == 0.f);
    CHECK(wrapped.process(-0.25f) == doctest::Approx(0.5f));
    CHECK(wrapped.lastIndex() == 1.f);

    // Not a number: the previous input.
    phasorSubdiv held;
    held.div(4);
    const float before = held.process(0.3f);
    CHECK(held.process(std::numeric_limits<float>::quiet_NaN()) == before);
    CHECK(held.process(std::numeric_limits<float>::infinity()) == before);
    CHECK(held.lastIndex() == 1.f);
  }

  TEST_CASE("phasorSubdiv: does not depend on the sample rate (#1074)") {
    const std::vector<float> in = Ramp(2 * N);
    const int steps[] = {1, 2};
    const auto at = [&](unsigned int rate) {
      TestHelpers::ScopedSampleRate scoped(rate);
      phasorSubdiv fn;
      fn.div(3).pattern(steps, 2);
      return Process(fn, in).out;
    };
    CHECK(at(44100) == at(96000));
  }

  TEST_CASE("phasorSubdiv: setters and blocks allocate nothing (#1074)") {
    phasorSubdiv fn;
    buffer in(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = static_cast<float>(i) / N;
    const int steps[] = {2, 1, 1};
    const float chances[] = {1.f, 0.5f, 0.f};
    fn(in);
    {
      TestHelpers::ProbeScope probe;
      fn.div(4).pattern(steps, 3).prob(chances, 3).lockProb(true).syncUpdate(update::output);
      fn(in);
      fn.div(9).syncUpdate(update::input);
      fn(in);
      fn(in);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
