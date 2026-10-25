// Tests for YSE::DSP::comb (YseEngine/dsp/delay/comb.hpp), the comb filter
// behind ~comb and ~teeth (issue #1094): the impulse response against its
// closed form, whole and fractional, equal and separate delays against an
// independent direct-form loop, the magnitude response of the feedforward
// comb on sines, the live sample rate, the memory and clamping, per-sample
// signals for the comb~ and the teeth~ call, non-finite input, overflow,
// clear(), block size independence and allocation freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>
#include "dsp/delay/comb.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::comb;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  // Runs `fn` over `in` in blocks of `block` samples.
  std::vector<float> Run(comb& fn, const std::vector<float>& in, unsigned int block = N) {
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

  // The direct form y(n) = a x(n) + b x(n-F) + c y(n-B), written here
  // independently of the class, with F and B read by linear interpolation.
  std::vector<double> Direct(double a, double b, double c, double f, double back,
                             const std::vector<float>& x) {
    std::vector<double> y(x.size(), 0.0);
    const auto past = [](const auto& v, std::size_t n, std::size_t k) -> double {
      return n >= k ? static_cast<double>(v[n - k]) : 0.0;
    };
    const auto tap = [&](const auto& v, std::size_t n, double d) -> double {
      const auto whole = static_cast<std::size_t>(d);
      const double frac = d - static_cast<double>(whole);
      return (1.0 - frac) * past(v, n, whole) + frac * past(v, n, whole + 1);
    };
    for (std::size_t n = 0; n < x.size(); ++n)
      y[n] = a * x[n] + b * tap(x, n, f) + c * tap(y, n, back);
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

  TEST_CASE("comb: defaults, clamping and memory (#1094)") {
    TestHelpers::ScopedSampleRate rate(48000);
    comb fn;
    CHECK(fn.feedforwardDelay() == 0.f);
    CHECK(fn.feedbackDelay() == 0.f);
    CHECK(fn.gain() == 0.f);
    CHECK(fn.feedforward() == 0.f);
    CHECK(fn.feedback() == 0.f);
    CHECK(fn.maximumDelay() == comb::DEFAULT_MAXIMUM_DELAY);
    CHECK(fn.capacity() == doctest::Approx(10.0));
    // Silent by default: all gains 0.
    const std::vector<float> silent = Run(fn, Noise(2 * N, 1));
    CHECK(*std::max_element(silent.begin(), silent.end()) == 0.f);
    CHECK(*std::min_element(silent.begin(), silent.end()) == 0.f);

    // A delay of 0 reads one sample back.
    CHECK(fn.feedforwardSamples() == 1.f);
    CHECK(fn.feedbackSamples() == 1.f);
    // delay() sets both; the separate setters one each.
    fn.delay(5.f);
    CHECK(fn.feedforwardSamples() == doctest::Approx(240.0));
    CHECK(fn.feedbackSamples() == doctest::Approx(240.0));
    fn.feedforwardDelay(2.f).feedbackDelay(3.f);
    CHECK(fn.feedforwardSamples() == doctest::Approx(96.0));
    CHECK(fn.feedbackSamples() == doctest::Approx(144.0));
    // Beyond the maximum: clamped to it; negative: one sample.
    fn.feedforwardDelay(50.f).feedbackDelay(-3.f);
    CHECK(fn.feedforwardDelay() == 50.f);
    CHECK(fn.feedforwardSamples() == doctest::Approx(480.0));
    CHECK(fn.feedbackSamples() == 1.f);

    // The feedback gain is clamped to -1 .. 1, the others are not.
    fn.feedback(1.5f);
    CHECK(fn.feedback() == 1.f);
    fn.feedback(-7.f);
    CHECK(fn.feedback() == -1.f);
    fn.gain(3.f).feedforward(-4.f);
    CHECK(fn.gain() == 3.f);
    CHECK(fn.feedforward() == -4.f);
    // Non-finite settings are ignored.
    fn.feedback(0.4f);
    fn.feedback(std::nanf(""));
    fn.gain(std::numeric_limits<float>::infinity());
    fn.feedforward(std::nanf(""));
    fn.delay(std::nanf(""));
    fn.feedforwardDelay(std::numeric_limits<float>::infinity());
    fn.feedbackDelay(-std::numeric_limits<float>::infinity());
    CHECK(fn.feedback() == 0.4f);
    CHECK(fn.gain() == 3.f);
    CHECK(fn.feedforward() == -4.f);
    CHECK(fn.feedforwardDelay() == 50.f);
    CHECK(fn.feedbackDelay() == -3.f);

    // The maximum delay: clamped at 0, the default when not finite.
    CHECK(comb(-5.f).maximumDelay() == 0.f);
    CHECK(comb(1e9f).maximumDelay() == comb::MAX_MAXIMUM_DELAY);
    CHECK(comb(std::nanf("")).maximumDelay() == comb::DEFAULT_MAXIMUM_DELAY);
    comb zero(0.f);
    zero.delay(3.f);
    CHECK(zero.feedforwardSamples() == 1.f);

    // Memory sized for a lower highest rate: above it, the longest delay
    // shrinks with the rate.
    comb small(10.f, 48000);
    YSE::SAMPLERATE = 96000;
    CHECK(small.capacity() == doctest::Approx(5.0));
    small.delay(10.f);
    CHECK(small.feedbackSamples() == doctest::Approx(480.0));
    CHECK(fn.capacity() == doctest::Approx(10.0));
  }

  TEST_CASE("comb: impulse response against the closed form (#1094)") {
    TestHelpers::ScopedSampleRate rate(48000);
    for (const float a : {0.f, 1.f, 0.5f}) {
      for (const float b : {0.f, 1.f, -0.7f}) {
        for (const float c : {0.f, 0.6f, -0.9f}) {
          CAPTURE(a);
          CAPTURE(b);
          CAPTURE(c);
          comb fn;
          fn.delay(1.f).gain(a).feedforward(b).feedback(c); // 48 samples
          const std::size_t d = 48;
          const std::vector<float> y = Run(fn, Impulse(8 * d + 5));
          // y(0) = a, y(kD) = (a c + b) c^(k-1), 0 between.
          for (std::size_t n = 0; n < y.size(); ++n) {
            CAPTURE(n);
            double want = 0.0;
            if (n == 0)
              want = a;
            else if (n % d == 0) {
              const std::size_t echo = n / d;
              want = (static_cast<double>(a) * c + b) *
                     std::pow(static_cast<double>(c), static_cast<double>(echo) - 1.0);
            }
            CHECK(static_cast<double>(y[n]) == doctest::Approx(want).epsilon(1e-6));
          }
        }
      }
    }

    // Separate delays: F = 30, B = 100 samples. y(0) = a, y(30) = b and
    // every 100 after them the same times c.
    comb fn;
    fn.feedforwardDelay(30.f / 48.f).feedbackDelay(100.f / 48.f);
    fn.gain(1.f).feedforward(0.5f).feedback(0.8f);
    REQUIRE(fn.feedforwardSamples() == doctest::Approx(30.0));
    REQUIRE(fn.feedbackSamples() == doctest::Approx(100.0));
    const std::vector<float> y = Run(fn, Impulse(600));
    for (std::size_t n = 0; n < y.size(); ++n) {
      CAPTURE(n);
      double want = 0.0;
      // The echo number: exact, n is a multiple of 100 (or 30 past one).
      if (n % 100 == 0) {
        const std::size_t echo = n / 100;
        want = std::pow(0.8, static_cast<double>(echo));
      }
      if (n >= 30 && (n - 30) % 100 == 0) {
        const std::size_t echo = (n - 30) / 100;
        want = 0.5 * std::pow(0.8, static_cast<double>(echo));
      }
      CHECK(static_cast<double>(y[n]) == doctest::Approx(want).epsilon(1e-5));
    }
  }

  TEST_CASE("comb: whole and fractional delays against an independent direct form (#1094)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::vector<float> noise = Noise(6 * N, 1094);
    struct Setting {
      float ff;
      float fb;
      float a;
      float b;
      float c;
    };
    for (const Setting s :
         {Setting{1.f, 1.f, 1.f, 1.f, 0.f}, Setting{0.37f, 0.37f, 0.5f, 0.f, 0.9f},
          Setting{3.3333f, 3.3333f, 0.3f, -0.6f, -0.7f}, Setting{0.02f, 7.1f, 1.f, 0.5f, 0.8f},
          Setting{9.9f, 0.5f, -0.4f, 1.2f, 0.99f}, Setting{0.f, 20.f, 0.2f, 0.3f, 0.5f}}) {
      CAPTURE(s.ff);
      CAPTURE(s.fb);
      comb fn;
      fn.feedforwardDelay(s.ff).feedbackDelay(s.fb).gain(s.a).feedforward(s.b).feedback(s.c);
      const double f = fn.feedforwardSamples();
      const double back = fn.feedbackSamples();
      CHECK(f == doctest::Approx(std::clamp(static_cast<double>(s.ff * 48.f), 1.0, 480.0)));
      CHECK(back == doctest::Approx(std::clamp(static_cast<double>(s.fb * 48.f), 1.0, 480.0)));
      const std::vector<float> got = Run(fn, noise);
      const std::vector<double> want = Direct(s.a, s.b, s.c, f, back, noise);
      double peak = 1.0;
      for (const double v : want)
        peak = std::max(peak, std::abs(v));
      CHECK(MaxAbsDiff(got, want) < 1e-5 * peak);
    }
  }

  TEST_CASE("comb: the feedforward comb notches and peaks on sines (#1094)") {
    TestHelpers::ScopedSampleRate rate(48000);
    // a = b = 1, D = 48 samples: |H| = 2 |cos(pi f D / fs)|, a notch at 500
    // Hz and a peak (2) at 1000 Hz.
    for (const double hz : {250.0, 500.0, 1000.0, 1500.0}) {
      CAPTURE(hz);
      comb fn;
      fn.delay(1.f).gain(1.f).feedforward(1.f);
      std::vector<float> in(40 * N);
      for (std::size_t i = 0; i < in.size(); ++i)
        in[i] = static_cast<float>(std::sin(2.0 * PI * hz * static_cast<double>(i) / 48000.0));
      const std::vector<float> out = Run(fn, in);
      const std::size_t from = out.size() - 4800;
      const double want = 2.0 * std::abs(std::cos(PI * hz * 48.0 / 48000.0)) * Rms(in, from);
      CHECK(Rms(out, from) == doctest::Approx(want).epsilon(1e-4).scale(1.0));
    }
  }

  TEST_CASE("comb: the delays follow the live sample rate (#1094)") {
    TestHelpers::ScopedSampleRate rate(48000);
    comb fn;
    fn.delay(2.f).feedforward(1.f).feedback(0.5f);
    std::vector<float> y = Run(fn, Impulse(400));
    CHECK(y[96] == doctest::Approx(1.0));
    CHECK(y[192] == doctest::Approx(0.5));
    YSE::SAMPLERATE = 96000;
    fn.clear();
    CHECK(fn.feedforwardSamples() == doctest::Approx(192.0));
    y = Run(fn, Impulse(400));
    CHECK(y[96] == 0.f);
    CHECK(y[192] == doctest::Approx(1.0));
    CHECK(y[384] == doctest::Approx(0.5));
    // Through the signal call too: 44.1 samples, between 44 and 45.
    YSE::SAMPLERATE = 44100;
    fn.clear();
    buffer in(N);
    buffer delay(N);
    buffer gain(N);
    buffer ff(N);
    buffer fb(N);
    in = 0.f;
    in.getPtr()[0] = 1.f;
    delay = 1.f;
    gain = 0.f;
    ff = 1.f;
    fb = 0.f;
    buffer& out = fn(in, delay, gain, ff, fb);
    CHECK(out.getPtr()[44] == doctest::Approx(0.9).epsilon(1e-4));
    CHECK(out.getPtr()[45] == doctest::Approx(0.1).epsilon(1e-3));
  }

  TEST_CASE("comb: signals are read per sample, in the comb~ and the teeth~ call (#1094)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::vector<float> noise = Noise(N, 3);
    buffer in(N);
    buffer ffDelay(N);
    buffer fbDelay(N);
    buffer gain(N);
    buffer ff(N);
    buffer fb(N);
    for (int i = 0; i < static_cast<int>(N); ++i) {
      const auto k = static_cast<float>(i);
      in.getPtr()[i] = noise[static_cast<std::size_t>(i)];
      ffDelay.getPtr()[i] = 0.5f + 0.03f * k;
      fbDelay.getPtr()[i] = 4.f - 0.02f * k;
      gain.getPtr()[i] = 1.f - 0.01f * k;
      ff.getPtr()[i] = -0.5f + 0.01f * k;
      fb.getPtr()[i] = -0.9f + 0.014f * k;
    }
    // Non-finite values are ignored, as the setters do.
    ffDelay.getPtr()[10] = std::nanf("");
    fb.getPtr()[11] = std::numeric_limits<float>::infinity();
    gain.getPtr()[12] = std::nanf("");

    // teeth~: every signal its own.
    {
      comb fn;
      comb reference;
      for (int block = 0; block < 3; ++block) {
        CAPTURE(block);
        buffer& out = fn(in, ffDelay, fbDelay, gain, ff, fb);
        REQUIRE(out.getLength() == N);
        for (int i = 0; i < static_cast<int>(N); ++i) {
          CAPTURE(i);
          reference.feedforwardDelay(ffDelay.getPtr()[i]).feedbackDelay(fbDelay.getPtr()[i]);
          reference.gain(gain.getPtr()[i]).feedforward(ff.getPtr()[i]).feedback(fb.getPtr()[i]);
          CHECK(out.getPtr()[i] == reference.process(in.getPtr()[i]));
        }
      }
      CHECK(fn.feedforwardDelay() == ffDelay.getPtr()[N - 1]);
      CHECK(fn.feedbackDelay() == fbDelay.getPtr()[N - 1]);
      CHECK(fn.gain() == gain.getPtr()[N - 1]);
      CHECK(fn.feedforward() == ff.getPtr()[N - 1]);
      CHECK(fn.feedback() == fb.getPtr()[N - 1]);
    }
    // comb~: one delay signal for both paths.
    {
      comb fn;
      comb reference;
      for (int block = 0; block < 2; ++block) {
        buffer& out = fn(in, fbDelay, gain, ff, fb);
        for (int i = 0; i < static_cast<int>(N); ++i) {
          CAPTURE(i);
          reference.delay(fbDelay.getPtr()[i]);
          reference.gain(gain.getPtr()[i]).feedforward(ff.getPtr()[i]).feedback(fb.getPtr()[i]);
          CHECK(out.getPtr()[i] == reference.process(in.getPtr()[i]));
        }
      }
      CHECK(fn.feedforwardDelay() == fbDelay.getPtr()[N - 1]);
      CHECK(fn.feedbackDelay() == fbDelay.getPtr()[N - 1]);
      // The shortest buffer sets the length.
      buffer shortGain(N / 2);
      shortGain = 0.2f;
      CHECK(fn(in, fbDelay, shortGain, ff, fb).getLength() == N / 2);
      CHECK(fn(in, ffDelay, fbDelay, gain, shortGain, fb).getLength() == N / 2);
    }
  }

  TEST_CASE("comb: non-finite input, overflow, feedback at 1 and clear() (#1094)") {
    TestHelpers::ScopedSampleRate rate(48000);
    comb fn;
    comb clean;
    fn.delay(0.5f).gain(1.f).feedforward(0.3f).feedback(0.6f);
    clean.delay(0.5f).gain(1.f).feedforward(0.3f).feedback(0.6f);
    for (int i = 0; i < 200; ++i) {
      CAPTURE(i);
      const float x = i == 0 ? 1.f : 0.f;
      float dirty = x;
      if (i == 5) dirty = std::nanf("");
      if (i == 6) dirty = std::numeric_limits<float>::infinity();
      CHECK(fn.process(dirty) == clean.process(x));
    }
    // An output that overflows a float is written as 0 rather than latched.
    comb huge;
    huge.delay(0.f).gain(3.f).feedforward(1.f);
    CHECK(huge.process(1e38f) == doctest::Approx(3e38));
    CHECK(huge.process(1e38f) == 0.f); // 3e38 + 1e38 overflows
    CHECK(huge.process(0.f) == doctest::Approx(1e38)); // not latched

    // Feedback at exactly 1: an echo that never decays, until clear().
    comb ring;
    ring.delay(1.f).feedforward(1.f).feedback(1.f);
    std::vector<float> y = Run(ring, Impulse(48 * 50 + 1));
    CHECK(y[48 * 50] == 1.f);
    ring.clear();
    y = Run(ring, std::vector<float>(4 * N, 0.f));
    CHECK(*std::max_element(y.begin(), y.end()) == 0.f);
    CHECK(*std::min_element(y.begin(), y.end()) == 0.f);

    // clear(): the same as a new filter.
    fn.clear();
    comb fresh;
    fresh.delay(0.5f).gain(1.f).feedforward(0.3f).feedback(0.6f);
    for (int i = 0; i < 100; ++i)
      CHECK(fn.process(i == 0 ? 1.f : 0.f) == fresh.process(i == 0 ? 1.f : 0.f));
  }

  TEST_CASE("comb: blocks, single samples and buffer clamping (#1094)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::vector<float> in = Noise(5 * N, 11);
    const auto setup = [](comb& fn) {
      fn.feedforwardDelay(1.7f).feedbackDelay(2.3f).gain(0.5f).feedforward(0.4f).feedback(0.7f);
    };
    std::vector<float> reference;
    {
      comb fn;
      setup(fn);
      reference = Run(fn, in);
    }
    for (const unsigned int block : {1u, 7u, 64u, N}) {
      CAPTURE(block);
      comb fn;
      setup(fn);
      CHECK(Run(fn, in, block) == reference);
    }
    comb single;
    setup(single);
    for (std::size_t i = 0; i < in.size(); ++i)
      CHECK(single.process(in[i]) == reference[i]);
    comb fn;
    buffer longer(2 * N);
    CHECK(fn(longer).getLength() == N);
  }

  TEST_CASE("comb: processing allocates nothing (#1094)") {
    TestHelpers::ScopedSampleRate rate(48000);
    comb fn(50.f);
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
      fn.delay(12.5f).gain(0.7f).feedforward(0.5f).feedback(0.8f);
      fn.feedforwardDelay(3.f).feedbackDelay(9.f);
      fn(in);
      fn(shorter);
      fn(in, delay, gain, gain, gain);
      fn(in, delay, delay, gain, gain, gain);
      fn.process(0.25f);
      fn.clear();
      fn(in);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
