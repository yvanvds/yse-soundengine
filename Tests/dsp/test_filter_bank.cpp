// Tests for YSE::DSP::filterBank (YseEngine/dsp/filters/filterBank.hpp), the
// bank of parallel bandpasses behind ~fffb (issue #1096): the defaults and
// the size clamping, the coefficients against filterCoefficients' bandpass,
// each filter's impulse response against an independent direct-form
// recursion, the gain at the center frequency and the magnitude response off
// it on sines, the independence of the filters, the setters and the spreads,
// the live sample rate, non-finite input and settings, clear(), block size
// independence and allocation freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <random>
#include <vector>
#include "dsp/filters/filterBank.hpp"
#include "dsp/filters/filterCoefficients.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::filterBank;
using YSE::DSP::filterCoefficients;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  // Runs `fn` over `in` in blocks of `block` samples; one vector per filter.
  std::vector<std::vector<float>> Run(filterBank& fn, const std::vector<float>& in,
                                      unsigned int block = N) {
    std::vector<std::vector<float>> out(fn.size());
    for (std::size_t start = 0; start < in.size(); start += block) {
      const auto length =
          static_cast<unsigned int>(std::min<std::size_t>(block, in.size() - start));
      buffer b(length);
      for (unsigned int i = 0; i < length; ++i)
        b.getPtr()[i] = in[start + i];
      fn(b);
      for (unsigned int f = 0; f < fn.size(); ++f) {
        REQUIRE(fn.output(f).getLength() == length);
        for (unsigned int i = 0; i < length; ++i)
          out[f].push_back(fn.output(f).getPtr()[i]);
      }
    }
    return out;
  }

  std::vector<float> Impulse(unsigned int samples) {
    std::vector<float> v(samples, 0.f);
    v[0] = 1.f;
    return v;
  }

  std::vector<float> Sine(double hz, double rate, unsigned int samples) {
    std::vector<float> v(samples);
    for (unsigned int i = 0; i < samples; ++i)
      v[i] = static_cast<float>(std::sin(2.0 * PI * hz * i / rate));
    return v;
  }

  std::vector<float> Noise(unsigned int samples, unsigned int seed) {
    std::mt19937 gen(seed);
    std::uniform_real_distribution<float> dist(-1.f, 1.f);
    std::vector<float> v(samples);
    for (float& s : v)
      s = dist(gen);
    return v;
  }

  // The peak of the last `tail` samples.
  float Peak(const std::vector<float>& v, std::size_t tail) {
    float peak = 0.f;
    for (std::size_t i = v.size() - tail; i < v.size(); ++i)
      peak = std::max(peak, std::fabs(v[i]));
    return peak;
  }

  // The cookbook bandpass, as the header gives it, in double precision.
  double Magnitude(double f, double q, double gain, double rate, double probe) {
    const double w = 2.0 * PI * f / rate;
    const double alpha = std::sin(w) / (2.0 * q);
    const std::complex<double> z = std::polar(1.0, -2.0 * PI * probe / rate);
    const std::complex<double> num = gain * alpha * (1.0 - z * z);
    const std::complex<double> den = (1.0 + alpha) - 2.0 * std::cos(w) * z + (1.0 - alpha) * z * z;
    return std::abs(num / den);
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("filterBank: defaults and the number of filters (#1096)") {
    filterBank fn;
    CHECK(fn.size() == filterBank::DEFAULT_FILTERS);
    CHECK(fn.frequency(0) == 100.f);
    CHECK(fn.frequency(1) == 200.f);
    CHECK(fn.frequency(2) == 400.f);
    CHECK(fn.frequency(3) == 800.f);
    for (unsigned int i = 0; i < fn.size(); ++i) {
      CHECK(fn.q(i) == filterBank::DEFAULT_Q);
      CHECK(fn.gain(i) == filterBank::DEFAULT_GAIN);
    }
    // An index beyond the bank reads the last filter.
    CHECK(fn.frequency(99) == 800.f);

    CHECK(filterBank(0).size() == 1);
    CHECK(filterBank(1).size() == 1);
    CHECK(filterBank(16).size() == 16);
    CHECK(filterBank(128).size() == 128);
    CHECK(filterBank(5000).size() == filterBank::MAX_FILTERS);
  }

  TEST_CASE("filterBank: the coefficients are filterCoefficients' gain bandpass (#1096)") {
    TestHelpers::ScopedSampleRate rate(48000);
    filterBank fn(3);
    fn.frequency(0, 440.f).q(0, 5.f).gain(0, 0.5f);
    fn.frequency(1, 3000.f).q(1, 40.f).gain(1, -2.f);
    for (unsigned int i = 0; i < fn.size(); ++i) {
      CAPTURE(i);
      const auto expected = filterCoefficients::Compute(
          filterCoefficients::GAINBPASS, fn.frequency(i), fn.gain(i), fn.q(i), 48000.f);
      const auto got = fn.coefficients(i);
      CHECK(got.a0 == expected.a0);
      CHECK(got.a1 == expected.a1);
      CHECK(got.a2 == expected.a2);
      CHECK(got.b1 == expected.b1);
      CHECK(got.b2 == expected.b2);
      const auto statics = filterBank::Coefficients(fn.frequency(i), fn.q(i), fn.gain(i), 48000.f);
      CHECK(statics.a0 == expected.a0);
      CHECK(statics.b2 == expected.b2);
    }
    // The bandpass: a2 = -a0, a1 = 0.
    const auto c = fn.coefficients(0);
    CHECK(c.a1 == 0.f);
    CHECK(c.a2 == -c.a0);
  }

  TEST_CASE("filterBank: each filter's impulse response against a direct-form recursion "
            "(#1096)") {
    TestHelpers::ScopedSampleRate rate(44100);
    filterBank fn(4);
    fn.frequency(0, 250.f).q(0, 2.f).gain(0, 1.f);
    fn.frequency(1, 1000.f).q(1, 10.f).gain(1, 0.7f);
    fn.frequency(2, 5000.f).q(2, 0.5f).gain(2, 2.f);
    fn.frequency(3, 15000.f).q(3, 100.f).gain(3, -1.f);
    const auto out = Run(fn, Impulse(1024));
    for (unsigned int f = 0; f < fn.size(); ++f) {
      CAPTURE(f);
      const auto c = fn.coefficients(f);
      double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
      for (unsigned int n = 0; n < 1024; ++n) {
        CAPTURE(n);
        const double x = n == 0 ? 1.0 : 0.0;
        const double y = c.a0 * x + c.a1 * x1 + c.a2 * x2 - c.b1 * y1 - c.b2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        CHECK(out[f][n] == doctest::Approx(y).epsilon(1e-5).scale(1e-6));
      }
    }
  }

  TEST_CASE("filterBank: the gain at the center frequency and the response off it, on sines "
            "(#1096)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const double sr = 48000.0;
    struct Case {
      float f;
      float q;
      float gain;
    };
    for (const Case k : {Case{200.f, 4.f, 1.f}, Case{1000.f, 20.f, 0.5f}, Case{6000.f, 2.f, 3.f},
                         Case{12000.f, 50.f, 1.f}}) {
      CAPTURE(k.f);
      CAPTURE(k.q);
      for (const double probe : {static_cast<double>(k.f), k.f * 0.5, k.f * 1.5, k.f * 1.05}) {
        CAPTURE(probe);
        filterBank fn(1);
        fn.frequency(0, k.f).q(0, k.q).gain(0, k.gain);
        const auto out = Run(fn, Sine(probe, sr, 48000));
        const double expected = Magnitude(k.f, k.q, k.gain, sr, probe);
        CHECK(Peak(out[0], 4800) == doctest::Approx(expected).epsilon(0.01));
        if (probe == k.f) CHECK(expected == doctest::Approx(k.gain).epsilon(1e-9));
      }
    }
  }

  TEST_CASE("filterBank: the filters are independent and each matches a bank of one "
            "(#1096)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto noise = Noise(4000, 7);
    filterBank fn(5);
    fn.frequencyRatio(150.f, 2.5f).qAll(8.f);
    fn.gain(2, 0.25f).q(4, 30.f);
    const auto out = Run(fn, noise);
    for (unsigned int f = 0; f < fn.size(); ++f) {
      CAPTURE(f);
      filterBank one(1);
      one.frequency(0, fn.frequency(f)).q(0, fn.q(f)).gain(0, fn.gain(f));
      const auto single = Run(one, noise);
      CHECK(single[0] == out[f]);
    }
    // A change to one filter leaves the others' output unchanged.
    filterBank changed(5);
    changed.frequencyRatio(150.f, 2.5f).qAll(8.f);
    changed.gain(2, 0.25f).q(4, 30.f);
    changed.frequency(1, 3000.f);
    const auto after = Run(changed, noise);
    CHECK(after[0] == out[0]);
    CHECK(after[1] != out[1]);
    CHECK(after[2] == out[2]);
    CHECK(after[3] == out[3]);
    CHECK(after[4] == out[4]);
  }

  TEST_CASE("filterBank: setters, spreads and invalid settings (#1096)") {
    filterBank fn(4);
    fn.frequencyAll(700.f).qAll(3.f).gainAll(0.2f);
    for (unsigned int i = 0; i < 4; ++i) {
      CHECK(fn.frequency(i) == 700.f);
      CHECK(fn.q(i) == 3.f);
      CHECK(fn.gain(i) == 0.2f);
    }
    fn.frequencyRatio(440.f, 2.f);
    CHECK(fn.frequency(0) == 440.f);
    CHECK(fn.frequency(1) == 880.f);
    CHECK(fn.frequency(2) == 1760.f);
    CHECK(fn.frequency(3) == 3520.f);
    fn.frequencyRatio(1000.f, 0.5f);
    CHECK(fn.frequency(3) == 125.f);
    fn.harmonic(100.f);
    CHECK(fn.frequency(0) == 100.f);
    CHECK(fn.frequency(1) == 200.f);
    CHECK(fn.frequency(2) == 300.f);
    CHECK(fn.frequency(3) == 400.f);
    CHECK(filterBank::RatioFrequency(100.f, 3.f, 2) == 900.f);
    CHECK(filterBank::HarmonicFrequency(55.f, 3) == 220.f);
    // A spread past the float range is held at the largest float.
    CHECK(filterBank::RatioFrequency(1e30f, 1e30f, 3) == std::numeric_limits<float>::max());

    // Ignored: an index beyond the bank, non-finite values, a ratio not
    // above 0.
    const float inf = std::numeric_limits<float>::infinity();
    fn.frequency(4, 50.f).q(4, 50.f).gain(4, 50.f);
    fn.frequency(0, std::nanf("")).q(1, inf).gain(2, -inf);
    fn.frequencyAll(std::nanf("")).qAll(std::nanf("")).gainAll(inf);
    fn.frequencyRatio(200.f, 0.f).frequencyRatio(200.f, -2.f).frequencyRatio(inf, 2.f);
    fn.frequencyRatio(200.f, std::nanf("")).harmonic(std::nanf(""));
    CHECK(fn.frequency(0) == 100.f);
    CHECK(fn.frequency(3) == 400.f);
    CHECK(fn.q(1) == 3.f);
    CHECK(fn.gain(2) == 0.2f);
    // Negative gains are allowed.
    fn.gain(1, -0.5f);
    CHECK(fn.gain(1) == -0.5f);
  }

  TEST_CASE("filterBank: the coefficients follow the live sample rate (#1096)") {
    filterBank fn(2);
    fn.frequency(0, 1000.f).q(0, 10.f);
    std::vector<float> at48;
    {
      TestHelpers::ScopedSampleRate rate(48000);
      const auto c = fn.coefficients(0);
      const auto e = filterBank::Coefficients(1000.f, 10.f, 1.f, 48000.f);
      CHECK(c.b1 == e.b1);
      at48 = Run(fn, Sine(1000.0, 48000.0, 9600))[0];
      CHECK(Peak(at48, 960) == doctest::Approx(1.0).epsilon(0.01));
    }
    {
      TestHelpers::ScopedSampleRate rate(96000);
      const auto c = fn.coefficients(0);
      const auto e = filterBank::Coefficients(1000.f, 10.f, 1.f, 96000.f);
      CHECK(c.b1 == e.b1);
      // Still 1000 Hz at the new rate: a 1000 Hz sine passes at its full
      // level.
      fn.clear();
      const auto at96 = Run(fn, Sine(1000.0, 96000.0, 19200))[0];
      CHECK(Peak(at96, 1920) == doctest::Approx(1.0).epsilon(0.01));
    }
  }

  TEST_CASE("filterBank: non-finite input is skipped, clear() and overflow reset the state "
            "(#1096)") {
    TestHelpers::ScopedSampleRate rate(48000);
    filterBank fn(2);
    std::vector<float> in = Noise(N, 3);
    in[10] = std::nanf("");
    in[11] = std::numeric_limits<float>::infinity();
    const auto out = Run(fn, in);

    // The same input without the two samples, the outputs held across them.
    filterBank ref(2);
    std::vector<float> clean = in;
    clean.erase(clean.begin() + 10, clean.begin() + 12);
    const auto expected = Run(ref, clean);
    for (unsigned int f = 0; f < 2; ++f) {
      CAPTURE(f);
      CHECK(out[f][10] == out[f][9]);
      CHECK(out[f][11] == out[f][9]);
      for (unsigned int i = 12; i < N; ++i) {
        CAPTURE(i);
        CHECK(out[f][i] == expected[f][i - 2]);
      }
      for (const float s : out[f])
        CHECK(std::isfinite(s));
    }

    // clear(): a new start from silence, as a fresh bank.
    fn.clear();
    filterBank fresh(2);
    const auto noise = Noise(N, 4);
    CHECK(Run(fn, noise) == Run(fresh, noise));

    // An input whose product overflows starts the filter again from silence.
    filterBank big(1);
    big.gain(0, 1e30f);
    std::vector<float> huge(N, 3e38f);
    const auto blown = Run(big, huge);
    for (const float s : blown[0])
      CHECK(std::isfinite(s));
  }

  TEST_CASE("filterBank: block size does not change the output (#1096)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto noise = Noise(1000, 11);
    filterBank a(3);
    filterBank b(3);
    filterBank c(3);
    const auto full = Run(a, noise, N);
    CHECK(Run(b, noise, 1) == full);
    CHECK(Run(c, noise, 37) == full);

    // A buffer longer than STANDARD_BUFFERSIZE is cut to it.
    filterBank d(1);
    buffer longer(N * 2);
    longer = 0.5f;
    d(longer);
    CHECK(d.output(0).getLength() == N);
  }

  TEST_CASE("filterBank: is stable on noise at high Q across the band (#1096)") {
    TestHelpers::ScopedSampleRate rate(44100);
    filterBank fn(filterBank::MAX_FILTERS);
    fn.frequencyRatio(20.f, 1.05f).qAll(500.f);
    const auto out = Run(fn, Noise(44100, 5));
    for (unsigned int f = 0; f < fn.size(); ++f) {
      CAPTURE(f);
      for (const float s : out[f])
        REQUIRE(std::isfinite(s));
      CHECK(Peak(out[f], 4410) < 100.f);
    }
  }

  TEST_CASE("filterBank: processing allocates nothing (#1096)") {
    TestHelpers::ScopedSampleRate rate(48000);
    filterBank fn(16);
    buffer in(N);
    buffer shorter(N / 2);
    in = 0.5f;
    shorter = 0.25f;
    fn(in);
    {
      TestHelpers::ProbeScope probe;
      fn.frequency(3, 1200.f).q(3, 4.f).gain(3, 0.5f);
      fn.frequencyAll(300.f).qAll(7.f).gainAll(2.f);
      fn.frequencyRatio(50.f, 1.5f).harmonic(80.f);
      fn(in);
      fn(shorter);
      fn(in);
      (void)fn.coefficients(2);
      fn.clear();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
