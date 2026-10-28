// Tests for YSE::DSP::slide (YseEngine/dsp/filters/slide.hpp), the logarithmic
// slide behind ~slide (issue #1099): pass-through at the defaults, the step
// response against its closed form y(n) = 1 - (1 - 1/s)^(n+1) with separate
// rise and fall amounts, the snap onto the input, amounts below 1 and
// non-finite settings, an envelope follower on a rectified sine, the
// per-sample amount signals, non-finite input, reset(), independence from the
// sample rate and the block size, and allocation freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>
#include "dsp/filters/slide.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::slide;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // Runs `fn` over `in` in blocks of `block` samples.
  std::vector<float> Run(slide& fn, const std::vector<float>& in, unsigned int block = N) {
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
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("slide: the defaults pass the input through (#1099)") {
    slide fn;
    CHECK(fn.up() == 1.f);
    CHECK(fn.down() == 1.f);
    const std::vector<float> in = Noise(3 * N, 1);
    CHECK(Run(fn, in) == in);
  }

  TEST_CASE("slide: a step up and down follows the closed form, each with its own amount "
            "(#1099)") {
    slide fn;
    fn.up(10.f).down(100.f);

    // Rising from 0 to 1 at slide 10: y(n) = 1 - 0.9^(n+1), until within 1e-6
    // of 1, where it is 1.
    int reachedUp = -1;
    for (int n = 0; n < 400; ++n) {
      CAPTURE(n);
      const double expected = 1.0 - std::pow(0.9, n + 1);
      const float y = fn.process(1.f);
      if (1.0 - expected > 2e-6) CHECK(std::fabs(y - expected) <= 1e-6);
      if (y == 1.f && reachedUp < 0) reachedUp = n;
    }
    // 0.9^(n+1) <= 1e-6 first at n + 1 = 132.
    CHECK(reachedUp >= 130);
    CHECK(reachedUp <= 132);
    CHECK(fn.value() == 1.f);

    // Falling from 1 to 0 at slide 100: y(n) = 0.99^(n+1).
    int reachedDown = -1;
    for (int n = 0; n < 3000; ++n) {
      CAPTURE(n);
      const double expected = std::pow(0.99, n + 1);
      const float y = fn.process(0.f);
      if (expected > 2e-6) CHECK(std::fabs(y - expected) <= 1e-6);
      CHECK(y >= 0.f);
      if (y == 0.f && reachedDown < 0) reachedDown = n;
    }
    // 0.99^(n+1) <= 1e-6 first at n + 1 = 1375: ten times slower, and it
    // ends at 0 exactly, not in denormals.
    CHECK(reachedDown >= 1373);
    CHECK(reachedDown <= 1375);
    CHECK(fn.value() == 0.f);
  }

  TEST_CASE("slide: rising and falling each use their own amount, one sample at a time "
            "(#1099)") {
    slide fn;
    fn.up(1.f).down(4.f);
    CHECK(fn.process(2.f) == 2.f); // up at 1: there at once
    CHECK(fn.process(0.f) == 1.5f); // down at 4: a quarter of the way
    CHECK(fn.process(1.5f) == 1.5f);
  }

  TEST_CASE("slide: the snap is relative to the input (#1099)") {
    slide fn;
    fn.up(2.f);
    // Towards 1000: within 1e-3 (one part in a million of 1000) is there.
    float y = 0.f;
    int n = 0;
    while (y != 1000.f && n < 100) {
      y = fn.process(1000.f);
      ++n;
    }
    CHECK(y == 1000.f);
    // 1000 * 0.5^n <= 1e-3 first at n = 20.
    CHECK(n == 20);
  }

  TEST_CASE("slide: amounts below 1 or not a number are used as 1; non-finite settings are "
            "ignored (#1099)") {
    CHECK(slide::Effective(10.f) == 10.f);
    CHECK(slide::Effective(1.f) == 1.f);
    CHECK(slide::Effective(0.5f) == 1.f);
    CHECK(slide::Effective(0.f) == 1.f);
    CHECK(slide::Effective(-3.f) == 1.f);
    CHECK(slide::Effective(std::numeric_limits<float>::quiet_NaN()) == 1.f);

    slide fn;
    fn.up(0.f).down(-5.f);
    CHECK(fn.up() == 0.f); // kept as given
    const std::vector<float> in = Noise(N, 2);
    CHECK(Run(fn, in) == in); // used as 1: no overshoot, no division by 0

    fn.up(8.f).down(16.f);
    fn.up(std::numeric_limits<float>::quiet_NaN());
    fn.down(std::numeric_limits<float>::infinity());
    CHECK(fn.up() == 8.f);
    CHECK(fn.down() == 16.f);
  }

  TEST_CASE("slide: the output never passes the input (#1099)") {
    slide fn;
    fn.up(3.f).down(7.f);
    float previous = 0.f;
    for (const float x : Noise(4 * N, 3)) {
      const float y = fn.process(x);
      CHECK(y >= std::min(previous, x));
      CHECK(y <= std::max(previous, x));
      previous = y;
    }
  }

  TEST_CASE("slide: a fast rise and slow fall follow the envelope of a rectified sine "
            "(#1099)") {
    TestHelpers::ScopedSampleRate rate(48000);
    slide fn;
    fn.up(1.f).down(2000.f);
    std::vector<float> in(48000);
    for (std::size_t i = 0; i < in.size(); ++i) {
      const double amplitude = i < 24000 ? 0.8 : 0.2;
      in[i] =
          static_cast<float>(std::fabs(amplitude * std::sin(2.0 * 3.14159265358979323846 * 2000.0 *
                                                            static_cast<double>(i) / 48000.0)));
    }
    const std::vector<float> out = Run(fn, in);
    // Loud half: the follower holds near the peak, ripple well under 1%.
    const auto [loMin, loMax] = std::minmax_element(out.begin() + 20000, out.begin() + 24000);
    CHECK(*loMax <= 0.8f + 1e-6f);
    CHECK(*loMin >= 0.79f);
    // Quiet half, after the fall has settled: near 0.2.
    const auto [qMin, qMax] = std::minmax_element(out.begin() + 44000, out.end());
    CHECK(*qMax <= 0.21f);
    CHECK(*qMin >= 0.19f);
  }

  TEST_CASE("slide: the amount signals are read per sample (#1099)") {
    slide fn;
    slide reference;
    buffer in(N);
    buffer up(N);
    buffer down(N);
    for (int i = 0; i < static_cast<int>(N); ++i) {
      in.getPtr()[i] = std::sin(static_cast<float>(i) * 0.2f);
      up.getPtr()[i] = 1.f + static_cast<float>(i) * 0.25f;
      down.getPtr()[i] = 2.f + static_cast<float>(i) * 0.5f;
    }
    // A non-finite value keeps the previous value for that sample.
    up.getPtr()[10] = std::numeric_limits<float>::quiet_NaN();
    down.getPtr()[20] = std::numeric_limits<float>::infinity();
    buffer& out = fn(in, up, down);
    REQUIRE(out.getLength() == N);
    // An independent loop over the same per-sample amounts.
    double y = 0.0;
    float u = 1.f;
    float d = 1.f;
    for (int i = 0; i < static_cast<int>(N); ++i) {
      CAPTURE(i);
      if (std::isfinite(up.getPtr()[i])) u = up.getPtr()[i];
      if (std::isfinite(down.getPtr()[i])) d = down.getPtr()[i];
      const double x = in.getPtr()[i];
      y += (x - y) / (x > y ? u : d);
      CHECK(std::fabs(out.getPtr()[i] - y) <= 1e-6);
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

  TEST_CASE("slide: non-finite input is skipped, reset() returns to 0 (#1099)") {
    slide fn;
    fn.up(4.f).down(4.f);
    CHECK(fn.process(1.f) == 0.25f);
    CHECK(fn.process(std::numeric_limits<float>::quiet_NaN()) == 0.25f);
    CHECK(fn.process(std::numeric_limits<float>::infinity()) == 0.25f);
    CHECK(fn.process(-std::numeric_limits<float>::infinity()) == 0.25f);
    CHECK(fn.process(1.f) == 0.4375f);
    fn.reset();
    CHECK(fn.value() == 0.f);
    CHECK(fn.process(1.f) == 0.25f);
  }

  TEST_CASE("slide: the amounts count samples, whatever the sample rate (#1099)") {
    const std::vector<float> in = Noise(2 * N, 4);
    std::vector<float> at44;
    std::vector<float> at96;
    {
      TestHelpers::ScopedSampleRate rate(44100);
      slide fn;
      fn.up(5.f).down(50.f);
      at44 = Run(fn, in);
    }
    {
      TestHelpers::ScopedSampleRate rate(96000);
      slide fn;
      fn.up(5.f).down(50.f);
      at96 = Run(fn, in);
    }
    CHECK(at44 == at96);
  }

  TEST_CASE("slide: block size does not change the output (#1099)") {
    const std::vector<float> in = Noise(4 * N + 17, 5);
    slide a;
    slide b;
    a.up(6.f).down(30.f);
    b.up(6.f).down(30.f);
    CHECK(Run(a, in, N) == Run(b, in, 7));
  }

  TEST_CASE("slide: processing allocates nothing (#1099)") {
    slide fn;
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
      fn.up(12.f).down(3.f);
      fn(in);
      fn(shorter);
      fn(in, up, down);
      (void)fn.process(0.1f);
      fn.reset();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
