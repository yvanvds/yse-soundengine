// Tests for YSE::DSP::crossover (YseEngine/dsp/filters/crossover.hpp), the
// third-order crossover behind ~cross (issue #1095): the magnitude response
// of both bands against the Butterworth closed form, the impulse response
// against an independent direct-form third-order filter, the flat sum (an
// allpass) on impulses and sines, -3 dB at the cutoff and 18 dB per octave,
// the cutoff clamping and the cutoff of 0, the live sample rate, the
// per-sample cutoff signal, non-finite input and settings, clear(), block
// size independence and allocation freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <random>
#include <vector>
#include "dsp/filters/crossover.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::crossover;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  struct Bands {
    std::vector<float> low;
    std::vector<float> high;
  };

  // Runs `fn` over `in` in blocks of `block` samples.
  Bands Run(crossover& fn, const std::vector<float>& in, unsigned int block = N) {
    Bands out;
    for (std::size_t start = 0; start < in.size(); start += block) {
      const auto length =
          static_cast<unsigned int>(std::min<std::size_t>(block, in.size() - start));
      buffer b(length);
      for (unsigned int i = 0; i < length; ++i)
        b.getPtr()[i] = in[start + i];
      buffer& result = fn(b);
      REQUIRE(&result == &fn.lowpass());
      REQUIRE(fn.lowpass().getLength() == length);
      REQUIRE(fn.highpass().getLength() == length);
      for (unsigned int i = 0; i < length; ++i) {
        out.low.push_back(fn.lowpass().getPtr()[i]);
        out.high.push_back(fn.highpass().getPtr()[i]);
      }
    }
    return out;
  }

  std::vector<float> Impulse(unsigned int samples) {
    std::vector<float> v(samples, 0.f);
    v[0] = 1.f;
    return v;
  }

  double K(double hz, double sr) {
    return std::tan(PI * hz / sr);
  }

  // The squared magnitude of a third-order Butterworth band at angular
  // frequency w (radians per sample), after the prewarped bilinear
  // transform: 1 / (1 + (tan(w / 2) / K)^6) for the lowpass.
  double LowPower(double w, double k) {
    const double r = std::pow(std::tan(w / 2.0) / k, 6.0);
    return 1.0 / (1.0 + r);
  }

  double HighPower(double w, double k) {
    const double r = std::pow(std::tan(w / 2.0) / k, 6.0);
    return r / (1.0 + r);
  }

  // The response of a section at angular frequency w.
  std::complex<double> Response(const crossover::section& s, double w) {
    const std::complex<double> z1 = std::polar(1.0, -w);
    return (s.b0 + s.b1 * z1 + s.b2 * z1 * z1) / (1.0 + s.a1 * z1 + s.a2 * z1 * z1);
  }

  // One band as a single third-order direct-form filter, its polynomials
  // expanded here from the analog prototype rather than taken from the
  // class: numerator K^3 (1 + z^-1)^3 or (1 - z^-1)^3 over
  // ((K + 1) + (K - 1) z^-1)((1 + K + K^2) + 2 (K^2 - 1) z^-1 + (1 - K + K^2) z^-2).
  struct DirectForm {
    std::array<double, 4> b{};
    std::array<double, 4> a{};
    std::array<double, 4> x{};
    std::array<double, 4> y{};

    DirectForm(double k, bool lowpass) {
      const std::array<double, 2> d1 = {k + 1.0, k - 1.0};
      const std::array<double, 3> d2 = {1.0 + k + k * k, 2.0 * (k * k - 1.0), 1.0 - k + k * k};
      for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 3; ++j)
          a[i + j] += d1[i] * d2[j];
      const double g = lowpass ? k * k * k : 1.0;
      const double sign = lowpass ? 1.0 : -1.0;
      b = {g, 3.0 * g * sign, 3.0 * g, g * sign};
      for (int i = 3; i >= 0; --i) {
        b[i] /= a[0];
        a[i] /= a[0];
      }
    }

    double operator()(double in) {
      for (int i = 3; i > 0; --i) {
        x[i] = x[i - 1];
        y[i] = y[i - 1];
      }
      x[0] = in;
      double out = b[0] * x[0];
      for (int i = 1; i < 4; ++i)
        out += b[i] * x[i] - a[i] * y[i];
      y[0] = out;
      return out;
    }
  };

  // The amplitude of each band and of their sum for a unit sine at `hz`
  // after `fn` at 48 kHz, projected onto a sine and a cosine over the second
  // half second, once the transient has died. Exact for an even `hz`, a
  // whole number of cycles there.
  struct SineGains {
    double low, high, sum;
  };

  SineGains Gains(crossover& fn, double hz) {
    const double w = 2.0 * PI * hz / 48000.0;
    constexpr unsigned int total = 48000;
    constexpr unsigned int measure = 24000;
    std::array<double, 6> acc{};
    for (unsigned int n = 0; n < total; ++n) {
      const crossover::outputs o = fn.process(static_cast<float>(std::sin(w * n)));
      if (n >= total - measure) {
        const double s = std::sin(w * n);
        const double c = std::cos(w * n);
        const double sum = static_cast<double>(o.lowpass) + o.highpass;
        acc[0] += o.lowpass * s;
        acc[1] += o.lowpass * c;
        acc[2] += o.highpass * s;
        acc[3] += o.highpass * c;
        acc[4] += sum * s;
        acc[5] += sum * c;
      }
    }
    const auto amp = [&](int i) {
      return 2.0 * std::sqrt(acc[i] * acc[i] + acc[i + 1] * acc[i + 1]) / measure;
    };
    return {amp(0), amp(2), amp(4)};
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("crossover: both bands' magnitude against the Butterworth closed form (#1095)") {
    const float sr = 48000.f;
    for (const float hz : {20.f, 200.f, 1000.f, 5000.f, 15000.f}) {
      CAPTURE(hz);
      const crossover::coefficients c = crossover::Coefficients(hz, sr);
      const double k = K(hz, sr);
      for (const double w : {0.001, 0.01, 0.05, 0.2, 0.5, 1.0, 2.0, 3.0}) {
        CAPTURE(w);
        const std::complex<double> low = Response(c.low1, w) * Response(c.low2, w);
        const std::complex<double> high = Response(c.high1, w) * Response(c.high2, w);
        CHECK(std::norm(low) == doctest::Approx(LowPower(w, k)).epsilon(1e-9));
        CHECK(std::norm(high) == doctest::Approx(HighPower(w, k)).epsilon(1e-9));
        // The sum is an allpass: a gain of exactly 1.
        CHECK(std::abs(low + high) == doctest::Approx(1.0).epsilon(1e-9));
      }
      // At the cutoff each band is 3 dB down.
      const double wc = 2.0 * PI * hz / sr;
      const std::complex<double> low = Response(c.low1, wc) * Response(c.low2, wc);
      const std::complex<double> high = Response(c.high1, wc) * Response(c.high2, wc);
      CHECK(std::abs(low) == doctest::Approx(std::sqrt(0.5)).epsilon(1e-9));
      CHECK(std::abs(high) == doctest::Approx(std::sqrt(0.5)).epsilon(1e-9));
      // Unity at 0 Hz and Nyquist for the bands that pass there, 0 for the
      // others.
      CHECK(std::abs(Response(c.low1, 0.0) * Response(c.low2, 0.0)) ==
            doctest::Approx(1.0).epsilon(1e-9));
      CHECK(std::abs(Response(c.high1, 0.0) * Response(c.high2, 0.0)) < 1e-12);
      CHECK(std::abs(Response(c.high1, PI) * Response(c.high2, PI)) ==
            doctest::Approx(1.0).epsilon(1e-9));
      CHECK(std::abs(Response(c.low1, PI) * Response(c.low2, PI)) < 1e-12);
      // Both bands share the denominators.
      CHECK(c.low1.a1 == c.high1.a1);
      CHECK(c.low2.a1 == c.high2.a1);
      CHECK(c.low2.a2 == c.high2.a2);
      CHECK(c.low1.b2 == 0.0);
      CHECK(c.low1.a2 == 0.0);
    }
  }

  TEST_CASE("crossover: defaults, clamping and invalid settings (#1095)") {
    TestHelpers::ScopedSampleRate rate(48000);
    crossover fn;
    CHECK(fn.cutoff() == crossover::DEFAULT_CUTOFF);
    CHECK(fn.cutoff() == 1000.f);
    CHECK(fn.coefficient().low1.b0 == crossover::Coefficients(1000.f, 48000.f).low1.b0);
    fn.cutoff(std::numeric_limits<float>::quiet_NaN());
    fn.cutoff(std::numeric_limits<float>::infinity());
    CHECK(fn.cutoff() == 1000.f);
    fn.cutoff(300.f);
    CHECK(fn.cutoff() == 300.f);

    // Above 0.49 of the sample rate the cutoff clamps there; below 0 to 0.
    const crossover::coefficients top = crossover::Coefficients(0.49f * 48000.f, 48000.f);
    CHECK(crossover::Coefficients(24000.f, 48000.f).low2.a1 == doctest::Approx(top.low2.a1));
    CHECK(crossover::Coefficients(1e9f, 48000.f).low1.b0 == doctest::Approx(top.low1.b0));
    const crossover::coefficients zero = crossover::Coefficients(0.f, 48000.f);
    CHECK(crossover::Coefficients(-50.f, 48000.f).low1.b0 == zero.low1.b0);
    CHECK(zero.low1.b0 == 0.0);
    CHECK(zero.low2.b0 == 0.0);
    CHECK(zero.high1.b0 == 1.0);
    // No sample rate or a NaN cutoff: as a cutoff of 0.
    CHECK(crossover::Coefficients(1000.f, 0.f).low1.b0 == 0.0);
    CHECK(crossover::Coefficients(std::nanf(""), 48000.f).low1.b0 == 0.0);
  }

  TEST_CASE("crossover: impulse response against an independent third-order direct form "
            "(#1095)") {
    TestHelpers::ScopedSampleRate rate(48000);
    for (const float hz : {80.f, 1000.f, 8000.f}) {
      CAPTURE(hz);
      crossover fn;
      fn.cutoff(hz);
      const Bands h = Run(fn, Impulse(4 * N));
      DirectForm low(K(hz, 48000.0), true);
      DirectForm high(K(hz, 48000.0), false);
      for (std::size_t n = 0; n < h.low.size(); ++n) {
        CAPTURE(n);
        const double x = n == 0 ? 1.0 : 0.0;
        CHECK(h.low[n] == doctest::Approx(low(x)).epsilon(1e-5).scale(1e-2));
        CHECK(h.high[n] == doctest::Approx(high(x)).epsilon(1e-5).scale(1e-2));
      }
    }
  }

  TEST_CASE("crossover: the bands sum to a second-order allpass (#1095)") {
    TestHelpers::ScopedSampleRate rate(48000);
    for (const float hz : {100.f, 2000.f, 12000.f}) {
      CAPTURE(hz);
      crossover fn;
      fn.cutoff(hz);
      const Bands h = Run(fn, Impulse(48000));
      // The sum's numerator is its denominator reversed:
      // (a2 + a1 z^-1 + z^-2) / (1 + a1 z^-1 + a2 z^-2), with the
      // denominator of the second-order section.
      const double k = K(hz, 48000.0);
      const double d = 1.0 + k + k * k;
      const double a1 = 2.0 * (k * k - 1.0) / d;
      const double a2 = (1.0 - k + k * k) / d;
      double x1 = 0.0;
      double x2 = 0.0;
      double y1 = 0.0;
      double y2 = 0.0;
      double energy = 0.0;
      for (std::size_t n = 0; n < h.low.size(); ++n) {
        CAPTURE(n);
        const double x = n == 0 ? 1.0 : 0.0;
        const double y = a2 * x + a1 * x1 + x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        const double sum = static_cast<double>(h.low[n]) + h.high[n];
        if (n < 512) CHECK(sum == doctest::Approx(y).epsilon(1e-5).scale(1e-2));
        energy += sum * sum;
      }
      // An allpass keeps the energy of the impulse.
      CHECK(energy == doctest::Approx(1.0).epsilon(1e-4));
    }
  }

  TEST_CASE("crossover: sine gains, -3 dB at the cutoff, 18 dB per octave and a flat sum "
            "(#1095)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const double hz = 1000.0;
    const double k = K(hz, 48000.0);
    for (const double f : {50.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 16000.0}) {
      CAPTURE(f);
      crossover fn;
      fn.cutoff(static_cast<float>(hz));
      const SineGains g = Gains(fn, f);
      const double w = 2.0 * PI * f / 48000.0;
      CHECK(g.low == doctest::Approx(std::sqrt(LowPower(w, k))).epsilon(1e-3).scale(1e-4));
      CHECK(g.high == doctest::Approx(std::sqrt(HighPower(w, k))).epsilon(1e-3).scale(1e-4));
      CHECK(g.sum == doctest::Approx(1.0).epsilon(1e-3));
    }
    crossover at;
    at.cutoff(1000.f);
    const SineGains c = Gains(at, 1000.0);
    CHECK(20.0 * std::log10(c.low) == doctest::Approx(-3.0103).epsilon(1e-3));
    CHECK(20.0 * std::log10(c.high) == doctest::Approx(-3.0103).epsilon(1e-3));
    // An octave apart, well past the cutoff and far below the Nyquist
    // frequency (where the bilinear transform steepens the lowpass): 18 dB
    // per octave.
    crossover a;
    crossover b;
    a.cutoff(50.f);
    b.cutoff(50.f);
    const double slopeLow = 20.0 * std::log10(Gains(a, 800.0).low / Gains(b, 1600.0).low);
    CHECK(slopeLow == doctest::Approx(18.06).epsilon(0.01));
    crossover p;
    crossover q;
    p.cutoff(1600.f);
    q.cutoff(1600.f);
    const double slopeHigh = 20.0 * std::log10(Gains(p, 100.0).high / Gains(q, 50.0).high);
    CHECK(slopeHigh == doctest::Approx(18.06).epsilon(0.01));
  }

  TEST_CASE("crossover: a cutoff of 0 silences the lowpass and passes the input on the "
            "highpass (#1095)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::vector<float> in(3 * N);
    for (std::size_t i = 0; i < in.size(); ++i)
      in[i] = 0.5f + std::sin(static_cast<float>(i) * 0.07f);
    crossover fn;
    // Run at a real cutoff first, so the state holds something.
    fn.cutoff(200.f);
    (void)Run(fn, in);
    fn.cutoff(0.f);
    const Bands b = Run(fn, in);
    for (std::size_t i = 0; i < in.size(); ++i) {
      CAPTURE(i);
      CHECK(b.low[i] == 0.f);
      CHECK(b.high[i] == in[i]);
    }
  }

  TEST_CASE("crossover: the coefficients follow the live sample rate (#1095)") {
    TestHelpers::ScopedSampleRate rate(48000);
    crossover fn;
    fn.cutoff(2000.f);
    CHECK(fn.coefficient().low2.a1 == crossover::Coefficients(2000.f, 48000.f).low2.a1);
    YSE::SAMPLERATE = 96000;
    CHECK(fn.coefficient().low2.a1 == crossover::Coefficients(2000.f, 96000.f).low2.a1);
    CHECK(fn.coefficient().low2.a1 != crossover::Coefficients(2000.f, 48000.f).low2.a1);
    fn.clear();
    const Bands h = Run(fn, Impulse(N));
    DirectForm low(K(2000.0, 96000.0), true);
    DirectForm high(K(2000.0, 96000.0), false);
    for (unsigned int n = 0; n < N; ++n) {
      CAPTURE(n);
      const double x = n == 0 ? 1.0 : 0.0;
      CHECK(h.low[n] == doctest::Approx(low(x)).epsilon(1e-5).scale(1e-2));
      CHECK(h.high[n] == doctest::Approx(high(x)).epsilon(1e-5).scale(1e-2));
    }
    // Above the new limit the cutoff clamps there.
    YSE::SAMPLERATE = 22050;
    fn.cutoff(15000.f);
    CHECK(fn.coefficient().low2.a1 ==
          doctest::Approx(crossover::Coefficients(0.49f * 22050.f, 22050.f).low2.a1));
  }

  TEST_CASE("crossover: a cutoff signal is read per sample (#1095)") {
    TestHelpers::ScopedSampleRate rate(48000);
    crossover fn;
    crossover reference;
    buffer in(N);
    buffer cutoff(N);
    for (unsigned int i = 0; i < N; ++i) {
      in.getPtr()[i] = std::sin(static_cast<float>(i) * 0.3f);
      cutoff.getPtr()[i] = 300.f + static_cast<float>(i) * 50.f;
    }
    // A NaN keeps the previous value for that sample.
    cutoff.getPtr()[10] = std::numeric_limits<float>::quiet_NaN();
    buffer& out = fn(in, cutoff);
    REQUIRE(&out == &fn.lowpass());
    REQUIRE(out.getLength() == N);
    REQUIRE(fn.highpass().getLength() == N);
    for (unsigned int i = 0; i < N; ++i) {
      CAPTURE(i);
      reference.cutoff(cutoff.getPtr()[i]);
      const crossover::outputs o = reference.process(in.getPtr()[i]);
      CHECK(fn.lowpass().getPtr()[i] == o.lowpass);
      CHECK(fn.highpass().getPtr()[i] == o.highpass);
      if (i == 10) CHECK(reference.cutoff() == cutoff.getPtr()[9]);
    }
    CHECK(fn.cutoff() == cutoff.getPtr()[N - 1]);
    // The per-sample cutoff actually changes the result.
    crossover fixed;
    fixed.cutoff(300.f);
    fixed(in);
    CHECK(fixed.lowpass().getPtr()[N - 1] != fn.lowpass().getPtr()[N - 1]);

    // A shorter control signal shortens the block.
    buffer shortCutoff(N / 2);
    shortCutoff = 1000.f;
    fn(in, shortCutoff);
    CHECK(fn.lowpass().getLength() == N / 2);
    CHECK(fn.highpass().getLength() == N / 2);
  }

  TEST_CASE("crossover: non-finite input is skipped, clear() resets the state (#1095)") {
    TestHelpers::ScopedSampleRate rate(48000);
    crossover fn;
    crossover clean;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const Bands dirty = Run(fn, {1.f, nan, 0.5f, inf, -inf, 0.f});
    const Bands reference = Run(clean, {1.f, 0.5f, 0.f});
    const std::array<int, 6> map = {0, 0, 1, 1, 1, 2};
    for (std::size_t i = 0; i < map.size(); ++i) {
      CAPTURE(i);
      CHECK(dirty.low[i] == reference.low[map[i]]);
      CHECK(dirty.high[i] == reference.high[map[i]]);
    }
    fn.clear();
    clean.clear();
    const Bands silent = Run(fn, {0.f});
    CHECK(silent.low[0] == 0.f);
    CHECK(silent.high[0] == 0.f);
    const crossover::outputs a = fn.process(1.f);
    const crossover::outputs b = clean.process(1.f);
    CHECK(a.lowpass == b.lowpass);
    CHECK(a.highpass == b.highpass);
  }

  TEST_CASE("crossover: stable on noise at every cutoff (#1095)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::mt19937 rng(1095); // NOLINT(cert-msc51-cpp,bugprone-random-generator-seed): repeatable
    std::uniform_real_distribution<float> dist(-1.f, 1.f);
    std::vector<float> noise(48000);
    for (float& v : noise)
      v = dist(rng);
    for (const float hz : {5.f, 10.f, 1000.f, 20000.f, 23520.f}) {
      CAPTURE(hz);
      crossover fn;
      fn.cutoff(hz);
      (void)Run(fn, noise);
      // Then silence: everything decays.
      const Bands tail = Run(fn, std::vector<float>(96000, 0.f));
      CHECK(std::abs(tail.low.back()) < 1e-6f);
      CHECK(std::abs(tail.high.back()) < 1e-6f);
    }
  }

  TEST_CASE("crossover: blocks, single samples and buffer clamping (#1095)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::vector<float> in(5 * N);
    for (std::size_t i = 0; i < in.size(); ++i)
      in[i] =
          std::sin(static_cast<float>(i) * 0.11f) + 0.3f * std::sin(static_cast<float>(i) * 1.7f);
    crossover whole;
    whole.cutoff(3000.f);
    const Bands reference = Run(whole, in);
    for (const unsigned int block : {1u, 7u, 64u, N}) {
      CAPTURE(block);
      crossover fn;
      fn.cutoff(3000.f);
      const Bands b = Run(fn, in, block);
      CHECK(b.low == reference.low);
      CHECK(b.high == reference.high);
    }
    crossover single;
    single.cutoff(3000.f);
    for (std::size_t i = 0; i < in.size(); ++i) {
      const crossover::outputs o = single.process(in[i]);
      CHECK(o.lowpass == reference.low[i]);
      CHECK(o.highpass == reference.high[i]);
    }

    // A longer buffer is processed up to STANDARD_BUFFERSIZE.
    crossover fn;
    buffer longer(2 * N);
    longer = 1.f;
    CHECK(fn(longer).getLength() == N);
    CHECK(fn.highpass().getLength() == N);
  }

  TEST_CASE("crossover: processing allocates nothing (#1095)") {
    TestHelpers::ScopedSampleRate rate(48000);
    crossover fn;
    buffer in(N);
    buffer shorter(N / 2);
    buffer cutoff(N);
    in = 0.5f;
    shorter = 0.25f;
    for (unsigned int i = 0; i < N; ++i)
      cutoff.getPtr()[i] = 500.f + static_cast<float>(i);
    fn(in);
    {
      TestHelpers::ProbeScope probe;
      fn.cutoff(1200.f);
      fn(in);
      fn(shorter);
      fn(in, cutoff);
      (void)fn.process(0.1f);
      (void)fn.coefficient();
      fn.cutoff(0.f);
      fn(in);
      fn.clear();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
