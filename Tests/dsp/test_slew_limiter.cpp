// Tests for YSE::DSP::slewLimiter (YseEngine/dsp/filters/slewLimiter.hpp),
// the limit on sample-to-sample change behind ~deltaclip (issue #1107): the
// defaults hold the output at 0, unlimited passes the input bit for bit, a
// step against its closed-form ramp with separate rise and fall limits, a
// square wave into a trapezoid, a sine slower than the limit untouched and a
// faster one bounded, a minimum above the maximum, a positive minimum that
// forces a rise, the per-sample limit signals, non-finite settings and input,
// set() / reset(), independence from the block split and the sample rate, and
// allocation freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>
#include "dsp/filters/slewLimiter.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::slewLimiter;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr float INF = std::numeric_limits<float>::infinity();
  constexpr double PI = 3.14159265358979323846;

  // Runs `fn` over `in` in blocks of `block` samples.
  std::vector<float> Run(slewLimiter& fn, const std::vector<float>& in, unsigned int block = N) {
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

  TEST_CASE("slewLimiter: the defaults of 0 and 0 hold the output where it is (#1107)") {
    slewLimiter fn;
    CHECK(fn.minimum() == 0.f);
    CHECK(fn.maximum() == 0.f);
    CHECK(fn.value() == 0.f);
    for (const float y : Run(fn, Noise(2 * N, 1)))
      CHECK(y == 0.f);
    fn.set(0.75f);
    for (const float y : Run(fn, Noise(N, 2)))
      CHECK(y == 0.75f);
  }

  TEST_CASE("slewLimiter: unlimited, the input passes bit for bit (#1107)") {
    slewLimiter fn;
    fn.minimum(-INF).maximum(INF);
    CHECK(fn.minimum() == -INF);
    CHECK(fn.maximum() == INF);
    const std::vector<float> in = Noise(3 * N, 3);
    CHECK(Run(fn, in) == in);
    // Limits wider than any change in the signal do the same.
    slewLimiter wide;
    wide.minimum(-2.f).maximum(2.f);
    CHECK(Run(wide, in) == in);
  }

  TEST_CASE("slewLimiter: a step rises and falls in straight lines, each at its own limit "
            "(#1107)") {
    slewLimiter fn;
    fn.minimum(-0.125f).maximum(0.25f);
    // 0 -> 1 at 0.25 per sample: y(n) = 0.25 (n + 1), landing on 1.
    for (int n = 0; n < 4; ++n) {
      CAPTURE(n);
      CHECK(fn.process(1.f) == 0.25f * static_cast<float>(n + 1));
    }
    CHECK(fn.process(1.f) == 1.f);
    CHECK(fn.value() == 1.f);
    // 1 -> 0 at 0.125 per sample: y(n) = 1 - 0.125 (n + 1).
    for (int n = 0; n < 8; ++n) {
      CAPTURE(n);
      CHECK(fn.process(0.f) == 1.f - 0.125f * static_cast<float>(n + 1));
    }
    CHECK(fn.process(0.f) == 0.f);
    // A change within the limits is taken whole.
    CHECK(fn.process(0.2f) == 0.2f);
    CHECK(fn.process(0.1f) == 0.1f);
  }

  TEST_CASE("slewLimiter: a square wave becomes a trapezoid (#1107)") {
    slewLimiter fn;
    fn.minimum(-0.5f).maximum(0.25f);
    // A square between -1 and 1, 16 samples per half period.
    std::vector<float> in(8 * N);
    for (std::size_t i = 0; i < in.size(); ++i)
      in[i] = (i / 16) % 2 == 0 ? 1.f : -1.f;
    const std::vector<float> out = Run(fn, in);
    // The first rise from 0: 4 samples to reach 1.
    CHECK(out[0] == 0.25f);
    CHECK(out[3] == 1.f);
    // Each fall from 1 to -1 takes 4 samples at 0.5, each rise from -1 to 1
    // takes 8 at 0.25; then the output sits on the input.
    for (std::size_t half = 1; half < in.size() / 16; ++half) {
      CAPTURE(half);
      const std::size_t start = half * 16;
      const bool rising = half % 2 == 0;
      const float from = rising ? -1.f : 1.f;
      const float step = rising ? 0.25f : -0.5f;
      const std::size_t length = rising ? 8 : 4;
      for (std::size_t k = 0; k < 16; ++k) {
        CAPTURE(k);
        const float expected = k < length ? from + step * static_cast<float>(k + 1) : in[start];
        CHECK(out[start + k] == expected);
      }
    }
  }

  TEST_CASE("slewLimiter: a sine slower than the limit passes; a faster one is bounded "
            "(#1107)") {
    // A 0.5 amplitude sine at w rad/sample rises at most 0.5 w per sample,
    // a little less between samples: 2 * 0.5 * sin(w / 2).
    const double w = 2.0 * PI / 64.0;
    const double steepest = std::sin(w / 2.0);
    std::vector<float> in(4 * N);
    for (std::size_t i = 0; i < in.size(); ++i)
      in[i] = static_cast<float>(0.5 * std::sin(w * static_cast<double>(i)));

    slewLimiter loose;
    const auto limit = static_cast<float>(steepest * 1.01);
    loose.minimum(-limit).maximum(limit);
    CHECK(Run(loose, in) == in);

    // At half the steepest slope the output changes by at most the limit
    // each sample, reaches the limit on the steep parts, and tracks a
    // triangle of the same period: never more than the 0.5 amplitude.
    slewLimiter tight;
    const auto half = static_cast<float>(steepest * 0.5);
    tight.minimum(-half).maximum(half);
    const std::vector<float> out = Run(tight, in);
    float previous = 0.f;
    float biggest = 0.f;
    for (std::size_t i = 0; i < out.size(); ++i) {
      CAPTURE(i);
      const float change = out[i] - previous;
      CHECK(std::fabs(change) <= half * (1.f + 1e-5f));
      biggest = std::max(biggest, std::fabs(change));
      CHECK(std::fabs(out[i]) <= 0.5f + 1e-6f);
      previous = out[i];
    }
    CHECK(biggest >= half * (1.f - 1e-5f));
  }

  TEST_CASE("slewLimiter: a minimum above the maximum always moves by the maximum, as ~clip "
            "takes its high limit (#1107)") {
    slewLimiter fn;
    fn.minimum(0.5f).maximum(-0.25f);
    CHECK(fn.process(1.f) == -0.25f);
    CHECK(fn.process(-1.f) == -0.5f);
    CHECK(fn.process(-0.5f) == -0.75f);
  }

  TEST_CASE("slewLimiter: a positive minimum forces a rise, a negative maximum a fall "
            "(#1107)") {
    slewLimiter up;
    up.minimum(0.125f).maximum(1.f);
    for (int n = 0; n < 8; ++n)
      CHECK(up.process(0.f) == 0.125f * static_cast<float>(n + 1));
    // A rise within the limits is taken whole.
    CHECK(up.process(1.5f) == 1.5f);

    slewLimiter down;
    down.minimum(-1.f).maximum(-0.25f);
    CHECK(down.process(0.f) == -0.25f);
    CHECK(down.process(0.f) == -0.5f);
    CHECK(down.process(-1.f) == -1.f);
  }

  TEST_CASE("slewLimiter: the limit signals are read per sample (#1107)") {
    slewLimiter fn;
    slewLimiter reference;
    buffer in(N);
    buffer lo(N);
    buffer hi(N);
    for (int i = 0; i < static_cast<int>(N); ++i) {
      in.getPtr()[i] = std::sin(static_cast<float>(i) * 0.3f);
      lo.getPtr()[i] = -0.01f * static_cast<float>(i % 17);
      hi.getPtr()[i] = 0.02f * static_cast<float>(i % 11);
    }
    // A NaN keeps the previous limit for that sample; an infinity is a
    // limit like any other.
    lo.getPtr()[10] = std::numeric_limits<float>::quiet_NaN();
    hi.getPtr()[20] = std::numeric_limits<float>::quiet_NaN();
    hi.getPtr()[30] = INF;
    buffer& out = fn(in, lo, hi);
    REQUIRE(out.getLength() == N);
    // An independent loop over the same per-sample limits.
    float y = 0.f;
    float l = 0.f;
    float h = 0.f;
    for (int i = 0; i < static_cast<int>(N); ++i) {
      CAPTURE(i);
      if (!std::isnan(lo.getPtr()[i])) l = lo.getPtr()[i];
      if (!std::isnan(hi.getPtr()[i])) h = hi.getPtr()[i];
      const float x = in.getPtr()[i];
      const float d = x - y;
      if (d >= l && d <= h)
        y = x;
      else
        y += std::min(std::max(d, l), h);
      CHECK(out.getPtr()[i] == y);
      // The same as setting the limits one sample at a time.
      reference.minimum(lo.getPtr()[i]).maximum(hi.getPtr()[i]);
      CHECK(reference.process(x) == out.getPtr()[i]);
    }
    CHECK(fn.minimum() == lo.getPtr()[N - 1]);
    CHECK(fn.maximum() == hi.getPtr()[N - 1]);

    // A shorter limit signal shortens the block.
    buffer shortHi(N / 2);
    shortHi = 0.1f;
    CHECK(fn(in, lo, shortHi).getLength() == N / 2);
  }

  TEST_CASE("slewLimiter: NaN settings are ignored; non-finite input holds the output "
            "(#1107)") {
    slewLimiter fn;
    fn.minimum(-0.5f).maximum(0.5f);
    fn.minimum(std::numeric_limits<float>::quiet_NaN());
    fn.maximum(std::numeric_limits<float>::quiet_NaN());
    CHECK(fn.minimum() == -0.5f);
    CHECK(fn.maximum() == 0.5f);

    CHECK(fn.process(0.25f) == 0.25f);
    CHECK(fn.process(std::numeric_limits<float>::quiet_NaN()) == 0.25f);
    CHECK(fn.process(INF) == 0.25f);
    CHECK(fn.process(-INF) == 0.25f);
    CHECK(fn.process(2.f) == 0.75f);
  }

  TEST_CASE("slewLimiter: set() and reset() choose the output the next change starts from "
            "(#1107)") {
    slewLimiter fn;
    fn.minimum(-0.25f).maximum(0.25f);
    fn.set(2.f);
    CHECK(fn.value() == 2.f);
    CHECK(fn.process(0.f) == 1.75f);
    // A non-finite set() is ignored.
    fn.set(std::numeric_limits<float>::quiet_NaN());
    CHECK(fn.value() == 1.75f);
    fn.reset();
    CHECK(fn.value() == 0.f);
    CHECK(fn.process(1.f) == 0.25f);
    // reset() keeps the limits.
    CHECK(fn.minimum() == -0.25f);
    CHECK(fn.maximum() == 0.25f);
  }

  TEST_CASE("slewLimiter: block size does not change the output (#1107)") {
    const std::vector<float> in = Noise(4 * N + 17, 5);
    slewLimiter a;
    slewLimiter b;
    slewLimiter c;
    a.minimum(-0.05f).maximum(0.1f);
    b.minimum(-0.05f).maximum(0.1f);
    c.minimum(-0.05f).maximum(0.1f);
    const std::vector<float> blocks = Run(a, in, N);
    CHECK(blocks == Run(b, in, 7));
    std::vector<float> samples;
    samples.reserve(in.size());
    for (const float x : in)
      samples.push_back(c.process(x));
    CHECK(blocks == samples);
  }

  TEST_CASE("slewLimiter: a block longer than STANDARD_BUFFERSIZE is processed up to it "
            "(#1107)") {
    slewLimiter fn;
    fn.minimum(-INF).maximum(INF);
    buffer in(2 * N);
    in = 0.5f;
    CHECK(fn(in).getLength() == N);
    buffer lo(2 * N);
    buffer hi(2 * N);
    lo = -1.f;
    hi = 1.f;
    CHECK(fn(in, lo, hi).getLength() == N);
  }

  TEST_CASE("slewLimiter: the limits count per sample, whatever the sample rate (#1107)") {
    const std::vector<float> in = Noise(2 * N, 6);
    std::vector<float> at44;
    std::vector<float> at96;
    {
      TestHelpers::ScopedSampleRate rate(44100);
      slewLimiter fn;
      fn.minimum(-0.02f).maximum(0.03f);
      at44 = Run(fn, in);
    }
    {
      TestHelpers::ScopedSampleRate rate(96000);
      slewLimiter fn;
      fn.minimum(-0.02f).maximum(0.03f);
      at96 = Run(fn, in);
    }
    CHECK(at44 == at96);
  }

  TEST_CASE("slewLimiter: processing allocates nothing (#1107)") {
    slewLimiter fn;
    buffer in(N);
    buffer shorter(N / 2);
    buffer lo(N);
    buffer hi(N);
    in = 0.5f;
    shorter = 0.25f;
    lo = -0.1f;
    hi = 0.1f;
    fn(in);
    {
      TestHelpers::ProbeScope probe;
      fn.minimum(-0.2f).maximum(0.3f);
      fn(in);
      fn(shorter);
      fn(in, lo, hi);
      (void)fn.process(0.1f);
      fn.set(0.5f);
      fn.reset();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
