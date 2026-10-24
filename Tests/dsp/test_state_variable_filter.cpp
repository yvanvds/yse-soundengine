// Tests for YSE::DSP::stateVariableFilter
// (YseEngine/dsp/filters/stateVariableFilter.hpp), the filter behind ~svf
// (issue #1089): the frequency modes and coefficients against their closed
// form, the four impulse responses against independent bilinear-transform
// biquads, the gains on sines against the analog prototype's magnitude
// response, stability at every setting, the live sample rate, the per-sample
// frequency and resonance signals, non-finite input and settings, clear(),
// block size independence and allocation freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <random>
#include <vector>
#include "dsp/filters/stateVariableFilter.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using svf = YSE::DSP::stateVariableFilter;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  enum output { LP = 0, HP = 1, BP = 2, NOTCH = 3 };

  // The four outputs of a run, in output order.
  using Outputs = std::array<std::vector<float>, 4>;

  // Runs `fn` over `in` in blocks of `block` samples.
  Outputs Run(svf& fn, const std::vector<float>& in, unsigned int block = N) {
    Outputs out;
    for (std::size_t start = 0; start < in.size(); start += block) {
      const auto length =
          static_cast<unsigned int>(std::min<std::size_t>(block, in.size() - start));
      buffer b(length);
      for (unsigned int i = 0; i < length; ++i)
        b.getPtr()[i] = in[start + i];
      buffer& result = fn(b);
      REQUIRE(&result == &fn.lowpass());
      const std::array<buffer*, 4> bufs{&fn.lowpass(), &fn.highpass(), &fn.bandpass(), &fn.notch()};
      for (int o = 0; o < 4; ++o) {
        REQUIRE(bufs[o]->getLength() == length);
        for (unsigned int i = 0; i < length; ++i)
          out[o].push_back(bufs[o]->getPtr()[i]);
      }
    }
    return out;
  }

  std::vector<float> Impulse(unsigned int samples) {
    std::vector<float> v(samples, 0.f);
    v[0] = 1.f;
    return v;
  }

  // The damping and the prewarped g, computed here from the header's
  // definitions, independently of the class.
  double Damping(double resonance) {
    return std::max(2.0 * (1.0 - std::clamp(resonance, 0.0, 1.0)), 0.001);
  }

  double WarpedG(double hz, double sr) {
    return std::tan(PI * hz / sr);
  }

  // The analog prototype s^2 + k s + 1 through the bilinear transform with
  // s = (1/g) (1 - z^-1) / (1 + z^-1): a biquad per output.
  struct Biquad {
    double b0, b1, b2, a1, a2; // a0 normalised to 1
  };

  std::array<Biquad, 4> Bilinear(double g, double k) {
    const double d0 = 1.0 + k * g + g * g;
    const double d1 = 2.0 * g * g - 2.0;
    const double d2 = 1.0 - k * g + g * g;
    const double a1 = d1 / d0;
    const double a2 = d2 / d0;
    std::array<Biquad, 4> f{};
    f[LP] = {g * g / d0, 2.0 * g * g / d0, g * g / d0, a1, a2};
    f[HP] = {1.0 / d0, -2.0 / d0, 1.0 / d0, a1, a2};
    f[BP] = {g / d0, 0.0, -g / d0, a1, a2};
    f[NOTCH] = {(1.0 + g * g) / d0, (2.0 * g * g - 2.0) / d0, (1.0 + g * g) / d0, a1, a2};
    return f;
  }

  std::vector<double> Direct(const Biquad& f, const std::vector<float>& x) {
    std::vector<double> y(x.size(), 0.0);
    double x1 = 0.0;
    double x2 = 0.0;
    double y1 = 0.0;
    double y2 = 0.0;
    for (std::size_t n = 0; n < x.size(); ++n) {
      const double v = f.b0 * x[n] + f.b1 * x1 + f.b2 * x2 - f.a1 * y1 - f.a2 * y2;
      x2 = x1;
      x1 = x[n];
      y2 = y1;
      y1 = v;
      y[n] = v;
    }
    return y;
  }

  // The analog prototype's magnitude at digital angular frequency w.
  double Magnitude(output o, double g, double k, double w) {
    const std::complex<double> s(0.0, std::tan(w / 2.0) / g);
    const std::complex<double> den = s * s + k * s + 1.0;
    switch (o) {
    case LP:
      return std::abs(1.0 / den);
    case HP:
      return std::abs(s * s / den);
    case BP:
      return std::abs(s / den);
    case NOTCH:
    default:
      return std::abs((s * s + 1.0) / den);
    }
  }

  // The amplitude of a unit sine at `hz` on each output at 48 kHz, projected
  // onto a sine and a cosine over the second half second.
  std::array<double, 4> SineGain(svf& fn, double hz) {
    const double w = 2.0 * PI * hz / 48000.0;
    constexpr int total = 48000;
    constexpr int measure = 24000;
    std::array<double, 4> s{};
    std::array<double, 4> c{};
    for (int n = 0; n < total; ++n) {
      const svf::outputs y = fn.process(static_cast<float>(std::sin(w * n)));
      if (n >= total - measure) {
        const std::array<float, 4> v{y.lowpass, y.highpass, y.bandpass, y.notch};
        for (int o = 0; o < 4; ++o) {
          s[o] += v[o] * std::sin(w * n);
          c[o] += v[o] * std::cos(w * n);
        }
      }
    }
    std::array<double, 4> gain{};
    for (int o = 0; o < 4; ++o)
      gain[o] = 2.0 * std::sqrt(s[o] * s[o] + c[o] * c[o]) / measure;
    return gain;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("stateVariableFilter: frequency modes, damping and coefficients against their "
            "closed form (#1089)") {
    const float sr = 48000.f;
    // Hz: clamped to 0..SR/4.
    CHECK(svf::CenterFrequency(1000.f, svf::HZ, sr) == 1000.0);
    CHECK(svf::CenterFrequency(-5.f, svf::HZ, sr) == 0.0);
    CHECK(svf::CenterFrequency(20000.f, svf::HZ, sr) == 12000.0);
    // Linear: 0..1 across 0..SR/4.
    CHECK(svf::CenterFrequency(0.5f, svf::LINEAR, sr) == doctest::Approx(6000.0));
    CHECK(svf::CenterFrequency(1.f, svf::LINEAR, sr) == doctest::Approx(12000.0));
    CHECK(svf::CenterFrequency(2.f, svf::LINEAR, sr) == doctest::Approx(12000.0));
    CHECK(svf::CenterFrequency(-1.f, svf::LINEAR, sr) == 0.0);
    // Radians: v = sin(2 pi fc / SR).
    for (const float v : {0.f, 0.1f, 0.5f, 0.9f, 1.f}) {
      CAPTURE(v);
      const double fc = svf::CenterFrequency(v, svf::RADIANS, sr);
      CHECK(std::sin(2.0 * PI * fc / 48000.0) == doctest::Approx(v).epsilon(1e-6));
    }
    CHECK(svf::CenterFrequency(1.f, svf::RADIANS, sr) == doctest::Approx(12000.0));
    CHECK(svf::CenterFrequency(3.f, svf::RADIANS, sr) == doctest::Approx(12000.0));
    // No sample rate or a non-finite value: 0.
    CHECK(svf::CenterFrequency(1000.f, svf::HZ, 0.f) == 0.0);
    CHECK(svf::CenterFrequency(std::numeric_limits<float>::quiet_NaN(), svf::LINEAR, sr) == 0.0);

    for (const float hz : {0.f, 50.f, 1000.f, 8000.f, 12000.f}) {
      for (const float r : {0.f, 0.01f, 0.5f, 0.9f, 1.f}) {
        CAPTURE(hz);
        CAPTURE(r);
        const svf::coefficients c = svf::Coefficients(hz, svf::HZ, r, sr);
        const double g = WarpedG(hz, 48000.0);
        const double k = Damping(r);
        CHECK(c.g == doctest::Approx(g).epsilon(1e-12));
        CHECK(c.k == doctest::Approx(k).epsilon(1e-12));
        CHECK(c.a1 == doctest::Approx(1.0 / (1.0 + g * (g + k))).epsilon(1e-12));
        CHECK(c.a2 == doctest::Approx(g / (1.0 + g * (g + k))).epsilon(1e-12));
        CHECK(c.a3 == doctest::Approx(g * g / (1.0 + g * (g + k))).epsilon(1e-12));
      }
    }
    // The damping: 2 at 0, 1 at 0.5, MIN_DAMPING at 1; clamped outside.
    CHECK(svf::Coefficients(1000.f, svf::HZ, 0.f, sr).k == 2.0);
    CHECK(svf::Coefficients(1000.f, svf::HZ, 0.5f, sr).k == 1.0);
    CHECK(svf::Coefficients(1000.f, svf::HZ, 1.f, sr).k == svf::MIN_DAMPING);
    CHECK(svf::Coefficients(1000.f, svf::HZ, 5.f, sr).k == svf::MIN_DAMPING);
    CHECK(svf::Coefficients(1000.f, svf::HZ, -1.f, sr).k == 2.0);
    CHECK(svf::Coefficients(1000.f, svf::HZ, std::numeric_limits<float>::quiet_NaN(), sr).k == 2.0);
    // Linear and radians agree with Hz at the frequency they mean.
    CHECK(svf::Coefficients(0.25f, svf::LINEAR, 0.3f, sr).g ==
          doctest::Approx(svf::Coefficients(3000.f, svf::HZ, 0.3f, sr).g).epsilon(1e-9));
    CHECK(svf::Coefficients(1.f, svf::RADIANS, 0.3f, sr).g == doctest::Approx(1.0).epsilon(1e-9));
  }

  TEST_CASE("stateVariableFilter: defaults and invalid settings (#1089)") {
    TestHelpers::ScopedSampleRate rate(48000);
    svf fn;
    CHECK(fn.frequency() == 0.f);
    CHECK(fn.resonance() == 0.f);
    CHECK(fn.mode() == svf::HZ);
    // Frequency 0: lowpass and bandpass silent, highpass and notch the input.
    std::vector<float> in(N);
    for (int i = 0; i < static_cast<int>(N); ++i)
      in[i] = std::sin(static_cast<float>(i) * 0.37f);
    const Outputs out = Run(fn, in);
    for (int i = 0; i < static_cast<int>(N); ++i) {
      CAPTURE(i);
      CHECK(out[LP][i] == 0.f);
      CHECK(out[BP][i] == 0.f);
      CHECK(out[HP][i] == in[i]);
      CHECK(out[NOTCH][i] == in[i]);
    }
    // Reaching 0 from a running filter lets the state go: no frozen constant
    // left on the lowpass or bandpass, in the block and the per-sample paths.
    for (const bool perSample : {false, true}) {
      CAPTURE(perSample);
      svf running;
      running.frequency(800.f).resonance(0.9f);
      (void)Run(running, std::vector<float>(N, 1.f));
      REQUIRE(running.lowpass().getPtr()[N - 1] != 0.f);
      buffer x(N);
      buffer f(N);
      buffer r(N);
      for (int i = 0; i < static_cast<int>(N); ++i)
        x.getPtr()[i] = in[i];
      f = 0.f;
      r = 0.9f;
      if (perSample) {
        running(x, f, r);
      } else {
        running.frequency(0.f);
        running(x);
      }
      for (int i = 0; i < static_cast<int>(N); ++i) {
        CAPTURE(i);
        CHECK(running.lowpass().getPtr()[i] == 0.f);
        CHECK(running.bandpass().getPtr()[i] == 0.f);
        CHECK(running.highpass().getPtr()[i] == in[i]);
        CHECK(running.notch().getPtr()[i] == in[i]);
      }
    }
    fn.frequency(500.f).resonance(0.4f).mode(svf::LINEAR);
    fn.frequency(std::numeric_limits<float>::quiet_NaN())
        .resonance(std::numeric_limits<float>::infinity())
        .mode(static_cast<svf::FREQUENCY_MODE>(7));
    CHECK(fn.frequency() == 500.f);
    CHECK(fn.resonance() == 0.4f);
    CHECK(fn.mode() == svf::LINEAR);
  }

  TEST_CASE("stateVariableFilter: the four impulse responses against bilinear biquads "
            "(#1089)") {
    TestHelpers::ScopedSampleRate rate(48000);
    for (const float r : {0.f, 0.01f, 0.5f, 0.95f, 1.f}) {
      for (const float hz : {30.f, 1000.f, 6000.f, 12000.f}) {
        CAPTURE(r);
        CAPTURE(hz);
        svf fn;
        fn.frequency(hz).resonance(r);
        const std::vector<float> x = Impulse(4 * N);
        const std::array<Biquad, 4> f = Bilinear(WarpedG(hz, 48000.0), Damping(r));
        const Outputs h = Run(fn, x);
        for (int o = 0; o < 4; ++o) {
          CAPTURE(o);
          const std::vector<double> expected = Direct(f[o], x);
          for (std::size_t n = 0; n < h[o].size(); ++n) {
            CAPTURE(n);
            CHECK(h[o][n] == doctest::Approx(expected[n]).epsilon(1e-5));
          }
        }
      }
    }
    // The same in linear and radians modes.
    for (const svf::FREQUENCY_MODE m : {svf::LINEAR, svf::RADIANS}) {
      CAPTURE(m);
      svf fn;
      fn.mode(m).frequency(0.3f).resonance(0.7f);
      const double hz = svf::CenterFrequency(0.3f, m, 48000.f);
      const std::vector<float> x = Impulse(N);
      const std::array<Biquad, 4> f = Bilinear(WarpedG(hz, 48000.0), Damping(0.7));
      const Outputs h = Run(fn, x);
      for (int o = 0; o < 4; ++o) {
        const std::vector<double> expected = Direct(f[o], x);
        for (int n = 0; n < static_cast<int>(N); ++n)
          CHECK(h[o][n] == doctest::Approx(expected[n]).epsilon(1e-5));
      }
    }
  }

  TEST_CASE("stateVariableFilter: sine gains follow the magnitude response on all four "
            "outputs (#1089)") {
    TestHelpers::ScopedSampleRate rate(48000);
    for (const float r : {0.01f, 0.5f, 0.9f}) {
      for (const double hz : {100.0, 500.0, 1000.0, 2000.0, 10000.0}) {
        CAPTURE(r);
        CAPTURE(hz);
        svf fn;
        fn.frequency(1000.f).resonance(r);
        const std::array<double, 4> gain = SineGain(fn, hz);
        const double g = WarpedG(1000.0, 48000.0);
        const double k = Damping(r);
        const double w = 2.0 * PI * hz / 48000.0;
        for (int o = 0; o < 4; ++o) {
          CAPTURE(o);
          CHECK(gain[o] == doctest::Approx(Magnitude(static_cast<output>(o), g, k, w))
                               .epsilon(2e-3)
                               .scale(1e-3));
        }
      }
    }
    // At the center: lowpass, highpass and bandpass at the Q (1/k), the notch
    // at 0; lowpass + highpass + bandpass * k is the input.
    svf fn;
    fn.frequency(1000.f).resonance(0.75f); // k 0.5, Q 2
    const std::array<double, 4> center = SineGain(fn, 1000.0);
    CHECK(center[LP] == doctest::Approx(2.0).epsilon(2e-3));
    CHECK(center[HP] == doctest::Approx(2.0).epsilon(2e-3));
    CHECK(center[BP] == doctest::Approx(2.0).epsilon(2e-3));
    CHECK(center[NOTCH] < 1e-3);
    svf sum;
    sum.frequency(2500.f).resonance(0.6f);
    for (int n = 0; n < 2000; ++n) {
      const float x =
          std::sin(static_cast<float>(n) * 0.05f) + 0.2f * std::sin(static_cast<float>(n) * 1.3f);
      const svf::outputs y = sum.process(x);
      CHECK(y.lowpass + y.highpass + 0.8f * y.bandpass == doctest::Approx(x).epsilon(1e-4));
      CHECK(y.notch == doctest::Approx(y.lowpass + y.highpass).epsilon(1e-5));
    }
    // Lowpass DC gain 1, highpass and bandpass 0.
    svf dc;
    dc.frequency(300.f).resonance(0.3f);
    const Outputs step = Run(dc, std::vector<float>(40 * N, 1.f));
    CHECK(step[LP].back() == doctest::Approx(1.0).epsilon(1e-5));
    CHECK(std::fabs(step[HP].back()) < 1e-5);
    CHECK(std::fabs(step[BP].back()) < 1e-5);
    CHECK(step[NOTCH].back() == doctest::Approx(1.0).epsilon(1e-5));
  }

  TEST_CASE("stateVariableFilter: stable at every frequency and resonance (#1089)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::mt19937 gen(1089); // NOLINT(cert-msc51-cpp,bugprone-random-generator-seed): repeatable
    std::uniform_real_distribution<float> noise(-1.f, 1.f);
    constexpr int noiseBlocks = 48000 * 3 / static_cast<int>(N);
    constexpr int silenceBlocks = 48000 * 5 / static_cast<int>(N);
    for (const float r : {0.f, 0.5f, 0.99f, 1.f, 100.f}) {
      for (const float hz : {1.f, 100.f, 5000.f, 11999.f, 12000.f, 1e6f}) {
        CAPTURE(r);
        CAPTURE(hz);
        svf fn;
        fn.frequency(hz).resonance(r);
        const svf::coefficients c = fn.coefficient();
        // Pole radius of the equivalent biquad below 1.
        const double d0 = 1.0 + c.k * c.g + c.g * c.g;
        const double radius = std::sqrt((1.0 - c.k * c.g + c.g * c.g) / d0);
        CHECK(radius < 1.0);
        // The output on an input bounded by 1 is bounded by the l1 norm of
        // the impulse response over the run.
        const std::array<Biquad, 4> f = Bilinear(c.g, c.k);
        std::array<double, 4> bound{};
        const std::vector<float> impulse = Impulse((noiseBlocks + silenceBlocks) * N);
        for (int o = 0; o < 4; ++o) {
          const std::vector<double> h = Direct(f[o], impulse);
          for (const double v : h)
            bound[o] += std::fabs(v);
          bound[o] = bound[o] * (1.0 + 1e-4) + 1e-6;
        }
        std::array<double, 4> peak{};
        buffer in(N);
        for (int block = 0; block < noiseBlocks; ++block) {
          for (int i = 0; i < static_cast<int>(N); ++i)
            in.getPtr()[i] = noise(gen);
          fn(in);
          const std::array<buffer*, 4> bufs{&fn.lowpass(), &fn.highpass(), &fn.bandpass(),
                                            &fn.notch()};
          for (int o = 0; o < 4; ++o)
            for (int i = 0; i < static_cast<int>(N); ++i)
              peak[o] = std::max(peak[o], static_cast<double>(std::fabs(bufs[o]->getPtr()[i])));
        }
        buffer silence(N);
        silence = 0.f;
        std::array<double, 4> last{};
        for (int block = 0; block < silenceBlocks; ++block) {
          fn(silence);
          const std::array<buffer*, 4> bufs{&fn.lowpass(), &fn.highpass(), &fn.bandpass(),
                                            &fn.notch()};
          for (int o = 0; o < 4; ++o) {
            last[o] = 0.0;
            for (int i = 0; i < static_cast<int>(N); ++i)
              last[o] = std::max(last[o], static_cast<double>(std::fabs(bufs[o]->getPtr()[i])));
          }
        }
        for (int o = 0; o < 4; ++o) {
          CAPTURE(o);
          CHECK(std::isfinite(peak[o]));
          CHECK(peak[o] <= bound[o]);
          CHECK(std::isfinite(last[o]));
          CHECK(last[o] <= bound[o]);
          // Where the ringing's time constant is well under the silence, it
          // dies.
          if (1.0 / (1.0 - radius) < 24000.0) CHECK(last[o] <= 1e-5 * std::max(peak[o], 1.0));
        }
      }
    }
  }

  TEST_CASE("stateVariableFilter: the coefficients follow the live sample rate (#1089)") {
    TestHelpers::ScopedSampleRate rate(48000);
    svf fn;
    fn.frequency(2000.f).resonance(0.6f);
    CHECK(fn.coefficient().g == doctest::Approx(WarpedG(2000.0, 48000.0)));
    YSE::SAMPLERATE = 96000;
    CHECK(fn.coefficient().g == doctest::Approx(WarpedG(2000.0, 96000.0)).epsilon(1e-12));
    fn.clear();
    const std::vector<float> x = Impulse(N);
    const std::array<Biquad, 4> f = Bilinear(WarpedG(2000.0, 96000.0), Damping(0.6));
    const Outputs h = Run(fn, x);
    for (int o = 0; o < 4; ++o) {
      const std::vector<double> expected = Direct(f[o], x);
      for (int n = 0; n < 32; ++n)
        CHECK(h[o][n] == doctest::Approx(expected[n]).epsilon(1e-5));
    }
    // The quarter-rate clamp moves with the rate, as does linear mode.
    YSE::SAMPLERATE = 22050;
    fn.frequency(15000.f);
    CHECK(fn.coefficient().g == doctest::Approx(1.0).epsilon(1e-9));
    fn.mode(svf::LINEAR).frequency(0.5f);
    CHECK(fn.coefficient().g == doctest::Approx(WarpedG(22050.0 / 8.0, 22050.0)).epsilon(1e-9));
  }

  TEST_CASE("stateVariableFilter: frequency and resonance signals are read per sample "
            "(#1089)") {
    TestHelpers::ScopedSampleRate rate(48000);
    svf fn;
    svf reference;
    buffer in(N);
    buffer frequency(N);
    buffer resonance(N);
    std::vector<float> x(N);
    for (int i = 0; i < static_cast<int>(N); ++i) {
      x[i] = std::sin(static_cast<float>(i) * 0.3f);
      in.getPtr()[i] = x[i];
      frequency.getPtr()[i] = 300.f + static_cast<float>(i) * 50.f;
      resonance.getPtr()[i] = static_cast<float>(i) / static_cast<float>(N);
    }
    // A non-finite value keeps the previous value for that sample.
    frequency.getPtr()[10] = std::numeric_limits<float>::quiet_NaN();
    resonance.getPtr()[20] = std::numeric_limits<float>::infinity();
    buffer& out = fn(in, frequency, resonance);
    REQUIRE(&out == &fn.lowpass());
    REQUIRE(out.getLength() == N);
    // An independent TPT loop over the same per-sample settings.
    double s1 = 0.0;
    double s2 = 0.0;
    float hz = 0.f;
    float r = 0.f;
    for (int i = 0; i < static_cast<int>(N); ++i) {
      CAPTURE(i);
      if (std::isfinite(frequency.getPtr()[i])) hz = frequency.getPtr()[i];
      if (std::isfinite(resonance.getPtr()[i])) r = resonance.getPtr()[i];
      const double g = WarpedG(std::min(static_cast<double>(hz), 12000.0), 48000.0);
      const double k = Damping(r);
      const double a1 = 1.0 / (1.0 + g * (g + k));
      const double v3 = x[i] - s2;
      const double v1 = a1 * s1 + g * a1 * v3;
      const double v2 = s2 + g * a1 * s1 + g * g * a1 * v3;
      s1 = 2.0 * v1 - s1;
      s2 = 2.0 * v2 - s2;
      CHECK(fn.lowpass().getPtr()[i] == doctest::Approx(v2).epsilon(1e-5));
      CHECK(fn.bandpass().getPtr()[i] == doctest::Approx(v1).epsilon(1e-5));
      CHECK(fn.highpass().getPtr()[i] == doctest::Approx(x[i] - k * v1 - v2).epsilon(1e-5));
      CHECK(fn.notch().getPtr()[i] == doctest::Approx(x[i] - k * v1).epsilon(1e-5));
      // The same as setting the values one sample at a time.
      reference.frequency(frequency.getPtr()[i]).resonance(resonance.getPtr()[i]);
      const svf::outputs y = reference.process(x[i]);
      CHECK(y.lowpass == fn.lowpass().getPtr()[i]);
      CHECK(y.highpass == fn.highpass().getPtr()[i]);
      CHECK(y.bandpass == fn.bandpass().getPtr()[i]);
      CHECK(y.notch == fn.notch().getPtr()[i]);
    }
    CHECK(fn.frequency() == frequency.getPtr()[N - 1]);
    CHECK(fn.resonance() == resonance.getPtr()[N - 1]);

    // A shorter control signal shortens the block, on all four outputs.
    buffer shortResonance(N / 2);
    shortResonance = 0.3f;
    CHECK(fn(in, frequency, shortResonance).getLength() == N / 2);
    CHECK(fn.highpass().getLength() == N / 2);
    CHECK(fn.bandpass().getLength() == N / 2);
    CHECK(fn.notch().getLength() == N / 2);
  }

  TEST_CASE("stateVariableFilter: non-finite input is skipped, clear() resets the state "
            "(#1089)") {
    TestHelpers::ScopedSampleRate rate(48000);
    svf fn;
    fn.frequency(3000.f).resonance(0.8f);
    svf clean;
    clean.frequency(3000.f).resonance(0.8f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const Outputs dirty = Run(fn, {1.f, nan, 0.5f, inf, -inf, 0.f, 0.f});
    const Outputs reference = Run(clean, {1.f, 0.5f, 0.f, 0.f});
    for (int o = 0; o < 4; ++o) {
      CAPTURE(o);
      CHECK(dirty[o][0] == reference[o][0]);
      CHECK(dirty[o][1] == reference[o][0]); // holds
      CHECK(dirty[o][2] == reference[o][1]);
      CHECK(dirty[o][3] == reference[o][1]);
      CHECK(dirty[o][4] == reference[o][1]);
      CHECK(dirty[o][5] == reference[o][2]);
      CHECK(dirty[o][6] == reference[o][3]);
    }
    fn.clear();
    clean.clear();
    const Outputs zero = Run(fn, {0.f});
    for (int o = 0; o < 4; ++o)
      CHECK(zero[o][0] == 0.f);
    for (int i = 0; i < 4; ++i) {
      const svf::outputs a = fn.process(i == 0 ? 1.f : 0.f);
      const svf::outputs b = clean.process(i == 0 ? 1.f : 0.f);
      CHECK(a.lowpass == b.lowpass);
      CHECK(a.highpass == b.highpass);
      CHECK(a.bandpass == b.bandpass);
      CHECK(a.notch == b.notch);
    }
  }

  TEST_CASE("stateVariableFilter: blocks, single samples and buffer clamping (#1089)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::vector<float> in(5 * N);
    for (std::size_t i = 0; i < in.size(); ++i)
      in[i] =
          std::sin(static_cast<float>(i) * 0.11f) + 0.3f * std::sin(static_cast<float>(i) * 1.7f);
    svf whole;
    whole.frequency(3000.f).resonance(0.85f);
    const Outputs reference = Run(whole, in);
    for (const unsigned int block : {1u, 7u, 64u, N}) {
      CAPTURE(block);
      svf fn;
      fn.frequency(3000.f).resonance(0.85f);
      const Outputs out = Run(fn, in, block);
      for (int o = 0; o < 4; ++o)
        CHECK(out[o] == reference[o]);
    }
    svf single;
    single.frequency(3000.f).resonance(0.85f);
    for (std::size_t i = 0; i < in.size(); ++i) {
      const svf::outputs y = single.process(in[i]);
      CHECK(y.lowpass == reference[LP][i]);
      CHECK(y.highpass == reference[HP][i]);
      CHECK(y.bandpass == reference[BP][i]);
      CHECK(y.notch == reference[NOTCH][i]);
    }

    // A longer buffer is processed up to STANDARD_BUFFERSIZE.
    svf fn;
    buffer longer(2 * N);
    longer = 1.f;
    CHECK(fn(longer).getLength() == N);
    CHECK(fn.notch().getLength() == N);
  }

  TEST_CASE("stateVariableFilter: processing allocates nothing (#1089)") {
    TestHelpers::ScopedSampleRate rate(48000);
    svf fn;
    buffer in(N);
    buffer shorter(N / 2);
    buffer frequency(N);
    buffer resonance(N);
    in = 0.5f;
    shorter = 0.25f;
    resonance = 0.7f;
    for (int i = 0; i < static_cast<int>(N); ++i)
      frequency.getPtr()[i] = 500.f + static_cast<float>(i);
    fn(in);
    {
      TestHelpers::ProbeScope probe;
      fn.frequency(1200.f).resonance(0.3f).mode(svf::RADIANS);
      fn(in);
      fn(shorter);
      fn.mode(svf::HZ);
      fn(in, frequency, resonance);
      (void)fn.process(0.1f);
      (void)fn.coefficient();
      fn.clear();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
