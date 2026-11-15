// Tests for YSE::DSP::pitchShifter (YseEngine/dsp/delay/pitchShifter.hpp),
// the real-time pitch shifter behind ~pitchshift (issue #1153):
//  - the setters and their limits, and the latency of every mode in closed
//    form;
//  - ratio 1 is the input latency() samples later: bit for bit in the
//    granular modes, to rounding in the phase vocoder;
//  - a sine shifted up and down by semitones comes out at the transposed
//    frequency, in every mode, with the original pitch gone;
//  - the latency measured (an impulse at ratio 1, the energy centroid of a
//    tone burst at other ratios) is latency(), in every mode;
//  - a grain takes the ratio at the sample it starts, a frame at the sample
//    it completes; cents in and out; a ratio changed every block glides
//    without a click;
//  - switching quality or enabled starts afresh; nothing depends on the
//    sample rate; non-finite input; processing allocates nothing.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>
#include "dsp/buffer.hpp"
#include "dsp/delay/pitchShifter.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::pitchShifter;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;
  constexpr pitchShifter::QUALITY QUALITIES[] = {pitchShifter::BASIC, pitchShifter::GOOD,
                                                 pitchShifter::BETTER, pitchShifter::BEST};

  // `input` through `fn` in blocks of N (the last one shorter), optionally
  // with a per-sample shift.
  std::vector<float> Run(pitchShifter& fn, const std::vector<float>& input,
                         const std::vector<float>* shift = nullptr) {
    std::vector<float> out;
    out.reserve(input.size());
    buffer in(N);
    buffer s(N);
    for (std::size_t start = 0; start < input.size(); start += N) {
      const auto length = static_cast<UInt>(std::min<std::size_t>(N, input.size() - start));
      in.resize(length);
      s.resize(length);
      std::copy(input.begin() + static_cast<std::ptrdiff_t>(start),
                input.begin() + static_cast<std::ptrdiff_t>(start + length), in.getPtr());
      if (shift != nullptr)
        std::copy(shift->begin() + static_cast<std::ptrdiff_t>(start),
                  shift->begin() + static_cast<std::ptrdiff_t>(start + length), s.getPtr());
      buffer& o = shift != nullptr ? fn(in, s) : fn(in);
      REQUIRE(o.getLength() == length);
      out.insert(out.end(), o.getPtr(), o.getPtr() + length);
    }
    return out;
  }

  std::vector<float> Noise(std::size_t length, unsigned int seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> d(-1.f, 1.f);
    std::vector<float> v(length);
    for (float& x : v)
      x = d(rng);
    return v;
  }

  std::vector<float> Sine(std::size_t length, double hz, double rate, double amplitude = 0.5) {
    std::vector<float> v(length);
    for (std::size_t n = 0; n < length; ++n)
      v[n] =
          static_cast<float>(amplitude * std::sin(2.0 * PI * hz * static_cast<double>(n) / rate));
    return v;
  }

  // The amplitude of `v[from..from+length)` at `hz`, Hann-windowed.
  double Amplitude(const std::vector<float>& v, std::size_t from, std::size_t length, double hz,
                   double rate) {
    double re = 0.0;
    double im = 0.0;
    double sum = 0.0;
    const double w = 2.0 * PI * hz / rate;
    for (std::size_t n = 0; n < length; ++n) {
      const double h =
          0.5 - 0.5 * std::cos(2.0 * PI * static_cast<double>(n) / static_cast<double>(length));
      const double x = h * v[from + n];
      re += x * std::cos(w * static_cast<double>(n));
      im -= x * std::sin(w * static_cast<double>(n));
      sum += h;
    }
    return 2.0 * std::hypot(re, im) / sum;
  }

  // The loudest frequency of `v[from..from+length)` between `low` and
  // `high` Hz, to 0.01 Hz.
  double Loudest(const std::vector<float>& v, std::size_t from, std::size_t length, double low,
                 double high, double rate) {
    double best = low;
    double level = -1.0;
    const auto steps = static_cast<int>(high - low);
    for (int i = 0; i <= steps; ++i) {
      const double hz = low + i;
      const double a = Amplitude(v, from, length, hz, rate);
      if (a > level) {
        level = a;
        best = hz;
      }
    }
    const double centre = best;
    for (int i = 0; i <= 200; ++i) {
      const double hz = centre - 1.0 + 0.01 * i;
      const double a = Amplitude(v, from, length, hz, rate);
      if (a > level) {
        level = a;
        best = hz;
      }
    }
    return best;
  }

  // The energy centroid of `v`, in samples.
  double Centroid(const std::vector<float>& v) {
    double moment = 0.0;
    double energy = 0.0;
    for (std::size_t n = 0; n < v.size(); ++n) {
      const double e = static_cast<double>(v[n]) * v[n];
      moment += e * static_cast<double>(n);
      energy += e;
    }
    return moment / energy;
  }

  double Semitones(double s) {
    return std::pow(2.0, s / 12.0);
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("pitchShifter: defaults, setters, their limits and the latency of every mode in "
            "closed form (#1153)") {
    pitchShifter fn;
    CHECK(fn.ratio() == 1.f);
    CHECK(fn.cents() == 0.f);
    CHECK(fn.quality() == pitchShifter::BASIC);
    CHECK_FALSE(fn.constantLatency());
    CHECK(fn.enabled());
    CHECK_FALSE(fn.usesCents());
    CHECK(fn.latency() == 2);

    // The ratio is held to two octaves either way; non-finite is ignored.
    fn.ratio(1.5f);
    CHECK(fn.ratio() == 1.5f);
    fn.ratio(std::numeric_limits<float>::quiet_NaN());
    CHECK(fn.ratio() == 1.5f);
    fn.ratio(std::numeric_limits<float>::infinity());
    CHECK(fn.ratio() == 1.5f);
    fn.ratio(10.f);
    CHECK(fn.ratio() == pitchShifter::MAXIMUM_RATIO);
    fn.ratio(0.f);
    CHECK(fn.ratio() == pitchShifter::MINIMUM_RATIO);
    fn.ratio(-3.f);
    CHECK(fn.ratio() == pitchShifter::MINIMUM_RATIO);

    // Cents.
    fn.cents(1200.f);
    CHECK(fn.ratio() == doctest::Approx(2.0).epsilon(1e-6));
    CHECK(fn.cents() == doctest::Approx(1200.0).epsilon(1e-4));
    fn.cents(-700.f);
    CHECK(fn.ratio() == doctest::Approx(Semitones(-7.0)).epsilon(1e-6));
    CHECK(fn.cents() == doctest::Approx(-700.0).epsilon(1e-4));
    fn.cents(std::numeric_limits<float>::quiet_NaN());
    CHECK(fn.cents() == doctest::Approx(-700.0).epsilon(1e-4));
    fn.cents(5000.f);
    CHECK(fn.ratio() == pitchShifter::MAXIMUM_RATIO);
    CHECK(pitchShifter::centsToRatio(0.f) == 1.f);
    CHECK(pitchShifter::centsToRatio(-2400.f) == doctest::Approx(0.25).epsilon(1e-6));
    CHECK(pitchShifter::centsToRatio(-9000.f) == pitchShifter::MINIMUM_RATIO);
    CHECK(pitchShifter::centsToRatio(std::numeric_limits<float>::quiet_NaN()) == 1.f);

    // Granular: ceil(|1 - r| G / 2) + 2 + G / 8 (2 at ratio 1), or
    // ceil(3 G / 2) + 2 + G / 8 constant.
    fn.ratio(2.f);
    CHECK(fn.latency() == 642);
    fn.ratio(0.5f);
    CHECK(fn.latency() == 386);
    fn.ratio(static_cast<float>(Semitones(1.0)));
    CHECK(fn.latency() == 161); // 0.0595 * 512 = 30.4
    fn.ratio(static_cast<float>(Semitones(-1.0)));
    CHECK(fn.latency() == 159); // 0.0561 * 512 = 28.7
    fn.ratio(4.f);
    CHECK(fn.latency() == 1666);
    fn.ratio(0.25f);
    CHECK(fn.latency() == 514);
    fn.constantLatency(true);
    CHECK(fn.constantLatency());
    CHECK(fn.latency() == 1666);
    fn.ratio(1.f);
    CHECK(fn.latency() == 1666);
    fn.quality(pitchShifter::GOOD);
    CHECK(fn.quality() == pitchShifter::GOOD);
    CHECK(fn.latency() == 3330);
    fn.constantLatency(false);
    CHECK(fn.latency() == 2);
    fn.ratio(2.f);
    CHECK(fn.latency() == 1282);

    // The phase vocoder: the frame size, whatever the ratio.
    fn.quality(pitchShifter::BETTER);
    CHECK(fn.latency() == 2048);
    fn.ratio(0.5f).constantLatency(true);
    CHECK(fn.latency() == 2048);
    fn.quality(pitchShifter::BEST);
    CHECK(fn.latency() == 4096);

    // Disabled: no delay. An unknown quality is ignored.
    fn.enabled(false);
    CHECK_FALSE(fn.enabled());
    CHECK(fn.latency() == 0);
    fn.enabled(true);
    CHECK(fn.latency() == 4096);
    fn.quality(static_cast<pitchShifter::QUALITY>(7));
    CHECK(fn.quality() == pitchShifter::BEST);
    fn.usesCents(true);
    CHECK(fn.usesCents());
  }

  TEST_CASE("pitchShifter: ratio 1 is the input latency() samples later, bit for bit in the "
            "granular modes (#1153)") {
    const std::vector<float> input = Noise(40 * N + 77, 1);
    for (const pitchShifter::QUALITY q : QUALITIES) {
      for (const bool constant : {false, true}) {
        CAPTURE(q);
        CAPTURE(constant);
        pitchShifter fn(q);
        fn.constantLatency(constant);
        const UInt latency = fn.latency();
        const std::vector<float> out = Run(fn, input);
        for (std::size_t n = 0; n < out.size(); ++n) {
          const float expected = n < latency ? 0.f : input[n - latency];
          if (q == pitchShifter::BASIC || q == pitchShifter::GOOD) {
            if (out[n] != expected) {
              CAPTURE(n);
              FAIL_CHECK("not bit-exact");
              break;
            }
          } else if (std::abs(out[n] - expected) > 1e-5f) {
            CAPTURE(n);
            FAIL_CHECK("not the input, latency samples later");
            break;
          }
        }
        // An impulse comes out at exactly latency().
        pitchShifter click(q);
        click.constantLatency(constant);
        std::vector<float> impulse(8192 + 1000, 0.f);
        impulse[1000] = 1.f;
        const std::vector<float> echo = Run(click, impulse);
        std::size_t loudest = 0;
        for (std::size_t n = 0; n < echo.size(); ++n)
          if (std::abs(echo[n]) > std::abs(echo[loudest])) loudest = n;
        CHECK(loudest == 1000 + latency);
        CHECK(echo[loudest] == doctest::Approx(1.0).epsilon(1e-5));
      }
    }
  }

  TEST_CASE("pitchShifter: a sine shifted up and down by semitones comes out at the transposed "
            "frequency in every mode, the original gone (#1153)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const double semitones[] = {7.0, -5.0, 12.0, -12.0, 1.0, 19.0};
    for (const pitchShifter::QUALITY q : QUALITIES) {
      for (const double s : semitones) {
        CAPTURE(q);
        CAPTURE(s);
        const double hz = 440.0;
        const double target = hz * Semitones(s);
        pitchShifter fn(q);
        fn.cents(static_cast<float>(100.0 * s));
        const std::vector<float> out = Run(fn, Sine(48000, hz, 48000.0));
        const std::size_t from = 16384; // past the latency and the first frames
        const std::size_t length = 16384;
        const double found = Loudest(out, from, length, target * 0.85, target * 1.15, 48000.0);
        CAPTURE(found);
        CHECK(std::abs(found - target) < 0.002 * target);
        const double level = Amplitude(out, from, length, target, 48000.0);
        CAPTURE(level);
        CHECK(level > 0.25);
        CHECK(level < 0.55);
        CHECK(Amplitude(out, from, length, hz, 48000.0) < 0.1 * level);
      }
    }
  }

  TEST_CASE("pitchShifter: GOOD's longer grains align a low pitch that BASIC's cannot; counted "
            "by zero crossings (#1153)") {
    TestHelpers::ScopedSampleRate rate(48000);
    // The rate of upward zero crossings of v[from..), interpolated.
    const auto crossings = [](const std::vector<float>& v, std::size_t from) {
      double first = -1.0;
      double last = -1.0;
      int count = 0;
      for (std::size_t n = from + 1; n < v.size(); ++n) {
        if (v[n - 1] < 0.f && v[n] >= 0.f) {
          const double t = static_cast<double>(n - 1) + v[n - 1] / (v[n - 1] - v[n]);
          if (first < 0.0) first = t;
          last = t;
          count++;
        }
      }
      return 48000.0 * (count - 1) / (last - first);
    };
    // 110 Hz has a period of 436 samples: within GOOD's search (512), past
    // BASIC's (256).
    for (const double s : {7.0, -5.0, 12.0, -12.0, 1.0, -1.0}) {
      CAPTURE(s);
      const double target = 110.0 * Semitones(s);
      pitchShifter good(pitchShifter::GOOD);
      good.cents(static_cast<float>(100.0 * s));
      const double found = crossings(Run(good, Sine(96000, 110.0, 48000.0)), 24000);
      CAPTURE(found);
      CHECK(std::abs(found - target) < 0.007 * target);
    }
    pitchShifter basic(pitchShifter::BASIC);
    basic.cents(100.f);
    const double off = crossings(Run(basic, Sine(96000, 110.0, 48000.0)), 24000);
    CHECK(std::abs(off - 110.0 * Semitones(1.0)) > 0.02 * 110.0);
  }

  TEST_CASE("pitchShifter: the energy centroid of a tone burst moves by latency() in every "
            "mode (#1153)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::size_t start = 4096;
    const std::size_t span = 24000;
    std::vector<float> burst(start + span + 16384, 0.f);
    for (std::size_t n = 0; n < span; ++n) {
      const double env = std::sin(PI * static_cast<double>(n) / static_cast<double>(span));
      burst[start + n] = static_cast<float>(
          0.5 * env * env * std::sin(2.0 * PI * 330.0 * static_cast<double>(n) / 48000.0));
    }
    const double in = Centroid(burst);
    for (const pitchShifter::QUALITY q : QUALITIES) {
      for (const float r : {1.5f, 0.7f, 2.f}) {
        CAPTURE(q);
        CAPTURE(r);
        pitchShifter fn(q);
        fn.ratio(r);
        const std::vector<float> out = Run(fn, burst);
        const double measured = Centroid(out) - in;
        CAPTURE(measured);
        CHECK(std::abs(measured - fn.latency()) < 32.0);
      }
    }
  }

  TEST_CASE("pitchShifter: a grain takes the ratio of the sample it starts at, a frame that of "
            "the sample it completes at; cents in the shift buffer (#1153)") {
    const std::vector<float> input = Noise(24 * N, 2);
    for (const pitchShifter::QUALITY q : QUALITIES) {
      CAPTURE(q);
      // Where a ratio is read: every half grain, or at the end of every hop.
      std::size_t every = 512;
      std::size_t at = 0;
      if (q == pitchShifter::GOOD) every = 1024;
      if (q == pitchShifter::BETTER) {
        every = 512;
        at = 511;
      }
      if (q == pitchShifter::BEST) {
        every = 512;
        at = 511;
      }
      pitchShifter scalar(q);
      scalar.ratio(1.25f);
      const std::vector<float> expected = Run(scalar, input);

      // Anything (a finite number) in between is never read.
      std::vector<float> shift(input.size());
      for (std::size_t n = 0; n < shift.size(); ++n)
        shift[n] = n % every == at ? 1.25f : 3.f + static_cast<float>(n % 7) * 0.1f;
      shift.back() = 1.25f;
      pitchShifter perSample(q);
      CHECK(Run(perSample, input, &shift) == expected);
      CHECK(perSample.ratio() == 1.25f);

      // The same in cents; NaN keeps the ratio.
      const float cents = static_cast<float>(1200.0 * std::log2(1.25));
      for (std::size_t n = 0; n < shift.size(); ++n)
        shift[n] = n % every == at ? cents : std::numeric_limits<float>::quiet_NaN();
      pitchShifter inCents(q);
      inCents.usesCents(true).ratio(pitchShifter::centsToRatio(cents));
      pitchShifter reference(q);
      reference.ratio(pitchShifter::centsToRatio(cents));
      CHECK(Run(inCents, input, &shift) == Run(reference, input));
      CHECK(inCents.ratio() == pitchShifter::centsToRatio(cents));
    }

    // A shorter shift buffer shortens the block.
    pitchShifter fn;
    buffer in(N);
    buffer s(N / 2);
    s = 1.f;
    CHECK(fn(in, s).getLength() == N / 2);
    CHECK(fn.output().getLength() == N / 2);
  }

  TEST_CASE("pitchShifter: a ratio changed every block glides without a click (#1153)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::vector<float> input = Sine(64 * N, 440.0, 48000.0);
    for (const pitchShifter::QUALITY q : {pitchShifter::BASIC, pitchShifter::GOOD}) {
      for (const bool constant : {false, true}) {
        CAPTURE(q);
        CAPTURE(constant);
        pitchShifter fn(q);
        fn.constantLatency(constant);
        buffer in(N);
        float worst = 0.f;
        float last = 0.f;
        for (std::size_t block = 0; block < 64; ++block) {
          // A slow vibrato of a semitone either way, as a pitch corrector
          // would drive it: one ratio per block.
          fn.ratio(static_cast<float>(Semitones(std::sin(static_cast<double>(block) * 0.3))));
          std::copy(input.begin() + static_cast<std::ptrdiff_t>(block * N),
                    input.begin() + static_cast<std::ptrdiff_t>((block + 1) * N), in.getPtr());
          buffer& out = fn(in);
          for (unsigned int i = 0; i < N; ++i) {
            worst = std::max(worst, std::abs(out.getPtr()[i] - last));
            last = out.getPtr()[i];
          }
        }
        // A sine of 0.5 at up to 466 Hz moves by at most 0.031 a sample.
        CAPTURE(worst);
        CHECK(worst < 0.045f);
      }
    }
  }

  TEST_CASE("pitchShifter: switching quality or enabled starts afresh; disabled passes the "
            "input straight through (#1153)") {
    const std::vector<float> input = Noise(32 * N, 3);
    pitchShifter fn;
    std::vector<float> first(input.begin(), input.begin() + 8 * N);
    Run(fn, first);

    // Disabled: the input, no delay.
    fn.enabled(false);
    std::vector<float> second(input.begin() + 8 * N, input.begin() + 12 * N);
    CHECK(Run(fn, second) == second);

    // Enabled again: silence for latency() samples, then the input.
    fn.enabled(true);
    std::vector<float> third(input.begin() + 12 * N, input.begin() + 20 * N);
    const std::vector<float> out = Run(fn, third);
    const UInt latency = fn.latency();
    for (std::size_t n = 0; n < out.size(); ++n)
      REQUIRE(out[n] == (n < latency ? 0.f : third[n - latency]));

    // A new quality: the same, at its latency.
    fn.quality(pitchShifter::BETTER);
    std::vector<float> fourth(input.begin() + 20 * N, input.end());
    const std::vector<float> vocoded = Run(fn, fourth);
    for (std::size_t n = 0; n < vocoded.size(); ++n) {
      CAPTURE(n);
      REQUIRE(std::abs(vocoded[n] - (n < 2048 ? 0.f : fourth[n - 2048])) < 1e-5f);
    }
    // Setting the same quality again changes nothing.
    pitchShifter same(pitchShifter::GOOD);
    pitchShifter again(pitchShifter::GOOD);
    same.ratio(1.3f);
    again.ratio(1.3f);
    std::vector<float> a = Run(same, first);
    std::vector<float> b = Run(again, first);
    same.quality(pitchShifter::GOOD);
    CHECK(Run(same, second) == Run(again, second));
    // reset() is a fresh start.
    same.reset();
    pitchShifter fresh(pitchShifter::GOOD);
    fresh.ratio(1.3f);
    CHECK(Run(same, third) == Run(fresh, third));
  }

  TEST_CASE("pitchShifter: nothing depends on the sample rate; non-finite input (#1153)") {
    const std::vector<float> input = Sine(16 * N, 500.0, 48000.0);
    for (const pitchShifter::QUALITY q : QUALITIES) {
      CAPTURE(q);
      std::vector<float> at48;
      {
        TestHelpers::ScopedSampleRate rate(48000);
        pitchShifter fn(q);
        fn.ratio(1.5f);
        at48 = Run(fn, input);
      }
      pitchShifter fn(q);
      fn.ratio(1.5f);
      std::vector<float> at96;
      {
        TestHelpers::ScopedSampleRate rate(96000); // after construction
        at96 = Run(fn, input);
      }
      CHECK(at48 == at96);

      // A NaN or infinity in the input: the output stays finite in the
      // granular modes, and recovers in the phase vocoder.
      std::vector<float> bad = input;
      bad[100] = std::numeric_limits<float>::quiet_NaN();
      bad[2000] = std::numeric_limits<float>::infinity();
      pitchShifter sick(q);
      sick.ratio(0.75f);
      const std::vector<float> out = Run(sick, bad);
      const std::size_t from = (q == pitchShifter::BASIC || q == pitchShifter::GOOD) ? 0 : 12000;
      bool finite = true;
      for (std::size_t n = from; n < out.size(); ++n)
        finite = finite && std::isfinite(out[n]);
      CHECK(finite);
    }
  }

  TEST_CASE("pitchShifter: processing and the setters allocate nothing (#1153)") {
    pitchShifter fn;
    buffer in(N);
    buffer s(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = std::sin(static_cast<float>(i) * 0.1f);
    s = 1.5f;
    fn(in);
    {
      TestHelpers::ProbeScope probe;
      for (int k = 0; k < 40; ++k) {
        fn.quality(QUALITIES[k % 4])
            .ratio(0.5f + 0.05f * static_cast<float>(k))
            .constantLatency(k % 3 == 0)
            .usesCents(k % 2 == 0)
            .enabled(k % 5 != 4);
        fn.cents(static_cast<float>(k * 30));
        (void)fn.latency();
        fn(in);
        fn(in, s);
      }
      fn.reset();
      fn(in);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }
}
