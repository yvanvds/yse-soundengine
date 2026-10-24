// Tests for YSE::DSP::onePole (YseEngine/dsp/filters/onePole.hpp), the
// single-pole lowpass behind ~onepole (issue #1086): the coefficient in each
// frequency mode, the impulse and step responses and the gain on a sine
// against their closed forms, the live sample rate, the frequency signal,
// non-finite input, clear(), block size independence and allocation freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/filters/onePole.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::onePole;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  // Runs `fn` over `in` in blocks of `block` samples.
  std::vector<float> Run(onePole& fn, const std::vector<float>& in, unsigned int block = N) {
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

  // The steady-state gain of y += a (x - y) at angular frequency w.
  double Gain(double a, double w) {
    const double b = 1.0 - a;
    const double re = 1.0 - b * std::cos(w);
    const double im = b * std::sin(w);
    return a / std::sqrt(re * re + im * im);
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("onePole: the coefficient in each frequency mode (#1086)") {
    using M = onePole;
    const float sr = 48000.f;
    CHECK(onePole::Coefficient(1000.f, M::HZ, sr) ==
          doctest::Approx(std::sin(2.0 * PI * 1000.0 / 48000.0)));
    CHECK(onePole::Coefficient(0.f, M::HZ, sr) == 0.f);
    // A quarter of the sample rate is fully open; above it, and below 0, clamp.
    CHECK(onePole::Coefficient(12000.f, M::HZ, sr) == doctest::Approx(1.0));
    CHECK(onePole::Coefficient(12000.f, M::HZ, sr) <= 1.f);
    CHECK(onePole::Coefficient(20000.f, M::HZ, sr) == doctest::Approx(1.0));
    CHECK(onePole::Coefficient(-50.f, M::HZ, sr) == 0.f);
    CHECK(onePole::Coefficient(1000.f, M::HZ, 0.f) == 0.f);

    // Linear: 0..1 spans 0..samplerate/4 in Hz.
    CHECK(onePole::Coefficient(0.5f, M::LINEAR, sr) == doctest::Approx(std::sin(PI / 4.0)));
    CHECK(onePole::Coefficient(0.25f, M::LINEAR, sr) ==
          doctest::Approx(onePole::Coefficient(3000.f, M::HZ, sr)));
    CHECK(onePole::Coefficient(1.f, M::LINEAR, sr) == doctest::Approx(1.0));
    CHECK(onePole::Coefficient(3.f, M::LINEAR, sr) == doctest::Approx(1.0));
    CHECK(onePole::Coefficient(-1.f, M::LINEAR, sr) == 0.f);

    // Radians: the coefficient itself.
    CHECK(onePole::Coefficient(0.3f, M::RADIANS, sr) == 0.3f);
    CHECK(onePole::Coefficient(1.5f, M::RADIANS, sr) == 1.f);
    CHECK(onePole::Coefficient(-0.5f, M::RADIANS, sr) == 0.f);

    CHECK(onePole::Coefficient(std::numeric_limits<float>::quiet_NaN(), M::RADIANS, sr) == 0.f);
  }

  TEST_CASE("onePole: defaults, setters and invalid settings (#1086)") {
    TestHelpers::ScopedSampleRate rate(48000);
    onePole fn;
    CHECK(fn.frequency() == 0.f);
    CHECK(fn.mode() == onePole::HZ);
    CHECK(fn.coefficient() == 0.f);
    // At 0 Hz nothing gets through.
    const std::vector<float> out = Run(fn, std::vector<float>(N, 1.f));
    CHECK(out[N - 1] == 0.f);

    fn.frequency(440.f).mode(onePole::LINEAR);
    CHECK(fn.frequency() == 440.f);
    CHECK(fn.mode() == onePole::LINEAR);
    fn.frequency(std::numeric_limits<float>::infinity());
    fn.frequency(std::numeric_limits<float>::quiet_NaN());
    CHECK(fn.frequency() == 440.f);
    fn.mode(static_cast<onePole::FREQUENCY_MODE>(7));
    CHECK(fn.mode() == onePole::LINEAR);
  }

  TEST_CASE("onePole: impulse and step responses against the closed form (#1086)") {
    TestHelpers::ScopedSampleRate rate(48000);
    for (const float hz : {50.f, 1000.f, 5000.f, 11000.f}) {
      CAPTURE(hz);
      const double a = std::sin(2.0 * PI * hz / 48000.0);
      onePole fn;
      fn.frequency(hz);
      const std::vector<float> impulse = Run(fn, Impulse(3 * N));
      for (unsigned int n = 0; n < 3 * N; ++n) {
        CAPTURE(n);
        CHECK(impulse[n] == doctest::Approx(a * std::pow(1.0 - a, n)).epsilon(1e-4));
      }
      onePole step;
      step.frequency(hz);
      const std::vector<float> rise = Run(step, std::vector<float>(3 * N, 1.f));
      for (unsigned int n = 0; n < 3 * N; ++n) {
        CAPTURE(n);
        CHECK(rise[n] == doctest::Approx(1.0 - std::pow(1.0 - a, n + 1)).epsilon(1e-4));
      }
    }

    // At a quarter of the sample rate, or a0 = 1 in radians mode, the input
    // passes unchanged.
    std::vector<float> in(N);
    for (unsigned int i = 0; i < N; ++i)
      in[i] = std::sin(static_cast<float>(i) * 0.37f);
    onePole open;
    open.frequency(12000.f);
    CHECK(open.coefficient() == 1.f);
    onePole raw;
    raw.mode(onePole::RADIANS).frequency(1.f);
    const std::vector<float> a = Run(open, in);
    const std::vector<float> b = Run(raw, in);
    for (unsigned int i = 0; i < N; ++i) {
      CAPTURE(i);
      CHECK(a[i] == doctest::Approx(in[i]).epsilon(1e-6));
      CHECK(b[i] == doctest::Approx(in[i]).epsilon(1e-6));
    }
  }

  TEST_CASE("onePole: the gain on a sine against the closed form, in every mode (#1086)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto measure = [](onePole& fn, double hz) {
      const unsigned int total = 48000;
      const unsigned int half = total / 2;
      std::vector<float> in(total);
      for (unsigned int i = 0; i < total; ++i)
        in[i] = static_cast<float>(std::sin(2.0 * PI * hz * i / 48000.0));
      const std::vector<float> out = Run(fn, in);
      // The RMS of the last half second (a whole number of cycles at each
      // frequency tested), well after the transient, as an amplitude.
      double sum = 0.0;
      for (unsigned int i = half; i < total; ++i)
        sum += static_cast<double>(out[i]) * static_cast<double>(out[i]);
      return std::sqrt(2.0 * sum / static_cast<double>(half));
    };

    struct setting {
      onePole::FREQUENCY_MODE mode;
      float value;
    };
    for (const setting s : {setting{onePole::HZ, 1000.f}, setting{onePole::HZ, 6000.f},
                            setting{onePole::LINEAR, 0.1f}, setting{onePole::RADIANS, 0.05f}}) {
      CAPTURE(s.mode);
      CAPTURE(s.value);
      const double a = onePole::Coefficient(s.value, s.mode, 48000.f);
      for (const double hz : {100.0, 1000.0, 4000.0, 10000.0}) {
        CAPTURE(hz);
        onePole fn;
        fn.mode(s.mode).frequency(s.value);
        CHECK(measure(fn, hz) == doctest::Approx(Gain(a, 2.0 * PI * hz / 48000.0)).epsilon(0.01));
      }
    }
  }

  TEST_CASE("onePole: the coefficient follows the live sample rate (#1086)") {
    TestHelpers::ScopedSampleRate rate(48000);
    onePole fn;
    fn.frequency(2000.f);
    CHECK(fn.coefficient() == doctest::Approx(std::sin(2.0 * PI * 2000.0 / 48000.0)));
    YSE::SAMPLERATE = 96000;
    CHECK(fn.coefficient() == doctest::Approx(std::sin(2.0 * PI * 2000.0 / 96000.0)));
    const double a = std::sin(2.0 * PI * 2000.0 / 96000.0);
    fn.clear();
    const std::vector<float> impulse = Run(fn, Impulse(N));
    CHECK(impulse[0] == doctest::Approx(a));
    CHECK(impulse[5] == doctest::Approx(a * std::pow(1.0 - a, 5)));
    // Linear and radians are fractions of the sample rate: unchanged.
    fn.mode(onePole::LINEAR).frequency(0.5f);
    CHECK(fn.coefficient() == doctest::Approx(std::sin(PI / 4.0)));
    YSE::SAMPLERATE = 22050;
    CHECK(fn.coefficient() == doctest::Approx(std::sin(PI / 4.0)));
  }

  TEST_CASE("onePole: the frequency signal is read from its first sample, once per block "
            "(#1086)") {
    TestHelpers::ScopedSampleRate rate(48000);
    onePole fn;
    onePole reference;
    buffer in(N);
    buffer freq(N);
    for (unsigned int i = 0; i < N; ++i) {
      in.getPtr()[i] = 1.f;
      freq.getPtr()[i] = 500.f + static_cast<float>(i) * 100.f;
    }
    reference.frequency(500.f);
    buffer refIn(N);
    refIn = 1.f;
    CHECK(TestHelpers::buffersNearlyEqual(fn(in, freq), reference(refIn), 0.f));
    CHECK(fn.frequency() == 500.f);

    // A non-finite first sample keeps the frequency.
    freq.getPtr()[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(TestHelpers::buffersNearlyEqual(fn(in, freq), reference(refIn), 0.f));
    CHECK(fn.frequency() == 500.f);

    // In linear mode the signal is read as 0-1.
    fn.mode(onePole::LINEAR);
    reference.mode(onePole::LINEAR).frequency(0.2f);
    freq = 0.2f;
    CHECK(TestHelpers::buffersNearlyEqual(fn(in, freq), reference(refIn), 0.f));
  }

  TEST_CASE("onePole: non-finite input is skipped, clear() resets the state (#1086)") {
    TestHelpers::ScopedSampleRate rate(48000);
    onePole fn;
    fn.mode(onePole::RADIANS).frequency(0.5f);
    std::vector<float> in = {1.f, std::numeric_limits<float>::quiet_NaN(), 1.f,
                             std::numeric_limits<float>::infinity(), 0.f};
    const std::vector<float> out = Run(fn, in);
    CHECK(out[0] == 0.5f);
    CHECK(out[1] == 0.5f);
    CHECK(out[2] == 0.75f);
    CHECK(out[3] == 0.75f);
    CHECK(out[4] == 0.375f);
    fn.clear();
    CHECK(fn.process(1.f) == 0.5f);
    fn.clear();
    CHECK(Run(fn, {0.f})[0] == 0.f);
  }

  TEST_CASE("onePole: blocks, single samples and buffer clamping (#1086)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::vector<float> in(5 * N);
    for (std::size_t i = 0; i < in.size(); ++i)
      in[i] =
          std::sin(static_cast<float>(i) * 0.11f) + 0.3f * std::sin(static_cast<float>(i) * 1.7f);
    onePole whole;
    whole.frequency(3000.f);
    const std::vector<float> reference = Run(whole, in);
    for (const unsigned int block : {1u, 7u, 64u, N}) {
      CAPTURE(block);
      onePole fn;
      fn.frequency(3000.f);
      CHECK(Run(fn, in, block) == reference);
    }
    onePole single;
    single.frequency(3000.f);
    for (std::size_t i = 0; i < in.size(); ++i)
      CHECK(single.process(in[i]) == reference[i]);

    // A longer buffer is processed up to STANDARD_BUFFERSIZE.
    onePole fn;
    buffer longer(2 * N);
    longer = 1.f;
    CHECK(fn(longer).getLength() == N);
  }

  TEST_CASE("onePole: processing allocates nothing (#1086)") {
    TestHelpers::ScopedSampleRate rate(48000);
    onePole fn;
    buffer in(N);
    buffer shorter(N / 2);
    buffer freq(N);
    in = 0.5f;
    shorter = 0.25f;
    freq = 800.f;
    fn(in);
    {
      TestHelpers::ProbeScope probe;
      fn.frequency(1200.f).mode(onePole::HZ);
      fn(in);
      fn(shorter);
      fn(in, freq);
      fn.mode(onePole::LINEAR).frequency(0.3f);
      fn(in);
      fn.mode(onePole::RADIANS);
      (void)fn.process(0.1f);
      fn.clear();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
