// Tests for YSE::DSP::phaseShift (YseEngine/dsp/filters/phaseShift.hpp), the
// second-order allpass behind ~phaseshift (issue #1098): the coefficients
// against the cookbook allpass written out here, the impulse response against
// an independent direct-form loop, unit gain and the phase on sines across the
// band (-180 degrees at the center frequency, so a dry/wet mix notches it),
// the Q's width, stability, the live sample rate, the per-sample frequency
// and Q signals, non-finite input and settings, overflow, clear(), block size
// independence and allocation freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <random>
#include <vector>
#include "dsp/filters/phaseShift.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::phaseShift;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  // Runs `fn` over `in` in blocks of `block` samples.
  std::vector<float> Run(phaseShift& fn, const std::vector<float>& in, unsigned int block = N) {
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

  // The cookbook allpass, normalised, computed here independently of the
  // class: H(z) = (b2 + b1 z^-1 + z^-2) / (1 + b1 z^-1 + b2 z^-2).
  struct Expected {
    double b1, b2;
  };

  Expected Closed(double hz, double q, double sr) {
    const double w = 2.0 * PI * hz / sr;
    const double alpha = std::sin(w) / (2.0 * q);
    return {-2.0 * std::cos(w) / (1.0 + alpha), (1.0 - alpha) / (1.0 + alpha)};
  }

  std::vector<double> Direct(const Expected& e, const std::vector<float>& x) {
    std::vector<double> y(x.size(), 0.0);
    for (std::size_t n = 0; n < x.size(); ++n) {
      const double x1 = n >= 1 ? x[n - 1] : 0.0;
      const double x2 = n >= 2 ? x[n - 2] : 0.0;
      const double y1 = n >= 1 ? y[n - 1] : 0.0;
      const double y2 = n >= 2 ? y[n - 2] : 0.0;
      y[n] = e.b2 * x[n] + e.b1 * x1 + x2 - e.b1 * y1 - e.b2 * y2;
    }
    return y;
  }

  std::complex<double> Response(const Expected& e, double w) {
    const std::complex<double> z1 = std::polar(1.0, -w);
    return (e.b2 + e.b1 * z1 + z1 * z1) / (1.0 + e.b1 * z1 + e.b2 * z1 * z1);
  }

  // The amplitude and phase of the response to a unit sine at `hz` at
  // 48 kHz, projected onto a sine and a cosine over the second half second,
  // once the transient has died. Exact for an even `hz`. With `dry`, the
  // input is added to the output first (a phaser's mix).
  std::complex<double> SineResponse(phaseShift& fn, double hz, bool dry = false) {
    const double w = 2.0 * PI * hz / 48000.0;
    constexpr int total = 48000;
    constexpr int measure = 24000;
    double s = 0.0;
    double c = 0.0;
    for (int n = 0; n < total; ++n) {
      const float x = static_cast<float>(std::sin(w * n));
      double y = fn.process(x);
      if (dry) y += x;
      if (n >= total - measure) {
        s += y * std::sin(w * n);
        c += y * std::cos(w * n);
      }
    }
    // y = A sin(wn + phi): s ~ A cos(phi) M / 2, c ~ A sin(phi) M / 2.
    return std::complex<double>(s, c) * (2.0 / measure);
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("phaseShift: the coefficients against the cookbook allpass, with clamping (#1098)") {
    const float sr = 48000.f;
    for (const float hz : {20.f, 300.f, 1000.f, 5000.f, 12000.f, 20000.f}) {
      for (const float q : {0.01f, 0.5f, 1.f, 4.f, 50.f, 10000.f}) {
        CAPTURE(hz);
        CAPTURE(q);
        const phaseShift::coefficients c = phaseShift::Coefficients(hz, q, sr);
        const Expected e = Closed(hz, q, sr);
        // In double precision, not rounded through a float. The frequency
        // is a float, so compare against the closed form at that float.
        CHECK(std::fabs(c.b1 - e.b1) <= 1e-12);
        CHECK(std::fabs(c.b2 - e.b2) <= 1e-12);
        // Both poles inside the unit circle.
        CHECK(std::fabs(c.b2) < 1.0);
        CHECK(std::fabs(c.b1) < 1.0 + c.b2);
      }
    }
    // The Q clamps to MIN_Q..MAX_Q, the frequency to just inside 0..SR/2.
    const phaseShift::coefficients low = phaseShift::Coefficients(1000.f, phaseShift::MIN_Q, sr);
    const phaseShift::coefficients high = phaseShift::Coefficients(1000.f, phaseShift::MAX_Q, sr);
    for (const float q : {0.f, -5.f, 0.001f})
      CHECK(phaseShift::Coefficients(1000.f, q, sr).b2 == low.b2);
    CHECK(phaseShift::Coefficients(1000.f, 1e6f, sr).b2 == high.b2);
    CHECK(phaseShift::Coefficients(30000.f, 5.f, sr).b1 ==
          phaseShift::Coefficients(24000.f, 5.f, sr).b1);
    CHECK(phaseShift::Coefficients(-100.f, 5.f, sr).b1 ==
          phaseShift::Coefficients(0.f, 5.f, sr).b1);
    CHECK(phaseShift::Coefficients(0.f, 5.f, sr).b2 < 1.0);
    // A high Q near 0 Hz keeps the poles off the unit circle, where the
    // float coefficients of filterCoefficients::Compute put them.
    CHECK(phaseShift::Coefficients(1.f, phaseShift::MAX_Q, sr).b2 < 1.0);
    CHECK(YSE::DSP::filterCoefficients::Compute(YSE::DSP::filterCoefficients::ALLPASS, 1.f, 1.f,
                                                phaseShift::MAX_Q, sr)
              .b2 == 1.f);
    // Not finite, or no sample rate: 0 for both, a two-sample delay.
    for (const phaseShift::coefficients& zero :
         {phaseShift::Coefficients(std::numeric_limits<float>::quiet_NaN(), 5.f, sr),
          phaseShift::Coefficients(1000.f, std::numeric_limits<float>::infinity(), sr),
          phaseShift::Coefficients(1000.f, 5.f, 0.f)}) {
      CHECK(zero.b1 == 0.0);
      CHECK(zero.b2 == 0.0);
    }
  }

  TEST_CASE("phaseShift: defaults and invalid settings (#1098)") {
    TestHelpers::ScopedSampleRate rate(48000);
    phaseShift fn;
    CHECK(fn.frequency() == phaseShift::DEFAULT_FREQUENCY);
    CHECK(fn.q() == phaseShift::DEFAULT_Q);
    CHECK(fn.frequency() == 1000.f);
    CHECK(fn.q() == 1.f);
    CHECK(fn.coefficient().b2 == phaseShift::Coefficients(1000.f, 1.f, 48000.f).b2);
    fn.frequency(500.f).q(4.f);
    fn.frequency(std::numeric_limits<float>::quiet_NaN())
        .q(-std::numeric_limits<float>::infinity());
    CHECK(fn.frequency() == 500.f);
    CHECK(fn.q() == 4.f);
    // Silence in, silence out.
    phaseShift silent;
    const std::vector<float> out = Run(silent, std::vector<float>(N, 0.f));
    CHECK(std::all_of(out.begin(), out.end(), [](float v) { return v == 0.f; }));
  }

  TEST_CASE("phaseShift: impulse response against the direct form (#1098)") {
    TestHelpers::ScopedSampleRate rate(48000);
    for (const float q : {0.3f, 1.f, 8.f, 200.f}) {
      for (const float hz : {100.f, 2000.f, 15000.f}) {
        CAPTURE(q);
        CAPTURE(hz);
        phaseShift fn;
        fn.frequency(hz).q(q);
        const Expected e = Closed(hz, q, 48000.0);
        const std::vector<float> x = Impulse(8 * N);
        const std::vector<double> expected = Direct(e, x);
        const std::vector<float> h = Run(fn, x);
        for (std::size_t n = 0; n < h.size(); ++n) {
          CAPTURE(n);
          CHECK(std::fabs(h[n] - expected[n]) <= 1e-5);
        }
        // h(0) = b2, h(1) = b1 (1 - b2).
        CHECK(h[0] == doctest::Approx(e.b2).epsilon(1e-6));
        CHECK(h[1] == doctest::Approx(e.b1 * (1.0 - e.b2)).epsilon(1e-5));
        // An allpass passes all the energy: the sum of h^2 is 1.
        double energy = 0.0;
        for (const float v : h)
          energy += static_cast<double>(v) * v;
        // Where the ringing has died within the impulse.
        if (q < 100.f && hz >= 2000.f) CHECK(energy == doctest::Approx(1.0).epsilon(1e-4));
      }
    }
  }

  TEST_CASE("phaseShift: unit gain everywhere, the phase of the closed form, -180 degrees at "
            "the center (#1098)") {
    TestHelpers::ScopedSampleRate rate(48000);
    for (const float q : {0.5f, 1.f, 5.f}) {
      for (const double hz : {50.0, 400.0, 1000.0, 2500.0, 8000.0, 20000.0}) {
        CAPTURE(q);
        CAPTURE(hz);
        phaseShift fn;
        fn.frequency(1000.f).q(q);
        const std::complex<double> measured = SineResponse(fn, hz);
        const std::complex<double> expected =
            Response(Closed(1000.0, q, 48000.0), 2.0 * PI * hz / 48000.0);
        CHECK(std::abs(expected) == doctest::Approx(1.0).epsilon(1e-9));
        CHECK(std::abs(measured) == doctest::Approx(1.0).epsilon(1e-3));
        CHECK(std::abs(measured - expected) < 2e-3);
      }
    }
    // At the center frequency the output is the input inverted: mixed with
    // the dry signal the center cancels, a phaser's notch.
    for (const float hz : {200.f, 1000.f, 6000.f}) {
      CAPTURE(hz);
      phaseShift fn;
      fn.frequency(hz).q(2.f);
      const std::complex<double> centre = SineResponse(fn, hz);
      CHECK(centre.real() == doctest::Approx(-1.0).epsilon(1e-3));
      CHECK(std::fabs(centre.imag()) < 1e-3);
      fn.clear();
      CHECK(std::abs(SineResponse(fn, hz, true)) < 2e-3);
      // An octave away the mix still passes most of the signal.
      fn.clear();
      CHECK(std::abs(SineResponse(fn, 2.0 * hz, true)) > 1.0);
    }
    // Below the center the phase lags by less than 180 degrees, above by
    // more; the higher the Q, the closer to 0 and -360 an octave away.
    const auto lag = [](float q, double hz) {
      const std::complex<double> h = Response(Closed(1000.0, q, 48000.0), 2.0 * PI * hz / 48000.0);
      double phase = -std::arg(h); // lag in 0..2 pi
      if (phase < 0.0) phase += 2.0 * PI;
      return phase;
    };
    CHECK(lag(1.f, 500.0) < PI);
    CHECK(lag(1.f, 2000.0) > PI);
    CHECK(lag(10.f, 500.0) < lag(1.f, 500.0));
    CHECK(lag(10.f, 2000.0) > lag(1.f, 2000.0));
    CHECK(lag(10.f, 500.0) < 0.1 * PI);
  }

  TEST_CASE("phaseShift: stable at every Q and frequency (#1098)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::mt19937 gen(1098); // NOLINT(cert-msc51-cpp,bugprone-random-generator-seed): repeatable
    std::uniform_real_distribution<float> noise(-1.f, 1.f);
    for (const float q : {0.01f, 1.f, 100.f, 10000.f}) {
      for (const float hz : {0.f, 20.f, 1000.f, 12000.f, 23990.f, 24000.f}) {
        CAPTURE(q);
        CAPTURE(hz);
        phaseShift fn;
        fn.frequency(hz).q(q);
        double peak = 0.0;
        buffer in(N);
        for (int block = 0; block < 48000 * 2 / static_cast<int>(N); ++block) {
          for (int i = 0; i < static_cast<int>(N); ++i)
            in.getPtr()[i] = noise(gen);
          buffer& out = fn(in);
          for (int i = 0; i < static_cast<int>(N); ++i)
            peak = std::max(peak, static_cast<double>(std::fabs(out.getPtr()[i])));
        }
        CHECK(std::isfinite(peak));
        // Ten seconds of silence: the ringing does not grow, and dies where
        // the Q lets it.
        buffer silence(N);
        silence = 0.f;
        double last = 0.0;
        for (int block = 0; block < 48000 * 10 / static_cast<int>(N); ++block) {
          buffer& out = fn(silence);
          last = 0.0;
          for (int i = 0; i < static_cast<int>(N); ++i)
            last = std::max(last, static_cast<double>(std::fabs(out.getPtr()[i])));
        }
        CHECK(std::isfinite(last));
        CHECK(last <= 2.0 * peak + 1e-6);
        if (q <= 1.f && hz >= 20.f && hz <= 23990.f) CHECK(last <= 1e-6 * peak);
      }
    }
  }

  TEST_CASE("phaseShift: the coefficients follow the live sample rate (#1098)") {
    TestHelpers::ScopedSampleRate rate(48000);
    phaseShift fn;
    fn.frequency(2000.f).q(3.f);
    CHECK(std::fabs(fn.coefficient().b1 - Closed(2000.0, 3.0, 48000.0).b1) < 1e-6);
    YSE::SAMPLERATE = 96000;
    const Expected e = Closed(2000.0, 3.0, 96000.0);
    CHECK(std::fabs(fn.coefficient().b1 - e.b1) < 1e-6);
    CHECK(std::fabs(fn.coefficient().b2 - e.b2) < 1e-6);
    fn.clear();
    const std::vector<float> x = Impulse(N);
    const std::vector<double> expected = Direct(e, x);
    const std::vector<float> h = Run(fn, x);
    for (int n = 0; n < static_cast<int>(N); ++n)
      CHECK(std::fabs(h[n] - expected[n]) <= 1e-5);
    // Above the new Nyquist the frequency clamps there.
    YSE::SAMPLERATE = 22050;
    fn.frequency(15000.f);
    CHECK(fn.coefficient().b1 == phaseShift::Coefficients(11025.f, 3.f, 22050.f).b1);
  }

  TEST_CASE("phaseShift: frequency and Q signals are read per sample (#1098)") {
    TestHelpers::ScopedSampleRate rate(48000);
    phaseShift fn;
    phaseShift reference;
    buffer in(N);
    buffer frequency(N);
    buffer q(N);
    std::vector<float> x(N);
    for (int i = 0; i < static_cast<int>(N); ++i) {
      x[i] = std::sin(static_cast<float>(i) * 0.3f);
      in.getPtr()[i] = x[i];
      frequency.getPtr()[i] = 300.f + static_cast<float>(i) * 50.f;
      q.getPtr()[i] = 0.5f + static_cast<float>(i) * 0.1f;
    }
    // A non-finite value keeps the previous value for that sample.
    frequency.getPtr()[10] = std::numeric_limits<float>::quiet_NaN();
    q.getPtr()[20] = std::numeric_limits<float>::infinity();
    buffer& out = fn(in, frequency, q);
    REQUIRE(out.getLength() == N);
    // An independent direct-form loop over the same per-sample settings.
    double x1 = 0.0;
    double x2 = 0.0;
    double y1 = 0.0;
    double y2 = 0.0;
    float hz = phaseShift::DEFAULT_FREQUENCY;
    float qq = phaseShift::DEFAULT_Q;
    for (int i = 0; i < static_cast<int>(N); ++i) {
      CAPTURE(i);
      if (std::isfinite(frequency.getPtr()[i])) hz = frequency.getPtr()[i];
      if (std::isfinite(q.getPtr()[i])) qq = q.getPtr()[i];
      const Expected e = Closed(hz, qq, 48000.0);
      const double y = e.b2 * x[i] + e.b1 * x1 + x2 - e.b1 * y1 - e.b2 * y2;
      x2 = x1;
      x1 = x[i];
      y2 = y1;
      y1 = y;
      CHECK(std::fabs(out.getPtr()[i] - y) <= 1e-5);
      // The same as setting the values one sample at a time.
      reference.frequency(frequency.getPtr()[i]).q(q.getPtr()[i]);
      CHECK(reference.process(x[i]) == out.getPtr()[i]);
    }
    CHECK(fn.frequency() == frequency.getPtr()[N - 1]);
    CHECK(fn.q() == q.getPtr()[N - 1]);

    // A shorter control signal shortens the block.
    buffer shortQ(N / 2);
    shortQ = 3.f;
    CHECK(fn(in, frequency, shortQ).getLength() == N / 2);
  }

  TEST_CASE("phaseShift: non-finite input is skipped, overflow restarts, clear() resets "
            "(#1098)") {
    TestHelpers::ScopedSampleRate rate(48000);
    phaseShift fn;
    fn.frequency(3000.f).q(2.f);
    phaseShift clean;
    clean.frequency(3000.f).q(2.f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const std::vector<float> dirty = Run(fn, {1.f, nan, 0.5f, inf, -inf, 0.f, 0.f});
    const std::vector<float> reference = Run(clean, {1.f, 0.5f, 0.f, 0.f});
    CHECK(dirty[0] == reference[0]);
    CHECK(dirty[1] == reference[0]); // holds
    CHECK(dirty[2] == reference[1]);
    CHECK(dirty[3] == reference[1]);
    CHECK(dirty[4] == reference[1]);
    CHECK(dirty[5] == reference[2]);
    CHECK(dirty[6] == reference[3]);

    // clear(): the next impulse response is the first one again.
    fn.clear();
    clean.clear();
    for (int i = 0; i < 8; ++i)
      CHECK(fn.process(i == 0 ? 1.f : 0.f) == clean.process(i == 0 ? 1.f : 0.f));
    fn.clear();
    CHECK(fn.process(0.f) == 0.f);

    // An output beyond a float starts the filter again from silence.
    phaseShift loud;
    loud.frequency(100.f).q(0.5f);
    const float big = std::numeric_limits<float>::max();
    // Alternating full-scale samples: h(0) - h(1) is above 1, so the second
    // output already exceeds a float.
    bool finite = true;
    for (int i = 0; i < 8; ++i)
      finite = finite && std::isfinite(loud.process(i % 2 == 0 ? big : -big));
    for (int i = 0; i < 8; ++i)
      finite = finite && std::isfinite(loud.process(0.f));
    CHECK(finite);
    loud.clear();
    phaseShift fresh;
    fresh.frequency(100.f).q(0.5f);
    for (int i = 0; i < 8; ++i)
      CHECK(loud.process(i == 0 ? 1.f : 0.f) == fresh.process(i == 0 ? 1.f : 0.f));
  }

  TEST_CASE("phaseShift: blocks, single samples and buffer clamping (#1098)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::vector<float> in(5 * N);
    for (std::size_t i = 0; i < in.size(); ++i)
      in[i] =
          std::sin(static_cast<float>(i) * 0.11f) + 0.3f * std::sin(static_cast<float>(i) * 1.7f);
    phaseShift whole;
    whole.frequency(3000.f).q(1.5f);
    const std::vector<float> reference = Run(whole, in);
    for (const unsigned int block : {1u, 7u, 64u, N}) {
      CAPTURE(block);
      phaseShift fn;
      fn.frequency(3000.f).q(1.5f);
      CHECK(Run(fn, in, block) == reference);
    }
    phaseShift single;
    single.frequency(3000.f).q(1.5f);
    for (std::size_t i = 0; i < in.size(); ++i)
      CHECK(single.process(in[i]) == reference[i]);

    // A longer buffer is processed up to STANDARD_BUFFERSIZE.
    phaseShift fn;
    buffer longer(2 * N);
    longer = 1.f;
    CHECK(fn(longer).getLength() == N);
  }

  TEST_CASE("phaseShift: processing allocates nothing (#1098)") {
    TestHelpers::ScopedSampleRate rate(48000);
    phaseShift fn;
    buffer in(N);
    buffer shorter(N / 2);
    buffer frequency(N);
    buffer q(N);
    in = 0.5f;
    shorter = 0.25f;
    q = 9.f;
    for (int i = 0; i < static_cast<int>(N); ++i)
      frequency.getPtr()[i] = 500.f + static_cast<float>(i);
    fn(in);
    {
      TestHelpers::ProbeScope probe;
      fn.frequency(1200.f).q(3.f);
      fn(in);
      fn(shorter);
      fn(in, frequency, q);
      (void)fn.process(0.1f);
      (void)fn.coefficient();
      fn.clear();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
