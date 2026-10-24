// Tests for the coefficient path of YSE::DSP::biQuad (YseEngine/dsp/filters.hpp),
// the filter behind ~biquad (issue #1090): the sign convention of
// y(n) = a0 x(n) + a1 x(n-1) + a2 x(n-2) - b1 y(n-1) - b2 y(n-2) against an
// independent loop and a closed form; agreement with the type path's lowpass;
// the sine gain against the magnitude response; smoothing of coefficient
// changes; per-sample coefficient signals; stoke() as a ringing oscillator;
// clear() and recovery from an unstable setting; degenerate settings from a
// running state; non-finite input and coefficients; the type path left as it
// was; block size independence and allocation freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <random>
#include <vector>
#include "dsp/filters.hpp"
#include "headers/constants.hpp"
#include "headers/enums.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::biQuad;
using YSE::DSP::buffer;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  using Coefs = std::array<double, 5>; // a0 a1 a2 b1 b2

  // Runs `fn` over `in` in blocks of `block` samples on whichever path it has.
  std::vector<float> Run(biQuad& fn, const std::vector<float>& in, unsigned int block = N) {
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

  std::vector<float> Noise(unsigned int samples, unsigned int seed) {
    std::mt19937 gen(seed); // NOLINT(cert-msc51-cpp,bugprone-random-generator-seed): repeatable
    std::uniform_real_distribution<float> dist(-1.f, 1.f);
    std::vector<float> v(samples);
    for (auto& s : v)
      s = dist(gen);
    return v;
  }

  // The equation as written, with the coefficients sample n uses in c[n].
  std::vector<double> Direct(const std::vector<Coefs>& c, const std::vector<float>& x) {
    std::vector<double> y(x.size(), 0.0);
    for (std::size_t n = 0; n < x.size(); ++n) {
      const double x1 = n >= 1 ? x[n - 1] : 0.0;
      const double x2 = n >= 2 ? x[n - 2] : 0.0;
      const double y1 = n >= 1 ? y[n - 1] : 0.0;
      const double y2 = n >= 2 ? y[n - 2] : 0.0;
      y[n] = c[n][0] * x[n] + c[n][1] * x1 + c[n][2] * x2 - c[n][3] * y1 - c[n][4] * y2;
    }
    return y;
  }

  std::vector<double> Direct(const Coefs& c, const std::vector<float>& x) {
    return Direct(std::vector<Coefs>(x.size(), c), x);
  }

  // The Butterworth-style bilinear lowpass, computed here independently.
  Coefs Lowpass(double hz, double q, double sr) {
    const double k = std::tan(PI * hz / sr);
    const double n = 1.0 / (1.0 + k / q + k * k);
    return {k * k * n, 2.0 * k * k * n, k * k * n, 2.0 * (k * k - 1.0) * n,
            (1.0 - k / q + k * k) * n};
  }

  double Gain(const Coefs& c, double w) {
    const std::complex<double> z1 = std::polar(1.0, -w);
    return std::abs((c[0] + c[1] * z1 + c[2] * z1 * z1) / (1.0 + c[3] * z1 + c[4] * z1 * z1));
  }

  void Set(biQuad& fn, const Coefs& c) {
    fn.coefficients(static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2]),
                    static_cast<float>(c[3]), static_cast<float>(c[4]));
  }

  Coefs AsFloat(const Coefs& c) {
    Coefs out{};
    for (int i = 0; i < 5; ++i)
      out[i] = static_cast<double>(static_cast<float>(c[i]));
    return out;
  }

  void CheckNear(const std::vector<float>& got, const std::vector<double>& want, double tol) {
    REQUIRE(got.size() == want.size());
    for (std::size_t i = 0; i < got.size(); ++i) {
      CAPTURE(i);
      CHECK(std::abs(static_cast<double>(got[i]) - want[i]) <= tol);
    }
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("biQuad coefficients: the feedback is subtracted, pinned by a closed form (#1090)") {
    // y(n) = x(n) - b1 y(n-1) with b1 = -0.5: h(n) = 0.5^n. A textbook that
    // adds the feedback would give (-0.5)^n.
    biQuad fn;
    fn.coefficients(1.f, 0.f, 0.f, -0.5f, 0.f);
    const std::vector<float> h = Run(fn, Impulse(20));
    for (int n = 0; n < 20; ++n) {
      CAPTURE(n);
      CHECK(h[n] == doctest::Approx(std::pow(0.5, n)).epsilon(1e-6));
    }
    // b2 alone: h(2m) = (-b2)^m.
    biQuad two;
    two.coefficients(1.f, 0.f, 0.f, 0.f, 0.25f);
    const std::vector<float> h2 = Run(two, Impulse(12));
    for (int n = 0; n < 12; ++n) {
      CAPTURE(n);
      const double expected = n % 2 == 0 ? std::pow(-0.25, n / 2) : 0.0;
      CHECK(h2[n] == doctest::Approx(expected).epsilon(1e-6));
    }
    // The feedforward taps come out in order.
    biQuad fir;
    fir.coefficients(0.5f, -0.25f, 0.125f, 0.f, 0.f);
    const std::vector<float> h3 = Run(fir, Impulse(5));
    CHECK(h3[0] == 0.5f);
    CHECK(h3[1] == -0.25f);
    CHECK(h3[2] == 0.125f);
    CHECK(h3[3] == 0.f);
    CHECK(h3[4] == 0.f);
    const biQuad::coefficientSet c = fir.coefficients();
    CHECK(c.a0 == 0.5f);
    CHECK(c.a1 == -0.25f);
    CHECK(c.a2 == 0.125f);
    CHECK(c.b1 == 0.f);
    CHECK(c.b2 == 0.f);
  }

  TEST_CASE("biQuad coefficients: impulse and noise against the direct form; the type path's "
            "lowpass as coefficients (#1090)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const Coefs lp = Lowpass(2000.0, 0.9, 48000.0);
    biQuad fn;
    Set(fn, lp);
    CheckNear(Run(fn, Impulse(3 * N)), Direct(AsFloat(lp), Impulse(3 * N)), 1e-6);

    const std::vector<float> noise = Noise(4 * N, 1090);
    biQuad n2;
    Set(n2, lp);
    CheckNear(Run(n2, noise), Direct(AsFloat(lp), noise), 1e-5);

    // The type path computes the same filter with its own coefficients and
    // form; the two agree to float rounding.
    biQuad typed;
    typed.set(YSE::BQ_LOWPASS, 2000.f, 0.9f);
    biQuad coef;
    Set(coef, lp);
    const std::vector<float> a = Run(typed, noise);
    const std::vector<float> b = Run(coef, noise);
    for (std::size_t i = 0; i < a.size(); ++i) {
      CAPTURE(i);
      CHECK(a[i] == doctest::Approx(b[i]).epsilon(1e-4).scale(1.0));
    }
  }

  TEST_CASE("biQuad coefficients: the sine gain is the magnitude response (#1090)") {
    const std::vector<Coefs> designs = {
        Lowpass(1000.0, 0.7071, 44100.0),
        Lowpass(8000.0, 4.0, 44100.0),
        {0.5, 0.0, -0.5, -1.6, 0.81}, // a resonant bandpass, poles at 0.9
        {1.0, -2.0, 1.0, 0.0, 0.0}}; // a second difference
    for (std::size_t d = 0; d < designs.size(); ++d) {
      CAPTURE(d);
      for (const double w : {0.05, 0.3, 1.0, 2.5}) {
        CAPTURE(w);
        biQuad fn;
        Set(fn, designs[d]);
        std::vector<float> in(16 * N);
        for (std::size_t i = 0; i < in.size(); ++i)
          in[i] = static_cast<float>(std::sin(w * static_cast<double>(i)));
        const std::vector<float> out = Run(fn, in);
        // Peak over the settled second half.
        float peak = 0.f;
        for (std::size_t i = in.size() / 2; i < in.size(); ++i)
          peak = std::max(peak, std::abs(out[i]));
        CHECK(peak == doctest::Approx(Gain(AsFloat(designs[d]), w)).epsilon(0.01));
      }
    }
  }

  TEST_CASE("biQuad coefficients: smoothing moves a change over SMOOTH_SAMPLES samples; off it "
            "jumps; the first coefficients apply at once (#1090)") {
    const Coefs first = {0.25, 0.0, 0.0, -0.5, 0.0};
    const Coefs second = {1.0, 0.5, -0.25, 0.3, 0.2};
    const std::vector<float> in = Noise(3 * N, 7);
    constexpr std::size_t change = 100;

    // The coefficients sample n uses: first until the change, then in a
    // straight line to second, reaching it on the 64th sample.
    std::vector<Coefs> smoothed(in.size(), AsFloat(first));
    std::vector<Coefs> jumped(in.size(), AsFloat(first));
    for (std::size_t n = change; n < in.size(); ++n) {
      const std::size_t k = n - change + 1;
      jumped[n] = AsFloat(second);
      for (int i = 0; i < 5; ++i) {
        const double from = AsFloat(first)[i];
        const double to = AsFloat(second)[i];
        smoothed[n][i] = k >= biQuad::SMOOTH_SAMPLES
                             ? to
                             : from + (to - from) * static_cast<double>(k) / biQuad::SMOOTH_SAMPLES;
      }
    }

    for (const bool smooth : {true, false}) {
      CAPTURE(smooth);
      biQuad fn;
      CHECK(fn.smooth()); // on by default
      fn.smooth(smooth);
      Set(fn, first);
      std::vector<float> out;
      for (std::size_t n = 0; n < in.size(); ++n) {
        if (n == change) Set(fn, second);
        out.push_back(fn.process(in[n]));
      }
      CheckNear(out, Direct(smooth ? smoothed : jumped, in), 1e-5);
    }

    // The same through blocks: a change between blocks ramps across the
    // boundary whatever the block size.
    for (const unsigned int block : {1u, 10u, 50u}) {
      CAPTURE(block);
      biQuad fn;
      Set(fn, first);
      std::vector<float> head(in.begin(), in.begin() + change);
      std::vector<float> tail(in.begin() + change, in.end());
      std::vector<float> out = Run(fn, head, block);
      Set(fn, second);
      const std::vector<float> rest = Run(fn, tail, block);
      out.insert(out.end(), rest.begin(), rest.end());
      CheckNear(out, Direct(smoothed, in), 1e-5);
    }

    // Turning smoothing off mid-move lands on the target at once.
    biQuad fn;
    Set(fn, first);
    (void)fn.process(0.f);
    Set(fn, second);
    fn.smooth(false);
    biQuad target;
    Set(target, second);
    for (int n = 0; n < 8; ++n)
      CHECK(fn.process(in[n]) == doctest::Approx(target.process(in[n])).epsilon(1e-6));
  }

  TEST_CASE("biQuad coefficients: coefficient signals are read per sample, unsmoothed, mixed "
            "with scalars (#1090)") {
    const std::vector<float> in = Noise(N, 11);
    buffer x(N);
    buffer a0(N);
    buffer b1(N);
    std::vector<Coefs> c(N);
    for (unsigned int i = 0; i < N; ++i) {
      x.getPtr()[i] = in[i];
      a0.getPtr()[i] = 0.2f + 0.004f * static_cast<float>(i);
      b1.getPtr()[i] = -0.9f + 0.01f * static_cast<float>(i % 50);
      c[i] = {static_cast<double>(a0.getPtr()[i]), 0.1, 0.05, static_cast<double>(b1.getPtr()[i]),
              0.3};
    }
    biQuad fn;
    fn.coefficients(0.f, 0.1f, 0.05f, 0.f, 0.3f);
    buffer& out = fn(x, &a0, nullptr, nullptr, &b1, nullptr);
    REQUIRE(out.getLength() == N);
    std::vector<float> got(out.getPtr(), out.getPtr() + N);
    for (auto& row : c)
      row = AsFloat(row);
    CheckNear(got, Direct(c, in), 1e-5);
    // A signal coefficient becomes the value a later scalar change moves from.
    CHECK(fn.coefficients().a0 == a0.getPtr()[N - 1]);
    CHECK(fn.coefficients().b1 == b1.getPtr()[N - 1]);

    // A non-finite signal sample keeps the coefficient before it.
    buffer bad(N);
    bad = 0.5f;
    bad.getPtr()[3] = std::numeric_limits<float>::quiet_NaN();
    biQuad g;
    g.coefficients(0.f, 0.f, 0.f, 0.f, 0.f);
    buffer impulse(N);
    impulse = 0.f;
    impulse.getPtr()[3] = 1.f;
    buffer& r = g(impulse, &bad, nullptr, nullptr, nullptr, nullptr);
    CHECK(r.getPtr()[3] == 0.5f);
  }

  TEST_CASE("biQuad coefficients: stoke() preloads the state, a ringing oscillator (#1090)") {
    // Poles on the unit circle at w, no input path: y(n) = 2 cos(w) y(n-1)
    // - y(n-2). With y(-1) = 0 and y(-2) = sin(-w) it is sin(w (n + 1)).
    const double w = 2.0 * PI * 440.0 / 48000.0;
    biQuad fn;
    fn.coefficients(0.f, 0.f, 0.f, static_cast<float>(-2.0 * std::cos(w)), 1.f);
    fn.stoke(0.f, static_cast<float>(std::sin(-w)));
    std::vector<float> silence(4 * N, 0.f);
    const std::vector<float> out = Run(fn, silence);
    for (std::size_t n = 0; n < out.size(); ++n) {
      CAPTURE(n);
      CHECK(std::abs(out[n] - std::sin(w * static_cast<double>(n + 1))) < 1e-3);
    }
    // The inputs too: an FIR of x(n-1) and x(n-2).
    biQuad fir;
    fir.coefficients(0.f, 1.f, 10.f, 0.f, 0.f);
    fir.stoke(0.f, 0.f, 2.f, 3.f);
    CHECK(fir.process(0.f) == 2.f + 30.f);
    CHECK(fir.process(0.f) == 20.f);
    CHECK(fir.process(0.f) == 0.f);
    // Non-finite values are 0.
    biQuad nan;
    nan.coefficients(0.f, 0.f, 0.f, -1.f, 0.f);
    nan.stoke(std::numeric_limits<float>::quiet_NaN());
    CHECK(nan.process(0.f) == 0.f);
  }

  TEST_CASE("biQuad coefficients: an unstable filter grows, starts again on overflow, and clear() "
            "recovers it (#1090)") {
    const std::vector<float> noise = Noise(N, 3);
    biQuad fn;
    fn.smooth(false);
    fn.coefficients(1.f, 0.f, 0.f, -0.5f, 0.f);
    (void)Run(fn, noise);
    // b1 = -1.5: a pole at 1.5, outside the unit circle, from a running state.
    fn.coefficients(1.f, 0.f, 0.f, -1.5f, 0.f);
    bool restarted = false;
    float largest = 0.f;
    for (int i = 0; i < 4000; ++i) {
      const float y = fn.process(i == 0 ? 1.f : 0.f);
      REQUIRE(std::isfinite(y));
      largest = std::max(largest, std::abs(y));
      if (i > 0 && y == 0.f) restarted = true;
    }
    CHECK(largest > 1e30f);
    CHECK(restarted);
    // Lowered again and cleared: the impulse response is the stable one.
    fn.coefficients(1.f, 0.f, 0.f, -0.5f, 0.f);
    fn.clear();
    const std::vector<float> h = Run(fn, Impulse(10));
    for (int n = 0; n < 10; ++n)
      CHECK(h[n] == doctest::Approx(std::pow(0.5, n)).epsilon(1e-6));
  }

  TEST_CASE("biQuad coefficients: degenerate settings from a running state (#1090)") {
    const std::vector<float> noise = Noise(N, 5);
    const Coefs lp = Lowpass(3000.0, 2.0, 44100.0);

    // All zero, unsmoothed: silence from the next sample, whatever the state.
    biQuad fn;
    fn.smooth(false);
    Set(fn, lp);
    (void)Run(fn, noise);
    fn.coefficients(0.f, 0.f, 0.f, 0.f, 0.f);
    for (const float y : Run(fn, noise))
      CHECK(y == 0.f);

    // Smoothed to zero: silent once the move is over and the state has
    // passed out of the taps.
    biQuad sm;
    Set(sm, lp);
    (void)Run(sm, noise);
    sm.coefficients(0.f, 0.f, 0.f, 0.f, 0.f);
    const std::vector<float> out = Run(sm, noise);
    for (std::size_t i = biQuad::SMOOTH_SAMPLES; i < out.size(); ++i) {
      CAPTURE(i);
      CHECK(out[i] == 0.f);
    }

    // A pure delay with the feedback gone: a2 = 1 is x(n-2), state included.
    biQuad delay;
    delay.smooth(false);
    Set(delay, lp);
    (void)delay.process(0.7f);
    (void)delay.process(-0.3f);
    delay.coefficients(0.f, 0.f, 1.f, 0.f, 0.f);
    CHECK(delay.process(0.f) == 0.7f);
    CHECK(delay.process(0.f) == -0.3f);
    CHECK(delay.process(0.f) == 0.f);

    // Poles exactly on the unit circle (b2 = 1) keep ringing, bounded.
    biQuad edge;
    edge.coefficients(1.f, 0.f, 0.f, 0.f, 1.f);
    float peak = 0.f;
    const std::vector<float> ring = Run(edge, Impulse(8 * N));
    for (const float y : ring)
      peak = std::max(peak, std::abs(y));
    CHECK(peak == 1.f);
  }

  TEST_CASE("biQuad coefficients: non-finite input and coefficients are ignored (#1090)") {
    biQuad fn;
    fn.coefficients(1.f, 0.f, 0.f, -0.5f, 0.f);
    CHECK(fn.process(1.f) == 1.f);
    CHECK(fn.process(std::numeric_limits<float>::quiet_NaN()) == 1.f); // held
    CHECK(fn.process(std::numeric_limits<float>::infinity()) == 1.f);
    CHECK(fn.process(0.f) == 0.5f); // the skipped samples left no trace
    fn.smooth(false);
    fn.coefficients(std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f,
                    std::numeric_limits<float>::infinity(), 0.f);
    CHECK(fn.coefficients().a0 == 1.f);
    CHECK(fn.coefficients().b1 == -0.5f);
    CHECK(fn.process(0.f) == 0.25f);
  }

  TEST_CASE("biQuad coefficients: the type path is left as it was, and selected again by its "
            "setters (#1090)") {
    TestHelpers::ScopedSampleRate rate(44100);
    const std::vector<float> noise = Noise(2 * N, 13);

    // The type path's own output: the direct form II loop the class had
    // before #1090, computed here from the same coefficients the type
    // setter derives (the coefficient path never touches them).
    const Coefs lp = Lowpass(1500.0, 0.7071, 44100.0);
    std::vector<double> legacy(noise.size());
    {
      float last = 0.f;
      float previous = 0.f;
      const auto f = [](double v) { return static_cast<float>(v); };
      for (std::size_t i = 0; i < noise.size(); ++i) {
        const float w = noise[i] + f(-lp[3]) * last + f(-lp[4]) * previous;
        legacy[i] = f(lp[0]) * w + f(lp[1]) * last + f(lp[2]) * previous;
        previous = last;
        last = w;
      }
    }
    biQuad typed;
    typed.set(YSE::BQ_LOWPASS, 1500.f, 0.7071f);
    CheckNear(Run(typed, noise), legacy, 1e-4);

    // Coefficient path, then a type setter: back to the type path.
    biQuad fn;
    fn.coefficients(0.f, 0.f, 0.f, 0.f, 0.f);
    CHECK(Run(fn, noise)[N] == 0.f);
    fn.clear();
    fn.set(YSE::BQ_LOWPASS, 1500.f, 0.7071f);
    CheckNear(Run(fn, noise), legacy, 1e-4);
    // setRaw (Pure Data's signs) selects it too.
    biQuad raw;
    raw.coefficients(1.f, 0.f, 0.f, 0.f, 0.f);
    raw.setRaw(0.5f, 0.f, 1.f, 0.f, 0.f);
    const std::vector<float> h = Run(raw, Impulse(4));
    CHECK(h[1] == 0.5f);
    CHECK(h[2] == 0.25f);

    // A copy keeps the path and the state.
    biQuad a;
    a.coefficients(1.f, 0.f, 0.f, -0.5f, 0.f);
    (void)a.process(1.f);
    biQuad b(a);
    CHECK(b.process(0.f) == a.process(0.f));
  }

  TEST_CASE("biQuad coefficients: blocks, single samples and buffer clamping (#1090)") {
    const std::vector<float> in = Noise(5 * N, 17);
    const Coefs lp = Lowpass(5000.0, 3.0, 48000.0);
    biQuad whole;
    Set(whole, lp);
    const std::vector<float> reference = Run(whole, in);
    for (const unsigned int block : {1u, 7u, 64u, N}) {
      CAPTURE(block);
      biQuad fn;
      Set(fn, lp);
      CHECK(Run(fn, in, block) == reference);
    }
    biQuad single;
    Set(single, lp);
    for (std::size_t i = 0; i < in.size(); ++i)
      CHECK(single.process(in[i]) == reference[i]);

    biQuad fn;
    Set(fn, lp);
    buffer longer(2 * N);
    longer = 1.f;
    CHECK(fn(longer).getLength() == N);
    buffer shortSignal(N / 4);
    shortSignal = 0.5f;
    CHECK(fn(longer, &shortSignal, nullptr, nullptr, nullptr, nullptr).getLength() == N / 4);
  }

  TEST_CASE("biQuad coefficients: processing allocates nothing (#1090)") {
    biQuad fn;
    buffer in(N);
    buffer shorter(N / 2);
    buffer a0(N);
    buffer b2(N);
    in = 0.5f;
    shorter = 0.25f;
    a0 = 0.3f;
    b2 = 0.2f;
    fn.coefficients(0.5f, 0.1f, 0.1f, -0.2f, 0.1f);
    fn(in);
    {
      TestHelpers::ProbeScope probe;
      fn.coefficients(0.4f, 0.2f, 0.1f, -0.3f, 0.1f);
      fn(in);
      fn(shorter);
      fn(in, &a0, nullptr, nullptr, nullptr, &b2);
      (void)fn.process(0.1f);
      fn.smooth(false).smooth(true);
      fn.stoke(0.1f, 0.2f, 0.3f, 0.4f);
      (void)fn.coefficients();
      fn.clear();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
