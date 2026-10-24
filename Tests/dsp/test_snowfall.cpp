// Tests for YSE::DSP::snowfall (YseEngine/dsp/generators/snowfall.hpp), the
// class behind ~snowfall (issue #1085). Speeds are picked so a step is a
// round number of units per sample: at 48 kHz, direction 480 moves 0.01 a
// sample. A DSP::phasor at 375 Hz is exactly 128 samples per cycle (an
// increment of 2^-7), so its resets land on 0 exactly.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/generators/snowfall.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::snowfall;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  using outputs = std::vector<std::vector<float>>;

  // Runs `fn` over `in` in blocks of `block` samples; one vector per output.
  outputs Run(snowfall& fn, const std::vector<float>& in, unsigned int block = N) {
    outputs out(fn.dimensions());
    for (std::size_t start = 0; start < in.size(); start += block) {
      const unsigned int length =
          static_cast<unsigned int>(std::min<std::size_t>(block, in.size() - start));
      buffer b(length);
      for (unsigned int i = 0; i < length; ++i)
        b.getPtr()[i] = in[start + i];
      buffer& first = fn(b);
      REQUIRE(first.getLength() == length);
      for (unsigned int d = 0; d < fn.dimensions(); ++d) {
        REQUIRE(fn.output(d).getLength() == length);
        for (unsigned int i = 0; i < length; ++i)
          out[d].push_back(fn.output(d).getPtr()[i]);
      }
    }
    return out;
  }

  std::vector<float> Constant(unsigned int samples, float value) {
    return std::vector<float>(samples, value);
  }

  std::vector<float> Ramp(unsigned int samples, float hz) {
    phasor source;
    std::vector<float> result;
    while (result.size() < samples) {
      buffer& out = source(hz, N);
      for (unsigned int i = 0; i < N && result.size() < samples; ++i)
        result.push_back(out.getPtr()[i]);
    }
    return result;
  }

  // 1 - |p mod 2 - 1|: a value bouncing between 0 and 1.
  double Triangle(double p) {
    double m = std::fmod(p, 2.0);
    if (m < 0.0) m += 2.0;
    return 1.0 - std::fabs(m - 1.0);
  }

  // A wide range and a scale that outputs the value as it is.
  void Unbounded(snowfall& fn, unsigned int d) {
    fn.minimum(d, -1000.f).maximum(d, 1000.f).scaleMinimum(d, -1000.f).scaleMaximum(d, 1000.f);
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("snowfall: defaults, and no particle before the ramp starts (#1085)") {
    TestHelpers::ScopedSampleRate rate(48000);
    snowfall fn;
    CHECK(fn.dimensions() == 1);
    CHECK_FALSE(fn.alive());
    const outputs out = Run(fn, Constant(N, 0.f));
    for (float v : out[0])
      CHECK(v == 0.f);
    CHECK_FALSE(fn.alive());

    // Default direction 1: a second to cross 0..1 at constant speed.
    const outputs moving = Run(fn, Constant(N, 0.5f));
    CHECK(fn.alive());
    for (unsigned int n = 0; n < N; ++n)
      CHECK(moving[0][n] == doctest::Approx((n + 1) / 48000.0).epsilon(1e-6));
  }

  TEST_CASE("snowfall: a constant ramp moves each dimension at its own speed (#1085)") {
    TestHelpers::ScopedSampleRate rate(48000);
    snowfall fn(4);
    const float speeds[] = {480.f, -240.f, 0.f, 4800.f};
    for (unsigned int d = 0; d < 4; ++d) {
      Unbounded(fn, d);
      fn.direction(d, speeds[d]).initial(d, static_cast<float>(d));
    }
    const outputs out = Run(fn, Constant(3 * N, 0.25f));
    for (unsigned int d = 0; d < 4; ++d) {
      CAPTURE(d);
      for (unsigned int n = 0; n < 3 * N; n += 7)
        CHECK(out[d][n] == doctest::Approx(d + speeds[d] * (n + 1) / 48000.0).epsilon(1e-6));
      CHECK(fn.velocity(d) == speeds[d]);
    }
  }

  TEST_CASE("snowfall: energyramp speeds the motion up with the ramp (#1085)") {
    TestHelpers::ScopedSampleRate rate(48000);
    snowfall fn;
    Unbounded(fn, 0);
    fn.direction(0, 480.f).energyRamp(0, 2.f);
    std::vector<float> in(256);
    for (unsigned int n = 0; n < 256; ++n)
      in[n] = (static_cast<float>(n) + 0.5f) / 256.f;
    const outputs out = Run(fn, in);
    double x = 0.0;
    for (unsigned int n = 0; n < 256; ++n) {
      x += 0.01 * (1.0 + 2.0 * in[n]);
      CHECK(out[0][n] == doctest::Approx(x).epsilon(1e-6));
    }
  }

  TEST_CASE("snowfall: a phasor starts a particle each cycle and its reset ends it (#1085)") {
    TestHelpers::ScopedSampleRate rate(48000);
    snowfall fn;
    Unbounded(fn, 0);
    fn.direction(0, 480.f).initial(0, 0.5f).endValue(0, -1.f);
    const outputs out = Run(fn, Ramp(8 * 128, 375.f));
    // The ramp reads 0 exactly at 128k: no particle there (the end value),
    // one from 128k + 1 on, from its initial value.
    for (unsigned int n = 0; n < 8 * 128; ++n) {
      CAPTURE(n);
      const unsigned int j = n % 128;
      if (j == 0)
        CHECK(out[0][n] == -1.f);
      else
        CHECK(out[0][n] == doctest::Approx(0.5 + 0.01 * j).epsilon(1e-6));
    }

    // A ramp that wraps without touching 0: one particle ends and the next
    // starts in the same sample.
    snowfall wraps;
    Unbounded(wraps, 0);
    wraps.direction(0, 480.f);
    std::vector<float> in(4 * 100);
    for (unsigned int n = 0; n < in.size(); ++n)
      in[n] = (static_cast<float>(n % 100) + 0.5f) / 100.f;
    const outputs w = Run(wraps, in);
    for (unsigned int n = 0; n < in.size(); ++n)
      CHECK(w[0][n] == doctest::Approx(0.01 * (n % 100 + 1)).epsilon(1e-6));

    // A ramp falling to 0 ends the particle; non-finite samples count as the
    // one before.
    snowfall gate;
    Unbounded(gate, 0);
    gate.direction(0, 480.f);
    std::vector<float> g(40, 0.5f);
    g[5] = std::numeric_limits<float>::quiet_NaN();
    g[6] = std::numeric_limits<float>::infinity();
    for (unsigned int n = 20; n < 40; ++n)
      g[n] = 0.f;
    const outputs o = Run(gate, g);
    for (unsigned int n = 0; n < 20; ++n)
      CHECK(o[0][n] == doctest::Approx(0.01 * (n + 1)).epsilon(1e-6));
    for (unsigned int n = 20; n < 40; ++n)
      CHECK(o[0][n] == 0.f);
    CHECK_FALSE(gate.alive());
  }

  TEST_CASE("snowfall: the boundary modes bounce, end, wrap and clip (#1085)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const unsigned int samples = 400;
    const auto make = [](snowfall::BOUNDARY_MODE mode) {
      snowfall fn;
      fn.direction(0, 480.f).initial(0, 0.955f).boundaryMode(mode).endValue(0, 0.25f);
      return fn;
    };

    snowfall bounce = make(snowfall::BOUNCE);
    const outputs b = Run(bounce, Constant(samples, 0.5f));
    for (unsigned int n = 0; n < samples; ++n) {
      CAPTURE(n);
      CHECK(b[0][n] == doctest::Approx(Triangle(0.955 + 0.01 * (n + 1))).epsilon(1e-5));
    }

    snowfall wrap = make(snowfall::WRAP);
    const outputs w = Run(wrap, Constant(samples, 0.5f));
    for (unsigned int n = 0; n < samples; ++n) {
      CAPTURE(n);
      const double p = std::fmod(0.955 + 0.01 * (n + 1), 1.0);
      // Within a rounding of the seam, either side of it.
      CHECK(std::min(std::fabs(w[0][n] - p), 1.0 - std::fabs(w[0][n] - p)) < 1e-5);
    }

    snowfall clip = make(snowfall::CLIP);
    const outputs c = Run(clip, Constant(samples, 0.5f));
    for (unsigned int n = 0; n < samples; ++n)
      CHECK(c[0][n] == doctest::Approx(std::min(1.0, 0.955 + 0.01 * (n + 1))).epsilon(1e-6));
    CHECK(clip.alive());

    // END: the boundary in the sample it is reached, then the end value.
    snowfall end = make(snowfall::END);
    const outputs e = Run(end, Constant(samples, 0.5f));
    for (unsigned int n = 0; n < 4; ++n)
      CHECK(e[0][n] == doctest::Approx(0.955 + 0.01 * (n + 1)).epsilon(1e-6));
    CHECK(e[0][4] == doctest::Approx(1.0));
    for (unsigned int n = 5; n < samples; ++n)
      CHECK(e[0][n] == 0.25f);
    CHECK_FALSE(end.alive());

    // With endmode CURRENT_VALUE it holds the boundary.
    snowfall hold = make(snowfall::END);
    hold.endMode(snowfall::CURRENT_VALUE);
    const outputs h = Run(hold, Constant(samples, 0.5f));
    for (unsigned int n = 4; n < samples; ++n)
      CHECK(h[0][n] == doctest::Approx(1.0));

    // The lower boundary too, going down.
    snowfall down;
    down.direction(0, -480.f).initial(0, 0.05f);
    const outputs d = Run(down, Constant(samples, 0.5f));
    for (unsigned int n = 0; n < samples; ++n)
      CHECK(d[0][n] == doctest::Approx(Triangle(0.05 - 0.01 * (n + 1))).epsilon(1e-5));
  }

  TEST_CASE("snowfall: squish scales the velocity at each bounce (#1085)") {
    TestHelpers::ScopedSampleRate rate(48000);
    snowfall fn;
    fn.direction(0, 480.f).initial(0, 0.975f).squish(0, 0.5f).interval(0.f);
    const outputs out = Run(fn, Constant(8, 0.5f));
    // 0.985, 0.995, then 1.005 reflects to 0.995 at half the speed.
    CHECK(out[0][0] == doctest::Approx(0.985));
    CHECK(out[0][1] == doctest::Approx(0.995));
    CHECK(out[0][2] == doctest::Approx(0.995));
    CHECK(fn.velocity(0) == doctest::Approx(-240.0));
    CHECK(out[0][3] == doctest::Approx(0.99));
    CHECK(out[0][4] == doctest::Approx(0.985));
  }

  TEST_CASE("snowfall: updates reverse with wanderprob and keep the bounces (#1085)") {
    // 64 kHz: 1 ms is 64 samples and a sample is 1/64 ms exactly.
    TestHelpers::ScopedSampleRate rate(64000);
    snowfall fn;
    Unbounded(fn, 0);
    fn.direction(0, 640.f).interval(1.f).wanderProbability(0, 1.f);
    const outputs out = Run(fn, Constant(6 * 64, 0.5f));
    double x = 0.0;
    for (unsigned int n = 0; n < 6 * 64; ++n) {
      x += ((n / 64) % 2 == 0) ? 0.01 : -0.01;
      CAPTURE(n);
      CHECK(out[0][n] == doctest::Approx(x).epsilon(1e-6));
    }

    // Without wander an update draws the same velocity, so a bounce's
    // reversal holds through the updates that follow.
    snowfall keep;
    keep.direction(0, 640.f).initial(0, 0.9f).interval(1.f);
    const outputs k = Run(keep, Constant(64, 0.5f));
    for (unsigned int n = 0; n < 64; ++n)
      CHECK(k[0][n] == doctest::Approx(Triangle(0.9 + 0.01 * (n + 1))).epsilon(1e-5));
    CHECK(keep.velocity(0) == doctest::Approx(-640.0));

    // wanderprob is clamped to 0..1.
    snowfall clamped;
    Unbounded(clamped, 0);
    clamped.direction(0, 640.f).interval(1.f).wanderProbability(0, 7.f);
    const outputs c = Run(clamped, Constant(6 * 64, 0.5f));
    CHECK(c[0] == out[0]);
  }

  TEST_CASE("snowfall: deviations draw within their range and a seed repeats them (#1085)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto make = [](std::uint32_t seed) {
      snowfall fn(2);
      for (unsigned int d = 0; d < 2; ++d) {
        Unbounded(fn, d);
        fn.initial(d, 10.f).initialDeviation(d, 2.f).direction(d, 480.f).directionDeviation(d,
                                                                                            240.f);
      }
      fn.interval(2.f).intervalDeviation(1.f).wanderProbability(0, 0.5f).seed(seed);
      return fn;
    };
    // Restarted every 100 samples.
    std::vector<float> in(20 * 100);
    for (unsigned int n = 0; n < in.size(); ++n)
      in[n] = (static_cast<float>(n % 100) + 0.5f) / 100.f;

    snowfall a = make(5);
    const outputs ra = Run(a, in);
    snowfall b = make(5);
    const outputs rb = Run(b, in, 17);
    snowfall c = make(6);
    const outputs rc = Run(c, in);
    CHECK(ra == rb);
    CHECK(ra != rc);

    // Each particle's first output is its start plus one step: within the
    // deviations, and not all the same.
    std::vector<float> firsts;
    for (unsigned int k = 0; k < 20; ++k) {
      for (unsigned int d = 0; d < 2; ++d) {
        const float first = ra[d][k * 100];
        CHECK(first >= 8.f - 0.015f);
        CHECK(first <= 12.f + 0.015f);
        firsts.push_back(first);
      }
    }
    std::sort(firsts.begin(), firsts.end());
    CHECK(firsts.front() < firsts.back() - 0.5f);

    // The speed between updates stays within direction +- directiondev.
    for (unsigned int n = 1; n < in.size(); ++n) {
      if (n % 100 == 0) continue;
      const double speed = std::fabs(ra[1][n] - ra[1][n - 1]) * 48000.0;
      CHECK(speed >= 240.0 - 1.0);
      CHECK(speed <= 720.0 + 1.0);
    }
  }

  TEST_CASE("snowfall: startmode, endmode and the output scale (#1085)") {
    TestHelpers::ScopedSampleRate rate(48000);
    // Ten samples on, ten off, ten on.
    std::vector<float> in(30, 0.5f);
    for (unsigned int n = 10; n < 20; ++n)
      in[n] = 0.f;

    snowfall fresh;
    Unbounded(fresh, 0);
    fresh.direction(0, 480.f).initial(0, 1.f).endValue(0, -5.f);
    const outputs f = Run(fresh, in);
    CHECK(f[0][9] == doctest::Approx(1.1));
    CHECK(f[0][15] == -5.f);
    CHECK(f[0][20] == doctest::Approx(1.01));

    snowfall current;
    Unbounded(current, 0);
    current.direction(0, 480.f)
        .initial(0, 1.f)
        .startMode(snowfall::CURRENT)
        .endMode(snowfall::CURRENT_VALUE);
    const outputs c = Run(current, in);
    CHECK(c[0][15] == doctest::Approx(1.1));
    CHECK(c[0][20] == doctest::Approx(1.11));
    CHECK(c[0][29] == doctest::Approx(1.2));

    // scalemin / scalemax map min..max; equal boundaries give scalemin.
    snowfall scaled;
    scaled.direction(0, 480.f).initial(0, 0.5f).scaleMinimum(0, -1.f).scaleMaximum(0, 1.f);
    const outputs s = Run(scaled, Constant(10, 0.5f));
    for (unsigned int n = 0; n < 10; ++n)
      CHECK(s[0][n] == doctest::Approx(2.0 * (0.5 + 0.01 * (n + 1)) - 1.0));

    snowfall reversed;
    reversed.minimum(0, 2.f).maximum(0, 0.f).initial(0, 1.f).direction(0, 480.f);
    const outputs r = Run(reversed, Constant(4, 0.5f));
    // min 2 max 0: value 1.01 sits at (1.01 - 2) / (0 - 2) of the way.
    CHECK(r[0][0] == doctest::Approx((1.01 - 2.0) / -2.0));

    snowfall flat;
    flat.minimum(0, 3.f).maximum(0, 3.f).scaleMinimum(0, 0.7f).initial(0, 1.f);
    const outputs fl = Run(flat, Constant(4, 0.5f));
    for (float v : fl[0])
      CHECK(v == 0.7f);
    CHECK(flat.value(0) == 3.f);

    // The initial value is kept within the boundaries.
    snowfall outside;
    outside.initial(0, 5.f).direction(0, 0.f);
    const outputs o = Run(outside, Constant(4, 0.5f));
    CHECK(o[0][0] == 1.f);
  }

  TEST_CASE("snowfall: dimensions, invalid settings and outputs (#1085)") {
    CHECK(snowfall(0).dimensions() == 1);
    CHECK(snowfall(9).dimensions() == 4);
    snowfall fn(2);
    fn.dimensions(3);
    CHECK(fn.dimensions() == 3);
    fn.dimensions(0);
    CHECK(fn.dimensions() == 1);
    CHECK(&fn.output(3) == &fn.output(0));
    CHECK(fn.value(2) == 0.f);
    CHECK(fn.velocity(2) == 0.f);

    TestHelpers::ScopedSampleRate rate(48000);
    snowfall bad;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    bad.direction(0, nan)
        .direction(7, 3.f)
        .interval(nan)
        .intervalDeviation(-1.f)
        .boundaryMode(static_cast<snowfall::BOUNDARY_MODE>(9))
        .endMode(static_cast<snowfall::END_MODE>(4))
        .startMode(static_cast<snowfall::START_MODE>(-1))
        .wanderProbability(0, nan);
    snowfall good;
    CHECK(Run(bad, Constant(300, 0.5f)) == Run(good, Constant(300, 0.5f)));
  }

  TEST_CASE("snowfall: speeds and intervals follow the live sample rate (#1085)") {
    for (unsigned int sr : {32000u, 64000u}) {
      CAPTURE(sr);
      TestHelpers::ScopedSampleRate rate(sr);
      snowfall fn;
      Unbounded(fn, 0);
      fn.direction(0, 0.5f).interval(250.f).wanderProbability(0, 1.f);
      const outputs out = Run(fn, Constant(sr, 0.5f));
      // Reversed at 250, 500 and 750 ms: up, down, up, down a quarter of
      // 0.5 each, so back to 0 after a second.
      CHECK(out[0][sr / 4 - 1] == doctest::Approx(0.125).epsilon(1e-4));
      CHECK(out[0][sr / 2 - 1] == doctest::Approx(0.0).epsilon(1e-4));
      CHECK(out[0][sr - 1] == doctest::Approx(0.0).epsilon(1e-4));
    }

    // A change between blocks takes effect in the next.
    snowfall fn;
    Unbounded(fn, 0);
    fn.direction(0, 480.f);
    {
      TestHelpers::ScopedSampleRate rate(48000);
      Run(fn, Constant(N, 0.5f));
    }
    const float before = fn.value(0);
    TestHelpers::ScopedSampleRate rate(96000);
    Run(fn, Constant(N, 0.5f));
    CHECK(fn.value(0) == doctest::Approx(before + 0.005 * N).epsilon(1e-6));
  }

  TEST_CASE("snowfall: blocks match samples, and are clamped to the buffer size (#1085)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto make = []() {
      snowfall fn(3);
      for (unsigned int d = 0; d < 3; ++d)
        fn.direction(d, 100.f * (d + 1)).directionDeviation(d, 50.f).wanderProbability(d, 0.3f);
      fn.interval(3.f).intervalDeviation(2.f).boundaryMode(snowfall::BOUNCE).squish(1, 0.9f);
      return fn;
    };
    const std::vector<float> in = Ramp(4 * N, 93.75f);
    snowfall whole = make();
    snowfall pieces = make();
    CHECK(Run(whole, in) == Run(pieces, in, 33));

    snowfall single = make();
    outputs bySample(3);
    float values[snowfall::MAX_DIMENSIONS] = {};
    for (float x : in) {
      single.process(x, values);
      for (unsigned int d = 0; d < 3; ++d)
        bySample[d].push_back(values[d]);
    }
    snowfall again = make();
    CHECK(Run(again, in) == bySample);

    snowfall big;
    buffer large(2 * N);
    CHECK(big(large).getLength() == N);
  }

  TEST_CASE("snowfall: processing allocates nothing (#1085)") {
    TestHelpers::ScopedSampleRate rate(48000);
    snowfall fn(4);
    buffer in(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = static_cast<float>(i % 32) / 32.f;
    fn(in);
    {
      TestHelpers::ProbeScope probe;
      for (int mode = snowfall::BOUNCE; mode <= snowfall::CLIP; ++mode) {
        fn.boundaryMode(static_cast<snowfall::BOUNDARY_MODE>(mode))
            .interval(0.1f)
            .intervalDeviation(0.05f)
            .wanderProbability(2, 0.5f)
            .directionDeviation(1, 3.f)
            .endMode(snowfall::CURRENT_VALUE)
            .startMode(snowfall::CURRENT)
            .dimensions(static_cast<UInt>(mode + 1));
        fn(in);
        for (UInt d = 0; d < fn.dimensions(); ++d)
          (void)fn.output(d);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
