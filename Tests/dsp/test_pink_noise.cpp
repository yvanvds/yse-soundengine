// Tests for YSE::DSP::pinkNoise (YseEngine/dsp/generators/pinkNoise.hpp),
// issue #1056. Noise has no closed-form output, so the reference is the
// spectrum: the power falls by 3 dB per octave (equal energy per octave),
// measured with a Welch-averaged periodogram over a fixed seed so the result
// is the same on every run. White noise (DSP::noise) through the same
// measurement gives a flat spectrum, which shows the measurement can tell
// the two apart.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>
#include "dsp/generators/pinkNoise.hpp"
#include "dsp/oscillators.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

namespace {

  constexpr unsigned int SEGMENT = 4096;
  constexpr unsigned int SEGMENTS = 64;
  const double PI = 3.14159265358979323846;

  // In-place iterative radix-2 FFT; SEGMENT is a power of two.
  void Fft(std::vector<std::complex<double>>& x) {
    const unsigned int n = static_cast<unsigned int>(x.size());
    for (unsigned int i = 1, j = 0; i < n; ++i) {
      unsigned int bit = n >> 1;
      for (; (j & bit) != 0; bit >>= 1)
        j ^= bit;
      j ^= bit;
      if (i < j) std::swap(x[i], x[j]);
    }
    for (unsigned int len = 2; len <= n; len <<= 1) {
      const std::complex<double> w = std::polar(1.0, -2.0 * PI / len);
      for (unsigned int i = 0; i < n; i += len) {
        std::complex<double> wk(1.0, 0.0);
        for (unsigned int k = 0; k < len / 2; ++k) {
          const std::complex<double> a = x[i + k];
          const std::complex<double> b = x[i + k + len / 2] * wk;
          x[i + k] = a + b;
          x[i + k + len / 2] = a - b;
          wk *= w;
        }
      }
    }
  }

  // Mean power per bin over SEGMENTS Hann-windowed segments of the signal
  // that `next` produces one block at a time.
  template <class Source> std::vector<double> PowerSpectrum(Source next) {
    std::vector<float> signal;
    signal.reserve(SEGMENT * SEGMENTS);
    while (signal.size() < SEGMENT * SEGMENTS) {
      YSE::DSP::buffer& block = next();
      signal.insert(signal.end(), block.getPtr(), block.getPtr() + block.getLength());
    }
    std::vector<double> power(SEGMENT / 2, 0.0);
    std::vector<std::complex<double>> x(SEGMENT);
    for (unsigned int s = 0; s < SEGMENTS; ++s) {
      for (unsigned int i = 0; i < SEGMENT; ++i) {
        const double hann = 0.5 - 0.5 * std::cos(2.0 * PI * i / SEGMENT);
        x[i] = std::complex<double>(signal[s * SEGMENT + i] * hann, 0.0);
      }
      Fft(x);
      for (unsigned int k = 0; k < SEGMENT / 2; ++k)
        power[k] += std::norm(x[k]);
    }
    return power;
  }

  // Energy, in dB, of the octave of bins [first, 2 * first).
  double OctaveDb(const std::vector<double>& power, unsigned int first) {
    double sum = 0.0;
    for (unsigned int k = first; k < 2 * first; ++k)
      sum += power[k];
    return 10.0 * std::log10(sum);
  }

  // Least-squares slope of the per-bin power in dB against log2(bin), over
  // bins [first, last]: dB per octave.
  double SlopeDbPerOctave(const std::vector<double>& power, unsigned int first, unsigned int last) {
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    const double n = static_cast<double>(last - first + 1);
    for (unsigned int k = first; k <= last; ++k) {
      const double x = std::log2(static_cast<double>(k));
      const double y = 10.0 * std::log10(power[k]);
      sx += x;
      sy += y;
      sxx += x * x;
      sxy += x * y;
    }
    return (n * sxy - sx * sy) / (n * sxx - sx * sx);
  }

  // Bins 8..1500 of a 4096-point segment: about 86 Hz to 16 kHz at 44.1 kHz,
  // clear of the window's leakage near DC and of the top of the band.
  constexpr unsigned int FIRST_BIN = 8;
  constexpr unsigned int LAST_BIN = 1500;

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("pinkNoise: the spectrum falls by 3 dB per octave (#1056)") {
    YSE::DSP::pinkNoise pink;
    const std::vector<double> power = PowerSpectrum([&]() -> YSE::DSP::buffer& { return pink(); });
    const double slope = SlopeDbPerOctave(power, FIRST_BIN, LAST_BIN);
    CHECK(slope > -3.3);
    CHECK(slope < -2.7);
  }

