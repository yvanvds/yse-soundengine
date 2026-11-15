// Tests for YSE::DSP::spectralFreqShift
// (YseEngine/dsp/spectral/spectralFreqShift.hpp), the frequency-domain
// frequency shifter behind ~fbinshift (issue #1165):
//  - a single-bin spectrum lands round(shift) bins away, up and down, the
//    frame's angle advancing 2 pi shift hop / size a frame, in closed form,
//    with the shift in Hz at the live sample rate;
//  - every bin moves by the same amount and turns by the same angle;
//  - out of range bins fold back (conjugated above Nyquist and below 0) or,
//    with clip, are dropped; a full spectrum's upper half mirrors the lower;
//    capacity, non-finite input, in place, a run that is not a whole frame;
//    the angle across reset, gaps and a change of hop;
//  - no shift is the identity, bit for bit;
//  - on DSP::stft, sines shifted by a whole or fractional number of bins,
//    up, down and through 0 Hz, come out at f + shift at about the level
//    that went in, so a harmonic pair stops being harmonic;
//  - processing allocates nothing.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <random>
#include <vector>
#include "dsp/buffer.hpp"
#include "dsp/spectral/spectralFreqShift.hpp"
#include "dsp/spectral/stft.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

using YSE::DSP::buffer;
using YSE::DSP::spectralFreqShift;
using YSE::DSP::stft;
using YSE::DSP::stftFrame;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;
  constexpr UInt RATE = 48000;

  struct SampleRateGuard {
    UInt saved;
    explicit SampleRateGuard(UInt rate = RATE) : saved(YSE::SAMPLERATE) {
      YSE::SAMPLERATE = rate;
    }
    ~SampleRateGuard() {
      YSE::SAMPLERATE = saved;
    }
    SampleRateGuard(const SampleRateGuard&) = delete;
    SampleRateGuard& operator=(const SampleRateGuard&) = delete;
    SampleRateGuard(SampleRateGuard&&) = delete;
    SampleRateGuard& operator=(SampleRateGuard&&) = delete;
  };

  double Wrap(double a) {
    return a - 2.0 * PI * std::floor(a / (2.0 * PI) + 0.5);
  }

  // `bins` bins of an FFT of `size` in Hz at `rate`.
  float Hz(double bins, UInt size, UInt rate = RATE) {
    return static_cast<float>(bins * rate / size);
  }

  // A frame header: the whole frame of `bins` bins of an FFT of `size`.
  stftFrame Header(UInt size, UInt hop, UInt bins, U64 index, bool full = false) {
    stftFrame f;
    f.firstBin = 0;
    f.count = bins;
    f.bins = bins;
    f.size = size;
    f.hop = hop;
    f.fullSpectrum = full;
    f.index = index;
    return f;
  }

  struct spectrum {
    std::vector<float> re;
    std::vector<float> im;
    explicit spectrum(UInt bins) : re(bins, 0.f), im(bins, 0.f) {}
  };

  spectrum Shift(spectralFreqShift& fn, const stftFrame& f, const spectrum& in) {
    spectrum out(f.count);
    fn(f, in.re.data(), in.im.data(), out.re.data(), out.im.data());
    return out;
  }

  // Every bin but `except` is 0.
  bool OnlyAt(const spectrum& s, UInt except) {
    for (UInt k = 0; k < s.re.size(); ++k)
      if (k != except && (s.re[k] != 0.f || s.im[k] != 0.f)) return false;
    return true;
  }

  spectrum Random(UInt bins, unsigned int seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> d(-1.f, 1.f);
    spectrum s(bins);
    for (UInt k = 0; k < bins; ++k) {
      s.re[k] = d(rng);
      s.im[k] = d(rng);
    }
    return s;
  }

  // `x` under a Hann window, and the window's sum.
  struct windowed {
    std::vector<double> x;
    double weight = 0.0;
    explicit windowed(const std::vector<float>& in) : x(in.size()) {
      const std::size_t L = in.size();
      for (std::size_t n = 0; n < L; ++n) {
        const double hann = 0.5 - 0.5 * std::cos(2.0 * PI * static_cast<double>(n) / L);
        x[n] = hann * static_cast<double>(in[n]);
        weight += hann;
      }
    }
  };

  // The amplitude of a sine at `bins` (bins of an FFT of `size`) in `w`.
  double Amplitude(const windowed& w, double bins, UInt size) {
    const double step = 2.0 * PI * bins / size;
    const std::complex<double> turn(std::cos(step), -std::sin(step));
    std::complex<double> phasor(1.0, 0.0);
    std::complex<double> sum(0.0, 0.0);
    for (const double v : w.x) {
      sum += v * phasor;
      phasor *= turn;
    }
    return 2.0 * std::abs(sum) / w.weight;
  }

  double Amplitude(const std::vector<float>& x, double bins, UInt size) {
    return Amplitude(windowed(x), bins, size);
  }

  // The frequency (in bins of `size`) where `x` is loudest between `low`
  // and `high` bins, to a hundredth of a bin.
  double Loudest(const std::vector<float>& x, UInt size, double low, double high) {
    const windowed w(x);
    double best = 0.0;
    double at = low;
    const auto steps = static_cast<UInt>(2.0 * (high - low));
    for (UInt i = 0; i <= steps; ++i) {
      const double b = low + 0.5 * i;
      const double a = Amplitude(w, b, size);
      if (a > best) {
        best = a;
        at = b;
      }
    }
    const double from = at - 0.5;
    for (UInt i = 0; i <= 100; ++i) {
      const double b = from + 0.01 * i;
      const double a = Amplitude(w, b, size);
      if (a > best) {
        best = a;
        at = b;
      }
    }
    return at;
  }

  // Sines of `bins` (in bins of `size`), amplitude 0.5 each, through
  // DSP::stft with `fn` on every frame (or none); the last `keep` samples.
  std::vector<float> ShiftedSines(spectralFreqShift* fn, UInt size, UInt overlap,
                                  const std::vector<double>& bins, std::size_t length,
                                  std::size_t keep) {
    stft fx(size, overlap);
    auto shift = [&](stftFrame& frame) {
      if (fn != nullptr) (*fn)(frame);
    };
    buffer b(N);
    std::vector<float> out;
    for (std::size_t start = 0; start < length; start += N) {
      for (unsigned int i = 0; i < N; ++i) {
        double v = 0.0;
        for (const double f : bins)
          v += 0.5 * std::sin(2.0 * PI * f * static_cast<double>(start + i) / size);
        b.getPtr()[i] = static_cast<float>(v);
      }
      buffer& o = fx(b, shift);
      out.insert(out.end(), o.getPtr(), o.getPtr() + N);
    }
    return std::vector<float>(out.end() - static_cast<std::ptrdiff_t>(keep), out.end());
  }
} // namespace

