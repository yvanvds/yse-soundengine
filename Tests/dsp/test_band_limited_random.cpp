// Tests for YSE::DSP::bandLimitedRandom
// (YseEngine/dsp/generators/bandLimitedRandom.hpp), issue #1057. The random
// values are DSP::noise's sequence, so the output has a closed form: the
// values 0, w0, w1, ... (w = DSP::noise's samples) one segment apart, joined
// by straight lines. The spectrum shows the band limit: the energy lies
// below the frequency, where white noise spreads it evenly.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <vector>
#include "dsp/generators/bandLimitedRandom.hpp"
#include "dsp/oscillators.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::bandLimitedRandom;

namespace {

  const double PI = 3.14159265358979323846;

  // The values the generator glides between: 0, then DSP::noise's samples.
  std::vector<double> Values(unsigned int count) {
    YSE::DSP::noise white;
    std::vector<double> values(1, 0.0);
    while (values.size() < count) {
      YSE::DSP::buffer& block = white();
      for (unsigned int i = 0; i < block.getLength() && values.size() < count; ++i)
        values.push_back(block.getPtr()[i]);
    }
    return values;
  }

  // `blocks` blocks of the generator at a constant frequency, concatenated.
  std::vector<float> Render(bandLimitedRandom& random, float frequency, unsigned int blocks) {
    std::vector<float> signal;
    for (unsigned int b = 0; b < blocks; ++b) {
      YSE::DSP::buffer& out = random(frequency);
      signal.insert(signal.end(), out.getPtr(), out.getPtr() + out.getLength());
    }
    return signal;
  }

  // The closed form for `samplesPerValue` samples per segment.
  void CheckClosedForm(const std::vector<float>& signal, unsigned int samplesPerValue) {
    const std::vector<double> v =
        Values(static_cast<unsigned int>(signal.size() / samplesPerValue) + 2);
    for (unsigned int n = 0; n < signal.size(); ++n) {
      const unsigned int k = n / samplesPerValue;
      const double t = static_cast<double>(n % samplesPerValue) / samplesPerValue;
      const double expected = v[k] + (v[k + 1] - v[k]) * t;
      if (std::fabs(signal[n] - expected) > 1e-6) {
        CAPTURE(n);
        CHECK(signal[n] == doctest::Approx(expected).epsilon(1e-6));
        return;
      }
    }
  }

  constexpr unsigned int SEGMENT = 4096;
  constexpr unsigned int SEGMENTS = 64;

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

