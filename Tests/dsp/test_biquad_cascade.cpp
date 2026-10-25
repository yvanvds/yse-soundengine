// Tests for YSE::DSP::biquadCascade (YseEngine/dsp/filters/biquadCascade.hpp),
// the filter behind ~cascade (issue #1091): sections in series against an
// independent direct-form loop and closed forms; a fourth-order Butterworth
// lowpass against its magnitude response; the list grammar (sets of five,
// capacity, incomplete sets); smoothing of running sections and fresh starts
// of new ones; clear, bypass and zero; non-finite values; block size
// independence, copies and allocation freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <vector>
#include "dsp/filters.hpp"
#include "dsp/filters/biquadCascade.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::biQuad;
using YSE::DSP::biquadCascade;
using YSE::DSP::buffer;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  // Runs `fn` over `in` in blocks of `block` samples.
  std::vector<float> Run(biquadCascade& fn, const std::vector<float>& in, unsigned int block = N) {
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

  // The same through process(), one sample at a time.
  std::vector<float> RunSamples(biquadCascade& fn, const std::vector<float>& in) {
    std::vector<float> out;
    out.reserve(in.size());
    for (float x : in)
      out.push_back(fn.process(x));
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

  // The sections in series as written, each rounded to a float between
  // sections as the class passes floats from one to the next.
  std::vector<double> Direct(const std::vector<float>& c, const std::vector<float>& x) {
    std::vector<double> signal(x.begin(), x.end());
    for (std::size_t s = 0; s + 5 <= c.size(); s += 5) {
      std::vector<double> y(signal.size(), 0.0);
      for (std::size_t n = 0; n < signal.size(); ++n) {
        const double x1 = n >= 1 ? signal[n - 1] : 0.0;
        const double x2 = n >= 2 ? signal[n - 2] : 0.0;
        const double y1 = n >= 1 ? y[n - 1] : 0.0;
        const double y2 = n >= 2 ? y[n - 2] : 0.0;
        y[n] = c[s] * signal[n] + c[s + 1] * x1 + c[s + 2] * x2 - c[s + 3] * y1 - c[s + 4] * y2;
      }
      for (std::size_t n = 0; n < y.size(); ++n)
        signal[n] = static_cast<double>(static_cast<float>(y[n]));
    }
    return signal;
  }

  // A lowpass section of the usual design: f in Hz, at `rate`.
  std::array<float, 5> Lowpass(double f, double q, double rate) {
    const double k = std::tan(PI * f / rate);
    const double n = 1.0 / (1.0 + k / q + k * k);
    const double a0 = n * k * k;
    return {static_cast<float>(a0), static_cast<float>(2.0 * a0), static_cast<float>(a0),
            static_cast<float>(2.0 * n * (k * k - 1.0)),
            static_cast<float>(n * (1.0 - k / q + k * k))};
  }

  // A fourth-order Butterworth lowpass at `f` as two sections.
  std::vector<float> Butterworth4(double f, double rate) {
    const auto a = Lowpass(f, 0.5411961, rate);
    const auto b = Lowpass(f, 1.3065630, rate);
    std::vector<float> c(a.begin(), a.end());
    c.insert(c.end(), b.begin(), b.end());
    return c;
  }

  // The steady-state gain of a sine at `f` through `fn`.
  double SineGain(biquadCascade& fn, double f, double rate) {
    const unsigned int total = 40 * N;
    std::vector<float> in(total);
    for (unsigned int i = 0; i < total; ++i)
      in[i] = static_cast<float>(std::sin(2.0 * PI * f * i / rate));
    const auto out = Run(fn, in);
    double inSq = 0.0;
    double outSq = 0.0;
    for (unsigned int i = total / 2; i < total; ++i) {
      inSq += static_cast<double>(in[i]) * in[i];
      outSq += static_cast<double>(out[i]) * out[i];
    }
    return std::sqrt(outSq / inSq);
  }

  void CheckClose(const std::vector<float>& actual, const std::vector<double>& expected,
                  double tolerance) {
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < actual.size(); ++i) {
      CAPTURE(i);
      CHECK(std::abs(static_cast<double>(actual[i]) - expected[i]) <= tolerance);
    }
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("biquadCascade: capacity, defaults and an empty cascade passing its input (#1091)") {
    biquadCascade fn;
    CHECK(fn.capacity() == biquadCascade::DEFAULT_CAPACITY);
    CHECK(fn.sections() == 0);
    CHECK(fn.smooth());
    CHECK_FALSE(fn.bypass());
    CHECK_FALSE(fn.zero());
    CHECK(biquadCascade(0).capacity() == 1);
    CHECK(biquadCascade(3).capacity() == 3);
    CHECK(biquadCascade(1000).capacity() == biquadCascade::MAX_CAPACITY);

    const auto in = Noise(3 * N, 1);
    CHECK(Run(fn, in) == in);
    CHECK(RunSamples(fn, in) == in);
    CHECK(fn.coefficients(0).a0 == 0.f);
  }

  TEST_CASE("biquadCascade: one section is the biQuad coefficient path (#1091)") {
    const float c[5] = {0.3f, 0.2f, -0.1f, -0.5f, 0.25f};
    biquadCascade fn;
    fn.coefficients(c, 5);
    CHECK(fn.sections() == 1);
    biQuad ref;
    ref.coefficients(c[0], c[1], c[2], c[3], c[4]);
    const auto in = Noise(4 * N, 2);
    const auto out = Run(fn, in);
    for (std::size_t i = 0; i < in.size(); ++i) {
      CAPTURE(i);
      CHECK(out[i] == ref.process(in[i]));
    }
    const auto set = fn.coefficients(0);
    CHECK(set.a0 == c[0]);
    CHECK(set.a1 == c[1]);
    CHECK(set.a2 == c[2]);
    CHECK(set.b1 == c[3]);
    CHECK(set.b2 == c[4]);
  }

  TEST_CASE("biquadCascade: sections in series against closed forms and a direct form (#1091)") {
    SUBCASE("delays add up: z^-1 then z^-2 is z^-3, times the gains") {
      const float c[10] = {0.f, 0.5f, 0.f, 0.f, 0.f, 0.f, 0.f, 3.f, 0.f, 0.f};
      biquadCascade fn(2);
      fn.coefficients(c, 10);
      const auto out = Run(fn, Impulse(N));
      for (unsigned int i = 0; i < N; ++i) {
        CAPTURE(i);
        CHECK(out[i] == (i == 3 ? 1.5f : 0.f));
      }
    }
    SUBCASE("two one-pole recursions: (1 - p z^-1)^-1 (1 - q z^-1)^-1") {
      // h(n) = (p^(n+1) - q^(n+1)) / (p - q)
      const double p = 0.9;
      const double q = -0.5;
      const float c[10] = {1.f, 0.f, 0.f, static_cast<float>(-p), 0.f,
                           1.f, 0.f, 0.f, static_cast<float>(-q), 0.f};
      biquadCascade fn;
      fn.coefficients(c, 10);
      const auto out = Run(fn, Impulse(2 * N));
      for (unsigned int n = 0; n < 2 * N; ++n) {
        CAPTURE(n);
        const double pf = static_cast<float>(p);
        const double qf = static_cast<float>(q);
        const double h = (std::pow(pf, n + 1) - std::pow(qf, n + 1)) / (pf - qf);
        CHECK(std::abs(out[n] - h) < 1e-5);
      }
    }
    SUBCASE("four sections on noise against an independent loop") {
      const std::vector<float> c = {0.2f, 0.4f,  0.2f,   -0.6f, 0.2f,  0.9f,  -0.3f,
                                    0.1f, -0.4f, 0.5f,   0.5f,  0.f,   -0.5f, -1.2f,
                                    0.6f, 1.f,   -1.95f, 1.f,   -1.8f, 0.9f};
      biquadCascade fn(4);
      fn.coefficients(c.data(), static_cast<unsigned int>(c.size()));
      CHECK(fn.sections() == 4);
      const auto in = Noise(6 * N, 3);
      CheckClose(Run(fn, in), Direct(c, in), 1e-5);
    }
  }

  TEST_CASE("biquadCascade: a fourth-order Butterworth lowpass by its magnitude response "
            "(#1091)") {
    const double rate = 44100.0;
    const double fc = 1000.0;
    const auto c = Butterworth4(fc, rate);
    const auto check = [&](double f) {
      CAPTURE(f);
      biquadCascade fn;
      fn.coefficients(c.data(), 10);
      // |H| = 1 / sqrt(1 + (f'/fc')^8) in the prewarped frequencies.
      const double ratio = std::tan(PI * f / rate) / std::tan(PI * fc / rate);
      const double expected = 1.0 / std::sqrt(1.0 + std::pow(ratio, 8.0));
      CHECK(SineGain(fn, f, rate) == doctest::Approx(expected).epsilon(0.01));
    };
    check(100.0);
    check(500.0);
    check(fc); // -3 dB
    check(2000.0);
    check(4000.0);
  }

  TEST_CASE("biquadCascade: the list grammar (#1091)") {
    const std::vector<float> two = {0.5f, 0.f, 0.f, 0.f, 0.f, 2.f, 0.f, 0.f, 0.f, 0.f};

    SUBCASE("an incomplete trailing set is ignored") {
      biquadCascade fn;
      std::vector<float> c = two;
      c.push_back(9.f);
      c.push_back(9.f);
      fn.coefficients(c.data(), static_cast<unsigned int>(c.size()));
      CHECK(fn.sections() == 2);
      CHECK(fn.coefficients(2).a0 == 0.f);
    }
    SUBCASE("fewer than five values, or none, change nothing") {
      biquadCascade fn;
      fn.coefficients(two.data(), 10);
      fn.coefficients(two.data(), 4);
      fn.coefficients(nullptr, 10);
      fn.coefficients(two.data(), 0);
      CHECK(fn.sections() == 2);
      CHECK(fn.coefficients(1).a0 == 2.f);
    }
    SUBCASE("sets beyond the capacity are ignored") {
      biquadCascade fn(1);
      fn.coefficients(two.data(), 10);
      CHECK(fn.sections() == 1);
      CHECK(fn.coefficients(0).a0 == 0.5f);
      CHECK(fn.coefficients(1).a0 == 0.f);
      const auto in = Noise(N, 4);
      const auto out = Run(fn, in);
      for (unsigned int i = 0; i < N; ++i)
        CHECK(out[i] == 0.5f * in[i]);
    }
    SUBCASE("a shorter list drops the last sections; a longer one starts them fresh") {
      biquadCascade fn;
      const std::vector<float> ring = {1.f, 0.f, 0.f, -1.9f, 0.95f, 1.f, 0.f, 0.f, -1.8f, 0.9f};
      fn.coefficients(ring.data(), 10);
      (void)Run(fn, Noise(2 * N, 5)); // both sections now hold state
      fn.coefficients(ring.data(), 5);
      CHECK(fn.sections() == 1);
      CHECK(fn.coefficients(1).a0 == 0.f);
      // Back to two: section 1 starts from silence at its coefficients, no
      // ramp, as a fresh cascade's would. Section 0 runs on as it was.
      biquadCascade first;
      first.coefficients(ring.data(), 5);
      (void)Run(first, Noise(2 * N, 5));
      fn.coefficients(ring.data(), 10);
      first.coefficients(ring.data(), 5);
      biQuad second;
      second.coefficients(ring[5], ring[6], ring[7], ring[8], ring[9]);
      const auto in = Noise(N, 6);
      const auto out = Run(fn, in);
      for (unsigned int i = 0; i < N; ++i) {
        CAPTURE(i);
        CHECK(out[i] == second.process(first.process(in[i])));
      }
    }
  }

  TEST_CASE("biquadCascade: smoothing of running sections (#1091)") {
    const float from[10] = {0.5f, 0.f, 0.f, -0.5f, 0.f, 1.f, 0.2f, 0.f, 0.3f, 0.f};
    const float to[10] = {0.1f, 0.3f, 0.1f, -0.2f, 0.4f, 0.4f, 0.f, 0.4f, -0.6f, 0.2f};
    for (bool smooth : {true, false}) {
      CAPTURE(smooth);
      biquadCascade fn;
      fn.smooth(smooth);
      CHECK(fn.smooth() == smooth);
      biQuad a;
      biQuad b;
      a.smooth(smooth);
      b.smooth(smooth);
      fn.coefficients(from, 10);
      a.coefficients(from[0], from[1], from[2], from[3], from[4]);
      b.coefficients(from[5], from[6], from[7], from[8], from[9]);
      const auto in = Noise(3 * N, 7);
      std::vector<float> expected;
      for (std::size_t i = 0; i < in.size(); ++i) {
        if (i == N + 17) {
          a.coefficients(to[0], to[1], to[2], to[3], to[4]);
          b.coefficients(to[5], to[6], to[7], to[8], to[9]);
        }
        expected.push_back(b.process(a.process(in[i])));
      }
      std::vector<float> out;
      for (std::size_t i = 0; i < in.size(); ++i) {
        if (i == N + 17) fn.coefficients(to, 10);
        out.push_back(fn.process(in[i]));
      }
      CHECK(out == expected);
      // The getter reports where a section is going.
      CHECK(fn.coefficients(1).b1 == to[8]);
    }
    // Turning smoothing off ends a move under way.
    biquadCascade fn;
    fn.coefficients(from, 10);
    (void)fn.process(0.f);
    fn.coefficients(to, 10);
    fn.smooth(false);
    biquadCascade direct;
    direct.smooth(false);
    direct.coefficients(to, 10);
    const auto in = Impulse(N);
    CHECK(Run(fn, in) == Run(direct, in));
  }

  TEST_CASE("biquadCascade: clear, bypass and zero (#1091)") {
    const auto c = Butterworth4(2000.0, 48000.0);

    SUBCASE("clear silences, keeps the sections, and the next list applies at once") {
      biquadCascade fn;
      fn.coefficients(c.data(), 10);
      (void)Run(fn, Noise(2 * N, 8));
      fn.clear();
      CHECK(fn.sections() == 2);
      CHECK(fn.coefficients(0).a0 == 0.f);
      CHECK(fn.coefficients(1).b2 == 0.f);
      const auto silent = Run(fn, Noise(N, 9));
      CHECK(std::all_of(silent.begin(), silent.end(), [](float v) { return v == 0.f; }));
      fn.coefficients(c.data(), 10);
      biquadCascade fresh;
      fresh.coefficients(c.data(), 10);
      const auto in = Noise(2 * N, 10);
      CHECK(Run(fn, in) == Run(fresh, in));
    }
    SUBCASE("bypass passes the input, zero silences, bypass wins; both keep the sections "
            "running") {
      biquadCascade fn;
      biquadCascade ref;
      fn.coefficients(c.data(), 10);
      ref.coefficients(c.data(), 10);
      const auto a = Noise(N, 11);
      const auto b = Noise(N, 12);
      const auto d = Noise(N, 13);
      fn.bypass(true);
      CHECK(fn.bypass());
      CHECK(Run(fn, a) == a);
      (void)Run(ref, a);
      fn.zero(true);
      CHECK(fn.zero());
      CHECK(Run(fn, b) == b); // bypass wins
      (void)Run(ref, b);
      fn.bypass(false);
      const auto zeroed = Run(fn, d);
      CHECK(std::all_of(zeroed.begin(), zeroed.end(), [](float v) { return v == 0.f; }));
      CHECK(fn.process(1.f) == 0.f);
      CHECK(fn.bypass(true).process(0.25f) == 0.25f);
      (void)Run(ref, d);
      (void)ref.process(1.f);
      (void)ref.process(0.25f);
      fn.bypass(false).zero(false);
      const auto e = Noise(N, 14);
      CHECK(Run(fn, e) == Run(ref, e));
    }
    SUBCASE("zero and bypass on an empty cascade") {
      biquadCascade fn;
      const auto in = Noise(N, 15);
      fn.zero(true);
      const auto out = Run(fn, in);
      CHECK(std::all_of(out.begin(), out.end(), [](float v) { return v == 0.f; }));
      fn.bypass(true);
      CHECK(Run(fn, in) == in);
    }
  }

  TEST_CASE("biquadCascade: non-finite coefficients and input, and an exploding section "
            "(#1091)") {
    const float nan = std::nanf("");
    const float inf = std::numeric_limits<float>::infinity();
    biquadCascade fn;
    fn.smooth(false);
    const float c[10] = {0.5f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, -0.5f, 0.f};
    fn.coefficients(c, 10);
    // A non-finite coefficient keeps that coefficient's value.
    const float bad[10] = {nan, 0.f, inf, 0.f, 0.f, 1.f, 0.f, 0.f, nan, 0.f};
    fn.coefficients(bad, 10);
    CHECK(fn.coefficients(0).a0 == 0.5f);
    CHECK(fn.coefficients(0).a2 == 0.f);
    CHECK(fn.coefficients(1).b1 == -0.5f);
    // A non-finite input sample is skipped: the output holds.
    const float first = fn.process(1.f);
    CHECK(std::isfinite(first));
    CHECK(std::isfinite(fn.process(nan)));
    CHECK(std::isfinite(fn.process(inf)));

    // An unstable section grows until it overflows a float and restarts
    // from silence; it never latches infinity.
    biquadCascade wild;
    const float grow[10] = {1.f, 0.f, 0.f, -2.5f, 1.f, 1.f, 0.f, 0.f, 0.f, 0.f};
    wild.coefficients(grow, 10);
    const auto out = Run(wild, Noise(20 * N, 16));
    CHECK(std::all_of(out.begin(), out.end(), [](float v) { return std::isfinite(v); }));
  }

  TEST_CASE("biquadCascade: block size independence, sample calls and copies (#1091)") {
    const auto c = Butterworth4(3000.0, 44100.0);
    const auto in = Noise(5 * N, 17);
    biquadCascade whole;
    whole.coefficients(c.data(), 10);
    const auto expected = Run(whole, in);
    for (unsigned int block : {1u, 7u, 64u, N}) {
      CAPTURE(block);
      biquadCascade fn;
      fn.coefficients(c.data(), 10);
      CHECK(Run(fn, in, block) == expected);
    }
    biquadCascade samples;
    samples.coefficients(c.data(), 10);
    CHECK(RunSamples(samples, in) == expected);

    // A block longer than STANDARD_BUFFERSIZE is cut to it.
    biquadCascade fn;
    buffer longer(2 * N);
    longer = 0.5f;
    CHECK(fn(longer).getLength() == N);
    fn.coefficients(c.data(), 10);
    CHECK(fn(longer).getLength() == N);

    // A copy carries the state; a move keeps it.
    biquadCascade a;
    a.coefficients(c.data(), 10);
    (void)Run(a, in);
    biquadCascade b(a);
    CHECK(b.capacity() == a.capacity());
    CHECK(b.sections() == 2);
    const auto more = Noise(N, 18);
    const auto fromA = Run(a, more);
    CHECK(Run(b, more) == fromA);
    biquadCascade moved(std::move(b));
    biquadCascade target(2);
    target = std::move(moved);
    CHECK(target.capacity() == biquadCascade::DEFAULT_CAPACITY);
    CHECK(Run(target, more) == Run(a, more));
  }

  TEST_CASE("biquadCascade: processing and settings allocate nothing (#1091)") {
    const auto c = Butterworth4(3000.0, 44100.0);
    biquadCascade fn;
    buffer in(N);
    buffer shorter(N / 2);
    in = 0.5f;
    shorter = 0.25f;
    fn(in);
    {
      TestHelpers::ProbeScope probe;
      fn.coefficients(c.data(), 10);
      fn(in);
      fn(shorter);
      (void)fn.process(0.1f);
      fn.coefficients(c.data(), 5);
      fn.smooth(false).smooth(true);
      fn.bypass(true);
      fn(in);
      fn.zero(true).bypass(false);
      fn(in);
      fn.zero(false);
      (void)fn.coefficients(1);
      fn.clear();
      fn(in);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