TEST_SUITE("dsp") {
  TEST_CASE("spectralFreqShift: defaults, setters and their limits (#1165)") {
    spectralFreqShift fn;
    CHECK(fn.capacity() == spectralFreqShift::DEFAULT_CAPACITY);
    CHECK(fn.frequencyShift() == 0.f);
    CHECK_FALSE(fn.clip());
    CHECK(fn.angle() == 0.0);
    CHECK(fn.frequencyShift(-120.f).frequencyShift() == -120.f);
    CHECK(fn.frequencyShift(std::numeric_limits<float>::infinity()).frequencyShift() == 0.f);
    CHECK(fn.frequencyShift(std::numeric_limits<float>::quiet_NaN()).frequencyShift() == 0.f);
    CHECK(fn.clip(true).clip());
    CHECK_FALSE(fn.clip(false).clip());
    CHECK(spectralFreqShift(10.f, 0).capacity() == 1);
    CHECK(spectralFreqShift(10.f, 1u << 20).capacity() == spectralFreqShift::MAXIMUM_CAPACITY);
    CHECK(spectralFreqShift(250.f, 64).frequencyShift() == 250.f);
    CHECK(spectralFreqShift(std::numeric_limits<float>::quiet_NaN()).frequencyShift() == 0.f);
  }

  TEST_CASE("spectralFreqShift: a single bin lands round(shift) bins away, up and down, its "
            "angle advancing 2 pi shift hop / size a frame, in closed form (#1165)") {
    SampleRateGuard guard;
    struct setting {
      UInt size;
      UInt hop;
      UInt bin;
      double shift; // in bins
    };
    // Whole and fractional shifts, up and down, at three sizes; at 2048
    // across the boundaries of the ~pfft passes of 128 bins.
    const setting settings[] = {{256, 64, 40, 7.0},      {256, 64, 40, -13.0},
                                {256, 64, 100, 2.3},     {256, 32, 20, -4.6},
                                {1024, 256, 300, 37.5},  {1024, 128, 300, -121.25},
                                {2048, 512, 100, 200.0}, {2048, 512, 900, -450.0},
                                {2048, 512, 128, -4.0},  {2048, 256, 124, 5.4}};
    for (const setting& s : settings) {
      CAPTURE(s.size);
      CAPTURE(s.hop);
      CAPTURE(s.bin);
      CAPTURE(s.shift);
      const UInt bins = s.size / 2;
      spectralFreqShift fn(Hz(s.shift, s.size));
      spectrum in(bins);
      in.re[s.bin] = 0.6f;
      in.im[s.bin] = 0.8f;
      const double shift = static_cast<double>(fn.frequencyShift()) * s.size / RATE;
      const auto lands = static_cast<UInt>(static_cast<long long>(s.bin) + std::llround(shift));
      REQUIRE(lands < bins);
      const double step = 2.0 * PI * shift * s.hop / s.size;
      for (U64 m = 0; m < 6; ++m) {
        CAPTURE(m);
        const spectrum out = Shift(fn, Header(s.size, s.hop, bins, m), in);
        const double angle = Wrap(step * static_cast<double>(m));
        CHECK(std::abs(Wrap(fn.angle() - angle)) < 1e-9);
        CHECK(OnlyAt(out, lands));
        // (0.6 + 0.8i) turned by the angle.
        CHECK(out.re[lands] ==
              doctest::Approx(0.6 * std::cos(angle) - 0.8 * std::sin(angle)).epsilon(1e-5));
        CHECK(out.im[lands] ==
              doctest::Approx(0.6 * std::sin(angle) + 0.8 * std::cos(angle)).epsilon(1e-5));
      }
    }
  }

  TEST_CASE("spectralFreqShift: the shift is in Hz at the live sample rate; half a bin rounds "
            "away from 0 (#1165)") {
    const UInt size = 512;
    const UInt bins = size / 2;
    spectrum in(bins);
    in.re[40] = 1.f;
    for (const UInt rate : {48000u, 96000u}) {
      CAPTURE(rate);
      SampleRateGuard guard(rate);
      // 1500 Hz: 16 bins of 93.75 Hz at 48 kHz, 8 at 96 kHz. The same
      // object follows the rate from one frame to the next.
      spectralFreqShift fn(1500.f);
      const UInt expect = rate == 48000u ? 56 : 48;
      const spectrum out = Shift(fn, Header(size, 128, bins, 0), in);
      CHECK(OnlyAt(out, expect));
      CHECK(out.re[expect] == 1.f);
      // The angle: 2 pi 1500 hop / rate a frame, whatever the size.
      Shift(fn, Header(size, 128, bins, 1), in);
      CHECK(fn.angle() == doctest::Approx(Wrap(2.0 * PI * 1500.0 * 128 / rate)));
    }
    SampleRateGuard guard;
    spectralFreqShift up(Hz(2.5, size));
    CHECK(OnlyAt(Shift(up, Header(size, 128, bins, 0), in), 43));
    spectralFreqShift down(Hz(-2.5, size));
    CHECK(OnlyAt(Shift(down, Header(size, 128, bins, 0), in), 37));
    spectralFreqShift below(Hz(-2.4, size));
    CHECK(OnlyAt(Shift(below, Header(size, 128, bins, 0), in), 38));
  }

  TEST_CASE("spectralFreqShift: every bin moves by the same amount and turns by the same "
            "angle (#1165)") {
    SampleRateGuard guard;
    const UInt size = 1024;
    const UInt bins = size / 2;
    const spectrum in = Random(bins, 5);
    for (const double shift : {17.0, -23.0, 6.3}) {
      CAPTURE(shift);
      spectralFreqShift fn(Hz(shift, size));
      fn.clip(true);
      const auto d = static_cast<long long>(std::llround(shift));
      const double step = 2.0 * PI * shift * 256 / size;
      for (U64 m = 0; m < 3; ++m) {
        const spectrum out = Shift(fn, Header(size, 256, bins, m), in);
        const double angle = step * static_cast<double>(m);
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        for (UInt j = 0; j < bins; ++j) {
          const long long k = static_cast<long long>(j) - d;
          if (k < 0 || k >= static_cast<long long>(bins)) {
            CHECK(out.re[j] == 0.f);
            CHECK(out.im[j] == 0.f);
            continue;
          }
          const double re = in.re[static_cast<std::size_t>(k)];
          const double im = in.im[static_cast<std::size_t>(k)];
          CHECK(out.re[j] == doctest::Approx(re * c - im * s).epsilon(1e-5));
          CHECK(out.im[j] == doctest::Approx(re * s + im * c).epsilon(1e-5));
          if (m == 0) {
            CHECK(out.re[j] == static_cast<float>(re));
            CHECK(out.im[j] == static_cast<float>(im));
          }
        }
      }
    }
  }

  TEST_CASE("spectralFreqShift: below 0 Hz and above Nyquist folds back as the conjugate, or "
            "with clip is dropped (#1165)") {
    SampleRateGuard guard;
    const UInt size = 512;
    const UInt bins = size / 2;
    spectrum in(bins);
    in.re[40] = 0.6f;
    in.im[40] = 0.8f;
    // 40 - 50 = -10, which aliases to +10, the conjugate.
    spectralFreqShift down(Hz(-50.0, size));
    const spectrum out = Shift(down, Header(size, 128, bins, 0), in);
    CHECK(OnlyAt(out, 10));
    CHECK(out.re[10] == doctest::Approx(0.6f));
    CHECK(out.im[10] == doctest::Approx(-0.8f));
    down.clip(true);
    CHECK(OnlyAt(Shift(down, Header(size, 128, bins, 1), in), 1000));
    // Exactly 0 Hz is kept.
    spectralFreqShift zero(Hz(-40.0, size));
    CHECK(OnlyAt(Shift(zero, Header(size, 128, bins, 0), in), 0));
    // A whole size away and more folds as the same alias: -40 - 512.
    spectralFreqShift far(Hz(-552.0, size));
    CHECK(OnlyAt(Shift(far, Header(size, 128, bins, 0), in), 0));

    // 256: Nyquist is bin 128. Bin 100 + 50 = 150 aliases to 256 - 150.
    spectrum high(129);
    high.re[100] = 0.6f;
    high.im[100] = 0.8f;
    spectralFreqShift fold(Hz(50.0, 256));
    const spectrum folded = Shift(fold, Header(256, 64, 129, 0), high);
    CHECK(OnlyAt(folded, 106));
    CHECK(folded.re[106] == doctest::Approx(0.6f));
    CHECK(folded.im[106] == doctest::Approx(-0.8f));
    spectralFreqShift clip(Hz(50.0, 256));
    clip.clip(true);
    CHECK(OnlyAt(Shift(clip, Header(256, 64, 129, 0), high), 1000));
    // Exactly on Nyquist is kept in a frame that holds it (DSP::stft's,
    // size / 2 + 1 bins) and dropped from one that does not (a ~pfft's).
    spectralFreqShift up(Hz(28.0, 256));
    CHECK(OnlyAt(Shift(up, Header(256, 64, 129, 0), high), 128));
    spectralFreqShift upHalf(Hz(28.0, 256));
    CHECK(OnlyAt(Shift(upHalf, Header(256, 64, 128, 0), high), 1000));
  }

  TEST_CASE("spectralFreqShift: no shift is the identity, bit for bit, frame after frame "
            "(#1165)") {
    SampleRateGuard guard;
    spectralFreqShift fn;
    for (U64 m = 0; m < 8; ++m) {
      const spectrum in = Random(513, static_cast<unsigned int>(m) + 1);
      const spectrum out = Shift(fn, Header(1024, 256, 513, m), in);
      CHECK(out.re == in.re);
      CHECK(out.im == in.im);
      CHECK(fn.angle() == 0.0);
    }
  }

  TEST_CASE("spectralFreqShift: a full spectrum's upper half mirrors the lower; capacity, "
            "non-finite input, in place, and a run that is not a whole frame (#1165)") {
    SampleRateGuard guard;
    // Full spectrum of 64: bin 8 + 4 = 12, its mirror 52 the conjugate.
    spectrum full(64);
    full.re[8] = 0.6f;
    full.im[8] = 0.8f;
    full.re[56] = 123.f; // the upper half is not read
    spectralFreqShift fn(Hz(4.0, 64));
    const spectrum out = Shift(fn, Header(64, 16, 64, 0, true), full);
    for (UInt k = 0; k < 64; ++k) {
      CAPTURE(k);
      if (k == 12 || k == 52) continue;
      CHECK(out.re[k] == 0.f);
      CHECK(out.im[k] == 0.f);
    }
    CHECK(out.re[12] == doctest::Approx(0.6f));
    CHECK(out.im[12] == doctest::Approx(0.8f));
    CHECK(out.re[52] == out.re[12]);
    CHECK(out.im[52] == -out.im[12]);

    // Capacity 100: bins 100 and up neither read nor written.
    spectralFreqShift small(Hz(40.0, 512), 100);
    spectrum wide(256);
    wide.re[40] = 1.f;
    wide.re[150] = 1.f;
    const spectrum cut = Shift(small, Header(512, 128, 256, 0), wide);
    CHECK(OnlyAt(cut, 80));
    spectrum high(256);
    high.re[60] = 1.f; // 100: past the capacity
    CHECK(OnlyAt(Shift(small, Header(512, 128, 256, 1), high), 1000));

    // Non-finite counts as 0.
    spectrum noisy(256);
    noisy.re[30] = 1.f;
    noisy.re[31] = std::numeric_limits<float>::quiet_NaN();
    noisy.im[29] = std::numeric_limits<float>::infinity();
    spectralFreqShift a(Hz(30.0, 512));
    const spectrum clean = Shift(a, Header(512, 128, 256, 0), noisy);
    CHECK(OnlyAt(clean, 60));
    CHECK(clean.re[60] == 1.f);

    // In place gives what separate arrays give, up and down.
    for (const double shift : {11.7, -11.7}) {
      spectralFreqShift x(Hz(shift, 512));
      spectralFreqShift y(Hz(shift, 512));
      for (U64 m = 0; m < 4; ++m) {
        spectrum in = Random(256, static_cast<unsigned int>(m) + 20);
        const spectrum apart = Shift(x, Header(512, 128, 256, m), in);
        stftFrame f = Header(512, 128, 256, m);
        f.real = in.re.data();
        f.imaginary = in.im.data();
        y(f);
        CHECK(in.re == apart.re);
        CHECK(in.im == apart.im);
      }
    }

    // A run that does not start at bin 0 passes through, and restarts the
    // angle.
    spectralFreqShift run(Hz(10.3, 512));
    Shift(run, Header(512, 128, 256, 0), wide);
    Shift(run, Header(512, 128, 256, 1), wide);
    CHECK(run.angle() != 0.0);
    spectrum part(16);
    part.re[3] = 1.f;
    part.im[5] = std::numeric_limits<float>::quiet_NaN();
    stftFrame f = Header(512, 128, 256, 2);
    f.firstBin = 128;
    f.count = 16;
    const spectrum through = Shift(run, f, part);
    CHECK(OnlyAt(through, 3));
    CHECK(through.re[3] == 1.f);
    CHECK(run.angle() == 0.0);
  }

  TEST_CASE("spectralFreqShift: the angle restarts after reset, a change of hop or an index "
            "that goes back; a gap advances it by every frame missed (#1165)") {
    SampleRateGuard guard;
    const UInt size = 512;
    const spectrum in = Random(256, 9);
    spectralFreqShift fn(Hz(3.3, size));
    const double step = 2.0 * PI * fn.frequencyShift() * 128 / RATE;
    Shift(fn, Header(size, 128, 256, 10), in);
    CHECK(fn.angle() == 0.0);
    Shift(fn, Header(size, 128, 256, 11), in);
    CHECK(fn.angle() == doctest::Approx(Wrap(step)));
    Shift(fn, Header(size, 128, 256, 15), in); // 4 frames on
    CHECK(fn.angle() == doctest::Approx(Wrap(5.0 * step)));
    Shift(fn, Header(size, 128, 256, 15), in); // not past the one before
    CHECK(fn.angle() == 0.0);
    Shift(fn, Header(size, 128, 256, 16), in);
    CHECK(fn.angle() == doctest::Approx(Wrap(step)));
    Shift(fn, Header(size, 64, 256, 17), in); // a new hop
    CHECK(fn.angle() == 0.0);
    Shift(fn, Header(size, 64, 256, 18), in);
    CHECK(fn.angle() == doctest::Approx(Wrap(step / 2.0)));
    fn.reset();
    CHECK(fn.angle() == 0.0);
    Shift(fn, Header(size, 64, 256, 19), in);
    CHECK(fn.angle() == 0.0);
    // A new shift changes the advance, not the angle reached.
    Shift(fn, Header(size, 64, 256, 20), in);
    const double reached = fn.angle();
    fn.frequencyShift(Hz(-1.0, size));
    Shift(fn, Header(size, 64, 256, 21), in);
    CHECK(fn.angle() == doctest::Approx(Wrap(reached - 2.0 * PI * 64 / size)));
  }

  TEST_CASE("spectralFreqShift: on DSP::stft sines shifted by a whole or fractional number of "
            "bins, up, down and through 0 Hz, come out at f + shift (#1165)") {
    SampleRateGuard guard;
    struct setting {
      double bins; // the sine, in bins of the FFT
      double shift; // in bins
      UInt overlap;
    };
    const setting settings[] = {{50.3, 12.0, 4},  {50.3, 7.6, 4},   {120.45, -33.25, 4},
                                {37.8, -20.1, 8}, {80.0, 100.5, 4}, {10.2, -20.5, 4},
                                {200.7, -0.4, 4}};
    constexpr UInt SIZE = 2048;
    for (const setting& s : settings) {
      CAPTURE(s.bins);
      CAPTURE(s.shift);
      CAPTURE(s.overlap);
      spectralFreqShift fn(Hz(s.shift, SIZE));
      const std::vector<float> out =
          ShiftedSines(&fn, SIZE, s.overlap, {s.bins}, 64 * N + 16384, 16384);
      // Through 0 Hz the alias: a sine at |f + shift|.
      const double target = std::abs(s.bins + s.shift);
      CAPTURE(target);
      CHECK(std::abs(Loudest(out, SIZE, 1.0, 1000.0) - target) < 0.05);
      const double level = Amplitude(out, target, SIZE);
      CAPTURE(level);
      CHECK(level > 0.4);
      CHECK(level < 0.6);
      // The original frequency gone, where it is not next to the new one.
      if (std::abs(target - s.bins) > 2.0) CHECK(Amplitude(out, s.bins, SIZE) < 0.02 * level);
    }

    // A harmonic pair, 40 and 80 bins, shifted by 15.5: 55.5 and 95.5, no
    // longer a fifth and an octave apart.
    spectralFreqShift pair(Hz(15.5, SIZE));
    const std::vector<float> out =
        ShiftedSines(&pair, SIZE, 4, {40.0, 80.0}, 64 * N + 16384, 16384);
    CHECK(std::abs(Loudest(out, SIZE, 45.0, 70.0) - 55.5) < 0.05);
    CHECK(std::abs(Loudest(out, SIZE, 85.0, 110.0) - 95.5) < 0.05);
    CHECK(Amplitude(out, 111.0, SIZE) < 0.01); // where a pitch shift's octave would be

    // No shift on DSP::stft: plain DSP::stft, sample for sample.
    spectralFreqShift same;
    const std::vector<float> shifted = ShiftedSines(&same, 512, 4, {20.25}, 32 * N, 4 * N);
    const std::vector<float> plain = ShiftedSines(nullptr, 512, 4, {20.25}, 32 * N, 4 * N);
    CHECK(shifted == plain);
  }

  TEST_CASE("spectralFreqShift: processing allocates nothing (#1165)") {
    SampleRateGuard guard;
    spectralFreqShift fn(321.f);
    spectrum s = Random(1025, 11);
    stftFrame f = Header(2048, 512, 1025, 0);
    f.real = s.re.data();
    f.imaginary = s.im.data();
    fn(f);
    {
      TestHelpers::ProbeScope probe;
      for (U64 m = 1; m < 16; ++m) {
        f.index = m;
        fn.frequencyShift(-500.f + 70.f * static_cast<float>(m)).clip(m % 2 == 0);
        fn(f);
      }
      fn.reset();
      fn(f);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }
}
