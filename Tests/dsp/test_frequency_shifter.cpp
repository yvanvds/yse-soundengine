// Tests for YSE::DSP::frequencyShifter (YseEngine/dsp/modules/frequencyShifter.hpp),
// the single-sideband frequency shifter behind ~freqshift (issue #1110): both
// outputs against the closed form on a separate DSP::hilbert, sines moved up
// and down by the shift with the other sideband suppressed, negative shifts
// and the mirror below 0 Hz, the oscillator following the live sample rate,
// the per-sample shift, resetPhase() and clear(), non-finite input and
// settings, block size independence and allocation freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/modules/frequencyShifter.hpp"
#include "dsp/modules/hilbert.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::frequencyShifter;
using YSE::DSP::hilbert;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  struct Pair {
    std::vector<float> up;
    std::vector<float> down;
  };

  // Runs `fn` over `in` in blocks of `block` samples at its own shift.
  Pair Run(frequencyShifter& fn, const std::vector<float>& in, unsigned int block = N) {
    Pair out;
    for (std::size_t start = 0; start < in.size(); start += block) {
      const auto length =
          static_cast<unsigned int>(std::min<std::size_t>(block, in.size() - start));
      buffer b(length);
      for (unsigned int i = 0; i < length; ++i)
        b.getPtr()[i] = in[start + i];
      buffer& result = fn(b);
      REQUIRE(&result == &fn.up());
      REQUIRE(fn.up().getLength() == length);
      REQUIRE(fn.down().getLength() == length);
      for (unsigned int i = 0; i < length; ++i) {
        out.up.push_back(fn.up().getPtr()[i]);
        out.down.push_back(fn.down().getPtr()[i]);
      }
    }
    return out;
  }

  // The amplitude of `v` at `w` radians per sample from sample `from` on,
  // fitted as a cos(w n) + b sin(w n) by least squares.
  double Amplitude(const std::vector<float>& v, double w, std::size_t from) {
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
    return std::hypot(a, b);
  }

  std::vector<float> Cosine(double hz, double rate, std::size_t samples) {
    std::vector<float> v(samples);
    const double w = 2.0 * PI * hz / rate;
    for (std::size_t n = 0; n < samples; ++n)
      v[n] = static_cast<float>(std::cos(w * static_cast<double>(n)));
    return v;
  }

  std::vector<float> Mixture(std::size_t samples) {
    std::vector<float> v(samples);
    for (std::size_t i = 0; i < v.size(); ++i) {
      const auto n = static_cast<float>(i);
      v[i] = 0.6f * std::sin(n * 0.043f) + 0.3f * std::sin(n * 0.71f) + 0.1f * std::cos(n * 2.3f);
    }
    return v;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("frequencyShifter: at the default shift of 0 both outputs are hilbert's real part "
            "(#1110)") {
    frequencyShifter fn;
    CHECK(fn.shift() == frequencyShifter::DEFAULT_SHIFT);
    CHECK(fn.shift() == 0.f);
    CHECK(fn.phase() == 0.f);
    const std::vector<float> in = Mixture(3 * N);
    const Pair out = Run(fn, in);
    hilbert reference;
    for (std::size_t n = 0; n < in.size(); ++n) {
      CAPTURE(n);
      const hilbert::outputs h = reference.process(in[n]);
      CHECK(out.up[n] == h.real);
      CHECK(out.down[n] == h.real);
    }
    CHECK(fn.phase() == 0.f);
  }

  TEST_CASE("frequencyShifter: both outputs against the closed form on a separate hilbert "
            "(#1110)") {
    TestHelpers::ScopedSampleRate rate(48000);
    for (const float hz : {37.f, 1000.5f, -250.f, 12000.f, 60000.f}) {
      CAPTURE(hz);
      frequencyShifter fn;
      fn.shift(hz);
      CHECK(fn.shift() == hz);
      const std::vector<float> in = Mixture(4 * N);
      const Pair out = Run(fn, in);
      hilbert reference;
      for (std::size_t n = 0; n < in.size(); ++n) {
        CAPTURE(n);
        const hilbert::outputs h = reference.process(in[n]);
        double p = static_cast<double>(n) * static_cast<double>(hz) / 48000.0;
        p -= std::floor(p);
        const double c = std::cos(2.0 * PI * p);
        const double s = std::sin(2.0 * PI * p);
        CHECK(std::abs(out.up[n] - (h.real * c - h.imaginary * s)) < 1e-5);
        CHECK(std::abs(out.down[n] - (h.real * c + h.imaginary * s)) < 1e-5);
      }
    }
  }

  TEST_CASE("frequencyShifter: a sine moves up and down by the shift, the other sideband "
            "suppressed by 40 dB in the band (#1110)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const double inputs[] = {1000.0, 440.0, 5000.0, 300.0, 2000.0};
    const float shifts[] = {200.f, 37.f, 2500.f, 60.f, -700.f};
    for (std::size_t i = 0; i < 5; ++i) {
      CAPTURE(inputs[i]);
      CAPTURE(shifts[i]);
      frequencyShifter fn;
      fn.shift(shifts[i]);
      const Pair out = Run(fn, Cosine(inputs[i], 48000.0, 48000));
      const double higher = 2.0 * PI * (inputs[i] + shifts[i]) / 48000.0;
      const double lower = 2.0 * PI * (inputs[i] - shifts[i]) / 48000.0;
      // Skip the first quarter second: the slowest pole (0.993) has settled.
      CHECK(Amplitude(out.up, higher, 12000) == doctest::Approx(1.0).epsilon(2e-3));
      CHECK(Amplitude(out.up, lower, 12000) < 0.01);
      CHECK(Amplitude(out.down, lower, 12000) == doctest::Approx(1.0).epsilon(2e-3));
      CHECK(Amplitude(out.down, higher, 12000) < 0.01);
    }
  }

  TEST_CASE("frequencyShifter: a frequency moved below 0 Hz comes back mirrored (#1110)") {
    TestHelpers::ScopedSampleRate rate(48000);
    frequencyShifter fn;
    fn.shift(1500.f);
    const Pair out = Run(fn, Cosine(1000.0, 48000.0, 48000));
    const double mirrored = 2.0 * PI * 500.0 / 48000.0;
    const double higher = 2.0 * PI * 2500.0 / 48000.0;
    // Down: 1000 - 1500 = -500, heard at 500 Hz.
    CHECK(Amplitude(out.down, mirrored, 12000) == doctest::Approx(1.0).epsilon(2e-3));
    CHECK(Amplitude(out.down, higher, 12000) < 0.01);
    CHECK(Amplitude(out.up, higher, 12000) == doctest::Approx(1.0).epsilon(2e-3));
  }

  TEST_CASE("frequencyShifter: the oscillator follows the live sample rate (#1110)") {
    const std::vector<float> in = Mixture(3 * N);
    {
      TestHelpers::ScopedSampleRate rate(48000);
      frequencyShifter fn;
      fn.shift(375.f); // 1/128 of a cycle per sample
      Run(fn, std::vector<float>(in.begin(), in.begin() + 32));
      CHECK(fn.phase() == doctest::Approx(0.25).epsilon(1e-9));
    }
    {
      TestHelpers::ScopedSampleRate rate(96000);
      frequencyShifter fn;
      fn.shift(375.f); // 1/256 of a cycle per sample
      Run(fn, std::vector<float>(in.begin(), in.begin() + 32));
      CHECK(fn.phase() == doctest::Approx(0.125).epsilon(1e-9));
    }

    // A rate change between blocks changes the step from the next block on.
    TestHelpers::ScopedSampleRate rate(48000);
    frequencyShifter fn;
    fn.shift(375.f);
    Run(fn, std::vector<float>(in.begin(), in.begin() + 64));
    CHECK(fn.phase() == doctest::Approx(0.5).epsilon(1e-9));
    {
      TestHelpers::ScopedSampleRate faster(96000);
      Run(fn, std::vector<float>(in.begin(), in.begin() + 64));
      CHECK(fn.phase() == doctest::Approx(0.75).epsilon(1e-9));
      // And a sine shifted at 96 kHz lands on the same frequency in Hz.
      frequencyShifter at96;
      at96.shift(200.f);
      const Pair out = Run(at96, Cosine(1000.0, 96000.0, 96000));
      const double shifted = 2.0 * PI * 1200.0 / 96000.0;
      CHECK(Amplitude(out.up, shifted, 24000) == doctest::Approx(1.0).epsilon(2e-3));
    }
  }

  TEST_CASE("frequencyShifter: the two-buffer call reads the shift per sample (#1110)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::vector<float> in = Mixture(2 * N);
    std::vector<float> shifts(2 * N);
    for (std::size_t i = 0; i < shifts.size(); ++i)
      shifts[i] = 100.f + 50.f * static_cast<float>(i % 7);
    shifts[20] = std::numeric_limits<float>::quiet_NaN();
    shifts[N + 3] = std::numeric_limits<float>::infinity();

    frequencyShifter fn;
    frequencyShifter reference;
    for (unsigned int block = 0; block < 2; ++block) {
      CAPTURE(block);
      buffer x(N);
      buffer s(N);
      for (unsigned int i = 0; i < N; ++i) {
        x.getPtr()[i] = in[block * N + i];
        s.getPtr()[i] = shifts[block * N + i];
      }
      buffer& result = fn(x, s);
      REQUIRE(&result == &fn.up());
      for (unsigned int i = 0; i < N; ++i) {
        CAPTURE(i);
        // A sample that is not finite keeps the last finite shift.
        reference.shift(shifts[block * N + i]);
        const frequencyShifter::outputs o = reference.process(in[block * N + i]);
        CHECK(fn.up().getPtr()[i] == o.up);
        CHECK(fn.down().getPtr()[i] == o.down);
      }
    }
    CHECK(fn.shift() == shifts[2 * N - 1]);
    CHECK(fn.phase() == reference.phase());

    // The block is as long as the shorter buffer.
    buffer x(N);
    buffer s(N / 4);
    x = 0.5f;
    s = 10.f;
    fn(x, s);
    CHECK(fn.up().getLength() == N / 4);
    CHECK(fn.down().getLength() == N / 4);
    CHECK(fn.shift() == 10.f);
  }

  TEST_CASE("frequencyShifter: resetPhase() sets the oscillator, clear() the filter (#1110)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::vector<float> in = Mixture(2 * N);

    // At phase 0.25 the carrier is (cos, sin) = (0, 1): up is -im, down +im.
    frequencyShifter quarter;
    quarter.resetPhase(0.25f);
    CHECK(quarter.phase() == 0.25f);
    hilbert reference;
    const hilbert::outputs h = reference.process(in[0]);
    const frequencyShifter::outputs o = quarter.process(in[0]);
    CHECK(o.up == doctest::Approx(-h.imaginary).epsilon(1e-6));
    CHECK(o.down == doctest::Approx(h.imaginary).epsilon(1e-6));
    CHECK(std::abs(o.up + h.imaginary) < 1e-6);

    // Wrapped into 0..1.
    frequencyShifter wrapped;
    wrapped.resetPhase(1.75f);
    CHECK(wrapped.phase() == doctest::Approx(0.75f));
    wrapped.resetPhase(-0.25f);
    CHECK(wrapped.phase() == doctest::Approx(0.75f));
    wrapped.resetPhase(std::numeric_limits<float>::quiet_NaN());
    CHECK(wrapped.phase() == 0.f);

    // resetPhase(): the oscillator starts again, the filter state stays, so
    // the output matches a shifter fed the same input at phase 0 from there.
    frequencyShifter used;
    used.shift(321.f);
    Run(used, in);
    used.resetPhase();
    CHECK(used.phase() == 0.f);
    hilbert filter;
    for (const float v : in)
      filter.process(v);
    const Pair after = Run(used, in);
    for (std::size_t n = 0; n < in.size(); ++n) {
      CAPTURE(n);
      const hilbert::outputs q = filter.process(in[n]);
      const double p = static_cast<double>(n) * 321.0 / 48000.0;
      const double c = std::cos(2.0 * PI * p);
      const double s = std::sin(2.0 * PI * p);
      CHECK(std::abs(after.up[n] - (q.real * c - q.imaginary * s)) < 1e-5);
    }

    // clear(): the filter starts again, the phase runs on.
    frequencyShifter cleared;
    cleared.shift(321.f);
    Run(cleared, in);
    const float phaseBefore = cleared.phase();
    cleared.clear();
    CHECK(cleared.phase() == phaseBefore);
    frequencyShifter other;
    other.shift(321.f);
    other.resetPhase(phaseBefore);
    const Pair a = Run(cleared, in);
    const Pair b = Run(other, in);
    for (std::size_t n = 0; n < in.size(); ++n) {
      CAPTURE(n);
      CHECK(std::abs(a.up[n] - b.up[n]) < 1e-5);
      CHECK(std::abs(a.down[n] - b.down[n]) < 1e-5);
    }
  }

  TEST_CASE("frequencyShifter: non-finite settings are ignored, non-finite input skipped "
            "(#1110)") {
    TestHelpers::ScopedSampleRate rate(48000);
    frequencyShifter fn;
    fn.shift(100.f);
    fn.shift(std::numeric_limits<float>::quiet_NaN());
    CHECK(fn.shift() == 100.f);
    fn.shift(std::numeric_limits<float>::infinity());
    fn.shift(-std::numeric_limits<float>::infinity());
    CHECK(fn.shift() == 100.f);

    // A non-finite input sample: the filter holds, the oscillator runs on.
    const std::vector<float> in = Mixture(N);
    std::vector<float> dirty = in;
    dirty[10] = std::numeric_limits<float>::quiet_NaN();
    dirty[90] = -std::numeric_limits<float>::infinity();
    const Pair out = Run(fn, dirty);
    hilbert reference;
    for (std::size_t n = 0; n < dirty.size(); ++n) {
      CAPTURE(n);
      const hilbert::outputs h = reference.process(dirty[n]);
      CHECK(std::isfinite(out.up[n]));
      CHECK(std::isfinite(out.down[n]));
      const double p = static_cast<double>(n) * 100.0 / 48000.0;
      const double c = std::cos(2.0 * PI * p);
      const double s = std::sin(2.0 * PI * p);
      CHECK(std::abs(out.up[n] - (h.real * c - h.imaginary * s)) < 1e-5);
      CHECK(std::abs(out.down[n] - (h.real * c + h.imaginary * s)) < 1e-5);
    }
    CHECK(fn.phase() == doctest::Approx(static_cast<double>(N) * 100.0 / 48000.0));
  }

  TEST_CASE("frequencyShifter: process() and blocks of any size agree (#1110)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::vector<float> in = Mixture(5 * N);
    frequencyShifter whole;
    whole.shift(-433.25f);
    const Pair blocks = Run(whole, in);
    for (const unsigned int size : {1u, 13u, 32u, 64u, 100u}) {
      CAPTURE(size);
      frequencyShifter fn;
      fn.shift(-433.25f);
      const Pair other = Run(fn, in, size);
      CHECK(other.up == blocks.up);
      CHECK(other.down == blocks.down);
    }
    frequencyShifter single;
    single.shift(-433.25f);
    for (std::size_t i = 0; i < in.size(); ++i) {
      CAPTURE(i);
      const frequencyShifter::outputs o = single.process(in[i]);
      CHECK(o.up == blocks.up[i]);
      CHECK(o.down == blocks.down[i]);
    }

    // A block longer than STANDARD_BUFFERSIZE is cut to it.
    frequencyShifter longer;
    buffer big(2 * N);
    big = 0.25f;
    longer(big);
    CHECK(longer.up().getLength() == N);
    CHECK(longer.down().getLength() == N);
    buffer shifts(2 * N);
    shifts = 5.f;
    longer(big, shifts);
    CHECK(longer.up().getLength() == N);
  }

  TEST_CASE("frequencyShifter: processing allocates nothing (#1110)") {
    frequencyShifter fn;
    buffer in(N);
    buffer shorter(N / 2);
    buffer shifts(N);
    in = 0.5f;
    shorter = -0.5f;
    shifts = 120.f;
    fn(in);
    {
      TestHelpers::ProbeScope probe;
      fn.shift(80.f);
      fn(in);
      fn(shorter);
      fn(in, shifts);
      fn(in);
      fn.process(0.25f);
      fn.clear();
      fn.resetPhase(0.5f);
      fn(in);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