  TEST_CASE("pinkNoise: every octave holds the same energy (#1056)") {
    YSE::DSP::pinkNoise pink;
    const std::vector<double> power = PowerSpectrum([&]() -> YSE::DSP::buffer& { return pink(); });
    // Octaves starting at bins 8, 16, ..., 1024 (the last one ends at Nyquist).
    // Below bin 8 the window leaks the strong lowest bins into the octave.
    const double reference = OctaveDb(power, 16);
    for (unsigned int first = 8; first <= 1024; first *= 2) {
      CAPTURE(first);
      CHECK(std::fabs(OctaveDb(power, first) - reference) < 0.5);
    }
  }

  TEST_CASE("pinkNoise: the same measurement finds white noise flat (#1056)") {
    YSE::DSP::noise white;
    const std::vector<double> power = PowerSpectrum([&]() -> YSE::DSP::buffer& { return white(); });
    const double slope = SlopeDbPerOctave(power, FIRST_BIN, LAST_BIN);
    CHECK(slope > -0.3);
    CHECK(slope < 0.3);
  }

  TEST_CASE("pinkNoise: level, range and zero mean (#1056)") {
    YSE::DSP::pinkNoise pink;
    double sum = 0.0, squares = 0.0;
    float peak = 0.f;
    const unsigned int blocks = 4096;
    for (unsigned int b = 0; b < blocks; ++b) {
      YSE::DSP::buffer& out = pink();
      REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
      for (unsigned int i = 0; i < out.getLength(); ++i) {
        const float v = out.getPtr()[i];
        REQUIRE(std::isfinite(v));
        sum += v;
        squares += static_cast<double>(v) * v;
        peak = std::max(peak, std::fabs(v));
      }
    }
    const double n = static_cast<double>(blocks) * YSE::STANDARD_BUFFERSIZE;
    const double rms = std::sqrt(squares / n);
    CHECK(peak <= 1.f);
    CHECK(peak > 0.2f);
    CHECK(rms > 0.15);
    CHECK(rms < 0.25);
    CHECK(std::fabs(sum / n) < 0.01);
  }

  TEST_CASE("pinkNoise: a seed makes the sequence repeatable (#1056)") {
    // Two generators with the same seed agree; another seed does not.
    YSE::DSP::pinkNoise a;
    YSE::DSP::pinkNoise b;
    YSE::DSP::pinkNoise c;
    c.seed(12345u);
    for (int block = 0; block < 8; ++block) {
      YSE::DSP::buffer& outA = a();
      CHECK(TestHelpers::buffersNearlyEqual(outA, b(), 0.f));
      CHECK_FALSE(TestHelpers::buffersNearlyEqual(outA, c(), 1e-6f));
    }

    // seed() also clears the filter: re-seeding a running generator repeats
    // what it made from that seed the first time.
    YSE::DSP::pinkNoise x;
    x.seed(777u);
    std::vector<float> expected;
    for (int block = 0; block < 3; ++block) {
      YSE::DSP::buffer& out = x();
      expected.insert(expected.end(), out.getPtr(), out.getPtr() + out.getLength());
    }
    x.seed(777u);
    std::vector<float> again;
    for (int block = 0; block < 3; ++block) {
      YSE::DSP::buffer& out = x();
      again.insert(again.end(), out.getPtr(), out.getPtr() + out.getLength());
    }
    CHECK(expected == again);
  }

  TEST_CASE("pinkNoise: the default seed is DSP::noise's (#1056)") {
    // Same white source: the first pink sample is the first white sample
    // through the filter's direct path, (sum of the input gains) * gain.
    YSE::DSP::noise white;
    YSE::DSP::pinkNoise pink;
    const double w = white().getPtr()[0];
    const double gains =
        0.0555179 + 0.0750759 + 0.1538520 + 0.3104856 + 0.5329522 - 0.0168980 + 0.5362;
    CHECK(pink().getPtr()[0] == doctest::Approx(w * gains * 0.11).epsilon(1e-5));
  }

  TEST_CASE("pinkNoise: block length and the sample rate (#1056)") {
    YSE::DSP::pinkNoise a;
    YSE::DSP::pinkNoise b;
    CHECK(a(32).getLength() == 32);
    CHECK(a(YSE::STANDARD_BUFFERSIZE * 4).getLength() == YSE::STANDARD_BUFFERSIZE);
    b(32);
    b(YSE::STANDARD_BUFFERSIZE);
    // Nothing depends on the rate: the same seed gives the same samples at
    // another rate.
    TestHelpers::ScopedSampleRate rate(96000);
    CHECK(TestHelpers::buffersNearlyEqual(a(), b(), 0.f));
  }

  TEST_CASE("pinkNoise: processing allocates nothing (#1056)") {
    YSE::DSP::pinkNoise pink;
    pink();
    {
      TestHelpers::ProbeScope probe;
      pink();
      pink(32);
      pink(YSE::STANDARD_BUFFERSIZE * 4);
      pink.seed(42u);
      pink();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
