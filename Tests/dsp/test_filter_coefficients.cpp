// Tests for YSE::DSP::filterCoefficients (YseEngine/dsp/filters/filterCoefficients.hpp),
// the coefficient generator behind ~filtercoeff (issue #1092): every type's
// coefficients against the cookbook formulas computed here independently; the
// magnitude response each type promises (gain at the frequency, at 0 Hz and
// at the Nyquist frequency, flat allpass); the shared formulas of biQuad's
// type path; signal-rate settings, resampling across blocks, the live sample
// rate, clamping and stability, non-finite settings; a sweep feeding biQuad's
// coefficient path; allocation freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <random>
#include <vector>
#include "dsp/filters.hpp"
#include "dsp/filters/filterCoefficients.hpp"
#include "headers/constants.hpp"
#include "headers/enums.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::biQuad;
using YSE::DSP::buffer;
using YSE::DSP::filterCoefficients;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  using Coefs = std::array<double, 5>; // a0 a1 a2 b1 b2

  Coefs Of(const filterCoefficients::coefficientSet& c) {
    return {c.a0, c.a1, c.a2, c.b1, c.b2};
  }

  // The cookbook (Bristow-Johnson), written out here per type from its
  // b0 b1 b2 / a0 a1 a2 tables, normalised and in biquad~'s signs.
  Coefs Cookbook(filterCoefficients::TYPE type, double f, double gain, double q, double sr) {
    const double w = 2.0 * PI * f / sr;
    const double c = std::cos(w);
    const double s = std::sin(w);
    const double alpha = s / (2.0 * q);
    const double A = std::sqrt(gain);
    double b0 = 0, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    double scale = 1.0;
    switch (type) {
    case filterCoefficients::GAINLPASS:
      scale = gain;
      [[fallthrough]];
    case filterCoefficients::LOWPASS:
      b0 = (1 - c) / 2, b1 = 1 - c, b2 = (1 - c) / 2;
      a0 = 1 + alpha, a1 = -2 * c, a2 = 1 - alpha;
      break;
    case filterCoefficients::GAINHPASS:
      scale = gain;
      [[fallthrough]];
    case filterCoefficients::HIGHPASS:
      b0 = (1 + c) / 2, b1 = -(1 + c), b2 = (1 + c) / 2;
      a0 = 1 + alpha, a1 = -2 * c, a2 = 1 - alpha;
      break;
    case filterCoefficients::GAINBPASS:
      scale = gain;
      [[fallthrough]];
    case filterCoefficients::BANDPASS:
      b0 = alpha, b1 = 0, b2 = -alpha;
      a0 = 1 + alpha, a1 = -2 * c, a2 = 1 - alpha;
      break;
    case filterCoefficients::GAINRESONANT:
      scale = gain;
      [[fallthrough]];
    case filterCoefficients::RESONANT:
      b0 = s / 2, b1 = 0, b2 = -s / 2;
      a0 = 1 + alpha, a1 = -2 * c, a2 = 1 - alpha;
      break;
    case filterCoefficients::GAINBSTOP:
      scale = gain;
      [[fallthrough]];
    case filterCoefficients::BANDSTOP:
      b0 = 1, b1 = -2 * c, b2 = 1;
      a0 = 1 + alpha, a1 = -2 * c, a2 = 1 - alpha;
      break;
    case filterCoefficients::GAINAPASS:
      scale = gain;
      [[fallthrough]];
    case filterCoefficients::ALLPASS:
      b0 = 1 - alpha, b1 = -2 * c, b2 = 1 + alpha;
      a0 = 1 + alpha, a1 = -2 * c, a2 = 1 - alpha;
      break;
    case filterCoefficients::PEAKNOTCH:
      b0 = 1 + alpha * A, b1 = -2 * c, b2 = 1 - alpha * A;
      a0 = 1 + alpha / A, a1 = -2 * c, a2 = 1 - alpha / A;
      break;
    case filterCoefficients::LOWSHELF: {
      const double sa = s / 2 * std::sqrt((A + 1 / A) * (1 / q - 1) + 2);
      b0 = A * ((A + 1) - (A - 1) * c + 2 * std::sqrt(A) * sa);
      b1 = 2 * A * ((A - 1) - (A + 1) * c);
      b2 = A * ((A + 1) - (A - 1) * c - 2 * std::sqrt(A) * sa);
      a0 = (A + 1) + (A - 1) * c + 2 * std::sqrt(A) * sa;
      a1 = -2 * ((A - 1) + (A + 1) * c);
      a2 = (A + 1) + (A - 1) * c - 2 * std::sqrt(A) * sa;
      break;
    }
    case filterCoefficients::HIGHSHELF: {
      const double sa = s / 2 * std::sqrt((A + 1 / A) * (1 / q - 1) + 2);
      b0 = A * ((A + 1) + (A - 1) * c + 2 * std::sqrt(A) * sa);
      b1 = -2 * A * ((A - 1) + (A + 1) * c);
      b2 = A * ((A + 1) + (A - 1) * c - 2 * std::sqrt(A) * sa);
      a0 = (A + 1) - (A - 1) * c + 2 * std::sqrt(A) * sa;
      a1 = 2 * ((A - 1) - (A + 1) * c);
      a2 = (A + 1) - (A - 1) * c - 2 * std::sqrt(A) * sa;
      break;
    }
    default:
      return {1, 0, 0, 0, 0};
    }
    return {scale * b0 / a0, scale * b1 / a0, scale * b2 / a0, a1 / a0, a2 / a0};
  }

  // |H(e^jw)| of y(n) = a0 x(n) + a1 x(n-1) + a2 x(n-2) - b1 y(n-1) - b2 y(n-2).
  double Gain(const Coefs& c, double w) {
    const std::complex<double> z1 = std::polar(1.0, -w);
    return std::abs((c[0] + c[1] * z1 + c[2] * z1 * z1) / (1.0 + c[3] * z1 + c[4] * z1 * z1));
  }

  // Both poles strictly inside the unit circle (the stability triangle).
  bool Stable(const Coefs& c) {
    return std::abs(c[4]) < 1.0 && std::abs(c[3]) < 1.0 + c[4];
  }

  void CheckCoefs(const Coefs& got, const Coefs& want, double tol) {
    for (int i = 0; i < 5; ++i) {
      CAPTURE(i);
      if (tol == 0.0)
        CHECK(got[i] == want[i]);
      else
        CHECK(got[i] == doctest::Approx(want[i]).epsilon(tol).scale(1.0));
    }
  }

  // Sample i of the five outputs.
  Coefs At(filterCoefficients& fn, unsigned int i) {
    return {fn.a0().getPtr()[i], fn.a1().getPtr()[i], fn.a2().getPtr()[i], fn.b1().getPtr()[i],
            fn.b2().getPtr()[i]};
  }

  constexpr filterCoefficients::TYPE ALL_TYPES[] = {
      filterCoefficients::LOWPASS,   filterCoefficients::HIGHPASS,
      filterCoefficients::BANDPASS,  filterCoefficients::BANDSTOP,
      filterCoefficients::PEAKNOTCH, filterCoefficients::LOWSHELF,
      filterCoefficients::HIGHSHELF, filterCoefficients::RESONANT,
      filterCoefficients::ALLPASS,   filterCoefficients::GAINLPASS,
      filterCoefficients::GAINHPASS, filterCoefficients::GAINBPASS,
      filterCoefficients::GAINBSTOP, filterCoefficients::GAINRESONANT,
      filterCoefficients::GAINAPASS};
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("filterCoefficients: every type against the cookbook formulas (#1092)") {
    struct setting {
      double f, gain, q, sr;
    };
    const setting settings[] = {{1000, 1.0, 0.7071, 44100},
                                {250, 2.5, 3.0, 48000},
                                {8000, 0.3, 0.5, 48000},
                                {15000, 4.0, 12.0, 96000}};
    for (const auto type : ALL_TYPES) {
      CAPTURE(static_cast<int>(type));
      for (const setting& s : settings) {
        CAPTURE(s.f);
        CAPTURE(s.q);
        // A shelf slope too steep for its gain has no cookbook value; the
        // clamping test covers it.
        const double A = std::sqrt(s.gain);
        if ((type == filterCoefficients::LOWSHELF || type == filterCoefficients::HIGHSHELF) &&
            (A + 1 / A) * (1 / s.q - 1) + 2 < 1e-3)
          continue;
        CheckCoefs(Of(filterCoefficients::Compute(
                       type, static_cast<float>(s.f), static_cast<float>(s.gain),
                       static_cast<float>(s.q), static_cast<float>(s.sr))),
                   Cookbook(type, static_cast<float>(s.f), static_cast<float>(s.gain),
                            static_cast<float>(s.q), s.sr),
                   1e-6);
      }
    }
    // OFF, and a type out of range, pass the input.
    CheckCoefs(Of(filterCoefficients::Compute(filterCoefficients::OFF, 500.f, 3.f, 2.f, 44100.f)),
               {1, 0, 0, 0, 0}, 0);
    CheckCoefs(Of(filterCoefficients::Compute(static_cast<filterCoefficients::TYPE>(99), 500.f, 3.f,
                                              2.f, 44100.f)),
               {1, 0, 0, 0, 0}, 0);
  }

  TEST_CASE("filterCoefficients: the magnitude response each type promises (#1092)") {
    const double sr = 48000.0;
    const double f = 2000.0;
    const double w = 2.0 * PI * f / sr;
    const auto design = [&](filterCoefficients::TYPE type, double gain, double q) {
      return Of(filterCoefficients::Compute(type, static_cast<float>(f), static_cast<float>(gain),
                                            static_cast<float>(q), static_cast<float>(sr)));
    };
    const double tol = 1e-4;
    for (const double q : {0.5, 0.7071, 4.0}) {
      CAPTURE(q);
      // Lowpass: 1 at 0 Hz, 0 at Nyquist, Q at the cutoff.
      const Coefs lp = design(filterCoefficients::LOWPASS, 1.0, q);
      CHECK(Gain(lp, 0.0) == doctest::Approx(1.0).epsilon(tol));
      CHECK(Gain(lp, PI) == doctest::Approx(0.0).scale(1.0).epsilon(tol));
      CHECK(Gain(lp, w) == doctest::Approx(q).epsilon(tol));
      // Highpass: the mirror image.
      const Coefs hp = design(filterCoefficients::HIGHPASS, 1.0, q);
      CHECK(Gain(hp, 0.0) == doctest::Approx(0.0).scale(1.0).epsilon(tol));
      CHECK(Gain(hp, PI) == doctest::Approx(1.0).epsilon(tol));
      CHECK(Gain(hp, w) == doctest::Approx(q).epsilon(tol));
      // Bandpass: 1 at the center, 0 at both ends; resonant: Q at the center.
      const Coefs bp = design(filterCoefficients::BANDPASS, 1.0, q);
      CHECK(Gain(bp, w) == doctest::Approx(1.0).epsilon(tol));
      CHECK(Gain(bp, 0.0) == doctest::Approx(0.0).scale(1.0).epsilon(tol));
      const Coefs rs = design(filterCoefficients::RESONANT, 1.0, q);
      CHECK(Gain(rs, w) == doctest::Approx(q).epsilon(tol));
      // Bandstop: 0 at the center, 1 at both ends.
      const Coefs bs = design(filterCoefficients::BANDSTOP, 1.0, q);
      CHECK(Gain(bs, w) == doctest::Approx(0.0).scale(1.0).epsilon(tol));
      CHECK(Gain(bs, 0.0) == doctest::Approx(1.0).epsilon(tol));
      CHECK(Gain(bs, PI) == doctest::Approx(1.0).epsilon(tol));
      // Allpass: flat.
      const Coefs ap = design(filterCoefficients::ALLPASS, 1.0, q);
      for (const double v : {0.0, 0.1, w, 1.5, 3.0})
        CHECK(Gain(ap, v) == doctest::Approx(1.0).epsilon(tol));
    }
    for (const double gain : {0.25, 1.0, 3.0}) {
      CAPTURE(gain);
      // Peaknotch: the gain at the center, 1 far from it.
      const Coefs pk = design(filterCoefficients::PEAKNOTCH, gain, 2.0);
      CHECK(Gain(pk, w) == doctest::Approx(gain).epsilon(tol));
      CHECK(Gain(pk, 0.0) == doctest::Approx(1.0).epsilon(tol));
      CHECK(Gain(pk, PI) == doctest::Approx(1.0).epsilon(tol));
      // Shelves: the gain on their side, 1 on the other, sqrt(gain) at f.
      const Coefs ls = design(filterCoefficients::LOWSHELF, gain, 1.0);
      CHECK(Gain(ls, 0.0) == doctest::Approx(gain).epsilon(tol));
      CHECK(Gain(ls, PI) == doctest::Approx(1.0).epsilon(tol));
      CHECK(Gain(ls, w) == doctest::Approx(std::sqrt(gain)).epsilon(tol));
      const Coefs hs = design(filterCoefficients::HIGHSHELF, gain, 1.0);
      CHECK(Gain(hs, 0.0) == doctest::Approx(1.0).epsilon(tol));
      CHECK(Gain(hs, PI) == doctest::Approx(gain).epsilon(tol));
      CHECK(Gain(hs, w) == doctest::Approx(std::sqrt(gain)).epsilon(tol));
      // The gain types: the plain type times the gain, everywhere.
      const std::pair<filterCoefficients::TYPE, filterCoefficients::TYPE> pairs[] = {
          {filterCoefficients::GAINLPASS, filterCoefficients::LOWPASS},
          {filterCoefficients::GAINHPASS, filterCoefficients::HIGHPASS},
          {filterCoefficients::GAINBPASS, filterCoefficients::BANDPASS},
          {filterCoefficients::GAINBSTOP, filterCoefficients::BANDSTOP},
          {filterCoefficients::GAINRESONANT, filterCoefficients::RESONANT},
          {filterCoefficients::GAINAPASS, filterCoefficients::ALLPASS}};
      for (const auto& pair : pairs) {
        const filterCoefficients::TYPE scaled = pair.first;
        const filterCoefficients::TYPE plain = pair.second;
        CAPTURE(static_cast<int>(scaled));
        const Coefs a = design(scaled, gain, 2.0);
        const Coefs b = design(plain, gain, 2.0);
        // The plain types ignore the gain.
        CheckCoefs(b, design(plain, 1.0, 2.0), 0);
        for (const double v : {0.05, w, 2.0})
          CHECK(Gain(a, v) == doctest::Approx(gain * Gain(b, v)).epsilon(tol));
      }
    }
  }

  TEST_CASE("filterCoefficients: biQuad's type path shares the lowpass, highpass, bandpass and "
            "notch (#1092)") {
    TestHelpers::ScopedSampleRate rate(44100);
    std::mt19937 gen(1092); // NOLINT(cert-msc51-cpp,bugprone-random-generator-seed): repeatable
    std::uniform_real_distribution<float> dist(-1.f, 1.f);
    buffer noise(N);
    for (unsigned int i = 0; i < N; ++i)
      noise.getPtr()[i] = dist(gen);

    const std::pair<YSE::BQ_TYPE, filterCoefficients::TYPE> pairs[] = {
        {YSE::BQ_LOWPASS, filterCoefficients::LOWPASS},
        {YSE::BQ_HIGHPASS, filterCoefficients::HIGHPASS},
        {YSE::BQ_BANDPASS, filterCoefficients::BANDPASS},
        {YSE::BQ_NOTCH, filterCoefficients::BANDSTOP}};
    for (const auto& pair : pairs) {
      const YSE::BQ_TYPE bq = pair.first;
      const filterCoefficients::TYPE fc = pair.second;
      CAPTURE(static_cast<int>(bq));
      biQuad typed;
      typed.set(bq, 1800.f, 1.3f);
      const filterCoefficients::coefficientSet c =
          filterCoefficients::Compute(fc, 1800.f, 1.f, 1.3f, 44100.f);
      biQuad direct;
      direct.coefficients(c.a0, c.a1, c.a2, c.b1, c.b2);
      for (int block = 0; block < 4; ++block) {
        CAPTURE(block);
        buffer a = typed(noise);
        buffer& b = direct(noise);
        for (unsigned int i = 0; i < N; ++i) {
          CAPTURE(i);
          CHECK(a.getPtr()[i] == doctest::Approx(b.getPtr()[i]).epsilon(1e-4).scale(1.0));
        }
      }
    }
  }

  TEST_CASE("filterCoefficients: defaults, setters, getters and the live sample rate (#1092)") {
    TestHelpers::ScopedSampleRate rate(48000);
    filterCoefficients fn;
    CHECK(fn.type() == filterCoefficients::LOWPASS);
    CHECK(fn.frequency() == 1000.f);
    CHECK(fn.gain() == 1.f);
    CHECK(fn.q() == doctest::Approx(0.7071f));
    CHECK(fn.resampling() == 1);
    fn(N);
    REQUIRE(fn.a0().getLength() == N);
    const Coefs first =
        Of(filterCoefficients::Compute(filterCoefficients::LOWPASS, 1000.f, 1.f, fn.q(), 48000.f));
    for (unsigned int i = 0; i < N; ++i)
      CheckCoefs(At(fn, i), first, 0);
    CheckCoefs(Of(fn.coefficients()), first, 0);

    // Non-finite settings and a type out of range are ignored.
    fn.frequency(std::nanf("")).gain(std::numeric_limits<float>::infinity()).q(std::nanf(""));
    fn.type(static_cast<filterCoefficients::TYPE>(-1))
        .type(static_cast<filterCoefficients::TYPE>(filterCoefficients::TYPE_COUNT));
    CHECK(fn.frequency() == 1000.f);
    CHECK(fn.gain() == 1.f);
    CHECK(fn.type() == filterCoefficients::LOWPASS);

    fn.type(filterCoefficients::PEAKNOTCH).frequency(3000.f).gain(2.f).q(5.f);
    fn(N);
    CheckCoefs(At(fn, 0), Cookbook(filterCoefficients::PEAKNOTCH, 3000, 2, 5, 48000), 1e-6);

    // The sample rate is read live: the same settings at another rate.
    {
      TestHelpers::ScopedSampleRate other(96000);
      fn(N);
      CheckCoefs(At(fn, N - 1), Cookbook(filterCoefficients::PEAKNOTCH, 3000, 2, 5, 96000), 1e-6);
    }
    fn(N);
    CheckCoefs(At(fn, 0), Cookbook(filterCoefficients::PEAKNOTCH, 3000, 2, 5, 48000), 1e-6);

    // OFF passes the input.
    fn.type(filterCoefficients::OFF);
    fn(N / 2);
    CHECK(fn.a0().getLength() == N / 2);
    CheckCoefs(At(fn, 3), {1, 0, 0, 0, 0}, 0);
    // A block longer than the capacity is clamped.
    fn(2 * N);
    CHECK(fn.b2().getLength() == N);
    CHECK(&fn.output(0) == &fn.a0());
    CHECK(&fn.output(4) == &fn.b2());
    CHECK(&fn.output(9) == &fn.b2());
  }

  TEST_CASE("filterCoefficients: settings read per sample from signals (#1092)") {
    TestHelpers::ScopedSampleRate rate(44100);
    filterCoefficients fn;
    fn.type(filterCoefficients::GAINBPASS);
    buffer f(N);
    buffer g(N);
    buffer q(N);
    for (unsigned int i = 0; i < N; ++i) {
      f.getPtr()[i] = 200.f + 50.f * static_cast<float>(i);
      g.getPtr()[i] = 0.5f + 0.01f * static_cast<float>(i);
      q.getPtr()[i] = 1.f + 0.1f * static_cast<float>(i);
    }
    // A non-finite sample keeps the value before it.
    f.getPtr()[10] = std::nanf("");
    g.getPtr()[11] = std::numeric_limits<float>::infinity();
    fn(f, g, q);
    REQUIRE(fn.a0().getLength() == N);
    for (unsigned int i = 0; i < N; ++i) {
      CAPTURE(i);
      const float fi = i == 10 ? f.getPtr()[9] : f.getPtr()[i];
      const float gi = i == 11 ? g.getPtr()[10] : g.getPtr()[i];
      CheckCoefs(At(fn, i), Cookbook(filterCoefficients::GAINBPASS, fi, gi, q.getPtr()[i], 44100),
                 1e-5);
    }
    CHECK(fn.frequency() == f.getPtr()[N - 1]);
    CHECK(fn.gain() == g.getPtr()[N - 1]);
    CHECK(fn.q() == q.getPtr()[N - 1]);

    // The shortest buffer sets the length.
    buffer shortQ(N / 4);
    shortQ = 2.f;
    fn(f, g, shortQ);
    CHECK(fn.a0().getLength() == N / 4);
  }

  TEST_CASE("filterCoefficients: resampling holds the coefficients, counting across blocks "
            "(#1092)") {
    TestHelpers::ScopedSampleRate rate(48000);
    for (const unsigned int factor : {2u, 3u, 5u, 64u, 200u}) {
      CAPTURE(factor);
      filterCoefficients fn;
      fn.resampling(factor);
      CHECK(fn.resampling() == factor);
      const unsigned int blocks = 4;
      const unsigned int sizes[blocks] = {7, N, 33, N};
      unsigned int n = 0;
      unsigned int computedAt = 0;
      for (unsigned int b = 0; b < blocks; ++b) {
        buffer f(sizes[b]);
        buffer g(sizes[b]);
        buffer q(sizes[b]);
        for (unsigned int i = 0; i < sizes[b]; ++i)
          f.getPtr()[i] = 100.f + static_cast<float>(n + i);
        g = 1.f;
        q = 2.f;
        fn(f, g, q);
        for (unsigned int i = 0; i < sizes[b]; ++i, ++n) {
          CAPTURE(n);
          if (n % factor == 0) computedAt = n;
          CheckCoefs(At(fn, i),
                     Cookbook(filterCoefficients::LOWPASS, 100.0 + computedAt, 1, 2, 48000), 1e-5);
        }
      }
    }
    // Clamping; setting the factor restarts the count.
    filterCoefficients fn;
    fn.resampling(0);
    CHECK(fn.resampling() == 1);
    fn.resampling(1u << 20);
    CHECK(fn.resampling() == filterCoefficients::MAX_RESAMPLING);
    fn.resampling(4);
    fn(3);
    fn.frequency(5000.f);
    fn.resampling(4);
    fn(1);
    CheckCoefs(At(fn, 0), Cookbook(filterCoefficients::LOWPASS, 5000, 1, fn.q(), 48000), 1e-5);
  }

  TEST_CASE("filterCoefficients: clamping keeps every type finite at the edges and stable in "
            "the audible range (#1092)") {
    const float sr = 44100.f;
    for (const auto type : ALL_TYPES) {
      CAPTURE(static_cast<int>(type));
      for (const float f : {-100.f, 0.f, 1e-3f, 20.f, 1000.f, 20000.f, 22050.f, 30000.f}) {
        for (const float q : {-1.f, 0.f, 0.01f, 1.f, 100.f, 1e6f}) {
          for (const float gain : {-2.f, 0.f, 1.f, 16.f}) {
            CAPTURE(f);
            CAPTURE(q);
            CAPTURE(gain);
            const Coefs c = Of(filterCoefficients::Compute(type, f, gain, q, sr));
            for (const double v : c)
              CHECK(std::isfinite(v));
            // At the clamped edges with a high Q the poles come closer to the
            // unit circle than a float can tell apart; in between they are
            // inside it.
            if (f >= 20.f && f <= 20000.f && q <= 100.f) CHECK(Stable(c));
          }
        }
      }
    }
    // A shelf slope steeper than the gain allows is held at the steepest.
    const Coefs steep =
        Of(filterCoefficients::Compute(filterCoefficients::LOWSHELF, 500.f, 16.f, 1000.f, sr));
    CHECK(Stable(steep));
    CHECK(Gain(steep, 0.0) == doctest::Approx(16.0).epsilon(1e-3));
    // Above half the sample rate reads as just below it.
    CheckCoefs(Of(filterCoefficients::Compute(filterCoefficients::LOWPASS, 30000.f, 1.f, 1.f, sr)),
               Of(filterCoefficients::Compute(filterCoefficients::LOWPASS, 22050.f, 1.f, 1.f, sr)),
               0);
    // Non-finite settings and no sample rate pass nothing.
    const float nan = std::nanf("");
    CheckCoefs(Of(filterCoefficients::Compute(filterCoefficients::LOWPASS, nan, 1.f, 1.f, sr)),
               {0, 0, 0, 0, 0}, 0);
    CheckCoefs(Of(filterCoefficients::Compute(filterCoefficients::LOWPASS, 100.f, nan, 1.f, sr)),
               {0, 0, 0, 0, 0}, 0);
    CheckCoefs(Of(filterCoefficients::Compute(filterCoefficients::LOWPASS, 100.f, 1.f, nan, sr)),
               {0, 0, 0, 0, 0}, 0);
    CheckCoefs(Of(filterCoefficients::Compute(filterCoefficients::LOWPASS, 100.f, 1.f, 1.f, 0.f)),
               {0, 0, 0, 0, 0}, 0);
  }

  TEST_CASE("filterCoefficients: a sweep driving biQuad's coefficient signals (#1092)") {
    TestHelpers::ScopedSampleRate rate(48000);
    // A steady sine through a lowpass whose cutoff is the sine's frequency,
    // given per sample: the gain there is the Q.
    const double hz = 3000.0;
    const double w = 2.0 * PI * hz / 48000.0;
    filterCoefficients fn;
    fn.type(filterCoefficients::LOWPASS);
    biQuad bq;
    buffer f(N);
    buffer g(N);
    buffer q(N);
    f = static_cast<float>(hz);
    g = 1.f;
    q = 2.f;
    float peak = 0.f;
    for (int block = 0; block < 40; ++block) {
      buffer in(N);
      for (unsigned int i = 0; i < N; ++i)
        in.getPtr()[i] = static_cast<float>(std::sin(w * static_cast<double>(block * N + i)));
      fn(f, g, q);
      buffer& out = bq(in, &fn.a0(), &fn.a1(), &fn.a2(), &fn.b1(), &fn.b2());
      if (block >= 20)
        for (unsigned int i = 0; i < N; ++i)
          peak = std::max(peak, std::abs(out.getPtr()[i]));
    }
    CHECK(peak == doctest::Approx(2.0).epsilon(2e-3));

    // A sweep: the output equals the direct form with each sample's
    // coefficients, computed here from the cookbook.
    filterCoefficients sweep;
    sweep.type(filterCoefficients::RESONANT);
    biQuad chain;
    std::vector<double> y;
    std::vector<float> x;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (int block = 0; block < 4; ++block) {
      buffer in(N);
      for (unsigned int i = 0; i < N; ++i) {
        const unsigned int n = block * N + i;
        in.getPtr()[i] = (n % 37 == 0) ? 1.f : 0.f;
        f.getPtr()[i] = 300.f + 10.f * static_cast<float>(n);
        q.getPtr()[i] = 8.f;
      }
      sweep(f, g, q);
      buffer& out = chain(in, &sweep.a0(), &sweep.a1(), &sweep.a2(), &sweep.b1(), &sweep.b2());
      for (unsigned int i = 0; i < N; ++i) {
        const unsigned int n = block * N + i;
        const Coefs c = Cookbook(filterCoefficients::RESONANT, 300.f + 10.f * static_cast<float>(n),
                                 1, 8, 48000);
        Coefs cf{};
        for (int k = 0; k < 5; ++k)
          cf[k] = static_cast<float>(c[k]);
        const double xn = in.getPtr()[i];
        const double yn = cf[0] * xn + cf[1] * x1 + cf[2] * x2 - cf[3] * y1 - cf[4] * y2;
        x2 = x1;
        x1 = xn;
        y2 = y1;
        y1 = yn;
        CAPTURE(n);
        CHECK(out.getPtr()[i] == doctest::Approx(yn).epsilon(1e-4).scale(1.0));
      }
    }
  }

  TEST_CASE("filterCoefficients: processing allocates nothing (#1092)") {
    filterCoefficients fn;
    buffer f(N);
    buffer g(N);
    buffer q(N);
    buffer shorter(N / 2);
    f = 700.f;
    g = 2.f;
    q = 3.f;
    shorter = 1.f;
    fn(N);
    {
      TestHelpers::ProbeScope probe;
      for (int t = 0; t < filterCoefficients::TYPE_COUNT; ++t) {
        fn.type(static_cast<filterCoefficients::TYPE>(t));
        fn(f, g, q);
        fn(f, g, shorter);
        fn(N);
      }
      fn.frequency(100.f).gain(0.5f).q(2.f).resampling(8);
      fn(N);
      (void)fn.coefficients();
      (void)filterCoefficients::Compute(filterCoefficients::HIGHSHELF, 1.f, 2.f, 3.f, 44100.f);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  TEST_CASE("filterCoefficients: Compute is ComputePrecise rounded to floats (#1098)") {
    for (int t = -1; t <= filterCoefficients::TYPE_COUNT; ++t) {
      for (const float hz : {0.f, 1.f, 440.f, 9000.f, 23999.f}) {
        for (const float q : {0.01f, 0.7f, 20.f, 10000.f}) {
          CAPTURE(t);
          CAPTURE(hz);
          CAPTURE(q);
          const auto type = static_cast<filterCoefficients::TYPE>(t);
          const filterCoefficients::coefficientSet c =
              filterCoefficients::Compute(type, hz, 2.f, q, 48000.f);
          const filterCoefficients::preciseSet p =
              filterCoefficients::ComputePrecise(type, hz, 2.f, q, 48000.f);
          CHECK(c.a0 == static_cast<float>(p.a0));
          CHECK(c.a1 == static_cast<float>(p.a1));
          CHECK(c.a2 == static_cast<float>(p.a2));
          CHECK(c.b1 == static_cast<float>(p.b1));
          CHECK(c.b2 == static_cast<float>(p.b2));
        }
      }
    }
    // Not finite: all 0 in both.
    const filterCoefficients::preciseSet zero = filterCoefficients::ComputePrecise(
        filterCoefficients::ALLPASS, std::nanf(""), 1.f, 1.f, 48000.f);
    CHECK(zero.a0 == 0.0);
    CHECK(zero.b2 == 0.0);
  }

  // ─── the matched "imitation analog" bandpass and peak (#1209) ──────────────

  TEST_CASE("filterCoefficients: ComputeMatched matches the analog prototypes at 0 Hz, the "
            "frequency and half the sample rate, and follows them between (#1209)") {
    constexpr double sr = 48000.0;
    const double frequencies[] = {50.0, 440.0, 1000.0, 5000.0, 12000.0, 18000.0, 22000.0};
    const double qs[] = {0.3, 0.7071, 2.0, 8.0};
    const double gains[] = {0.125, 0.5, 2.0, 8.0};
    // The worst deviation from the prototype in dB over the sweep, for a
    // frequency up to a quarter of the sample rate and above it: near half
    // the sample rate the mapped poles bend the response between the
    // matched points more.
    double worst[2] = {0.0, 0.0};
    const auto note = [&worst](double f, double db) {
      double& slot = worst[f <= sr / 4.0 ? 0 : 1];
      slot = std::max(slot, db);
    };
    for (const double f : frequencies) {
      for (const double q : qs) {
        const double w0 = 2.0 * PI * f / sr;
        // The bandpass prototype: (W/Q) / sqrt((1 - W^2)^2 + (W/Q)^2).
        const auto bandpass = [&](double w) {
          const double v = w / w0;
          return (v / q) / std::sqrt((1 - v * v) * (1 - v * v) + (v / q) * (v / q));
        };
        {
          CAPTURE(f);
          CAPTURE(q);
          const filterCoefficients::preciseSet p = filterCoefficients::ComputeMatchedPrecise(
              filterCoefficients::BANDPASS, static_cast<float>(f), 1.f, static_cast<float>(q),
              static_cast<float>(sr));
          const Coefs c{p.a0, p.a1, p.a2, p.b1, p.b2};
          CHECK(Stable(c));
          CHECK(Gain(c, 0.0) == doctest::Approx(0.0).scale(1.0).epsilon(1e-9));
          CHECK(Gain(c, w0) == doctest::Approx(1.0).epsilon(1e-6));
          CHECK(Gain(c, PI) == doctest::Approx(bandpass(PI)).epsilon(1e-6));
          // The cookbook bandpass has a zero at half the sample rate; this
          // one does not.
          CHECK(Gain(c, PI) > 0.0);
          // Between: compared wherever the prototype is above -40 dB.
          for (int i = 1; i < 512; ++i) {
            const double w = PI * i / 512.0;
            const double want = bandpass(w);
            if (want < 0.01) continue;
            note(f, std::abs(20.0 * std::log10(Gain(c, w) / want)));
          }
        }
        for (const double g : gains) {
          CAPTURE(f);
          CAPTURE(q);
          CAPTURE(g);
          const double A = std::sqrt(g);
          const auto peak = [&](double w) {
            const double v = w / w0;
            const double d = (1 - v * v) * (1 - v * v);
            const double z = (v * A / q) * (v * A / q);
            const double p = (v / (A * q)) * (v / (A * q));
            return std::sqrt((d + z) / (d + p));
          };
          const filterCoefficients::preciseSet p = filterCoefficients::ComputeMatchedPrecise(
              filterCoefficients::PEAKNOTCH, static_cast<float>(f), static_cast<float>(g),
              static_cast<float>(q), static_cast<float>(sr));
          const Coefs c{p.a0, p.a1, p.a2, p.b1, p.b2};
          CHECK(Stable(c));
          CHECK(Gain(c, 0.0) == doctest::Approx(1.0).epsilon(1e-6));
          CHECK(Gain(c, w0) == doctest::Approx(g).epsilon(1e-6));
          CHECK(Gain(c, PI) == doctest::Approx(peak(PI)).epsilon(1e-6));
          for (int i = 1; i < 512; ++i) {
            const double w = PI * i / 512.0;
            note(f, std::abs(20.0 * std::log10(Gain(c, w) / peak(w))));
          }
        }
      }
    }
    // Measured 1.08 dB and 5.1 dB (a bandpass skirt at 22 kHz, Q 8).
    CHECK(worst[0] < 1.5);
    CHECK(worst[1] < 6.0);
  }

  TEST_CASE("filterCoefficients: ComputeMatched is Compute for the other types, scales the gain "
            "bandpass, and checks its settings as Compute does (#1209)") {
    // Every other type is Compute's, bit for bit.
    for (const filterCoefficients::TYPE type : ALL_TYPES) {
      CAPTURE(static_cast<int>(type));
      if (type == filterCoefficients::BANDPASS || type == filterCoefficients::GAINBPASS ||
          type == filterCoefficients::PEAKNOTCH)
        continue;
      CHECK(Of(filterCoefficients::ComputeMatched(type, 2500.f, 2.f, 3.f, 48000.f)) ==
            Of(filterCoefficients::Compute(type, 2500.f, 2.f, 3.f, 48000.f)));
    }
    CHECK(Of(filterCoefficients::ComputeMatched(filterCoefficients::OFF, 1.f, 1.f, 1.f, 1.f)) ==
          Coefs{1, 0, 0, 0, 0});

    // The gain bandpass is the bandpass, its feedforward times the gain.
    const Coefs bp = Of(filterCoefficients::ComputeMatched(filterCoefficients::BANDPASS, 3000.f,
                                                           2.5f, 4.f, 48000.f));
    const Coefs gbp = Of(filterCoefficients::ComputeMatched(filterCoefficients::GAINBPASS, 3000.f,
                                                            2.5f, 4.f, 48000.f));
    CheckCoefs(gbp, {2.5 * bp[0], 2.5 * bp[1], 2.5 * bp[2], bp[3], bp[4]}, 1e-6);
    // The plain bandpass ignores the gain.
    CHECK(Of(filterCoefficients::ComputeMatched(filterCoefficients::BANDPASS, 3000.f, 7.f, 4.f,
                                                48000.f)) == bp);

    // A peak of gain 1 passes the input unchanged.
    const Coefs flat = Of(filterCoefficients::ComputeMatched(filterCoefficients::PEAKNOTCH, 3000.f,
                                                             1.f, 4.f, 48000.f));
    CHECK(flat[0] == doctest::Approx(1.0).epsilon(1e-6));
    CHECK(flat[1] == doctest::Approx(flat[3]).epsilon(1e-6));
    CHECK(flat[2] == doctest::Approx(flat[4]).epsilon(1e-6));

    // Not finite, or no sample rate: all 0. Out of range: clamped, stable.
    for (const float bad : {std::nanf(""), std::numeric_limits<float>::infinity()}) {
      CHECK(Of(filterCoefficients::ComputeMatched(filterCoefficients::PEAKNOTCH, bad, 2.f, 1.f,
                                                  48000.f)) == Coefs{0, 0, 0, 0, 0});
      CHECK(Of(filterCoefficients::ComputeMatched(filterCoefficients::BANDPASS, 1000.f, 1.f, bad,
                                                  48000.f)) == Coefs{0, 0, 0, 0, 0});
    }
    CHECK(Of(filterCoefficients::ComputeMatched(filterCoefficients::BANDPASS, 1000.f, 1.f, 1.f,
                                                0.f)) == Coefs{0, 0, 0, 0, 0});
    for (const float f : {-100.f, 0.f, 20.f, 1000.f, 23999.f, 30000.f}) {
      for (const float q : {-1.f, 0.f, 0.01f, 1.f, 100.f, 1e6f}) {
        for (const float g : {-1.f, 0.f, 1e-6f, 1000.f}) {
          CAPTURE(f);
          CAPTURE(q);
          CAPTURE(g);
          for (const filterCoefficients::TYPE type :
               {filterCoefficients::BANDPASS, filterCoefficients::PEAKNOTCH}) {
            const Coefs c = Of(filterCoefficients::ComputeMatched(type, f, g, q, 48000.f));
            for (const double v : c)
              CHECK(std::isfinite(v));
            // As for Compute: near 0 Hz with a high (or, for the slow pole,
            // a very low) Q a float cannot tell the poles from the unit
            // circle. Near half the sample rate the mapped poles stay inside.
            if (f >= 20.f && q > 0.01f && q <= 100.f) CHECK(Stable(c));
          }
        }
      }
    }
  }

} // TEST_SUITE("dsp")
