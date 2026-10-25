// Tests for YSE::DSP::hilbert (YseEngine/dsp/modules/hilbert.hpp), the phase
// quadrature filter behind ~hilbert (issue #1097): the impulse response of
// both outputs against Pure Data's hilbert~ biquads run independently, the
// allpass magnitude and the 90 degree phase difference on sines across the
// band (the imaginary output lagging), the envelope of the pair, the design
// following the live sample rate, non-finite and overflowing input, clear(),
// block size independence, the older three-buffer call and allocation
// freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/modules/hilbert.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::hilbert;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  struct Pair {
    std::vector<float> re;
    std::vector<float> im;
  };

  // Runs `fn` over `in` in blocks of `block` samples.
  Pair Run(hilbert& fn, const std::vector<float>& in, unsigned int block = N) {
    Pair out;
    for (std::size_t start = 0; start < in.size(); start += block) {
      const auto length =
          static_cast<unsigned int>(std::min<std::size_t>(block, in.size() - start));
      buffer b(length);
      for (unsigned int i = 0; i < length; ++i)
        b.getPtr()[i] = in[start + i];
      buffer& result = fn(b);
      REQUIRE(&result == &fn.real());
      REQUIRE(fn.real().getLength() == length);
      REQUIRE(fn.imaginary().getLength() == length);
      for (unsigned int i = 0; i < length; ++i) {
        out.re.push_back(fn.real().getPtr()[i]);
        out.im.push_back(fn.imaginary().getPtr()[i]);
      }
    }
    return out;
  }

  // Pure Data's biquad~, written out independently: w = x + fb1 w1 + fb2 w2,
  // y = ff1 w + ff2 w1 + ff3 w2.
  struct PdBiquad {
    double fb1, fb2, ff1, ff2, ff3;
    double w1 = 0.0;
    double w2 = 0.0;
    double operator()(double x) {
      const double w = x + fb1 * w1 + fb2 * w2;
      const double y = ff1 * w + ff2 * w1 + ff3 * w2;
      w2 = w1;
      w1 = w;
      return y;
    }
  };

  // The amplitude and phase (radians) of `v` from sample `from` on, fitted
  // as a cos(w n) + b sin(w n) by least squares.
  struct Fit {
    double amplitude;
    double phase;
  };

  Fit Measure(const std::vector<float>& v, double w, std::size_t from) {
    double cc = 0.0;
    double ss = 0.0;
    double cs = 0.0;
    double vc = 0.0;
    double vs = 0.0;
    for (std::size_t n = from; n < v.size(); ++n) {
      const double c = std::cos(w * static_cast<double>(n));
      const double s = std::sin(w * static_cast<double>(n));
      cc += c * c;
      ss += s * s;
      cs += c * s;
      vc += v[n] * c;
      vs += v[n] * s;
    }
    const double det = cc * ss - cs * cs;
    const double a = (vc * ss - vs * cs) / det;
    const double b = (vs * cc - vc * cs) / det;
    // a cos + b sin = A cos(w n + phase)
    return {std::hypot(a, b), std::atan2(-b, a)};
  }

  // The phase lead of `a` over `b` in degrees, wrapped into -180..180.
  double LeadDegrees(const Fit& a, const Fit& b) {
    double d = (a.phase - b.phase) * 180.0 / PI;
    while (d > 180.0)
      d -= 360.0;
    while (d <= -180.0)
      d += 360.0;
    return d;
  }

  std::vector<float> Cosine(double w, std::size_t samples) {
    std::vector<float> v(samples);
    for (std::size_t n = 0; n < samples; ++n)
      v[n] = static_cast<float>(std::cos(w * static_cast<double>(n)));
    return v;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("hilbert: each section is an allpass with Pure Data's hilbert~ coefficients "
            "(#1097)") {
    // Pd's biquad~ takes fb1 fb2 ff1 ff2 ff3; an allpass section here is
    // a1 = -fb1, a2 = -fb2 with the numerator the denominator reversed.
    for (const hilbert::section& s : hilbert::REAL_SECTIONS) {
      CHECK(std::abs(s.a2) < 1.0); // stable: |a2| < 1, |a1| < 1 + a2
      CHECK(std::abs(s.a1) < 1.0 + s.a2);
    }
    for (const hilbert::section& s : hilbert::IMAGINARY_SECTIONS) {
      CHECK(std::abs(s.a2) < 1.0);
      CHECK(std::abs(s.a1) < 1.0 + s.a2);
    }
    CHECK(hilbert::REAL_SECTIONS[0].a1 == 0.02569);
    CHECK(hilbert::REAL_SECTIONS[0].a2 == -0.260502);
    CHECK(hilbert::REAL_SECTIONS[1].a1 == -1.8685);
    CHECK(hilbert::REAL_SECTIONS[1].a2 == 0.870686);
    CHECK(hilbert::IMAGINARY_SECTIONS[0].a1 == -1.94632);
    CHECK(hilbert::IMAGINARY_SECTIONS[0].a2 == 0.94657);
    CHECK(hilbert::IMAGINARY_SECTIONS[1].a1 == -0.83774);
    CHECK(hilbert::IMAGINARY_SECTIONS[1].a2 == 0.06338);
  }

  TEST_CASE("hilbert: impulse response of both outputs against Pd's hilbert~ biquads (#1097)") {
    hilbert fn;
    std::vector<float> impulse(4 * N, 0.f);
    impulse[0] = 1.f;
    const Pair out = Run(fn, impulse);

    PdBiquad r1{-0.02569, 0.260502, -0.260502, 0.02569, 1.0};
    PdBiquad r2{1.8685, -0.870686, 0.870686, -1.8685, 1.0};
    PdBiquad i1{1.94632, -0.94657, 0.94657, -1.94632, 1.0};
    PdBiquad i2{0.83774, -0.06338, 0.06338, -0.83774, 1.0};
    double reEnergy = 0.0;
    double imEnergy = 0.0;
    for (std::size_t n = 0; n < impulse.size(); ++n) {
      CAPTURE(n);
      const double x = impulse[n];
      const double re = r2(r1(x));
      const double im = i2(i1(x));
      CHECK(std::abs(out.re[n] - re) < 1e-6);
      CHECK(std::abs(out.im[n] - im) < 1e-6);
      reEnergy += re * re;
      imEnergy += im * im;
    }
    // An allpass keeps the impulse's energy (what is left after 512 samples
    // is below 1e-3).
    CHECK(reEnergy == doctest::Approx(1.0).epsilon(1e-3));
    CHECK(imEnergy == doctest::Approx(1.0).epsilon(1e-3));
  }

  TEST_CASE("hilbert: the imaginary output lags the real one by 90 degrees across the band, "
            "both at unit gain (#1097)") {
    TestHelpers::ScopedSampleRate rate(48000);
    // 0.0018 .. 0.308 times the sample rate: within 1 degree of 90.
    for (const double ratio : {0.002, 0.005, 0.01, 0.0208, 0.05, 0.1, 0.2, 0.25, 0.3}) {
      CAPTURE(ratio);
      const double w = 2.0 * PI * ratio;
      hilbert fn;
      const std::vector<float> in = Cosine(w, 48000);
      const Pair out = Run(fn, in);
      // Skip the first quarter second: the slowest pole (0.993) has settled.
      const Fit re = Measure(out.re, w, 12000);
      const Fit im = Measure(out.im, w, 12000);
      CHECK(re.amplitude == doctest::Approx(1.0).epsilon(1e-3));
      CHECK(im.amplitude == doctest::Approx(1.0).epsilon(1e-3));
      const double lead = LeadDegrees(re, im);
      CHECK(lead > 89.0);
      CHECK(lead < 91.0);
    }
    // Outside the band the difference falls towards 0.
    for (const double ratio : {0.0003, 0.45}) {
      CAPTURE(ratio);
      const double w = 2.0 * PI * ratio;
      hilbert fn;
      const Pair out = Run(fn, Cosine(w, 48000));
      const double lead = LeadDegrees(Measure(out.re, w, 12000), Measure(out.im, w, 12000));
      CHECK(lead > 0.0);
      CHECK(lead < 60.0);
    }
  }

  TEST_CASE("hilbert: the pair's magnitude is the envelope of a sine (#1097)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const double w = 2.0 * PI * 1000.0 / 48000.0;
    hilbert fn;
    std::vector<float> in = Cosine(w, 9600);
    for (float& v : in)
      v *= 0.5f;
    const Pair out = Run(fn, in);
    for (std::size_t n = 4800; n < in.size(); ++n) {
      CAPTURE(n);
      // Within 1 degree: the magnitude ripples by at most sin(1 deg) / 2.
      CHECK(std::hypot(out.re[n], out.im[n]) == doctest::Approx(0.5).epsilon(0.01));
    }
  }

  TEST_CASE("hilbert: the design follows the live sample rate (#1097)") {
    // Nothing depends on SAMPLERATE: the same samples at any rate, so the
    // band is the same fraction of the rate.
    std::vector<float> in(3 * N);
    for (std::size_t i = 0; i < in.size(); ++i)
      in[i] = std::sin(static_cast<float>(i) * 0.07f) + 0.3f * std::sin(static_cast<float>(i));
    Pair at48;
    Pair at96;
    {
      TestHelpers::ScopedSampleRate rate(48000);
      hilbert fn;
      at48 = Run(fn, in);
    }
    {
      TestHelpers::ScopedSampleRate rate(96000);
      hilbert fn;
      at96 = Run(fn, in);
    }
    CHECK(at48.re == at96.re);
    CHECK(at48.im == at96.im);

    // A rate change mid-stream changes nothing either.
    TestHelpers::ScopedSampleRate rate(44100);
    hilbert fn;
    const std::vector<float> first(in.begin(), in.begin() + N);
    const std::vector<float> rest(in.begin() + N, in.end());
    Pair out = Run(fn, first);
    {
      TestHelpers::ScopedSampleRate faster(192000);
      const Pair more = Run(fn, rest);
      out.re.insert(out.re.end(), more.re.begin(), more.re.end());
      out.im.insert(out.im.end(), more.im.begin(), more.im.end());
    }
    CHECK(out.re == at48.re);
    CHECK(out.im == at48.im);
  }

  TEST_CASE("hilbert: process() and blocks of any size agree (#1097)") {
    std::vector<float> in(5 * N);
    for (std::size_t i = 0; i < in.size(); ++i)
      in[i] = std::sin(static_cast<float>(i) * 0.031f) * std::cos(static_cast<float>(i) * 0.4f);
    hilbert whole;
    const Pair blocks = Run(whole, in);
    for (const unsigned int size : {1u, 7u, 64u, 100u}) {
      CAPTURE(size);
      hilbert fn;
      const Pair other = Run(fn, in, size);
      CHECK(other.re == blocks.re);
      CHECK(other.im == blocks.im);
    }
    hilbert single;
    for (std::size_t i = 0; i < in.size(); ++i) {
      CAPTURE(i);
      const hilbert::outputs o = single.process(in[i]);
      CHECK(o.real == blocks.re[i]);
      CHECK(o.imaginary == blocks.im[i]);
    }

    // A block longer than STANDARD_BUFFERSIZE is cut to it.
    hilbert longer;
    buffer big(2 * N);
    big = 0.25f;
    longer(big);
    CHECK(longer.real().getLength() == N);
    CHECK(longer.imaginary().getLength() == N);
  }

  TEST_CASE("hilbert: non-finite input is skipped, overflow restarts, clear() resets (#1097)") {
    std::vector<float> in(2 * N);
    for (std::size_t i = 0; i < in.size(); ++i)
      in[i] = std::sin(static_cast<float>(i) * 0.05f);

    // Non-finite samples: the outputs and state hold, so the rest matches a
    // filter that never saw them.
    std::vector<float> dirty;
    for (std::size_t i = 0; i < in.size(); ++i) {
      dirty.push_back(in[i]);
      if (i == 10) dirty.push_back(std::numeric_limits<float>::quiet_NaN());
      if (i == 200) dirty.push_back(std::numeric_limits<float>::infinity());
    }
    hilbert clean;
    hilbert fn;
    const Pair a = Run(clean, in);
    const Pair b = Run(fn, dirty);
    std::size_t j = 0;
    for (std::size_t i = 0; i < dirty.size(); ++i) {
      CAPTURE(i);
      if (!std::isfinite(dirty[i])) {
        CHECK(b.re[i] == b.re[i - 1]);
        CHECK(b.im[i] == b.im[i - 1]);
        continue;
      }
      CHECK(b.re[i] == a.re[j]);
      CHECK(b.im[i] == a.im[j]);
      j++;
    }

    // Full-scale floats alternating in sign overshoot a float: every output
    // stays finite, and the filter goes on afterwards.
    hilbert loud;
    std::vector<float> huge(N);
    for (std::size_t i = 0; i < huge.size(); ++i)
      huge[i] = (i % 2 == 0 ? 1.f : -1.f) * std::numeric_limits<float>::max();
    const Pair blown = Run(loud, huge);
    for (std::size_t i = 0; i < huge.size(); ++i) {
      CAPTURE(i);
      CHECK(std::isfinite(blown.re[i]));
      CHECK(std::isfinite(blown.im[i]));
    }
    loud.clear();
    hilbert fresh;
    CHECK(Run(loud, in).re == Run(fresh, in).re);

    // clear(): the next block is that of a new filter.
    hilbert used;
    Run(used, in);
    used.clear();
    hilbert again;
    const Pair x = Run(used, in);
    const Pair y = Run(again, in);
    CHECK(x.re == y.re);
    CHECK(x.im == y.im);
  }

  TEST_CASE("hilbert: the three-buffer call copies both outputs (#1097)") {
    hilbert fn;
    hilbert reference;
    buffer in(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = std::sin(static_cast<float>(i) * 0.2f);
    buffer out1(N);
    buffer out2(N);
    fn(in, out1, out2);
    reference(in);
    CHECK(TestHelpers::buffersNearlyEqual(out1, reference.real(), 0.f));
    CHECK(TestHelpers::buffersNearlyEqual(out2, reference.imaginary(), 0.f));
  }

  TEST_CASE("hilbert: processing allocates nothing (#1097)") {
    hilbert fn;
    buffer in(N);
    buffer shorter(N / 2);
    in = 0.5f;
    shorter = -0.5f;
    fn(in);
    {
      TestHelpers::ProbeScope probe;
      fn(in);
      fn(shorter);
      fn(in);
      fn.process(0.25f);
      fn.clear();
      fn(in);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