  // Mean power per bin over SEGMENTS Hann-windowed segments.
  std::vector<double> PowerSpectrum(const std::vector<float>& signal) {
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

  // Mean power per bin over bins [first, last), in dB.
  double BandDb(const std::vector<double>& power, unsigned int first, unsigned int last) {
    double sum = 0.0;
    for (unsigned int k = first; k < last; ++k)
      sum += power[k];
    return 10.0 * std::log10(sum / (last - first));
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("bandLimitedRandom: straight lines between DSP::noise's values (#1057)") {
    // 16 samples a value: the step is exactly 1/16, so the closed form holds
    // to float precision over many segments.
    bandLimitedRandom random;
    const float frequency = static_cast<float>(YSE::SAMPLERATE) / 16.f;
    const std::vector<float> signal = Render(random, frequency, 16);
    CHECK(signal[0] == 0.f); // starts at 0, no jump
    CheckClosedForm(signal, 16);
  }

  TEST_CASE("bandLimitedRandom: a frequency signal drives it like the number (#1057)") {
    bandLimitedRandom byNumber;
    bandLimitedRandom bySignal;
    const float frequency = static_cast<float>(YSE::SAMPLERATE) / 32.f;
    YSE::DSP::buffer freq(YSE::STANDARD_BUFFERSIZE);
    freq = frequency;
    for (int block = 0; block < 4; ++block) {
      YSE::DSP::buffer& a = byNumber(frequency);
      CHECK(TestHelpers::buffersNearlyEqual(a, bySignal(freq), 0.f));
    }

    // A per-sample frequency: 0 for the first half of the block holds the
    // value, then a step of 1/8 moves on.
    bandLimitedRandom stepped;
    for (unsigned int i = 0; i < freq.getLength(); ++i)
      freq.getPtr()[i] = i < 64 ? 0.f : static_cast<float>(YSE::SAMPLERATE) / 8.f;
    YSE::DSP::buffer& out = stepped(freq);
    const std::vector<double> v = Values(4);
    for (unsigned int i = 0; i <= 64; ++i)
      CHECK(out.getPtr()[i] == 0.f);
    // Sample 65 is one step of 1/8 along from 0 to the first value.
    CHECK(out.getPtr()[65] == doctest::Approx(v[1] / 8.0).epsilon(1e-6));
    CHECK(out.getPtr()[72] == doctest::Approx(v[1]).epsilon(1e-6));
    CHECK(out.getPtr()[80] == doctest::Approx(v[2]).epsilon(1e-6));
  }

  TEST_CASE("bandLimitedRandom: the sign of the frequency is ignored (#1057)") {
    bandLimitedRandom up;
    bandLimitedRandom down;
    const float frequency = static_cast<float>(YSE::SAMPLERATE) / 20.f;
    for (int block = 0; block < 4; ++block) {
      YSE::DSP::buffer& a = up(frequency);
      CHECK(TestHelpers::buffersNearlyEqual(a, down(-frequency), 0.f));
    }
  }

  TEST_CASE("bandLimitedRandom: 0 holds, the sample rate and above pick every sample (#1057)") {
    bandLimitedRandom random;
    const float frequency = static_cast<float>(YSE::SAMPLERATE) / 4.f;
    // Seven steps of 1/4 stop three quarters of the way from v1 to v2.
    random(frequency, 7);
    const std::vector<double> v = Values(3);
    YSE::DSP::buffer& still = random(0.f);
    const float held = still.getPtr()[0];
    CHECK(held == doctest::Approx(v[1] + (v[2] - v[1]) * 0.75).epsilon(1e-6));
    for (unsigned int i = 0; i < still.getLength(); ++i)
      CHECK(still.getPtr()[i] == held);

    // Not a finite number: holds as 0 does, nothing non-finite comes out.
    YSE::DSP::buffer& nan = random(std::numeric_limits<float>::quiet_NaN());
    CHECK(nan.getPtr()[0] == held);
    CHECK(nan.getPtr()[127] == held);
    YSE::DSP::buffer& inf = random(std::numeric_limits<float>::infinity(), 4);
    for (unsigned int i = 0; i < inf.getLength(); ++i)
      CHECK(std::isfinite(inf.getPtr()[i]));

    // At the sample rate: one value per sample, DSP::noise's, one sample late.
    bandLimitedRandom fast;
    bandLimitedRandom faster;
    CheckClosedForm(Render(fast, static_cast<float>(YSE::SAMPLERATE), 4), 1);
    CheckClosedForm(Render(faster, static_cast<float>(YSE::SAMPLERATE) * 10.f, 4), 1);
  }

  TEST_CASE("bandLimitedRandom: the energy lies below the frequency (#1057)") {
    // One value every 64 samples: the frequency sits at bin 64 of a
    // 4096-point segment. Linear interpolation shapes the spectrum as
    // sinc^4, so above twice the frequency the power is down by more than
    // 30 dB. White noise through the same measurement is flat.
    bandLimitedRandom random;
    const unsigned int blocks = SEGMENT * SEGMENTS / YSE::STANDARD_BUFFERSIZE;
    const std::vector<double> power =
        PowerSpectrum(Render(random, static_cast<float>(YSE::SAMPLERATE) / 64.f, blocks));
    const double below = BandDb(power, 4, 32); // clear of the window near DC
    const double above = BandDb(power, 128, 1024);
    CAPTURE(below);
    CAPTURE(above);
    CHECK(below - above > 30.0);

    YSE::DSP::noise white;
    std::vector<float> noise;
    while (noise.size() < SEGMENT * SEGMENTS) {
      YSE::DSP::buffer& out = white();
      noise.insert(noise.end(), out.getPtr(), out.getPtr() + out.getLength());
    }
    const std::vector<double> flat = PowerSpectrum(noise);
    CHECK(std::fabs(BandDb(flat, 4, 32) - BandDb(flat, 128, 1024)) < 1.0);
  }

  TEST_CASE("bandLimitedRandom: range and zero mean (#1057)") {
    bandLimitedRandom random;
    const std::vector<float> signal =
        Render(random, static_cast<float>(YSE::SAMPLERATE) / 10.f, 2048);
    double sum = 0.0;
    float low = 0.f, high = 0.f;
    for (float v : signal) {
      REQUIRE(std::isfinite(v));
      sum += v;
      low = std::min(low, v);
      high = std::max(high, v);
    }
    CHECK(low >= -1.f);
    CHECK(high <= 1.f);
    CHECK(low < -0.9f);
    CHECK(high > 0.9f);
    CHECK(std::fabs(sum / signal.size()) < 0.02);
  }

  TEST_CASE("bandLimitedRandom: a seed makes the sequence repeatable (#1057)") {
    const float frequency = static_cast<float>(YSE::SAMPLERATE) / 8.f;
    bandLimitedRandom a;
    bandLimitedRandom b;
    bandLimitedRandom c;
    c.seed(12345u);
    CHECK(Render(a, frequency, 4) == Render(b, frequency, 4));
    bandLimitedRandom d;
    CHECK(Render(c, frequency, 4) != Render(d, frequency, 4));

    // Re-seeding a running generator starts again from 0 and repeats it.
    bandLimitedRandom x;
    x.seed(777u);
    const std::vector<float> first = Render(x, frequency, 3);
    x.seed(777u);
    CHECK(Render(x, frequency, 3) == first);
    CHECK(first[0] == 0.f);

    // The default seed is DSP::noise's: seeding with it is a new generator.
    bandLimitedRandom fresh;
    x.seed(bandLimitedRandom::DEFAULT_SEED);
    CHECK(Render(x, frequency, 2) == Render(fresh, frequency, 2));
  }

  TEST_CASE("bandLimitedRandom: block length and the live sample rate (#1057)") {
    bandLimitedRandom a;
    CHECK(a(100.f, 32).getLength() == 32);
    CHECK(a(100.f, YSE::STANDARD_BUFFERSIZE * 4).getLength() == YSE::STANDARD_BUFFERSIZE);

    // The rate is read per block: at 96 kHz, 6000 Hz is 16 samples a value.
    TestHelpers::ScopedSampleRate rate(96000);
    bandLimitedRandom b;
    CheckClosedForm(Render(b, 6000.f, 8), 16);
  }

  TEST_CASE("bandLimitedRandom: processing allocates nothing (#1057)") {
    bandLimitedRandom random;
    YSE::DSP::buffer freq(YSE::STANDARD_BUFFERSIZE);
    freq = 440.f;
    random(440.f);
    {
      TestHelpers::ProbeScope probe;
      random(440.f);
      random(220.f, 32);
      random(freq);
      random(10.f, YSE::STANDARD_BUFFERSIZE * 4);
      random.seed(42u);
      random(440.f);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
