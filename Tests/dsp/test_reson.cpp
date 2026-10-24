// Tests for YSE::DSP::reson (YseEngine/dsp/filters/reson.hpp), the resonant
// bandpass behind ~reson (issue #1088): the coefficients against their
// closed form, the impulse response against an independent direct-form loop,
// the gain at the center frequency, the gain on a sine against the magnitude
// response, stability at a high Q, the live sample rate, the per-sample gain,
// frequency and Q signals, non-finite input and settings, clear(), block size
// independence and allocation freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <random>
#include <vector>
#include "dsp/filters/reson.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::reson;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  // Runs `fn` over `in` in blocks of `block` samples.
  std::vector<float> Run(reson& fn, const std::vector<float>& in, unsigned int block = N) {
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

  // The closed form, computed here independently of the class.
  struct Expected {
    double a0, r, b1, b2;
  };

  Expected Closed(double gain, double hz, double q, double sr) {
    Expected e{};
    e.r = std::exp(-PI * (hz / q) / sr);
    e.b1 = 2.0 * e.r * std::cos(2.0 * PI * hz / sr);
    e.b2 = -e.r * e.r;
    e.a0 = gain * (1.0 - e.r);
    return e;
  }

  // The direct form y(n) = a0 (x(n) - r x(n-2)) + b1 y(n-1) + b2 y(n-2).
  std::vector<double> Direct(const Expected& e, const std::vector<float>& x) {
    std::vector<double> y(x.size(), 0.0);
    for (std::size_t n = 0; n < x.size(); ++n) {
      const double x2 = n >= 2 ? x[n - 2] : 0.0;
      const double y1 = n >= 1 ? y[n - 1] : 0.0;
      const double y2 = n >= 2 ? y[n - 2] : 0.0;
      y[n] = e.a0 * (x[n] - e.r * x2) + e.b1 * y1 + e.b2 * y2;
    }
    return y;
  }

  // The steady-state gain at angular frequency w.
  double Gain(const Expected& e, double w) {
    const std::complex<double> z1 = std::polar(1.0, -w);
    return std::abs(e.a0 * (1.0 - e.r * z1 * z1) / (1.0 - e.b1 * z1 - e.b2 * z1 * z1));
  }

  // The amplitude of a unit sine at `hz` after `fn` at 48 kHz, projected
  // onto a sine and a cosine over the second half second, once the transient
  // has died. Exact for an even `hz`, a whole number of cycles there.
  double SineGain(reson& fn, double hz) {
    const double w = 2.0 * PI * hz / 48000.0;
    constexpr int total = 48000;
    constexpr int measure = 24000;
    double s = 0.0;
    double c = 0.0;
    for (int n = 0; n < total; ++n) {
      const float y = fn.process(static_cast<float>(std::sin(w * n)));
      if (n >= total - measure) {
        s += y * std::sin(w * n);
        c += y * std::cos(w * n);
      }
    }
    return 2.0 * std::sqrt(s * s + c * c) / measure;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("reson: the coefficients against their closed form, with clamping (#1088)") {
    const float sr = 48000.f;
    for (const float hz : {50.f, 1000.f, 5000.f, 20000.f, 24000.f}) {
      for (const float q : {0.01f, 0.5f, 2.f, 20.f, 500.f, 10000.f}) {
        CAPTURE(hz);
        CAPTURE(q);
        const reson::coefficients c = reson::Coefficients(0.7f, hz, q, sr);
        const Expected e = Closed(0.7, hz, q, sr);
        CHECK(c.r == doctest::Approx(e.r).epsilon(1e-9));
        CHECK(c.b1 == doctest::Approx(e.b1).epsilon(1e-9));
        CHECK(c.b2 == doctest::Approx(e.b2).epsilon(1e-9));
        CHECK(c.a0 == doctest::Approx(e.a0).epsilon(1e-6));
        // Poles inside the unit circle.
        CHECK(c.r < 1.0);
      }
    }
    // The Q clamps to MIN_Q..MAX_Q, the frequency to 0..SR/2.
    const reson::coefficients low = reson::Coefficients(1.f, 1000.f, reson::MIN_Q, sr);
    const reson::coefficients high = reson::Coefficients(1.f, 1000.f, reson::MAX_Q, sr);
    for (const float q : {0.f, -5.f, 0.001f, std::numeric_limits<float>::quiet_NaN()})
      CHECK(reson::Coefficients(1.f, 1000.f, q, sr).r == low.r);
    CHECK(reson::Coefficients(1.f, 1000.f, 1e6f, sr).r == high.r);
    CHECK(reson::Coefficients(1.f, 30000.f, 5.f, sr).b1 ==
          reson::Coefficients(1.f, 24000.f, 5.f, sr).b1);
    // A frequency of 0 (or below, or no sample rate, or not finite) passes
    // nothing, as does one too narrow for r to differ from 1.
    for (const reson::coefficients& zero :
         {reson::Coefficients(1.f, 0.f, 5.f, sr), reson::Coefficients(1.f, -100.f, 5.f, sr),
          reson::Coefficients(1.f, 1000.f, 5.f, 0.f),
          reson::Coefficients(1.f, std::numeric_limits<float>::quiet_NaN(), 5.f, sr),
          reson::Coefficients(1.f, 1e-15f, reson::MAX_Q, sr)}) {
      CHECK(zero.a0 == 0.0);
      CHECK(zero.r == 0.0);
      CHECK(zero.b1 == 0.0);
      CHECK(zero.b2 == 0.0);
    }
    // A non-finite gain reads as 0.
    CHECK(reson::Coefficients(std::numeric_limits<float>::infinity(), 1000.f, 5.f, sr).a0 == 0.0);
  }

  TEST_CASE("reson: defaults and invalid settings (#1088)") {
    TestHelpers::ScopedSampleRate rate(48000);
    reson fn;
    CHECK(fn.gain() == 0.f);
    CHECK(fn.frequency() == 0.f);
    CHECK(fn.q() == reson::MIN_Q);
    // Frequency 0: nothing through.
    const std::vector<float> out = Run(fn, std::vector<float>(N, 1.f));
    CHECK(std::all_of(out.begin(), out.end(), [](float v) { return v == 0.f; }));
    fn.gain(0.5f).frequency(500.f).q(4.f);
    fn.gain(std::numeric_limits<float>::quiet_NaN())
        .frequency(std::numeric_limits<float>::infinity())
        .q(-std::numeric_limits<float>::infinity());
    CHECK(fn.gain() == 0.5f);
    CHECK(fn.frequency() == 500.f);
    CHECK(fn.q() == 4.f);
    // A gain alone on a filter that passes nothing still passes nothing.
    reson silent;
    silent.gain(1.f);
    (void)silent.coefficient();
    silent.gain(2.f);
    CHECK(silent.coefficient().a0 == 0.0);
  }

  TEST_CASE("reson: impulse response against the direct form (#1088)") {
    TestHelpers::ScopedSampleRate rate(48000);
    for (const float q : {0.5f, 5.f, 50.f, 2000.f}) {
      for (const float hz : {200.f, 2000.f, 12000.f}) {
        CAPTURE(q);
        CAPTURE(hz);
        reson fn;
        fn.gain(0.8f).frequency(hz).q(q);
        const std::vector<float> x = Impulse(4 * N);
        const std::vector<double> expected = Direct(Closed(0.8, hz, q, 48000.0), x);
        const std::vector<float> h = Run(fn, x);
        for (std::size_t n = 0; n < h.size(); ++n) {
          CAPTURE(n);
          CHECK(h[n] == doctest::Approx(expected[n]).epsilon(1e-5));
        }
        // h(0) = a0, h(1) = a0 b1: the zeros enter at n = 2.
        const Expected e = Closed(0.8, hz, q, 48000.0);
        CHECK(h[0] == doctest::Approx(e.a0).epsilon(1e-6));
        CHECK(h[1] == doctest::Approx(e.a0 * e.b1).epsilon(1e-6));
      }
    }
  }

  TEST_CASE("reson: the gain at the center frequency is the gain set, and the sine gain "
            "follows the magnitude response (#1088)") {
    TestHelpers::ScopedSampleRate rate(48000);
    // Exactly `gain` at the center, by construction, for any frequency and Q.
    for (const double hz : {100.0, 1000.0, 7000.0, 20000.0}) {
      for (const double q : {0.3, 2.0, 30.0, 400.0}) {
        CAPTURE(hz);
        CAPTURE(q);
        CHECK(Gain(Closed(0.6, hz, q, 48000.0), 2.0 * PI * hz / 48000.0) ==
              doctest::Approx(0.6).epsilon(1e-9));
      }
    }
    for (const float q : {1.f, 10.f, 40.f}) {
      for (const double hz : {100.0, 500.0, 1000.0, 2000.0, 10000.0}) {
        CAPTURE(q);
        CAPTURE(hz);
        reson fn;
        fn.gain(0.5f).frequency(1000.f).q(q);
        const double expected = Gain(Closed(0.5, 1000.0, q, 48000.0), 2.0 * PI * hz / 48000.0);
        CHECK(SineGain(fn, hz) == doctest::Approx(expected).epsilon(1e-3));
      }
    }
    // A sine at the center comes out at the gain; Q 10 is about -3 dB at
    // the band edges cf +- cf / 20 and well down an octave away.
    reson fn;
    fn.gain(0.5f).frequency(1000.f).q(10.f);
    CHECK(SineGain(fn, 1000.0) == doctest::Approx(0.5).epsilon(1e-3));
    const Expected e = Closed(1.0, 1000.0, 10.0, 48000.0);
    CHECK(Gain(e, 2.0 * PI * 950.0 / 48000.0) ==
          doctest::Approx(1.0 / std::sqrt(2.0)).epsilon(0.03));
    CHECK(Gain(e, 2.0 * PI * 1050.0 / 48000.0) ==
          doctest::Approx(1.0 / std::sqrt(2.0)).epsilon(0.03));
    CHECK(20.0 * std::log10(Gain(e, 2.0 * PI * 2000.0 / 48000.0)) < -20.0);
    // At 0 Hz: gain (1 - r)^2 / (1 - b1 - b2), as the header states.
    reson dc;
    dc.gain(1.f).frequency(1000.f).q(2.f);
    const Expected d = Closed(1.0, 1000.0, 2.0, 48000.0);
    const std::vector<float> step = Run(dc, std::vector<float>(40 * N, 1.f));
    CHECK(step.back() ==
          doctest::Approx((1.0 - d.r) * (1.0 - d.r) / (1.0 - d.b1 - d.b2)).epsilon(1e-4));
    // A negative gain inverts.
    reson negative;
    negative.gain(-0.5f).frequency(1000.f).q(10.f);
    reson positive;
    positive.gain(0.5f).frequency(1000.f).q(10.f);
    const std::vector<float> h = Run(negative, Impulse(N));
    const std::vector<float> g = Run(positive, Impulse(N));
    for (int n = 0; n < static_cast<int>(N); ++n)
      CHECK(h[n] == -g[n]);
  }

  TEST_CASE("reson: stable at every Q and frequency (#1088)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::mt19937 gen(1088); // NOLINT(cert-msc51-cpp,bugprone-random-generator-seed): repeatable
    std::uniform_real_distribution<float> noise(-1.f, 1.f);
    for (const float q : {0.01f, 100.f, 10000.f, 1e9f}) {
      for (const float hz : {1.f, 20.f, 1000.f, 12000.f, 23999.f, 24000.f}) {
        CAPTURE(q);
        CAPTURE(hz);
        reson fn;
        fn.gain(1.f).frequency(hz).q(q);
        const reson::coefficients c = fn.coefficient();
        CHECK(c.r < 1.0); // pole radius below 1
        // Five seconds of full-scale noise: bounded by the sum of |h(n)|,
        // itself below |a0| (1 + r) / (1 - r)^2 for these poles.
        const double bound = std::fabs(c.a0) * (1.0 + c.r) / ((1.0 - c.r) * (1.0 - c.r)) + 1e-6;
        double peak = 0.0;
        buffer in(N);
        for (int block = 0; block < 48000 * 5 / static_cast<int>(N); ++block) {
          for (int i = 0; i < static_cast<int>(N); ++i)
            in.getPtr()[i] = noise(gen);
          buffer& out = fn(in);
          for (int i = 0; i < static_cast<int>(N); ++i)
            peak = std::max(peak, static_cast<double>(std::fabs(out.getPtr()[i])));
        }
        CHECK(std::isfinite(peak));
        CHECK(peak <= bound);
        // Then ten seconds of silence: the ringing stays bounded and, where
        // its time constant (1 / (1 - r) samples) is well under that, dies.
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
        CHECK(last <= bound);
        if (1.0 / (1.0 - c.r) < 48000.0) CHECK(last <= 1e-6 * peak);
      }
    }
  }

  TEST_CASE("reson: the coefficients follow the live sample rate (#1088)") {
    TestHelpers::ScopedSampleRate rate(48000);
    reson fn;
    fn.gain(1.f).frequency(2000.f).q(8.f);
    CHECK(fn.coefficient().b1 == doctest::Approx(Closed(1.0, 2000.0, 8.0, 48000.0).b1));
    YSE::SAMPLERATE = 96000;
    const Expected e = Closed(1.0, 2000.0, 8.0, 96000.0);
    CHECK(fn.coefficient().r == doctest::Approx(e.r).epsilon(1e-9));
    CHECK(fn.coefficient().b1 == doctest::Approx(e.b1).epsilon(1e-9));
    fn.clear();
    const std::vector<float> x = Impulse(N);
    const std::vector<double> expected = Direct(e, x);
    const std::vector<float> h = Run(fn, x);
    for (int n = 0; n < 16; ++n)
      CHECK(h[n] == doctest::Approx(expected[n]).epsilon(1e-5));
    // Above the new Nyquist the frequency clamps there.
    YSE::SAMPLERATE = 22050;
    fn.frequency(15000.f);
    CHECK(fn.coefficient().b1 == reson::Coefficients(1.f, 11025.f, 8.f, 22050.f).b1);
    // A gain change alone keeps r, b1 and b2.
    const reson::coefficients before = fn.coefficient();
    fn.gain(0.25f);
    const reson::coefficients after = fn.coefficient();
    CHECK(after.r == before.r);
    CHECK(after.b1 == before.b1);
    CHECK(after.a0 == doctest::Approx(0.25 * (1.0 - before.r)));
  }

  TEST_CASE("reson: gain, frequency and Q signals are read per sample (#1088)") {
    TestHelpers::ScopedSampleRate rate(48000);
    reson fn;
    reson reference;
    buffer in(N);
    buffer gain(N);
    buffer frequency(N);
    buffer q(N);
    std::vector<float> x(N);
    for (int i = 0; i < static_cast<int>(N); ++i) {
      x[i] = std::sin(static_cast<float>(i) * 0.3f);
      in.getPtr()[i] = x[i];
      gain.getPtr()[i] = 1.f - static_cast<float>(i) / static_cast<float>(N);
      frequency.getPtr()[i] = 300.f + static_cast<float>(i) * 50.f;
      q.getPtr()[i] = 1.f + static_cast<float>(i) * 0.25f;
    }
    // A non-finite value in any signal keeps the previous value for that sample.
    gain.getPtr()[5] = std::numeric_limits<float>::quiet_NaN();
    frequency.getPtr()[10] = std::numeric_limits<float>::quiet_NaN();
    q.getPtr()[20] = std::numeric_limits<float>::infinity();
    buffer& out = fn(in, gain, frequency, q);
    REQUIRE(out.getLength() == N);
    // An independent direct-form loop over the same per-sample settings.
    double x1 = 0.0;
    double x2 = 0.0;
    double y1 = 0.0;
    double y2 = 0.0;
    float g = 0.f;
    float hz = 0.f;
    float qq = reson::MIN_Q;
    for (int i = 0; i < static_cast<int>(N); ++i) {
      CAPTURE(i);
      if (std::isfinite(gain.getPtr()[i])) g = gain.getPtr()[i];
      if (std::isfinite(frequency.getPtr()[i])) hz = frequency.getPtr()[i];
      if (std::isfinite(q.getPtr()[i])) qq = q.getPtr()[i];
      const Expected e = Closed(g, hz, qq, 48000.0);
      const double y = e.a0 * (x[i] - e.r * x2) + e.b1 * y1 + e.b2 * y2;
      x2 = x1;
      x1 = x[i];
      y2 = y1;
      y1 = y;
      CHECK(out.getPtr()[i] == doctest::Approx(y).epsilon(1e-5));
      // The same as setting the values one sample at a time.
      reference.gain(gain.getPtr()[i]).frequency(frequency.getPtr()[i]).q(q.getPtr()[i]);
      CHECK(reference.process(x[i]) == out.getPtr()[i]);
    }
    CHECK(fn.gain() == gain.getPtr()[N - 1]);
    CHECK(fn.frequency() == frequency.getPtr()[N - 1]);
    CHECK(fn.q() == q.getPtr()[N - 1]);

    // A shorter control signal shortens the block.
    buffer shortQ(N / 2);
    shortQ = 3.f;
    CHECK(fn(in, gain, frequency, shortQ).getLength() == N / 2);
  }

  TEST_CASE("reson: non-finite input is skipped, clear() resets the state (#1088)") {
    TestHelpers::ScopedSampleRate rate(48000);
    reson fn;
    fn.gain(1.f).frequency(3000.f).q(7.f);
    reson clean;
    clean.gain(1.f).frequency(3000.f).q(7.f);
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
    fn.clear();
    clean.clear();
    CHECK(Run(fn, {0.f})[0] == 0.f);
    for (int i = 0; i < 4; ++i)
      CHECK(fn.process(i == 0 ? 1.f : 0.f) == clean.process(i == 0 ? 1.f : 0.f));
  }

  TEST_CASE("reson: blocks, single samples and buffer clamping (#1088)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::vector<float> in(5 * N);
    for (std::size_t i = 0; i < in.size(); ++i)
      in[i] =
          std::sin(static_cast<float>(i) * 0.11f) + 0.3f * std::sin(static_cast<float>(i) * 1.7f);
    reson whole;
    whole.gain(0.9f).frequency(3000.f).q(12.f);
    const std::vector<float> reference = Run(whole, in);
    for (const unsigned int block : {1u, 7u, 64u, N}) {
      CAPTURE(block);
      reson fn;
      fn.gain(0.9f).frequency(3000.f).q(12.f);
      CHECK(Run(fn, in, block) == reference);
    }
    reson single;
    single.gain(0.9f).frequency(3000.f).q(12.f);
    for (std::size_t i = 0; i < in.size(); ++i)
      CHECK(single.process(in[i]) == reference[i]);

    // A longer buffer is processed up to STANDARD_BUFFERSIZE.
    reson fn;
    buffer longer(2 * N);
    longer = 1.f;
    CHECK(fn(longer).getLength() == N);
  }

  TEST_CASE("reson: processing allocates nothing (#1088)") {
    TestHelpers::ScopedSampleRate rate(48000);
    reson fn;
    buffer in(N);
    buffer shorter(N / 2);
    buffer gain(N);
    buffer frequency(N);
    buffer q(N);
    in = 0.5f;
    shorter = 0.25f;
    gain = 0.7f;
    q = 9.f;
    for (int i = 0; i < static_cast<int>(N); ++i)
      frequency.getPtr()[i] = 500.f + static_cast<float>(i);
    fn(in);
    {
      TestHelpers::ProbeScope probe;
      fn.gain(0.5f).frequency(1200.f).q(30.f);
      fn(in);
      fn(shorter);
      fn(in, gain, frequency, q);
      (void)fn.process(0.1f);
      (void)fn.coefficient();
      fn.clear();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
