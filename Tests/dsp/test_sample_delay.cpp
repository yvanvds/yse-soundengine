// Tests for YSE::DSP::sampleDelay (YseEngine/dsp/delay/sampleDelay.hpp), the
// sample-accurate delay behind ~delay (issue #1104): an impulse comes out
// exactly the delay later, from 0 samples (no delay) to the maximum and
// below one block, whatever the block size; a sine against its closed form;
// fractions dropped, clamping and non-finite settings; milliseconds that
// follow the sample rate; the linear crossfade against its closed form, and
// a change during one; the interpolated signal delay; maximumDelay, clear and
// non-finite input; and allocation freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/delay/sampleDelay.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::sampleDelay;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // Runs `fn` over `in` in blocks of `block` samples, with an optional delay
  // signal of the same length.
  std::vector<float> Run(sampleDelay& fn, const std::vector<float>& in, unsigned int block = N,
                         const std::vector<float>* delays = nullptr) {
    std::vector<float> out;
    for (std::size_t start = 0; start < in.size(); start += block) {
      const auto length =
          static_cast<unsigned int>(std::min<std::size_t>(block, in.size() - start));
      buffer b(length);
      buffer d(length);
      for (unsigned int i = 0; i < length; ++i) {
        b.getPtr()[i] = in[start + i];
        if (delays != nullptr) d.getPtr()[i] = (*delays)[start + i];
      }
      buffer& result = delays != nullptr ? fn(b, d) : fn(b);
      REQUIRE(result.getLength() == length);
      for (unsigned int i = 0; i < length; ++i)
        out.push_back(result.getPtr()[i]);
    }
    return out;
  }

  std::vector<float> Impulse(std::size_t samples, std::size_t at = 0) {
    std::vector<float> v(samples, 0.f);
    v[at] = 1.f;
    return v;
  }

  // x(n) = n + 1: linear, so a linear crossfade or interpolation of it has a
  // closed form, and no two samples are equal.
  std::vector<float> Ramp(std::size_t samples) {
    std::vector<float> v(samples);
    for (std::size_t i = 0; i < samples; ++i)
      v[i] = static_cast<float>(i + 1);
    return v;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("sampleDelay: the defaults pass the input through (#1104)") {
    sampleDelay fn;
    CHECK(fn.memory() == sampleDelay::DEFAULT_MEMORY);
    CHECK(fn.maximumDelay() == sampleDelay::DEFAULT_MEMORY);
    CHECK(fn.delay() == 0.f);
    CHECK(fn.ramp() == 0.f);
    CHECK(fn.delaySamples() == 0);
    const std::vector<float> in = Ramp(3 * N);
    CHECK(Run(fn, in) == in);
  }

  TEST_CASE("sampleDelay: an impulse comes out exactly the delay later, below one block and "
            "at the maximum, for any block size (#1104)") {
    for (const unsigned int delay : {0u, 1u, 7u, N - 1, N, N + 5, 300u, 512u}) {
      for (const unsigned int block : {1u, 13u, N}) {
        CAPTURE(delay);
        CAPTURE(block);
        sampleDelay fn;
        fn.delay(static_cast<float>(delay));
        const std::vector<float> out = Run(fn, Impulse(4 * N + 600, 3), block);
        const std::vector<float> expected = Impulse(4 * N + 600, 3 + delay);
        CHECK(out == expected);
      }
    }
  }

  TEST_CASE("sampleDelay: a sine comes out as sin(w (n - D)) (#1104)") {
    constexpr unsigned int D = 37;
    sampleDelay fn(100);
    fn.delay(static_cast<float>(D));
    std::vector<float> in(4 * N);
    for (std::size_t i = 0; i < in.size(); ++i)
      in[i] = std::sin(0.05f * static_cast<float>(i));
    const std::vector<float> out = Run(fn, in);
    for (std::size_t i = 0; i < out.size(); ++i) {
      CAPTURE(i);
      const float expected = i < D ? 0.f : std::sin(0.05f * static_cast<float>(i - D));
      CHECK(out[i] == expected);
    }
  }

  TEST_CASE("sampleDelay: a fraction is dropped, the delay clamped, non-finite ignored "
            "(#1104)") {
    sampleDelay fn(64);
    fn.delay(2.9f);
    CHECK(fn.delay() == doctest::Approx(2.9f));
    CHECK(fn.delaySamples() == 2);
    fn.delay(-5.f);
    CHECK(fn.delaySamples() == 0);
    fn.delay(1000.f);
    CHECK(fn.delaySamples() == 64);
    fn.delay(10.f);
    fn.delay(std::numeric_limits<float>::quiet_NaN());
    fn.delay(std::numeric_limits<float>::infinity());
    CHECK(fn.delay() == 10.f);
    fn.delayMs(std::numeric_limits<float>::quiet_NaN());
    CHECK(fn.delay() == 10.f);
    CHECK_FALSE(fn.delayInMs());
    fn.ramp(-1.f);
    fn.ramp(std::numeric_limits<float>::quiet_NaN());
    CHECK(fn.ramp() == 0.f);

    // 2.9 delays by 2, as an impulse shows; beyond the maximum by 64.
    fn.delay(2.9f);
    CHECK(Run(fn, Impulse(N, 5)) == Impulse(N, 7));
    fn.delay(1e9f);
    CHECK(Run(fn, Impulse(2 * N, 5)) == Impulse(2 * N, 69));
  }

  TEST_CASE("sampleDelay: a delay in ms follows the sample rate, one in samples does not "
            "(#1104)") {
    {
      TestHelpers::ScopedSampleRate rate(48000);
      sampleDelay fn(1000);
      fn.delayMs(2.f);
      CHECK(fn.delayInMs());
      CHECK(fn.delaySamples() == 96);
      CHECK(Run(fn, Impulse(2 * N)) == Impulse(2 * N, 96));
    }
    {
      TestHelpers::ScopedSampleRate rate(44100);
      sampleDelay fn(1000);
      fn.delayMs(10.f);
      CHECK(fn.delaySamples() == 441);
      CHECK(Run(fn, Impulse(4 * N)) == Impulse(4 * N, 441));
      // The same object at another rate converts again on the next block.
      TestHelpers::ScopedSampleRate faster(96000);
      CHECK(fn.delaySamples() == 960);
      fn.delay(30.f);
      CHECK(fn.delaySamples() == 30);
    }
  }

  TEST_CASE("sampleDelay: a ramp crossfades linearly from the old delay to the new one "
            "(#1104)") {
    TestHelpers::ScopedSampleRate rate(48000);
    constexpr unsigned int F = 48; // 1 ms
    sampleDelay fn(100);
    fn.ramp(1.f);
    const std::vector<float> in = Ramp(4 * N);
    std::vector<float> out;
    for (std::size_t n = 0; n < in.size(); ++n) {
      if (n == N) fn.delay(10.f);
      out.push_back(fn.process(in[n]));
    }
    for (std::size_t n = 0; n < out.size(); ++n) {
      CAPTURE(n);
      const auto x = static_cast<double>(n + 1);
      double expected = x;
      if (n >= N) {
        const double k = static_cast<double>(std::min<std::size_t>(n - N + 1, F));
        expected = x - 10.0 * k / F; // (1 - g) x(n) + g x(n - 10)
      }
      CHECK(out[n] == doctest::Approx(expected).epsilon(1e-6));
    }
    CHECK(out.back() == in.back() - 10.f);
    CHECK_FALSE(fn.ramping());

    // The block call does the same.
    sampleDelay blocks(100);
    blocks.ramp(1.f);
    const std::vector<float> first = Run(blocks, std::vector<float>(in.begin(), in.begin() + N));
    blocks.delay(10.f);
    const std::vector<float> rest = Run(blocks, std::vector<float>(in.begin() + N, in.end()));
    for (std::size_t i = 0; i < rest.size(); ++i)
      CHECK(rest[i] == out[N + i]);
    (void)first;

    // Without a ramp the change jumps.
    sampleDelay jump(100);
    jump.delay(10.f);
    CHECK(jump.process(5.f) == 0.f);
    CHECK_FALSE(jump.ramping());
  }

  TEST_CASE("sampleDelay: a change during a crossfade starts from the delay fading in "
            "(#1104)") {
    TestHelpers::ScopedSampleRate rate(48000);
    constexpr double F = 48.0;
    sampleDelay fn(100);
    fn.ramp(1.f);
    const std::vector<float> in = Ramp(400);
    std::vector<float> out;
    for (std::size_t n = 0; n < in.size(); ++n) {
      if (n == 100) fn.delay(10.f);
      if (n == 120) fn.delay(30.f);
      if (n == 120) CHECK(fn.ramping());
      out.push_back(fn.process(in[n]));
    }
    for (std::size_t n = 120; n < out.size(); ++n) {
      CAPTURE(n);
      const auto x = static_cast<double>(n + 1);
      const double g = std::min(static_cast<double>(n - 120 + 1), F) / F;
      // From x(n - 10), the delay fading in, to x(n - 30).
      CHECK(out[n] == doctest::Approx((1.0 - g) * (x - 10.0) + g * (x - 30.0)).epsilon(1e-6));
    }
  }

  TEST_CASE("sampleDelay: a delay signal is read per sample and interpolated (#1104)") {
    sampleDelay fn(64);
    const std::vector<float> in = Ramp(3 * N);
    std::vector<float> delays(in.size());
    for (std::size_t i = 0; i < delays.size(); ++i)
      delays[i] = 0.25f * static_cast<float>(i % 160); // 0 .. 39.75, sweeping
    const std::vector<float> out = Run(fn, in, N, &delays);
    for (std::size_t n = 64; n < out.size(); ++n) {
      CAPTURE(n);
      // Linear interpolation of a linear signal is exact.
      CHECK(out[n] == doctest::Approx(in[n] - delays[n]).epsilon(1e-6));
    }

    // Clamped to 0 and the maximum; a non-finite sample reads as 0.
    sampleDelay clamp(64);
    std::vector<float> wild(in.size());
    for (std::size_t i = 0; i < wild.size(); ++i) {
      const std::size_t k = i % 4;
      wild[i] = k == 0   ? -3.f
                : k == 1 ? 1000.f
                : k == 2 ? std::numeric_limits<float>::quiet_NaN()
                         : std::numeric_limits<float>::infinity();
    }
    const std::vector<float> clamped = Run(clamp, in, N, &wild);
    for (std::size_t n = 64; n < clamped.size(); ++n) {
      CAPTURE(n);
      const std::size_t k = n % 4;
      CHECK(clamped[n] == (k == 1 ? in[n] - 64.f : in[n]));
    }

    // The number set before is still there for the calls without a signal.
    fn.delay(5.f);
    const std::vector<float> after = Run(fn, Ramp(N));
    CHECK(after[N - 1] == static_cast<float>(N - 5));
  }

  TEST_CASE("sampleDelay: maximumDelay, clear and non-finite input (#1104)") {
    sampleDelay fn(100);
    fn.delay(80.f);
    Run(fn, Ramp(N));
    // A lower maximum clamps the delay and clears the line.
    fn.maximumDelay(20);
    CHECK(fn.maximumDelay() == 20);
    CHECK(fn.delaySamples() == 20);
    std::vector<float> out = Run(fn, Impulse(N, 2));
    CHECK(out == Impulse(N, 22));
    // Never beyond the memory.
    fn.maximumDelay(1000);
    CHECK(fn.maximumDelay() == 100);
    CHECK(fn.delaySamples() == 80);

    // clear() silences what the line holds.
    fn.delay(10.f);
    Run(fn, Impulse(N, N - 3));
    fn.clear();
    out = Run(fn, std::vector<float>(N, 0.f));
    CHECK(std::all_of(out.begin(), out.end(), [](float s) { return s == 0.f; }));

    // A non-finite input sample is written as 0.
    sampleDelay pass;
    std::vector<float> in = Ramp(N);
    in[4] = std::numeric_limits<float>::quiet_NaN();
    in[9] = -std::numeric_limits<float>::infinity();
    out = Run(pass, in);
    CHECK(out[4] == 0.f);
    CHECK(out[9] == 0.f);
    CHECK(out[5] == in[5]);

    // A memory of 0 passes the input.
    sampleDelay none(0);
    none.delay(10.f);
    CHECK(none.maximumDelay() == 0);
    CHECK(Run(none, Ramp(N)) == Ramp(N));
  }

  TEST_CASE("sampleDelay: processing allocates nothing (#1104)") {
    TestHelpers::ScopedSampleRate rate(48000);
    sampleDelay fn(1000);
    buffer in(N);
    buffer d(N);
    in = 0.5f;
    d = 3.5f;
    fn(in);
    {
      TestHelpers::ProbeScope probe;
      fn.ramp(2.f).delay(100.f);
      fn(in);
      fn.delayMs(5.f);
      fn(in);
      fn(in, d);
      fn.process(0.25f);
      fn.maximumDelay(500);
      fn.clear();
      fn(in);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
