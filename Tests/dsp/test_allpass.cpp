// Tests for YSE::DSP::allpass (YseEngine/dsp/filters/allpass.hpp), the
// Schroeder allpass behind ~allpass (issue #1093): the impulse response
// against its closed form and against an independent direct-form loop (whole
// and fractional delays), unit gain on a sine and in impulse energy, the live
// sample rate, the memory and clamping, the gain at +-1 and other degenerate
// settings from a running state, per-sample delay and gain signals,
// non-finite input, clear(), block size independence and allocation freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>
#include "dsp/filters/allpass.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::allpass;
using YSE::DSP::buffer;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  // Runs `fn` over `in` in blocks of `block` samples.
  std::vector<float> Run(allpass& fn, const std::vector<float>& in, unsigned int block = N) {
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

  std::vector<float> Impulse(unsigned int samples) {
    std::vector<float> v(samples, 0.f);
    v[0] = 1.f;
    return v;
  }

  std::vector<float> Noise(unsigned int samples, unsigned int seed) {
    std::mt19937 gen(seed); // NOLINT(cert-msc51-cpp,bugprone-random-generator-seed): repeatable
    std::uniform_real_distribution<float> dist(-1.f, 1.f);
    std::vector<float> v(samples);
    for (float& s : v)
      s = dist(gen);
    return v;
  }

  // The direct form y(n) = -g x(n) + x(n-D) + g y(n-D), written here
  // independently of the class, with D read by linear interpolation.
  std::vector<double> Direct(double g, double d, const std::vector<float>& x) {
    const auto whole = static_cast<std::size_t>(d);
    const double frac = d - static_cast<double>(whole);
    std::vector<double> y(x.size(), 0.0);
    const auto past = [&](const auto& v, std::size_t n, std::size_t back) -> double {
      return n >= back ? static_cast<double>(v[n - back]) : 0.0;
    };
    for (std::size_t n = 0; n < x.size(); ++n) {
      const double xd = (1.0 - frac) * past(x, n, whole) + frac * past(x, n, whole + 1);
      const double yd = (1.0 - frac) * past(y, n, whole) + frac * past(y, n, whole + 1);
      y[n] = -g * x[n] + xd + g * yd;
    }
    return y;
  }

  double Rms(const std::vector<float>& v, std::size_t from) {
    double sum = 0.0;
    for (std::size_t i = from; i < v.size(); ++i)
      sum += static_cast<double>(v[i]) * v[i];
    return std::sqrt(sum / static_cast<double>(v.size() - from));
  }

  double MaxAbsDiff(const std::vector<float>& a, const std::vector<double>& b) {
    double worst = 0.0;
    for (std::size_t n = 0; n < a.size() && n < b.size(); ++n)
      worst = std::max(worst, std::abs(static_cast<double>(a[n]) - b[n]));
    return worst;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("allpass: defaults, clamping and memory (#1093)") {
    TestHelpers::ScopedSampleRate rate(48000);
    allpass fn;
    CHECK(fn.delay() == 0.f);
    CHECK(fn.gain() == 0.f);
    CHECK(fn.maximumDelay() == allpass::DEFAULT_MAXIMUM_DELAY);
    CHECK(fn.capacity() == doctest::Approx(10.0));
    // A delay of 0 reads one sample back, the shortest the recursion allows.
    CHECK(fn.delaySamples() == 1.f);
    fn.delay(5.f);
    CHECK(fn.delaySamples() == doctest::Approx(240.0));
    // Beyond the maximum: clamped to it; negative: one sample.
    fn.delay(50.f);
    CHECK(fn.delay() == 50.f);
    CHECK(fn.delaySamples() == doctest::Approx(480.0));
    fn.delay(-3.f);
    CHECK(fn.delaySamples() == 1.f);

    // The gain is clamped to -1 .. 1; non-finite settings are ignored.
    fn.gain(1.5f);
    CHECK(fn.gain() == 1.f);
    fn.gain(-7.f);
    CHECK(fn.gain() == -1.f);
    fn.gain(0.4f);
    fn.gain(std::nanf(""));
    fn.gain(std::numeric_limits<float>::infinity());
    CHECK(fn.gain() == 0.4f);
    fn.delay(2.f);
    fn.delay(std::nanf(""));
    fn.delay(-std::numeric_limits<float>::infinity());
    CHECK(fn.delay() == 2.f);

    // The maximum delay: clamped at 0, the default when not finite.
    CHECK(allpass(-5.f).maximumDelay() == 0.f);
    CHECK(allpass(std::nanf("")).maximumDelay() == allpass::DEFAULT_MAXIMUM_DELAY);
    allpass zero(0.f);
    zero.delay(3.f);
    CHECK(zero.delaySamples() == 1.f);

    // Memory sized for a lower highest rate: above it, the longest delay
    // shrinks with the rate.
    allpass small(10.f, 48000);
    CHECK(small.capacity() == doctest::Approx(10.0));
    YSE::SAMPLERATE = 96000;
    CHECK(small.capacity() == doctest::Approx(5.0));
    small.delay(10.f);
    CHECK(small.delaySamples() == doctest::Approx(480.0));
    // The default memory covers 192 kHz.
    CHECK(fn.capacity() == doctest::Approx(10.0));
  }

  TEST_CASE("allpass: impulse response against the closed form (#1093)") {
    TestHelpers::ScopedSampleRate rate(48000);
    for (const float g : {0.f, 0.3f, 0.7f, -0.5f, 0.95f}) {
      for (const float ms : {1.f, 2.5f, 9.f}) {
        CAPTURE(g);
        CAPTURE(ms);
        allpass fn;
        fn.delay(ms).gain(g);
        const auto d = static_cast<std::size_t>(std::lround(ms * 48.f));
        REQUIRE(fn.delaySamples() == static_cast<float>(d));
        const std::vector<float> y = Run(fn, Impulse(static_cast<unsigned int>(8 * d + 5)));
        // y(0) = -g, y(kD) = (1 - g^2) g^(k-1), 0 between.
        double energy = 0.0;
        for (std::size_t n = 0; n < y.size(); ++n) {
          CAPTURE(n);
          double want = 0.0;
          if (n == 0)
            want = -g;
          else if (n % d == 0) {
            const std::size_t echo = n / d; // exact: n is a multiple of d
            want = (1.0 - static_cast<double>(g) * g) *
                   std::pow(static_cast<double>(g), static_cast<double>(echo) - 1.0);
          }
          CHECK(static_cast<double>(y[n]) == doctest::Approx(want).epsilon(1e-6));
          energy += static_cast<double>(y[n]) * y[n];
        }
        // Allpass: the whole impulse response carries unit energy. Up to the
        // eighth echo that is g^2 + (1 - g^2)(1 - g^16) = 1 - g^16 (1 - g^2).
        const double g2 = static_cast<double>(g) * g;
        CHECK(energy == doctest::Approx(1.0 - std::pow(g2, 8.0) * (1.0 - g2)).epsilon(1e-5));
      }
    }
  }

  TEST_CASE("allpass: whole and fractional delays against an independent direct form "
            "(#1093)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::vector<float> noise = Noise(6 * N, 1093);
    for (const float g : {0.f, 0.5f, -0.8f, 0.99f}) {
      for (const float ms : {0.f, 0.02f, 0.37f, 1.f, 3.3333f, 10.f}) {
        CAPTURE(g);
        CAPTURE(ms);
        allpass fn;
        fn.delay(ms).gain(g);
        const double d = static_cast<double>(fn.delaySamples());
        CHECK(d == doctest::Approx(std::clamp(static_cast<double>(ms * 48.f), 1.0, 480.0)));
        const std::vector<float> got = Run(fn, noise);
        CHECK(MaxAbsDiff(got, Direct(g, d, noise)) < 1e-4);
      }
    }
  }

  TEST_CASE("allpass: a sine passes at unit gain, delayed in phase (#1093)") {
    TestHelpers::ScopedSampleRate rate(48000);
    for (const double hz : {100.0, 1000.0, 5000.0}) {
      for (const float g : {0.5f, -0.7f, 0.9f}) {
        CAPTURE(hz);
        CAPTURE(g);
        allpass fn;
        fn.delay(1.f).gain(g); // 48 samples, a whole number
        std::vector<float> in(200 * N);
        for (std::size_t i = 0; i < in.size(); ++i)
          in[i] = static_cast<float>(std::sin(2.0 * PI * hz * static_cast<double>(i) / 48000.0));
        const std::vector<float> out = Run(fn, in);
        // Steady state (g^k has died away), over a whole number of periods
        // of every frequency: the level equals the input's.
        const std::size_t from = out.size() - 4800;
        CHECK(Rms(out, from) == doctest::Approx(Rms(in, from)).epsilon(1e-4));
      }
    }
  }

  TEST_CASE("allpass: the delay follows the live sample rate (#1093)") {
    TestHelpers::ScopedSampleRate rate(48000);
    allpass fn;
    fn.delay(2.f).gain(0.5f);
    std::vector<float> y = Run(fn, Impulse(400));
    CHECK(y[96] == doctest::Approx(0.75));
    CHECK(y[192] == doctest::Approx(0.375));
    YSE::SAMPLERATE = 96000;
    fn.clear();
    CHECK(fn.delaySamples() == doctest::Approx(192.0));
    y = Run(fn, Impulse(400));
    CHECK(y[96] == 0.f);
    CHECK(y[192] == doctest::Approx(0.75));
    // Through the signal call too.
    YSE::SAMPLERATE = 44100;
    fn.clear();
    buffer in(N);
    buffer delay(N);
    buffer gain(N);
    in = 0.f;
    in.getPtr()[0] = 1.f;
    delay = 1.f;
    gain = 0.5f;
    buffer& out = fn(in, delay, gain);
    CHECK(out.getPtr()[0] == doctest::Approx(-0.5));
    // 44.1 samples: the echo lands between 44 and 45, interpolated.
    CHECK(out.getPtr()[44] == doctest::Approx(0.75 * 0.9).epsilon(1e-4));
    CHECK(out.getPtr()[45] == doctest::Approx(0.75 * 0.1).epsilon(1e-3));
  }

  TEST_CASE("allpass: degenerate settings from a running state (#1093)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::vector<float> noise = Noise(4 * N, 7);

    for (const float edge : {1.f, -1.f}) {
      CAPTURE(edge);
      allpass fn;
      fn.delay(3.f).gain(0.9f);
      Run(fn, noise); // memory full of a ringing 0.9 tail
      // At +-1 the output is exactly -g x from the first sample: no residue
      // of the earlier setting circulates.
      fn.gain(edge);
      std::vector<float> y = Run(fn, noise);
      for (std::size_t n = 0; n < y.size(); ++n) {
        CAPTURE(n);
        CHECK(y[n] == -edge * noise[n]);
      }
      // Silence in, silence out, at once and for good.
      y = Run(fn, std::vector<float>(4 * N, 0.f));
      CHECK(*std::max_element(y.begin(), y.end()) == 0.f);
      CHECK(*std::min_element(y.begin(), y.end()) == 0.f);
      // Back below 1: the filter runs on and decays as usual.
      fn.gain(0.6f);
      Run(fn, noise);
      y = Run(fn, std::vector<float>(40 * N, 0.f));
      double tail = 0.0;
      for (std::size_t n = y.size() - N; n < y.size(); ++n)
        tail = std::max(tail, std::abs(static_cast<double>(y[n])));
      CHECK(tail < 1e-6);
    }

    // A delay moved to 0 (one sample) and to beyond the maximum while
    // running stays bounded and decays.
    allpass fn;
    fn.delay(4.f).gain(0.95f);
    Run(fn, noise);
    fn.delay(0.f);
    std::vector<float> y = Run(fn, noise);
    for (const float s : y)
      CHECK(std::abs(s) < 50.f);
    fn.delay(1000.f);
    CHECK(fn.delaySamples() == doctest::Approx(480.0));
    Run(fn, noise);
    fn.gain(0.f); // a plain delay: the input 480 samples late
    Run(fn, std::vector<float>(4 * N, 0.f));
    y = Run(fn, noise);
    for (std::size_t n = 480; n < y.size(); ++n) {
      CAPTURE(n);
      CHECK(y[n] == noise[n - 480]);
    }

    // Long run near the edge: bounded.
    allpass edge;
    edge.delay(0.f).gain(0.999f);
    const std::vector<float> loud = Noise(200 * N, 99);
    y = Run(edge, loud);
    for (const float s : y)
      CHECK(std::isfinite(s));
    CHECK(*std::max_element(y.begin(), y.end()) < 100.f);
  }

  TEST_CASE("allpass: delay and gain signals are read per sample (#1093)") {
    TestHelpers::ScopedSampleRate rate(48000);
    allpass fn;
    allpass reference;
    buffer in(N);
    buffer delay(N);
    buffer gain(N);
    const std::vector<float> noise = Noise(N, 3);
    for (int i = 0; i < static_cast<int>(N); ++i) {
      in.getPtr()[i] = noise[static_cast<std::size_t>(i)];
      delay.getPtr()[i] = 0.5f + 0.03f * static_cast<float>(i);
      gain.getPtr()[i] = -0.9f + 0.014f * static_cast<float>(i);
    }
    // Non-finite values in the signals are ignored, as the setters do.
    delay.getPtr()[10] = std::nanf("");
    gain.getPtr()[11] = std::numeric_limits<float>::infinity();
    for (int block = 0; block < 3; ++block) {
      CAPTURE(block);
      buffer& out = fn(in, delay, gain);
      REQUIRE(out.getLength() == N);
      for (int i = 0; i < static_cast<int>(N); ++i) {
        CAPTURE(i);
        reference.delay(delay.getPtr()[i]).gain(gain.getPtr()[i]);
        CHECK(out.getPtr()[i] == reference.process(in.getPtr()[i]));
      }
    }
    CHECK(fn.delay() == delay.getPtr()[N - 1]);
    CHECK(fn.gain() == gain.getPtr()[N - 1]);

    // The shortest buffer sets the length.
    buffer shortGain(N / 2);
    shortGain = 0.2f;
    CHECK(fn(in, delay, shortGain).getLength() == N / 2);
  }

  TEST_CASE("allpass: non-finite input reads as 0, clear() silences the lines (#1093)") {
    TestHelpers::ScopedSampleRate rate(48000);
    allpass fn;
    allpass clean;
    fn.delay(0.5f).gain(0.6f);
    clean.delay(0.5f).gain(0.6f);
    for (int i = 0; i < 200; ++i) {
      CAPTURE(i);
      const float x = i == 0 ? 1.f : 0.f;
      float dirty = x;
      if (i == 5) dirty = std::nanf("");
      if (i == 6) dirty = std::numeric_limits<float>::infinity();
      CHECK(fn.process(dirty) == clean.process(x));
    }
    // An output that overflows a float is written as 0 rather than latched.
    allpass huge;
    huge.delay(0.f).gain(-0.5f);
    CHECK(huge.process(3e38f) == doctest::Approx(1.5e38));
    CHECK(huge.process(3e38f) == 0.f); // 1.5e38 + 3e38 overflows
    CHECK(huge.process(0.f) == doctest::Approx(3e38)); // not latched
    // clear(): the same as a new filter.
    fn.clear();
    allpass fresh;
    fresh.delay(0.5f).gain(0.6f);
    for (int i = 0; i < 100; ++i)
      CHECK(fn.process(i == 0 ? 1.f : 0.f) == fresh.process(i == 0 ? 1.f : 0.f));
  }

  TEST_CASE("allpass: blocks, single samples and buffer clamping (#1093)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::vector<float> in = Noise(5 * N, 11);
    std::vector<float> reference;
    {
      allpass fn;
      fn.delay(1.7f).gain(0.7f);
      reference = Run(fn, in);
    }
    for (const unsigned int block : {1u, 7u, 64u, N}) {
      CAPTURE(block);
      allpass fn;
      fn.delay(1.7f).gain(0.7f);
      CHECK(Run(fn, in, block) == reference);
    }
    allpass single;
    single.delay(1.7f).gain(0.7f);
    for (std::size_t i = 0; i < in.size(); ++i)
      CHECK(single.process(in[i]) == reference[i]);
    // A buffer longer than STANDARD_BUFFERSIZE is processed up to that.
    allpass fn;
    buffer longer(2 * N);
    CHECK(fn(longer).getLength() == N);
  }

  TEST_CASE("allpass: processing allocates nothing (#1093)") {
    TestHelpers::ScopedSampleRate rate(48000);
    allpass fn(50.f);
    buffer in(N);
    buffer shorter(N / 2);
    buffer delay(N);
    buffer gain(N);
    in = 0.5f;
    gain = 0.3f;
    for (int i = 0; i < static_cast<int>(N); ++i)
      delay.getPtr()[i] = 1.f + 0.2f * static_cast<float>(i);
    fn(in);
    {
      TestHelpers::ProbeScope probe;
      fn.delay(12.5f).gain(0.7f);
      fn(in);
      fn(shorter);
      fn(in, delay, gain);
      fn.process(0.25f);
      fn.clear();
      fn.gain(1.f);
      fn(in);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
