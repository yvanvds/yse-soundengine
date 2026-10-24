// Tests for YSE::DSP::phasorTwist (YseEngine/dsp/generators/phasorTwist.hpp),
// issue #1077. The input is a DSP::phasor at 375 Hz and 48 kHz: exactly 128
// samples per cycle, so sample n reads (n mod 128) / 128 and the output for
// curve c is the closed form in Bent() below.

#include <doctest/doctest.h>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>
#include "dsp/generators/phasor.hpp"
#include "dsp/generators/phasorTwist.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::phasorTwist;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr unsigned int PERIOD = 128;

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

  // The steepness for curve c, as the header documents it.
  double Steepness(double c) {
    const double h = 0.91 * std::pow(1.2 * std::fabs(c), 0.41);
    return h / (1.0 - h);
  }

  // The closed form for a rising input x (0..1) and curve c.
  double Bent(double x, double c) {
    if (c == 0.0) return x;
    const double k = Steepness(c);
    if (c > 0.0) return std::expm1(k * x) / std::expm1(k);
    return 1.0 - std::expm1(k * (1.0 - x)) / std::expm1(k);
  }

  // The closed form at sample n of the 128-sample rising input.
  double Expected(unsigned int n, double c) {
    return Bent(static_cast<double>(n % PERIOD) / PERIOD, c);
  }

  // Sample by sample through process(), calling `before(n)` ahead of
  // sample n so a test can change the class at an exact sample.
  std::vector<float> Process(phasorTwist& fn, const std::vector<float>& in,
                             const std::function<void(unsigned int)>& before = nullptr) {
    std::vector<float> out;
    for (unsigned int n = 0; n < in.size(); ++n) {
      if (before) before(n);
      out.push_back(fn.process(in[n]));
    }
    return out;
  }

  // A ramp falling from 1 towards 0 over 128 samples, wrapping 0 -> 1.
  std::vector<float> Falling(const std::vector<float>& rising) {
    std::vector<float> falling;
    falling.reserve(rising.size());
    for (unsigned int n = 0; n < rising.size(); ++n)
      falling.push_back(n % PERIOD == 0 ? 0.f : 1.f - rising[n]);
    return falling;
  }

  double FallingX(unsigned int n) {
    return n % PERIOD == 0 ? 0.0 : 1.0 - static_cast<double>(n % PERIOD) / PERIOD;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("phasorTwist: defaults and setters (#1077)") {
    phasorTwist fn;
    CHECK(fn.curve() == 0.f);
    CHECK(fn.shapeMode());
    CHECK_FALSE(fn.syncUpdate());
    fn.curve(0.4f).shapeMode(false).syncUpdate(true);
    CHECK(fn.curve() == 0.4f);
    CHECK_FALSE(fn.shapeMode());
    CHECK(fn.syncUpdate());
    // Clamped to -1 .. 1.
    fn.curve(3.f);
    CHECK(fn.curve() == 1.f);
    fn.curve(-1.5f);
    CHECK(fn.curve() == -1.f);
    // Non-finite values are ignored.
    fn.curve(-0.3f);
    fn.curve(std::numeric_limits<float>::quiet_NaN());
    fn.curve(std::numeric_limits<float>::infinity());
    fn.curve(-std::numeric_limits<float>::infinity());
    CHECK(fn.curve() == -0.3f);
  }

  TEST_CASE("phasorTwist: the curve against the closed form (#1077)") {
    const std::vector<float> in = Ramp(3 * PERIOD);

    // The steepness mapping at the points the header names.
    CHECK(Steepness(0.1) == doctest::Approx(0.62).epsilon(0.02));
    CHECK(Steepness(0.5) == doctest::Approx(2.8).epsilon(0.02));
    CHECK(Steepness(1.0) == doctest::Approx(50.0).epsilon(0.02));

    for (double c : {-1.0, -0.7, -0.3, -0.05, 0.0, 0.05, 0.3, 0.7, 1.0}) {
      CAPTURE(c);
      phasorTwist fn;
      fn.curve(static_cast<float>(c));
      const std::vector<float> out = Process(fn, in);
      bool close = true;
      bool inRange = true;
      bool sides = true;
      for (unsigned int n = 0; n < out.size(); ++n) {
        const double x = static_cast<double>(n % PERIOD) / PERIOD;
        if (std::fabs(out[n] - Expected(n, c)) > 1e-6) close = false;
        if (out[n] < 0.f || out[n] >= 1.f) inRange = false;
        // Above 0 below the linear input, below 0 above it.
        if (x > 0.0 && c > 0.0 && !(out[n] < x)) sides = false;
        if (x > 0.0 && c < 0.0 && !(out[n] > x)) sides = false;
        if (c == 0.0 && out[n] != in[n]) sides = false;
      }
      CHECK(close);
      CHECK(inRange);
      CHECK(sides);
      // Same start as the input, every cycle, and rising within it.
      CHECK(out[0] == 0.f);
      CHECK(out[PERIOD] == 0.f);
      for (unsigned int n = 1; n < PERIOD; ++n)
        CHECK(out[n] >= out[n - 1]);
    }

    // Steeper towards the ends: the midpoint of a 0.3 curve sits nearer the
    // diagonal than that of 0.7, which in turn is nearer than 1.
    std::vector<float> mid;
    for (float c : {0.3f, 0.7f, 1.f}) {
      phasorTwist fn;
      fn.curve(c);
      mid.push_back(Process(fn, in)[64]);
    }
    CHECK(mid[0] < 0.5f);
    CHECK(mid[1] < mid[0]);
    CHECK(mid[2] < mid[1]);

    // At -1 the curve rounds to 1 within the cycle; it is held below 1.
    phasorTwist steep;
    steep.curve(-1.f);
    const std::vector<float> top = Process(steep, in);
    CHECK(top[127] == 0.99999994f);
    CHECK(top[128] == 0.f);
  }

  TEST_CASE("phasorTwist: shapemode on falling ramps (#1077)") {
    const std::vector<float> falling = Falling(Ramp(2 * PERIOD));

    // On (the default): a falling ramp is bent like a rising one, so it
    // falls slowly first for a positive curve: 1 - Bent(1 - x).
    phasorTwist mirrored;
    mirrored.curve(0.6f);
    const std::vector<float> outOn = Process(mirrored, falling);
    // Off: the same function of x, the rising curve traced backwards.
    phasorTwist plain;
    plain.curve(0.6f).shapeMode(false);
    const std::vector<float> outOff = Process(plain, falling);

    // Sample 0 reads 0 and counts as rising, so it is the same either way.
    CHECK(outOn[0] == 0.f);
    CHECK(outOff[0] == 0.f);
    for (unsigned int n = 1; n < falling.size(); ++n) {
      CAPTURE(n);
      const double x = FallingX(n);
      CHECK(outOn[n] == doctest::Approx(1.0 - Bent(1.0 - x, 0.6)));
      CHECK(outOff[n] == doctest::Approx(Bent(x, 0.6)));
    }
    // The first falling step: on, it has barely moved; off, it drops fast.
    CHECK(1.f - outOn[1] < 1.0 / PERIOD);
    CHECK(1.f - outOff[1] > 1.0 / PERIOD);

    // Rising ramps are the same in either mode.
    const std::vector<float> rising = Ramp(PERIOD);
    phasorTwist a;
    a.curve(-0.4f);
    phasorTwist b;
    b.curve(-0.4f).shapeMode(false);
    CHECK(Process(a, rising) == Process(b, rising));

    // A new shape mode applies on the next sample.
    phasorTwist turned;
    turned.curve(0.6f);
    const std::vector<float> outTurned = Process(turned, falling, [&](unsigned int n) {
      if (n == 50) turned.shapeMode(false);
    });
    CHECK(outTurned[49] == doctest::Approx(1.0 - Bent(1.0 - FallingX(49), 0.6)));
    CHECK(outTurned[50] == doctest::Approx(Bent(FallingX(50), 0.6)));
  }

  TEST_CASE("phasorTwist: syncupdate off lands a new curve on the next sample (#1077)") {
    const std::vector<float> in = Ramp(2 * PERIOD);
    phasorTwist fn;
    const std::vector<float> out = Process(fn, in, [&](unsigned int n) {
      if (n == 40) fn.curve(0.5f);
      if (n == 150) fn.curve(-0.8f);
    });
    for (unsigned int n = 0; n < out.size(); ++n) {
      CAPTURE(n);
      const double c = n < 40 ? 0.0 : (n < 150 ? 0.5 : -0.8);
      CHECK(out[n] == doctest::Approx(Expected(n, c)));
    }
  }

  TEST_CASE("phasorTwist: syncupdate on waits for the ramp to wrap or turn (#1077)") {
    const std::vector<float> in = Ramp(4 * PERIOD);

    // Set before the first sample: applies from it.
    {
      phasorTwist fn;
      fn.syncUpdate(true).curve(0.75f);
      const std::vector<float> out = Process(fn, in);
      for (unsigned int n = 0; n < PERIOD; ++n)
        CHECK(out[n] == doctest::Approx(Expected(n, 0.75)));
    }

    // Changes mid-ramp wait for the wrap; turning sync off lands at once.
    phasorTwist fn;
    fn.syncUpdate(true);
    const std::vector<float> out = Process(fn, in, [&](unsigned int n) {
      if (n == 20) fn.curve(0.5f);
      if (n == 90) fn.curve(-0.5f);
      if (n == 128 + 100) fn.curve(0.9f);
      if (n == 3 * 128 + 5) fn.curve(0.2f);
      if (n == 3 * 128 + 10) fn.syncUpdate(false);
    });
    bool smooth = true;
    for (unsigned int n = 0; n < out.size(); ++n) {
      CAPTURE(n);
      double c = 0.0;
      if (n >= 128) c = -0.5; // the last curve set when the input wrapped
      if (n >= 256) c = 0.9;
      if (n >= 3 * 128 + 10) c = 0.2;
      CHECK(out[n] == doctest::Approx(Expected(n, c)));
      // Up to the sync change, every ramp rises without a jump back.
      if (n > 0 && n < 3 * 128 + 10 && n % PERIOD != 0 && out[n] < out[n - 1]) smooth = false;
    }
    CHECK(smooth);

    // A change of direction is a new ramp too: rise for 64 samples, then
    // fall; a curve set while rising lands where the input turns.
    std::vector<float> turn;
    for (unsigned int n = 0; n <= 64; ++n)
      turn.push_back(static_cast<float>(n) / PERIOD);
    for (unsigned int n = 1; n < 64; ++n)
      turn.push_back(static_cast<float>(64 - n) / PERIOD);
    phasorTwist turning;
    turning.syncUpdate(true);
    const std::vector<float> outTurn = Process(turning, turn, [&](unsigned int n) {
      if (n == 30) turning.curve(0.8f);
    });
    for (unsigned int n = 0; n <= 64; ++n)
      CHECK(outTurn[n] == doctest::Approx(turn[n]));
    for (unsigned int n = 65; n < turn.size(); ++n) {
      CAPTURE(n);
      CHECK(outTurn[n] == doctest::Approx(1.0 - Bent(1.0 - turn[n], 0.8)));
    }

    // A falling ramp lands a waiting curve where it wraps backwards (0 -> 1).
    const std::vector<float> falling = Falling(Ramp(2 * PERIOD));
    phasorTwist down;
    down.syncUpdate(true).curve(0.2f);
    const std::vector<float> outDown = Process(down, falling, [&](unsigned int n) {
      if (n == 60) down.curve(-0.6f);
    });
    for (unsigned int n = 1; n < outDown.size(); ++n) {
      CAPTURE(n);
      const double c = n < 129 ? 0.2 : -0.6;
      CHECK(outDown[n] == doctest::Approx(1.0 - Bent(1.0 - FallingX(n), c)));
    }
  }

  TEST_CASE("phasorTwist: out-of-range and non-finite input (#1077)") {
    const std::vector<float> rising = Ramp(3 * PERIOD);

    // Out of range: wrapped into 0..1.
    std::vector<float> shifted;
    shifted.reserve(rising.size());
    for (unsigned int n = 0; n < rising.size(); ++n)
      shifted.push_back(rising[n] + static_cast<float>(n % 3) * 2.f - 2.f);
    phasorTwist wrapped;
    wrapped.curve(0.6f);
    const std::vector<float> outWrapped = Process(wrapped, shifted);
    for (unsigned int n = 0; n < outWrapped.size(); ++n) {
      CAPTURE(n);
      CHECK(outWrapped[n] == doctest::Approx(Expected(n, 0.6)).epsilon(1e-4));
    }

    // Not a finite number: the previous input (0 before any).
    std::vector<float> holes(rising);
    holes[0] = std::numeric_limits<float>::quiet_NaN();
    holes[70] = std::numeric_limits<float>::infinity();
    holes[71] = -std::numeric_limits<float>::infinity();
    holes[72] = std::numeric_limits<float>::quiet_NaN();
    phasorTwist holed;
    holed.curve(-0.5f);
    const std::vector<float> out = Process(holed, holes);
    CHECK(out[0] == 0.f);
    CHECK(out[70] == out[69]);
    CHECK(out[72] == out[69]);
    CHECK(out[73] == doctest::Approx(Expected(73, -0.5)));
    for (float y : out)
      CHECK(std::isfinite(y));
  }

  TEST_CASE("phasorTwist: blocks match samples; nothing depends on the sample rate (#1077)") {
    const std::vector<float> in = Ramp(2 * N);
    phasorTwist samples;
    samples.curve(0.7f);
    const std::vector<float> one = Process(samples, in);

    phasorTwist blocks;
    blocks.curve(0.7f);
    buffer chunk(N);
    std::vector<float> two;
    for (unsigned int b = 0; b < 2; ++b) {
      for (unsigned int i = 0; i < N; ++i)
        chunk.getPtr()[i] = in[b * N + i];
      buffer& out = blocks(chunk);
      REQUIRE(out.getLength() == N);
      for (unsigned int i = 0; i < N; ++i)
        two.push_back(out.getPtr()[i]);
    }
    CHECK(one == two);

    TestHelpers::ScopedSampleRate rate(48000);
    phasorTwist a;
    a.curve(-0.3f);
    const std::vector<float> at48 = Process(a, in);
    YSE::SAMPLERATE = 96000;
    phasorTwist b;
    b.curve(-0.3f);
    const bool same = Process(b, in) == at48;
    CHECK(same);
  }

  TEST_CASE("phasorTwist: processing and setters allocate nothing (#1077)") {
    phasorTwist fn;
    buffer in(N);
    in = 0.25f;
    fn(in);
    TestHelpers::ProbeScope probe;
    fn.curve(0.7f).shapeMode(false).syncUpdate(true);
    fn(in);
    (void)fn.process(0.5f);
    CHECK(TestHelpers::g_alloc_count.load() == 0);
  }

} // TEST_SUITE("dsp")
