// Tests for YSE::DSP::blSaw, blRect and blTri
// (YseEngine/dsp/generators/bandLimitedOscillators.hpp), issue #1055.
//
// References: the textbook polyBLEP sawtooth and square (Valimaki & Huovilainen),
// written out here independently of the engine; the exact naive waveforms away
// from the edges; and an aliasing measurement that compares each oscillator
// with its naive counterpart at a pitch whose harmonics and folded aliases all
// land on distinct FFT bins.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <vector>
#include "dsp/generators/bandLimitedOscillators.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::blRect;
using YSE::DSP::blSaw;
using YSE::DSP::blTri;
using YSE::DSP::buffer;

namespace {

  const double PI = 3.14159265358979323846;
  constexpr unsigned int N = 4096; // FFT size, and samples per measurement
  constexpr unsigned int M = 239; // cycles in N samples: a prime, so aliases miss harmonics

  // The textbook two-sample polyBLEP residual for a jump of +2 at t = 0.
  double PolyBlep(double t, double dt) {
    if (t < dt) {
      t /= dt;
      return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt) {
      t = (t - 1.0) / dt;
      return t * t + t + t + 1.0;
    }
    return 0.0;
  }

  // Collect `count` samples, one block at a time.
  template <class Next> std::vector<float> Render(unsigned int count, Next next) {
    std::vector<float> out;
    out.reserve(count);
    while (out.size() < count) {
      buffer& block = next();
      out.insert(out.end(), block.getPtr(), block.getPtr() + block.getLength());
    }
    out.resize(count);
    return out;
  }

  // In-place iterative radix-2 FFT.
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

  // Alias power relative to harmonic power, in dB, of N samples holding
  // exactly M cycles (so every harmonic sits on a bin and no window is
  // needed). DC is left out: the rectangle carries some by design.
  double AliasDb(const std::vector<float>& signal) {
    std::vector<std::complex<double>> x(N);
    for (unsigned int i = 0; i < N; ++i)
      x[i] = std::complex<double>(signal[i], 0.0);
    Fft(x);
    double harmonic = 0.0, alias = 0.0;
    for (unsigned int k = 1; k < N / 2; ++k) {
      const double p = std::norm(x[k]);
      if (k % M == 0)
        harmonic += p;
      else
        alias += p;
    }
    return 10.0 * std::log10(alias / harmonic);
  }

  float MeasurementFrequency() {
    return static_cast<float>(static_cast<double>(YSE::SAMPLERATE) * M / N);
  }

  // The naive waveforms, sampled at phase n * M / N.
  template <class Shape> std::vector<float> Naive(Shape shape) {
    std::vector<float> out(N);
    for (unsigned int n = 0; n < N; ++n) {
      const double p = static_cast<double>((n * M) % N) / N;
      out[n] = static_cast<float>(shape(p));
    }
    return out;
  }

} // namespace

