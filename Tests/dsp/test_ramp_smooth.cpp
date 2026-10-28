// Tests for YSE::DSP::rampSmooth (YseEngine/dsp/filters/rampSmooth.hpp), the
// linear smoothing behind ~rampsmooth (issue #1100): pass-through at the
// defaults, a step up and down as straight lines of their own lengths that
// land on the input exactly, a ramp restarted from where the output is,
// whole-sample lengths, lengths below 1 and non-finite settings, a length that
// applies to the next ramp, the one-pole behaviour on a signal that changes
// every sample, the per-sample length signals, non-finite input, reset(),
// independence from the sample rate and the block size, and allocation
// freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>
#include "dsp/filters/rampSmooth.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::rampSmooth;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // Runs `fn` over `in` in blocks of `block` samples.
  std::vector<float> Run(rampSmooth& fn, const std::vector<float>& in, unsigned int block = N) {
    std::vector<float> out;
    for (std::size_t start = 0; start < in.size(); start += block) {
      const auto length =
          static_cast<unsigned int>(std::min<std::size_t>(block, in.size() - start));
      buffer b(length);
      for (unsigned int i = 0; i < length; ++i)
        b.getPtr()[i] = in[start + i];
      buffer& result = fn(b);
      REQUIRE(result.getLength() == length);
      for (unsigned int i = 0; i < length; ++i)
        out.push_back(result.getPtr()[i]);
    }
    return out;
  }

  std::vector<float> Noise(std::size_t samples, unsigned int seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-1.f, 1.f);
    std::vector<float> v(samples);
    for (float& s : v)
      s = dist(rng);
    return v;
  }

  // An independent reference: Max's description, written out with the
  // ramp's start, end and length kept explicitly, so it shares no state
  // layout with the class.
  struct Reference {
    float up = 1.f;
    float down = 1.f;
    double from = 0.0;
    double to = 0.0;
    double length = 1.0;
    double position = 1.0; // samples into the ramp; at `length` it is done
    float lastInput = 0.f;

    static double Whole(float samples) {
      return samples >= 1.f ? std::floor(static_cast<double>(samples)) : 1.0;
    }

    double Value() const {
      return position >= length ? to : from + (to - from) * position / length;
    }

    float Process(float x) {
      if (std::isfinite(x) && x != lastInput) {
        lastInput = x;
        from = Value();
        to = x;
        length = Whole(to > from ? up : down);
        position = 0.0;
      }
      if (position < length) position += 1.0;
      return static_cast<float>(Value());
    }
  };
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("rampSmooth: the defaults pass the input through (#1100)") {
    rampSmooth fn;
    CHECK(fn.up() == 1.f);
    CHECK(fn.down() == 1.f);
    const std::vector<float> in = Noise(3 * N, 1);
    CHECK(Run(fn, in) == in);
  }

  TEST_CASE("rampSmooth: a step up and down is a straight line of its own length that lands "
            "on the input (#1100)") {
    rampSmooth fn;
    fn.up(10.f).down(100.f);

    // Rising from 0 to 1 over 10 samples: 0.1, 0.2, ... 1.0, then 1.
    for (int n = 0; n < 10; ++n) {
      CAPTURE(n);
      CHECK(fn.process(1.f) == doctest::Approx((n + 1) / 10.0).epsilon(1e-6));
    }
    CHECK(fn.value() == 1.f);
    for (int n = 0; n < 50; ++n)
      CHECK(fn.process(1.f) == 1.f);

    // Falling from 1 to -1 over 100 samples, ten times slower.
    for (int n = 0; n < 100; ++n) {
      CAPTURE(n);
      const double expected = 1.0 - 2.0 * (n + 1) / 100.0;
      const float y = fn.process(-1.f);
      CHECK(std::fabs(y - expected) <= 1e-6);
    }
    CHECK(fn.value() == -1.f);
    CHECK(fn.process(-1.f) == -1.f);

    // An odd step lands exactly too: no rounding is left over.
    fn.up(3.f);
    float y = 0.f;
    for (int n = 0; n < 3; ++n)
      y = fn.process(0.7f);
    CHECK(y == 0.7f);
  }

  TEST_CASE("rampSmooth: a change during a ramp restarts it from where the output is "
            "(#1100)") {
    rampSmooth fn;
    fn.ramp(4.f);
    CHECK(fn.process(1.f) == 0.25f);
    CHECK(fn.process(1.f) == 0.5f);
    // A new value above: four samples from 0.5 to 2.5.
    CHECK(fn.process(2.5f) == 1.f);
    CHECK(fn.process(2.5f) == 1.5f);
    // A value below the output: the ramp-down length, from 1.5 to 0.
    fn.down(2.f);
    CHECK(fn.process(0.f) == 0.75f);
    CHECK(fn.process(0.f) == 0.f);
  }

  TEST_CASE("rampSmooth: lengths are whole samples; below 1 or not a number is 1; non-finite "
            "settings are ignored (#1100)") {
    CHECK(rampSmooth::Samples(10.f) == 10);
    CHECK(rampSmooth::Samples(10.9f) == 10);
    CHECK(rampSmooth::Samples(1.f) == 1);
    CHECK(rampSmooth::Samples(0.5f) == 1);
    CHECK(rampSmooth::Samples(0.f) == 1);
    CHECK(rampSmooth::Samples(-3.f) == 1);
    CHECK(rampSmooth::Samples(std::numeric_limits<float>::quiet_NaN()) == 1);
    CHECK(rampSmooth::Samples(1e30f) == 2147483648u);

    rampSmooth fn;
    fn.up(4.9f);
    CHECK(fn.up() == 4.9f); // kept as given
    CHECK(fn.process(1.f) == 0.25f); // ramps over 4

    rampSmooth zero;
    zero.up(0.f).down(-5.f); // Max's ramp 0: no smoothing
    const std::vector<float> in = Noise(N, 2);
    CHECK(Run(zero, in) == in);

    fn.up(8.f).down(16.f);
    fn.up(std::numeric_limits<float>::quiet_NaN());
    fn.down(std::numeric_limits<float>::infinity());
    fn.ramp(-std::numeric_limits<float>::infinity());
    CHECK(fn.up() == 8.f);
    CHECK(fn.down() == 16.f);
  }

  TEST_CASE("rampSmooth: a new length applies to the next ramp, not the one running (#1100)") {
    rampSmooth fn;
    fn.up(4.f);
    CHECK(fn.process(1.f) == 0.25f);
    fn.up(1.f);
    CHECK(fn.process(1.f) == 0.5f); // still the ramp over 4
    CHECK(fn.process(1.f) == 0.75f);
    CHECK(fn.process(2.f) == 2.f); // a new value: the new length
  }

  TEST_CASE("rampSmooth: on an input that changes every sample it is a one-pole lowpass "
            "(#1100)") {
    // Every sample starts a new ramp and walks 1/length of it.
    rampSmooth fn;
    fn.ramp(8.f);
    double y = 0.0;
    for (const float x : Noise(4 * N, 3)) {
      y += (static_cast<double>(x) - y) / 8.0;
      CHECK(std::fabs(fn.process(x) - y) <= 1e-6);
    }
  }

  TEST_CASE("rampSmooth: matches an independent reference on noise held for random lengths "
            "(#1100)") {
    // Reproducible test data.
    // NOLINTNEXTLINE(bugprone-random-generator-seed,cert-msc51-cpp)
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> hold(1, 40);
    std::vector<float> in;
    for (const float x : Noise(400, 8)) {
      const int n = hold(rng);
      for (int i = 0; i < n; ++i)
        in.push_back(x);
    }
    rampSmooth fn;
    fn.up(13.f).down(29.5f);
    Reference ref;
    ref.up = 13.f;
    ref.down = 29.5f;
    const std::vector<float> out = Run(fn, in);
    for (std::size_t i = 0; i < in.size(); ++i) {
      CAPTURE(i);
      CHECK(std::fabs(out[i] - ref.Process(in[i])) <= 1e-5);
    }
  }

  TEST_CASE("rampSmooth: the length signals are read per sample (#1100)") {
    rampSmooth fn;
    rampSmooth reference;
    buffer in(N);
    buffer up(N);
    buffer down(N);
    for (int i = 0; i < static_cast<int>(N); ++i) {
      // Held values so ramps both start and run.
      const int held = i / 16;
      in.getPtr()[i] = std::sin(static_cast<float>(held) * 1.3f);
      up.getPtr()[i] = 1.f + static_cast<float>(i) * 0.25f;
      down.getPtr()[i] = 2.f + static_cast<float>(i) * 0.5f;
    }
    // A non-finite value keeps the previous value for that sample.
    up.getPtr()[10] = std::numeric_limits<float>::quiet_NaN();
    down.getPtr()[20] = std::numeric_limits<float>::infinity();
    buffer& out = fn(in, up, down);
    REQUIRE(out.getLength() == N);
    Reference ref;
    for (int i = 0; i < static_cast<int>(N); ++i) {
      CAPTURE(i);
      if (std::isfinite(up.getPtr()[i])) ref.up = up.getPtr()[i];
      if (std::isfinite(down.getPtr()[i])) ref.down = down.getPtr()[i];
      CHECK(std::fabs(out.getPtr()[i] - ref.Process(in.getPtr()[i])) <= 1e-5);
      // The same as setting the values one sample at a time.
      reference.up(up.getPtr()[i]).down(down.getPtr()[i]);
      CHECK(reference.process(in.getPtr()[i]) == out.getPtr()[i]);
    }
    CHECK(fn.up() == up.getPtr()[N - 1]);
    CHECK(fn.down() == down.getPtr()[N - 1]);

    // A shorter control signal shortens the block.
    buffer shortDown(N / 2);
    shortDown = 3.f;
    CHECK(fn(in, up, shortDown).getLength() == N / 2);
  }

  TEST_CASE("rampSmooth: non-finite input is skipped, reset() returns to 0 (#1100)") {
    rampSmooth fn;
    fn.ramp(4.f);
    CHECK(fn.process(1.f) == 0.25f);
    // Skipped: the ramp carries on towards 1.
    CHECK(fn.process(std::numeric_limits<float>::quiet_NaN()) == 0.5f);
    CHECK(fn.process(std::numeric_limits<float>::infinity()) == 0.75f);
    CHECK(fn.process(-std::numeric_limits<float>::infinity()) == 1.f);
    CHECK(fn.process(1.f) == 1.f);
    fn.reset();
    CHECK(fn.value() == 0.f);
    CHECK(fn.process(0.f) == 0.f);
    CHECK(fn.process(1.f) == 0.25f);
  }

  TEST_CASE("rampSmooth: the lengths count samples, whatever the sample rate (#1100)") {
    const std::vector<float> in = Noise(2 * N, 4);
    std::vector<float> at44;
    std::vector<float> at96;
    {
      TestHelpers::ScopedSampleRate rate(44100);
      rampSmooth fn;
      fn.up(5.f).down(50.f);
      at44 = Run(fn, in);
    }
    {
      TestHelpers::ScopedSampleRate rate(96000);
      rampSmooth fn;
      fn.up(5.f).down(50.f);
      at96 = Run(fn, in);
    }
    CHECK(at44 == at96);
  }

  TEST_CASE("rampSmooth: block size does not change the output (#1100)") {
    std::vector<float> in;
    for (const float x : Noise(4 * N + 17, 5))
      for (int i = 0; i < 9; ++i)
        in.push_back(x);
    rampSmooth a;
    rampSmooth b;
    a.up(6.f).down(30.f);
    b.up(6.f).down(30.f);
    CHECK(Run(a, in, N) == Run(b, in, 7));
  }

  TEST_CASE("rampSmooth: processing allocates nothing (#1100)") {
    rampSmooth fn;
    buffer in(N);
    buffer shorter(N / 2);
    buffer up(N);
    buffer down(N);
    in = 0.5f;
    shorter = 0.25f;
    up = 3.f;
    down = 9.f;
    fn(in);
    {
      TestHelpers::ProbeScope probe;
      fn.up(12.f).down(3.f).ramp(5.f);
      fn(in);
      fn(shorter);
      fn(in, up, down);
      (void)fn.process(0.1f);
      fn.reset();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
