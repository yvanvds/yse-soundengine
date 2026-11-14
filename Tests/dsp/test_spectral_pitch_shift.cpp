// Tests for YSE::DSP::spectralPitchShift
// (YseEngine/dsp/spectral/spectralPitchShift.hpp), the peak-moving
// frequency-domain pitch shifter behind ~gizmo (issue #1164):
//  - a single-bin spectrum lands on the bin nearest ratio * bin, up and
//    down, its angle advancing 2 pi (target - bin) hop / size a frame, in
//    closed form; with the frequency shift in Hz at the live sample rate;
//  - every peak moves with its region, the regions split at the quietest
//    bin between peaks;
//  - out of range bins fold back (conjugated above Nyquist, mirrored below
//    0) or, with clip, are dropped; a full spectrum's upper half mirrors the
//    lower; capacity, non-finite input, in place, a run that is not a whole
//    frame;
//  - ratio 1 is the identity, bit for bit;
//  - on DSP::stft, a sine pitch-shifted by a ratio comes out at the
//    transposed frequency and about the same level;
//  - processing allocates nothing.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <random>
#include <vector>
#include "dsp/buffer.hpp"
#include "dsp/spectral/spectralPitchShift.hpp"
#include "dsp/spectral/stft.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

using YSE::DSP::buffer;
using YSE::DSP::spectralPitchShift;
using YSE::DSP::stft;
using YSE::DSP::stftFrame;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  struct SampleRateGuard {
    UInt saved;
    SampleRateGuard() : saved(YSE::SAMPLERATE) {}
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

  // A generator for a test's values, the same every run.
  std::mt19937 Generator(unsigned int seed) {
    return std::mt19937(seed);
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

  // `fn` on the frame `in` with header `f`, into a new spectrum.
  spectrum Shift(spectralPitchShift& fn, const stftFrame& f, const spectrum& in) {
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

  // The magnitude of `w` at `bins` (in bins of an FFT of `size`): the
  // amplitude of a sine at that frequency.
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

  // The frequency (in bins of `size`) where `x` is loudest, to a hundredth
  // of a bin.
  double Loudest(const std::vector<float>& x, UInt size) {
    const windowed w(x);
    double best = 0.0;
    double at = 0.0;
    for (UInt i = 2; i < size; ++i) {
      const double b = 0.5 * i;
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

  // A sine of `bins` (in bins of `size`) and amplitude 0.5 through DSP::stft
  // with `fn` on every frame; the last `keep` samples.
  std::vector<float> ShiftedSine(spectralPitchShift& fn, UInt size, UInt overlap, double bins,
                                 std::size_t length, std::size_t keep) {
    stft fx(size, overlap);
    auto shift = [&](stftFrame& frame) { fn(frame); };
    buffer b(N);
    std::vector<float> out;
    for (std::size_t start = 0; start < length; start += N) {
      for (unsigned int i = 0; i < N; ++i)
        b.getPtr()[i] = static_cast<float>(
            0.5 * std::sin(2.0 * PI * bins * static_cast<double>(start + i) / size));
      buffer& o = fx(b, shift);
      out.insert(out.end(), o.getPtr(), o.getPtr() + N);
    }
    return std::vector<float>(out.end() - static_cast<std::ptrdiff_t>(keep), out.end());
  }
} // namespace

TEST_SUITE("dsp") {
  TEST_CASE("spectralPitchShift: defaults, setters and their limits (#1164)") {
    spectralPitchShift fn;
    CHECK(fn.capacity() == spectralPitchShift::DEFAULT_CAPACITY);
    CHECK(fn.ratio() == 1.f);
    CHECK(fn.frequencyShift() == 0.f);
    CHECK_FALSE(fn.clip());
    CHECK(fn.ratio(2.5f).ratio() == 2.5f);
    CHECK(fn.ratio(-1.f).ratio() == 0.f);
    CHECK(fn.ratio(100.f).ratio() == spectralPitchShift::MAXIMUM_RATIO);
    CHECK(fn.ratio(std::numeric_limits<float>::quiet_NaN()).ratio() == 1.f);
    CHECK(fn.frequencyShift(-120.f).frequencyShift() == -120.f);
    CHECK(fn.frequencyShift(std::numeric_limits<float>::infinity()).frequencyShift() == 0.f);
    CHECK(fn.clip(true).clip());
    CHECK(spectralPitchShift(1.5f, 0).capacity() == 1);
    CHECK(spectralPitchShift(1.5f, 1u << 20).capacity() == spectralPitchShift::MAXIMUM_CAPACITY);
    CHECK(spectralPitchShift(0.5f, 64).ratio() == 0.5f);
  }

  TEST_CASE("spectralPitchShift: a single bin lands on the bin nearest ratio * bin, up and "
            "down, its angle advancing with the shift, in closed form (#1164)") {
    struct setting {
      UInt size;
      UInt hop;
      UInt bin;
      float ratio;
    };
    // bin * hop is a whole number of cycles, so a spectrum that stays put
    // from frame to frame is a partial on the bin (deviation 0).
    const setting settings[] = {{256, 64, 40, 1.25f},   {256, 64, 40, 0.65f},
                                {256, 64, 100, 1.2f},   {2048, 512, 100, 3.f},
                                {2048, 512, 900, 0.5f}, {2048, 512, 128, 0.96875f},
                                {2048, 256, 64, 1.37f}, {1024, 128, 32, 0.3f}};
    for (const setting& s : settings) {
      CAPTURE(s.size);
      CAPTURE(s.bin);
      CAPTURE(s.ratio);
      const UInt bins = s.size / 2;
      spectralPitchShift fn(s.ratio);
      spectrum in(bins);
      in.re[s.bin] = 1.f;
      const double target = static_cast<double>(s.ratio) * s.bin;
      const auto lands = static_cast<UInt>(std::lround(target));
      REQUIRE(lands < bins);
      const double step = 2.0 * PI * (target - s.bin) * s.hop / s.size;
      double angle = 0.0;
      for (U64 m = 0; m < 6; ++m) {
        CAPTURE(m);
        const spectrum out = Shift(fn, Header(s.size, s.hop, bins, m), in);
        CHECK(OnlyAt(out, lands));
        CHECK(out.re[lands] == doctest::Approx(std::cos(angle)).epsilon(1e-5));
        CHECK(out.im[lands] == doctest::Approx(std::sin(angle)).epsilon(1e-5));
        angle = Wrap(angle + step);
      }
    }
  }

  TEST_CASE("spectralPitchShift: the frequency shift is in Hz, at the live sample rate; a shift "
            "below 0 Hz folds back (#1164)") {
    SampleRateGuard guard;
    const UInt size = 512;
    const UInt bins = size / 2;
    spectrum in(bins);
    in.re[40] = 1.f;
    for (const UInt rate : {48000u, 96000u}) {
      CAPTURE(rate);
      YSE::SAMPLERATE = rate;
      // 1500 Hz: 16 bins of 93.75 Hz at 48 kHz, 8 at 96 kHz.
      spectralPitchShift fn;
      fn.frequencyShift(1500.f);
      const UInt expect = rate == 48000u ? 56 : 48;
      const spectrum out = Shift(fn, Header(size, 128, bins, 0), in);
      CHECK(OnlyAt(out, expect));
      CHECK(out.re[expect] == 1.f);
      // Ratio and shift: 40 * 0.5 + 16 (48 kHz) or + 8.
      fn.ratio(0.5f);
      const spectrum both = Shift(fn, Header(size, 128, bins, 0), in);
      CHECK(OnlyAt(both, rate == 48000u ? 36 : 28));
    }
    // 40 bins down 50: -10, which aliases to +10, the conjugate.
    YSE::SAMPLERATE = 48000;
    spectralPitchShift down;
    down.frequencyShift(-50.f * 48000.f / size);
    spectrum turned(bins);
    turned.re[40] = 0.6f;
    turned.im[40] = 0.8f;
    const spectrum out = Shift(down, Header(size, 128, bins, 0), turned);
    CHECK(OnlyAt(out, 10));
    CHECK(out.re[10] == doctest::Approx(0.6f));
    CHECK(out.im[10] == doctest::Approx(-0.8f));
    down.clip(true);
    CHECK(OnlyAt(Shift(down, Header(size, 128, bins, 1), turned), bins));
  }

  TEST_CASE("spectralPitchShift: above Nyquist folds back as the conjugate, or with clip is "
            "dropped (#1164)") {
    // 256: Nyquist is bin 128. Bin 100 * 1.5 = 150 aliases to 256 - 150.
    spectrum in(129);
    in.re[100] = 0.6f;
    in.im[100] = 0.8f;
    spectralPitchShift fold(1.5f);
    const spectrum folded = Shift(fold, Header(256, 64, 129, 0), in);
    CHECK(OnlyAt(folded, 106));
    CHECK(folded.re[106] == doctest::Approx(0.6f));
    CHECK(folded.im[106] == doctest::Approx(-0.8f));
    spectralPitchShift clip(1.5f);
    clip.clip(true);
    CHECK(OnlyAt(Shift(clip, Header(256, 64, 129, 0), in), 1000));
    // Exactly on Nyquist is kept in a frame that holds it (DSP::stft's,
    // size / 2 + 1 bins) and dropped from one that does not (a ~pfft's).
    spectrum at(129);
    at.re[64] = 1.f;
    spectralPitchShift two(2.f);
    CHECK(OnlyAt(Shift(two, Header(256, 64, 129, 0), at), 128));
    CHECK(Shift(two, Header(256, 64, 129, 1), at).re[128] != 0.f);
    spectralPitchShift twoHalf(2.f);
    CHECK(OnlyAt(Shift(twoHalf, Header(256, 64, 128, 0), at), 1000));
  }

  TEST_CASE("spectralPitchShift: every peak moves with its region, which ends at the quietest "
            "bin before the next peak (#1164)") {
    const UInt size = 512;
    const UInt bins = size / 2;
    spectrum in(bins);
    // Peak 20 (19, 21 beside it), peak 60 (58 .. 62); the quietest bin
    // between them is 40, which goes with peak 60.
    in.re[19] = 0.5f;
    in.re[20] = 1.f;
    in.re[21] = 0.5f;
    for (UInt k = 22; k < 58; ++k)
      in.re[k] = 0.01f + 0.001f * std::abs(static_cast<float>(k) - 40.f);
    in.re[58] = 0.2f;
    in.re[59] = 0.6f;
    in.re[60] = 2.f;
    in.re[61] = 0.6f;
    in.re[62] = 0.2f;
    spectralPitchShift fn(1.25f);
    const spectrum out = Shift(fn, Header(size, 128, bins, 0), in);
    // 20 -> 25 (+5) for bins 0 .. 39; 60 -> 75 (+15) for 40 .. 255.
    for (UInt k = 0; k < 40; ++k)
      CHECK(out.re[k + 5] == in.re[k]);
    for (UInt k = 40; k + 15 < bins; ++k)
      CHECK(out.re[k + 15] == in.re[k]);
    // Bins 45 .. 54 are where neither region lands.
    for (UInt k = 45; k < 55; ++k)
      CHECK(out.re[k] == 0.f);
    for (UInt k = 0; k < 5; ++k)
      CHECK(out.re[k] == 0.f);
  }

  TEST_CASE("spectralPitchShift: ratio 1 is the identity, bit for bit, frame after frame "
            "(#1164)") {
    std::mt19937 rng = Generator(7);
    std::uniform_real_distribution<float> d(-3.f, 3.f);
    spectralPitchShift fn;
    for (U64 m = 0; m < 8; ++m) {
      spectrum in(513);
      for (UInt k = 0; k < 513; ++k) {
        in.re[k] = d(rng);
        in.im[k] = d(rng);
      }
      const spectrum out = Shift(fn, Header(1024, 256, 513, m), in);
      CHECK(out.re == in.re);
      CHECK(out.im == in.im);
    }
  }

  TEST_CASE("spectralPitchShift: a full spectrum's upper half mirrors the lower; capacity, "
            "non-finite input, in place, and a run that is not a whole frame (#1164)") {
    // Full spectrum of 64: bin 8 * 1.5 = 12, its mirror 52 the conjugate.
    spectrum full(64);
    full.re[8] = 0.6f;
    full.im[8] = 0.8f;
    full.re[56] = 123.f; // the upper half is not read
    spectralPitchShift fn(1.5f);
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
    spectralPitchShift small(2.f, 100);
    spectrum wide(256);
    wide.re[40] = 1.f;
    wide.re[150] = 1.f;
    const spectrum cut = Shift(small, Header(512, 128, 256, 0), wide);
    CHECK(OnlyAt(cut, 80));
    spectrum high(256);
    high.re[60] = 1.f; // 120: past the capacity
    CHECK(OnlyAt(Shift(small, Header(512, 128, 256, 1), high), 1000));

    // Non-finite counts as 0: a NaN next to the peak is not carried along.
    spectrum noisy(256);
    noisy.re[30] = 1.f;
    noisy.re[31] = std::numeric_limits<float>::quiet_NaN();
    noisy.im[29] = std::numeric_limits<float>::infinity();
    spectralPitchShift a(2.f);
    const spectrum clean = Shift(a, Header(512, 128, 256, 0), noisy);
    CHECK(OnlyAt(clean, 60));
    CHECK(clean.re[60] == 1.f);

    // In place gives what separate arrays give.
    std::mt19937 rng = Generator(3);
    std::uniform_real_distribution<float> d(-1.f, 1.f);
    spectralPitchShift x(0.8f);
    spectralPitchShift y(0.8f);
    for (U64 m = 0; m < 4; ++m) {
      spectrum in(256);
      for (UInt k = 0; k < 256; ++k) {
        in.re[k] = d(rng);
        in.im[k] = d(rng);
      }
      const spectrum apart = Shift(x, Header(512, 128, 256, m), in);
      stftFrame f = Header(512, 128, 256, m);
      f.real = in.re.data();
      f.imaginary = in.im.data();
      y(f);
      CHECK(in.re == apart.re);
      CHECK(in.im == apart.im);
    }

    // A run that does not start at bin 0 passes through.
    spectralPitchShift run(2.f);
    spectrum part(16);
    part.re[3] = 1.f;
    part.im[5] = std::numeric_limits<float>::quiet_NaN();
    stftFrame f = Header(512, 128, 256, 0);
    f.firstBin = 128;
    f.count = 16;
    const spectrum through = Shift(run, f, part);
    CHECK(OnlyAt(through, 3));
    CHECK(through.re[3] == 1.f);
  }

  TEST_CASE("spectralPitchShift: on DSP::stft a sine pitch-shifted by a ratio comes out at the "
            "transposed frequency (#1164)") {
    struct setting {
      double bins; // the sine, in bins of the FFT
      float ratio;
    };
    // Off-bin sines, up and down, an octave and more.
    const setting settings[] = {
        {50.3, 1.5f}, {50.3, 0.75f}, {37.8, 2.f}, {120.45, 0.5f}, {80.0, 1.f / 1.0594631f}};
    constexpr UInt SIZE = 2048;
    for (const setting& s : settings) {
      CAPTURE(s.bins);
      CAPTURE(s.ratio);
      spectralPitchShift fn(s.ratio);
      const std::vector<float> out = ShiftedSine(fn, SIZE, 4, s.bins, 64 * N + 16384, 16384);
      const double target = s.bins * s.ratio;
      CAPTURE(target);
      CHECK(std::abs(Loudest(out, SIZE) - target) < 0.05);
      // About the level that went in, and the original pitch gone.
      const double level = Amplitude(out, target, SIZE);
      CAPTURE(level);
      CHECK(level > 0.4);
      CHECK(level < 0.6);
      CHECK(Amplitude(out, s.bins, SIZE) < 0.01 * level);
    }
    // Ratio 1 on DSP::stft: the input, size samples later.
    spectralPitchShift same;
    const std::vector<float> out = ShiftedSine(same, 512, 4, 20.25, 32 * N, 4 * N);
    stft plain(512, 4);
    buffer b(N);
    std::vector<float> ref;
    for (std::size_t start = 0; start < 32 * N; start += N) {
      for (unsigned int i = 0; i < N; ++i)
        b.getPtr()[i] = static_cast<float>(
            0.5 * std::sin(2.0 * PI * 20.25 * static_cast<double>(start + i) / 512));
      buffer& o = plain(b);
      ref.insert(ref.end(), o.getPtr(), o.getPtr() + N);
    }
    CHECK(std::vector<float>(ref.end() - 4 * N, ref.end()) == out);
  }

  TEST_CASE("spectralPitchShift: processing allocates nothing (#1164)") {
    spectralPitchShift fn(1.5f);
    std::mt19937 rng = Generator(11);
    std::uniform_real_distribution<float> d(-1.f, 1.f);
    std::vector<float> re(1025);
    std::vector<float> im(1025);
    for (UInt k = 0; k < 1025; ++k) {
      re[k] = d(rng);
      im[k] = d(rng);
    }
    stftFrame f = Header(2048, 512, 1025, 0);
    f.real = re.data();
    f.imaginary = im.data();
    fn(f);
    {
      TestHelpers::ProbeScope probe;
      for (U64 m = 1; m < 16; ++m) {
        f.index = m;
        fn.ratio(0.5f + 0.1f * static_cast<float>(m)).frequencyShift(10.f).clip(m % 2 == 0);
        fn(f);
      }
      fn.reset();
      fn(f);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }
}