TEST_SUITE("dsp") {

  // ─── blSaw ────────────────────────────────────────────────────────────────

  TEST_CASE("blSaw: matches the textbook polyBLEP sawtooth (#1055)") {
    TestHelpers::ScopedSampleRate rate(44100);
    // Not a divisor of 1: the jump lands anywhere in a sample. Rounded the way the
    // engine sees it, a float frequency over the rate.
    const double dt = static_cast<double>(static_cast<float>(0.0713 * 44100.0)) / 44100.0;
    blSaw saw;
    const std::vector<float> out =
        Render(1024, [&]() -> buffer& { return saw(static_cast<float>(dt * 44100.0)); });
    double p = 0.0;
    for (unsigned int n = 0; n < out.size(); ++n) {
      CAPTURE(n);
      const double expected = 2.0 * p - 1.0 - PolyBlep(p, dt);
      CHECK(out[n] == doctest::Approx(expected).epsilon(1e-5));
      p += dt;
      if (p >= 1.0) p -= 1.0;
    }
  }

  TEST_CASE("blSaw: away from the jump it is the exact ramp, and stays within -1..1 (#1055)") {
    TestHelpers::ScopedSampleRate rate(44100);
    blSaw saw;
    const double dt = 0.001;
    const std::vector<float> out = Render(2000, [&]() -> buffer& { return saw(44.1f); });
    // The first sample sits on the jump: halfway, 0.
    CHECK(out[0] == doctest::Approx(0.0).epsilon(1e-6));
    for (unsigned int n = 2; n < 998; ++n) {
      CAPTURE(n);
      CHECK(out[n] == doctest::Approx(2.0 * n * dt - 1.0).epsilon(1e-4));
    }
    for (float v : out) {
      CHECK(v >= -1.f);
      CHECK(v <= 1.f);
    }
  }

  TEST_CASE("blSaw: a negative frequency runs the ramp backwards (#1055)") {
    TestHelpers::ScopedSampleRate rate(44100);
    blSaw up;
    blSaw down;
    for (int block = 0; block < 6; ++block) {
      buffer& a = up(3141.f);
      buffer& b = down(-3141.f);
      for (unsigned int i = 0; i < a.getLength(); ++i) {
        CAPTURE(i);
        CHECK(b.getPtr()[i] == doctest::Approx(-a.getPtr()[i]).epsilon(1e-5));
      }
    }
  }

  TEST_CASE("blSaw: far less aliasing than a naive sawtooth (#1055)") {
    // The measurement that justifies polyBLEP over the old naive ramp: at
    // about 2.6 kHz (44.1 kHz) the folded energy drops by about 15 dB (from
    // about -11 dB to -27 dB of the harmonic energy), most of what is left near Nyquist.
    TestHelpers::ScopedSampleRate rate(44100);
    blSaw saw;
    const std::vector<float> bl =
        Render(N, [&]() -> buffer& { return saw(MeasurementFrequency()); });
    const double naive = AliasDb(Naive([](double p) { return 2.0 * p - 1.0; }));
    const double limited = AliasDb(bl);
    MESSAGE("saw aliasing: naive " << naive << " dB, polyBLEP " << limited << " dB");
    CHECK(limited < naive - 10.0);
    CHECK(limited < -20.0);
  }

  TEST_CASE("blSaw: sync restarts the cycle on a rise across the threshold (#1055)") {
    TestHelpers::ScopedSampleRate rate(44100);
    buffer frequency(YSE::STANDARD_BUFFERSIZE);
    frequency = 1000.f;
    buffer sync(YSE::STANDARD_BUFFERSIZE);
    sync = 0.f;
    for (unsigned int i = 50; i < sync.getLength(); ++i)
      sync.getPtr()[i] = 1.f;

    blSaw synced;
    blSaw fresh;
    buffer& out = synced(frequency, sync);
    buffer& reference = fresh(frequency);
    for (unsigned int i = 0; i < 50; ++i)
      CHECK(out.getPtr()[i] == reference.getPtr()[i]);
    for (unsigned int i = 50; i < out.getLength(); ++i) {
      CAPTURE(i);
      CHECK(out.getPtr()[i] == doctest::Approx(reference.getPtr()[i - 50]).epsilon(1e-5));
    }

    // Staying high is not a rise, and a higher threshold is never crossed.
    blSaw high;
    buffer ones(YSE::STANDARD_BUFFERSIZE);
    ones = 1.f;
    blSaw plain;
    CHECK(TestHelpers::buffersNearlyEqual(high(frequency, ones), plain(frequency), 0.f));
    blSaw raised;
    raised.syncThreshold(2.f);
    CHECK(raised.syncThreshold() == 2.f);
    blSaw plain2;
    CHECK(TestHelpers::buffersNearlyEqual(raised(frequency, sync), plain2(frequency), 0.f));
  }

  TEST_CASE("blSaw: phase jumps, held and out-of-range frequencies (#1055)") {
    TestHelpers::ScopedSampleRate rate(44100);
    blSaw saw;
    saw.phase(1.25f);
    CHECK(saw.phase() == doctest::Approx(0.25f));
    // Frequency 0 holds the phase: a constant at 2 * 0.25 - 1.
    buffer& still = saw(0.f);
    for (unsigned int i = 0; i < still.getLength(); ++i)
      CHECK(still.getPtr()[i] == doctest::Approx(-0.5f));
    // Not a number holds as well.
    saw(std::numeric_limits<float>::quiet_NaN());
    CHECK(saw.phase() == doctest::Approx(0.25f));
    // Beyond Nyquist is held at Nyquist, and the output stays finite and in range.
    buffer& fast = saw(1e9f);
    for (unsigned int i = 0; i < fast.getLength(); ++i) {
      CHECK(std::isfinite(fast.getPtr()[i]));
      CHECK(std::fabs(fast.getPtr()[i]) <= 1.f);
    }
  }

  TEST_CASE("blSaw: follows the live sample rate (#1055)") {
    // Rate / 100 Hz is a 100-sample period at any rate.
    std::vector<float> at44;
    {
      TestHelpers::ScopedSampleRate rate(44100);
      blSaw saw;
      at44 = Render(512, [&]() -> buffer& { return saw(441.f); });
    }
    TestHelpers::ScopedSampleRate rate(96000);
    blSaw saw;
    const std::vector<float> at96 = Render(512, [&]() -> buffer& { return saw(960.f); });
    for (unsigned int i = 0; i < at44.size(); ++i)
      CHECK(at96[i] == doctest::Approx(at44[i]).epsilon(1e-4));
  }

  // ─── blRect ───────────────────────────────────────────────────────────────

  TEST_CASE("blRect: matches the textbook polyBLEP pulse at several widths (#1055)") {
    TestHelpers::ScopedSampleRate rate(44100);
    const double dt = static_cast<double>(static_cast<float>(0.0613 * 44100.0)) / 44100.0;
    for (float w : {0.5f, 0.3f, 0.8f}) {
      CAPTURE(w);
      blRect rect;
      rect.width(w);
      CHECK(rect.width() == w);
      const std::vector<float> out =
          Render(1024, [&]() -> buffer& { return rect(static_cast<float>(dt * 44100.0)); });
      double p = 0.0;
      const double width = static_cast<double>(w);
      for (unsigned int n = 0; n < out.size(); ++n) {
        CAPTURE(n);
        double fall = p - width;
        if (fall < 0.0) fall += 1.0;
        const double expected = (p < width ? 1.0 : -1.0) + PolyBlep(p, dt) - PolyBlep(fall, dt);
        CHECK(out[n] == doctest::Approx(expected).epsilon(1e-5));
        p += dt;
        if (p >= 1.0) p -= 1.0;
      }
    }
  }

  TEST_CASE("blRect: the mean is 2 * width - 1; width wraps; 0 is a constant -1 (#1055)") {
    TestHelpers::ScopedSampleRate rate(44100);
    // 64 samples per cycle, 1024 samples: whole cycles.
    blRect rect;
    rect.width(0.25f);
    const std::vector<float> out = Render(1024, [&]() -> buffer& { return rect(44100.f / 64.f); });
    double sum = 0.0;
    for (float v : out)
      sum += v;
    CHECK(sum / out.size() == doctest::Approx(-0.5).epsilon(1e-3));

    blRect wrapped;
    wrapped.width(1.25f);
    blRect plain;
    plain.width(0.25f);
    CHECK(TestHelpers::buffersNearlyEqual(wrapped(1000.f), plain(1000.f), 1e-6f));

    blRect empty;
    empty.width(0.f);
    buffer& flat = empty(1000.f);
    for (unsigned int i = 0; i < flat.getLength(); ++i)
      CHECK(flat.getPtr()[i] == doctest::Approx(-1.f).epsilon(1e-6));
  }

  TEST_CASE("blRect: a per-sample width equals the constant one; sync (#1055)") {
    TestHelpers::ScopedSampleRate rate(44100);
    buffer frequency(YSE::STANDARD_BUFFERSIZE);
    frequency = 1500.f;
    buffer width(YSE::STANDARD_BUFFERSIZE);
    width = 0.3f;
    blRect bySignal;
    blRect byValue;
    byValue.width(0.3f);
    CHECK(TestHelpers::buffersNearlyEqual(bySignal(frequency, width), byValue(frequency), 0.f));

    buffer sync(YSE::STANDARD_BUFFERSIZE);
    sync = 0.f;
    for (unsigned int i = 70; i < sync.getLength(); ++i)
      sync.getPtr()[i] = 1.f;
    blRect synced;
    blRect fresh;
    buffer& out = synced(frequency, width, sync);
    buffer& reference = fresh(frequency, width);
    for (unsigned int i = 70; i < out.getLength(); ++i)
      CHECK(out.getPtr()[i] == doctest::Approx(reference.getPtr()[i - 70]).epsilon(1e-5));
  }

  TEST_CASE("blRect: far less aliasing than a naive square (#1055)") {
    TestHelpers::ScopedSampleRate rate(44100);
    blRect rect;
    const std::vector<float> bl =
        Render(N, [&]() -> buffer& { return rect(MeasurementFrequency()); });
    const double naive = AliasDb(Naive([](double p) { return p < 0.5 ? 1.0 : -1.0; }));
    const double limited = AliasDb(bl);
    MESSAGE("square aliasing: naive " << naive << " dB, polyBLEP " << limited << " dB");
    CHECK(limited < naive - 10.0);
  }

  // ─── blTri ────────────────────────────────────────────────────────────────

  TEST_CASE("blTri: away from the corners it is the exact triangle (#1055)") {
    TestHelpers::ScopedSampleRate rate(44100);
    const double dt = static_cast<double>(static_cast<float>(0.0093 * 44100.0)) / 44100.0;
    for (float duty : {0.5f, 0.2f, 0.9f}) {
      CAPTURE(duty);
      blTri tri;
      tri.duty(duty);
      CHECK(tri.duty() == duty);
      const std::vector<float> out =
          Render(1024, [&]() -> buffer& { return tri(static_cast<float>(dt * 44100.0)); });
      const double d = static_cast<double>(duty);
      double p = 0.0;
      for (unsigned int n = 0; n < out.size(); ++n) {
        CAPTURE(n);
        const double naive = p < d ? -1.0 + 2.0 * p / d : 1.0 - 2.0 * (p - d) / (1.0 - d);
        const bool nearCorner = p < dt || p > 1.0 - dt || std::fabs(p - d) < dt;
        if (nearCorner) {
          // The corner is rounded inwards, never past the peaks.
          CHECK(out[n] >= -1.f);
          CHECK(out[n] <= 1.f);
        } else {
          CHECK(out[n] == doctest::Approx(naive).epsilon(1e-4));
        }
        p += dt;
        if (p >= 1.0) p -= 1.0;
      }
    }
  }

  TEST_CASE("blTri: the corner sample is the polyBLAMP-rounded value (#1055)") {
    // dt = 1/8, duty 0.5: the first sample sits on the low corner. The slope
    // turns by 2/(0.25) per cycle = 1 per sample, and the residual at the
    // corner is 1/6.
    TestHelpers::ScopedSampleRate rate(44000);
    blTri tri;
    buffer& out = tri(5500.f, 8);
    CHECK(out.getPtr()[0] == doctest::Approx(-1.0 + 1.0 / 6.0).epsilon(1e-6));
    CHECK(out.getPtr()[4] == doctest::Approx(1.0 - 1.0 / 6.0).epsilon(1e-6));
    CHECK(out.getPtr()[2] == doctest::Approx(0.0).epsilon(1e-6));
  }

  TEST_CASE("blTri: duty is wrapped, then held within 0.05..0.95 (#1055)") {
    TestHelpers::ScopedSampleRate rate(44100);
    blTri zero;
    zero.duty(0.f);
    blTri low;
    low.duty(0.05f);
    CHECK(TestHelpers::buffersNearlyEqual(zero(800.f), low(800.f), 1e-6f));
    blTri wrapped;
    wrapped.duty(1.5f);
    blTri half;
    CHECK(TestHelpers::buffersNearlyEqual(wrapped(800.f), half(800.f), 1e-6f));
    blTri nan;
    nan.duty(std::numeric_limits<float>::quiet_NaN());
    blTri half2;
    CHECK(TestHelpers::buffersNearlyEqual(nan(800.f), half2(800.f), 0.f));

    buffer frequency(YSE::STANDARD_BUFFERSIZE);
    frequency = 800.f;
    buffer duty(YSE::STANDARD_BUFFERSIZE);
    duty = 0.97f;
    blTri bySignal;
    blTri byValue;
    byValue.duty(0.95f);
    CHECK(TestHelpers::buffersNearlyEqual(bySignal(frequency, duty), byValue(frequency), 1e-6f));
  }

  TEST_CASE("blTri: less aliasing than a naive triangle (#1055)") {
    TestHelpers::ScopedSampleRate rate(44100);
    blTri tri;
    const std::vector<float> bl =
        Render(N, [&]() -> buffer& { return tri(MeasurementFrequency()); });
    const double naive =
        AliasDb(Naive([](double p) { return p < 0.5 ? -1.0 + 4.0 * p : 3.0 - 4.0 * p; }));
    const double limited = AliasDb(bl);
    MESSAGE("triangle aliasing: naive " << naive << " dB, polyBLAMP " << limited << " dB");
    CHECK(limited < naive - 6.0);
  }

  TEST_CASE("blTri: sync restarts at the low corner (#1055)") {
    TestHelpers::ScopedSampleRate rate(44100);
    buffer frequency(YSE::STANDARD_BUFFERSIZE);
    frequency = 700.f;
    buffer duty(YSE::STANDARD_BUFFERSIZE);
    duty = 0.5f;
    buffer sync(YSE::STANDARD_BUFFERSIZE);
    sync = 0.f;
    for (unsigned int i = 90; i < sync.getLength(); ++i)
      sync.getPtr()[i] = 0.75f;
    blTri synced;
    blTri fresh;
    buffer& out = synced(frequency, duty, sync);
    buffer& reference = fresh(frequency, duty);
    for (unsigned int i = 90; i < out.getLength(); ++i)
      CHECK(out.getPtr()[i] == doctest::Approx(reference.getPtr()[i - 90]).epsilon(1e-5));
  }

  // ─── all three ────────────────────────────────────────────────────────────

  TEST_CASE("band-limited oscillators: block length and allocation freedom (#1055)") {
    blSaw saw;
    blRect rect;
    blTri tri;
    buffer frequency(YSE::STANDARD_BUFFERSIZE);
    frequency = 440.f;
    buffer shape(YSE::STANDARD_BUFFERSIZE);
    shape = 0.4f;
    buffer sync(YSE::STANDARD_BUFFERSIZE);
    sync = 0.f;
    CHECK(saw(440.f, 32).getLength() == 32);
    CHECK(rect(440.f, YSE::STANDARD_BUFFERSIZE * 4).getLength() == YSE::STANDARD_BUFFERSIZE);
    CHECK(tri(440.f).getLength() == YSE::STANDARD_BUFFERSIZE);
    {
      TestHelpers::ProbeScope probe;
      saw(440.f);
      saw(440.f, 32);
      saw(frequency);
      saw(frequency, sync);
      rect(440.f);
      rect(frequency);
      rect(frequency, shape);
      rect(frequency, shape, sync);
      tri(440.f);
      tri(frequency);
      tri(frequency, shape);
      tri(frequency, shape, sync);
      saw.phase(0.5f).syncThreshold(0.2f);
      rect.width(0.1f).phase(0.f);
      tri.duty(0.7f);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
