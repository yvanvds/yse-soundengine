// Tests for YSE::DSP::lores (YseEngine/dsp/filters/lores.hpp), the two-pole
// resonant lowpass behind ~lores (issue #1087): the coefficients against
// their closed form, the impulse response against the two-pole formula, the
// unity gain at 0 Hz, the gain on a sine against the magnitude response, the
// resonant peak, stability as the resonance approaches 1, the live sample
// rate, the per-sample cutoff and resonance signals, non-finite input and
// settings, clear(), block size independence and allocation freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <random>
#include <vector>
#include "dsp/filters/lores.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::lores;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  // Runs `fn` over `in` in blocks of `block` samples.
  std::vector<float> Run(lores& fn, const std::vector<float>& in, unsigned int block = N) {
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
    double q, r, theta, g, a1, a2;
  };

  Expected Closed(double hz, double res, double sr) {
    Expected e{};
    e.q = 1.0 / (std::sqrt(2.0) * (1.0 - res));
    const double w = 2.0 * PI * hz / sr;
    e.r = std::exp(-w / (2.0 * e.q));
    e.theta = w * std::sqrt(1.0 - 1.0 / (4.0 * e.q * e.q));
    e.a1 = -2.0 * e.r * std::cos(e.theta);
    e.a2 = e.r * e.r;
    e.g = 1.0 + e.a1 + e.a2;
    return e;
  }

  // The steady-state gain of g / (1 + a1 z^-1 + a2 z^-2) at angular
  // frequency w.
  double Gain(const Expected& e, double w) {
    const std::complex<double> z1 = std::polar(1.0, -w);
    return e.g / std::abs(1.0 + e.a1 * z1 + e.a2 * z1 * z1);
  }

  // The amplitude of a unit sine at `hz` after `fn` at 48 kHz, projected
  // onto a sine and a cosine over the second half second, once the transient
  // has died. Exact for an even `hz`, a whole number of cycles there.
  double SineGain(lores& fn, double hz) {
    const double w = 2.0 * PI * hz / 48000.0;
    constexpr unsigned int total = 48000;
    constexpr unsigned int measure = 24000;
    double s = 0.0;
    double c = 0.0;
    for (unsigned int n = 0; n < total; ++n) {
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

  TEST_CASE("lores: the coefficients against their closed form, with clamping (#1087)") {
    const float sr = 48000.f;
    for (const float hz : {50.f, 1000.f, 5000.f, 20000.f}) {
      for (const float res : {0.f, 0.3f, 0.7f, 0.9f, 0.99f}) {
        CAPTURE(hz);
        CAPTURE(res);
        const lores::coefficients c = lores::Coefficients(hz, res, sr);
        const Expected e = Closed(hz, res, sr);
        CHECK(c.a1 == doctest::Approx(e.a1).epsilon(1e-6));
        CHECK(c.a2 == doctest::Approx(e.a2).epsilon(1e-6));
        CHECK(c.g == doctest::Approx(e.g).epsilon(1e-6));
        // Unity gain at 0 Hz by construction.
        CHECK(c.g == doctest::Approx(1.0 + c.a1 + c.a2).epsilon(1e-12));
        // Poles inside the unit circle.
        CHECK(c.a2 < 1.0);
      }
    }
    // Resonance 0 is Butterworth's Q; 0.5 doubles it.
    CHECK(lores::Q(0.f) == doctest::Approx(1.0 / std::sqrt(2.0)));
    CHECK(lores::Q(0.5f) == doctest::Approx(std::sqrt(2.0)));
    // The resonance clamps to 0..MAX_RESONANCE, the cutoff to 0..SR/2.
    CHECK(lores::Q(1.f) == lores::Q(lores::MAX_RESONANCE));
    CHECK(lores::Q(5.f) == lores::Q(lores::MAX_RESONANCE));
    CHECK(lores::Q(-1.f) == lores::Q(0.f));
    CHECK(lores::Q(lores::MAX_RESONANCE) ==
          doctest::Approx(1.0 / (std::sqrt(2.0) * 0.001)).epsilon(1e-4));
    const lores::coefficients nyquist = lores::Coefficients(24000.f, 0.5f, sr);
    const lores::coefficients above = lores::Coefficients(30000.f, 0.5f, sr);
    CHECK(above.a1 == nyquist.a1);
    CHECK(above.a2 == nyquist.a2);
    // A cutoff of 0 (or below, or no sample rate, or not finite) passes nothing.
    for (const lores::coefficients& zero :
         {lores::Coefficients(0.f, 0.5f, sr), lores::Coefficients(-100.f, 0.5f, sr),
          lores::Coefficients(1000.f, 0.5f, 0.f),
          lores::Coefficients(std::numeric_limits<float>::quiet_NaN(), 0.5f, sr)}) {
      CHECK(zero.g == 0.0);
      CHECK(zero.a1 == 0.0);
      CHECK(zero.a2 == 0.0);
    }
    // A non-finite resonance reads as 0.
    const lores::coefficients nanRes =
        lores::Coefficients(1000.f, std::numeric_limits<float>::quiet_NaN(), sr);
    const lores::coefficients zeroRes = lores::Coefficients(1000.f, 0.f, sr);
    CHECK(nanRes.a1 == zeroRes.a1);
  }

  TEST_CASE("lores: defaults and invalid settings (#1087)") {
    TestHelpers::ScopedSampleRate rate(48000);
    lores fn;
    CHECK(fn.cutoff() == 0.f);
    CHECK(fn.resonance() == 0.f);
    // Cutoff 0: nothing through.
    const std::vector<float> out = Run(fn, std::vector<float>(N, 1.f));
    CHECK(std::all_of(out.begin(), out.end(), [](float v) { return v == 0.f; }));
    fn.cutoff(500.f).resonance(0.4f);
    fn.cutoff(std::numeric_limits<float>::quiet_NaN())
        .resonance(std::numeric_limits<float>::infinity());
    CHECK(fn.cutoff() == 500.f);
    CHECK(fn.resonance() == 0.4f);
  }

  TEST_CASE("lores: impulse response against the two-pole closed form (#1087)") {
    TestHelpers::ScopedSampleRate rate(48000);
    for (const float res : {0.f, 0.5f, 0.9f, 0.999f}) {
      for (const float hz : {200.f, 2000.f, 12000.f}) {
        CAPTURE(res);
        CAPTURE(hz);
        lores fn;
        fn.cutoff(hz).resonance(res);
        const Expected e = Closed(hz, res, 48000.0);
        const std::vector<float> h = Run(fn, Impulse(4 * N));
        // h(n) = g r^n sin((n + 1) theta) / sin(theta)
        for (unsigned int n = 0; n < h.size(); ++n) {
          CAPTURE(n);
          const double expected =
              e.g * std::pow(e.r, n) * std::sin((n + 1) * e.theta) / std::sin(e.theta);
          CHECK(h[n] == doctest::Approx(expected).epsilon(1e-5));
        }
      }
    }
  }

  TEST_CASE("lores: unity gain at 0 Hz at every resonance (#1087)") {
    TestHelpers::ScopedSampleRate rate(48000);
    for (const float res : {0.f, 0.5f, 0.9f}) {
      CAPTURE(res);
      lores fn;
      fn.cutoff(2000.f).resonance(res);
      const std::vector<float> step = Run(fn, std::vector<float>(40 * N, 1.f));
      CHECK(step.back() == doctest::Approx(1.0).epsilon(1e-5));
    }
  }

  TEST_CASE("lores: sine gain against the magnitude response, and the resonant peak (#1087)") {
    TestHelpers::ScopedSampleRate rate(48000);
    for (const float res : {0.f, 0.6f, 0.9f}) {
      for (const double hz : {100.0, 1000.0, 3000.0, 10000.0}) {
        CAPTURE(res);
        CAPTURE(hz);
        lores fn;
        fn.cutoff(1000.f).resonance(res);
        const double expected = Gain(Closed(1000.0, res, 48000.0), 2.0 * PI * hz / 48000.0);
        CHECK(SineGain(fn, hz) == doctest::Approx(expected).epsilon(1e-4));
      }
    }
    // Resonance 0: no peak, -3 dB near the cutoff (Butterworth), falling
    // about 12 dB per octave above it.
    const Expected flat = Closed(1000.0, 0.0, 48000.0);
    double maxFlat = 0.0;
    for (int k = 0; k < 160; ++k) // 10 Hz to about 24 kHz
      maxFlat = std::max(maxFlat, Gain(flat, 2.0 * PI * 10.0 * std::pow(1.05, k) / 48000.0));
    CHECK(maxFlat <= 1.0 + 1e-5);
    // As the header states: within 0.01 dB of flat up to a cutoff of a
    // tenth of the sample rate, and about a 2.3 dB lift at the Nyquist frequency.
    const Expected tenth = Closed(4800.0, 0.0, 48000.0);
    const Expected top = Closed(24000.0, 0.0, 48000.0);
    double maxTenth = 0.0;
    double maxTop = 0.0;
    for (int k = 0; k < 782; ++k) { // 10 Hz to just under 24 kHz
      const double hz = 10.0 * std::pow(1.01, k);
      maxTenth = std::max(maxTenth, Gain(tenth, 2.0 * PI * hz / 48000.0));
      maxTop = std::max(maxTop, Gain(top, 2.0 * PI * hz / 48000.0));
    }
    CHECK(20.0 * std::log10(maxTenth) < 0.01);
    CHECK(20.0 * std::log10(maxTop) == doctest::Approx(2.3).epsilon(0.05));
    CHECK(Gain(flat, 2.0 * PI * 1000.0 / 48000.0) ==
          doctest::Approx(1.0 / std::sqrt(2.0)).epsilon(0.02));
    const double octaveDrop =
        Gain(flat, 2.0 * PI * 4000.0 / 48000.0) / Gain(flat, 2.0 * PI * 8000.0 / 48000.0);
    CHECK(20.0 * std::log10(octaveDrop) == doctest::Approx(12.0).epsilon(0.1));
    // Resonance 0.9: Q = 7.07, a peak about 17 dB at the cutoff.
    lores peak;
    peak.cutoff(1000.f).resonance(0.9f);
    const double atCutoff = SineGain(peak, 1000.0);
    CHECK(20.0 * std::log10(atCutoff) == doctest::Approx(17.0).epsilon(0.05));
  }

  TEST_CASE("lores: stable as the resonance approaches 1, at every cutoff (#1087)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::mt19937 gen(1087); // NOLINT(cert-msc51-cpp,bugprone-random-generator-seed): repeatable
    std::uniform_real_distribution<float> noise(-1.f, 1.f);
    for (const float res : {0.99f, 0.999f, 1.f, 2.f}) {
      for (const float hz : {1.f, 20.f, 1000.f, 12000.f, 23999.f, 24000.f}) {
        CAPTURE(res);
        CAPTURE(hz);
        lores fn;
        fn.cutoff(hz).resonance(res);
        const lores::coefficients c = fn.coefficient();
        CHECK(c.a2 < 1.0); // pole radius below 1
        // Five seconds of full-scale noise: bounded by the sum of |h(n)|,
        // itself below g / (1 - r)^2 for these poles.
        const double r = std::sqrt(c.a2);
        const double bound = std::fabs(c.g) / ((1.0 - r) * (1.0 - r)) + 1e-6;
        double peak = 0.0;
        buffer in(N);
        for (unsigned int block = 0; block < 48000 * 5 / N; ++block) {
          for (unsigned int i = 0; i < N; ++i)
            in.getPtr()[i] = noise(gen);
          buffer& out = fn(in);
          for (unsigned int i = 0; i < N; ++i)
            peak = std::max(peak, static_cast<double>(std::fabs(out.getPtr()[i])));
        }
        CHECK(std::isfinite(peak));
        CHECK(peak <= bound);
        // Then ten seconds of silence: the ringing stays bounded and, where
        // its time constant (2Q / w samples) is well under that, dies away.
        buffer silence(N);
        silence = 0.f;
        double last = 0.0;
        for (unsigned int block = 0; block < 48000 * 10 / N; ++block) {
          buffer& out = fn(silence);
          last = 0.0;
          for (unsigned int i = 0; i < N; ++i)
            last = std::max(last, static_cast<double>(std::fabs(out.getPtr()[i])));
        }
        CHECK(std::isfinite(last));
        CHECK(last <= bound);
        if (hz >= 1000.f) CHECK(last < 1e-6 * peak);
      }
    }
  }

  TEST_CASE("lores: the coefficients follow the live sample rate (#1087)") {
    TestHelpers::ScopedSampleRate rate(48000);
    lores fn;
    fn.cutoff(2000.f).resonance(0.5f);
    CHECK(fn.coefficient().a1 == doctest::Approx(Closed(2000.0, 0.5, 48000.0).a1).epsilon(1e-6));
    YSE::SAMPLERATE = 96000;
    const Expected e = Closed(2000.0, 0.5, 96000.0);
    CHECK(fn.coefficient().a1 == doctest::Approx(e.a1).epsilon(1e-6));
    CHECK(fn.coefficient().a2 == doctest::Approx(e.a2).epsilon(1e-6));
    fn.clear();
    const std::vector<float> h = Run(fn, Impulse(N));
    CHECK(h[0] == doctest::Approx(e.g).epsilon(1e-6));
    CHECK(h[7] ==
          doctest::Approx(e.g * std::pow(e.r, 7) * std::sin(8 * e.theta) / std::sin(e.theta))
              .epsilon(1e-6));
    // Above the new Nyquist the cutoff clamps there.
    YSE::SAMPLERATE = 22050;
    fn.cutoff(15000.f);
    CHECK(fn.coefficient().a1 == lores::Coefficients(11025.f, 0.5f, 22050.f).a1);
  }

  TEST_CASE("lores: cutoff and resonance signals are read per sample (#1087)") {
    TestHelpers::ScopedSampleRate rate(48000);
    lores fn;
    lores reference;
    buffer in(N);
    buffer cutoff(N);
    buffer resonance(N);
    std::vector<float> x(N);
    for (unsigned int i = 0; i < N; ++i) {
      x[i] = std::sin(static_cast<float>(i) * 0.3f);
      in.getPtr()[i] = x[i];
      cutoff.getPtr()[i] = 300.f + static_cast<float>(i) * 50.f;
      resonance.getPtr()[i] = static_cast<float>(i) / static_cast<float>(N);
    }
    // A NaN in either signal keeps the previous value for that sample.
    cutoff.getPtr()[10] = std::numeric_limits<float>::quiet_NaN();
    resonance.getPtr()[20] = std::numeric_limits<float>::infinity();
    buffer& out = fn(in, cutoff, resonance);
    REQUIRE(out.getLength() == N);
    // An independent direct-form loop over the same per-sample settings.
    double y1 = 0.0;
    double y2 = 0.0;
    float hz = 0.f;
    float res = 0.f;
    for (unsigned int i = 0; i < N; ++i) {
      CAPTURE(i);
      if (std::isfinite(cutoff.getPtr()[i])) hz = cutoff.getPtr()[i];
      if (std::isfinite(resonance.getPtr()[i])) res = resonance.getPtr()[i];
      const Expected e = Closed(hz, std::min(res, lores::MAX_RESONANCE), 48000.0);
      const double y = e.g * x[i] - e.a1 * y1 - e.a2 * y2;
      y2 = y1;
      y1 = y;
      CHECK(out.getPtr()[i] == doctest::Approx(y).epsilon(1e-5));
      // The same as setting the values one sample at a time.
      reference.cutoff(cutoff.getPtr()[i]).resonance(resonance.getPtr()[i]);
      CHECK(reference.process(x[i]) == out.getPtr()[i]);
    }
    CHECK(fn.cutoff() == cutoff.getPtr()[N - 1]);
    CHECK(fn.resonance() == resonance.getPtr()[N - 1]);

    // A shorter control signal shortens the block.
    buffer shortCutoff(N / 2);
    shortCutoff = 1000.f;
    CHECK(fn(in, shortCutoff, resonance).getLength() == N / 2);
  }

  TEST_CASE("lores: non-finite input is skipped, clear() resets the state (#1087)") {
    TestHelpers::ScopedSampleRate rate(48000);
    lores fn;
    fn.cutoff(3000.f).resonance(0.7f);
    lores clean;
    clean.cutoff(3000.f).resonance(0.7f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const std::vector<float> dirty = Run(fn, {1.f, nan, 0.5f, inf, -inf, 0.f});
    const std::vector<float> reference = Run(clean, {1.f, 0.5f, 0.f});
    CHECK(dirty[0] == reference[0]);
    CHECK(dirty[1] == reference[0]); // holds
    CHECK(dirty[2] == reference[1]);
    CHECK(dirty[3] == reference[1]);
    CHECK(dirty[4] == reference[1]);
    CHECK(dirty[5] == reference[2]);
    fn.clear();
    clean.clear();
    CHECK(Run(fn, {0.f})[0] == 0.f);
    CHECK(fn.process(1.f) == clean.process(1.f));
  }

  TEST_CASE("lores: blocks, single samples and buffer clamping (#1087)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::vector<float> in(5 * N);
    for (std::size_t i = 0; i < in.size(); ++i)
      in[i] =
          std::sin(static_cast<float>(i) * 0.11f) + 0.3f * std::sin(static_cast<float>(i) * 1.7f);
    lores whole;
    whole.cutoff(3000.f).resonance(0.8f);
    const std::vector<float> reference = Run(whole, in);
    for (const unsigned int block : {1u, 7u, 64u, N}) {
      CAPTURE(block);
      lores fn;
      fn.cutoff(3000.f).resonance(0.8f);
      CHECK(Run(fn, in, block) == reference);
    }
    lores single;
    single.cutoff(3000.f).resonance(0.8f);
    for (std::size_t i = 0; i < in.size(); ++i)
      CHECK(single.process(in[i]) == reference[i]);

    // A longer buffer is processed up to STANDARD_BUFFERSIZE.
    lores fn;
    buffer longer(2 * N);
    longer = 1.f;
    CHECK(fn(longer).getLength() == N);
  }

  TEST_CASE("lores: processing allocates nothing (#1087)") {
    TestHelpers::ScopedSampleRate rate(48000);
    lores fn;
    buffer in(N);
    buffer shorter(N / 2);
    buffer cutoff(N);
    buffer resonance(N);
    in = 0.5f;
    shorter = 0.25f;
    for (unsigned int i = 0; i < N; ++i) {
      cutoff.getPtr()[i] = 500.f + static_cast<float>(i);
      resonance.getPtr()[i] = 0.5f;
    }
    fn(in);
    {
      TestHelpers::ProbeScope probe;
      fn.cutoff(1200.f).resonance(0.9f);
      fn(in);
      fn(shorter);
      fn(in, cutoff, resonance);
      (void)fn.process(0.1f);
      (void)fn.coefficient();
      fn.clear();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
