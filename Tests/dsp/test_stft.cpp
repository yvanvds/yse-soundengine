// Tests for DSP::stft, DSP::stftAnalysis, DSP::stftSynthesis and
// DSP::stftWindow (dsp/spectral/stft.hpp, issue #1041): the windows and the
// overlap-add gain in closed form; perfect reconstruction (an unchanged
// spectrum gives the input back exactly `size` samples later) for every
// window, several overlaps and sizes, the half and the full spectrum, with
// offsets; the bins a callback sees for a constant and a cosine; processing
// in the frame callback (a gain, a gate, a lowpass against a direct
// computation); one callback per hop at the sample it completes, whatever the
// block size; reset; the fitting of sizes and overlaps; allocation freedom.

#include <doctest/doctest.h>
#include <cmath>
#include <vector>
#include "dsp/spectral/stft.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

using YSE::DSP::buffer;
using YSE::DSP::stft;
using YSE::DSP::stftAnalysis;
using YSE::DSP::stftFrame;
using YSE::DSP::stftSynthesis;
using YSE::DSP::stftWindow;

namespace {
  constexpr unsigned int BLOCK = YSE::STANDARD_BUFFERSIZE;
  constexpr double kTwoPi = 6.283185307179586476925286766559;

  // A signal with content across the band, deterministic.
  std::vector<float> Signal(std::size_t length) {
    std::vector<float> x(length);
    unsigned int seed = 12345u;
    for (std::size_t n = 0; n < length; ++n) {
      seed = seed * 1664525u + 1013904223u;
      const float noise = static_cast<float>(seed >> 8) / 16777216.f - 0.5f;
      x[n] = 0.5f * std::sin(0.031f * static_cast<float>(n)) + 0.3f * noise;
    }
    return x;
  }

  // The whole input through `fx` in blocks of `block` samples, `f` called
  // for every frame.
  template <class F>
  std::vector<float> Run(stft& fx, const std::vector<float>& in, unsigned int block, F& f) {
    std::vector<float> out;
    buffer b(block);
    for (std::size_t start = 0; start < in.size(); start += block) {
      const auto length =
          static_cast<unsigned int>(std::min<std::size_t>(block, in.size() - start));
      b.resize(length);
      for (unsigned int n = 0; n < length; ++n)
        b.getPtr()[n] = in[start + n];
      buffer& o = fx(b, f);
      REQUIRE(o.getLength() == length);
      REQUIRE(&o == &fx.output());
      for (unsigned int n = 0; n < length; ++n)
        out.push_back(o.getPtr()[n]);
    }
    return out;
  }

  std::vector<float> Run(stft& fx, const std::vector<float>& in, unsigned int block = BLOCK) {
    auto identity = [](stftFrame&) {};
    return Run(fx, in, block, identity);
  }

  // out[n] == in[n - delay] * gain (0 before the input starts), within tol.
  void CheckDelayed(const std::vector<float>& in, const std::vector<float>& out, unsigned int delay,
                    float tol, float gain = 1.f) {
    REQUIRE(out.size() == in.size());
    float worst = 0.f;
    std::size_t at = 0;
    for (std::size_t n = 0; n < out.size(); ++n) {
      const float expected = n >= delay ? gain * in[n - delay] : 0.f;
      const float err = std::abs(out[n] - expected);
      if (err > worst) {
        worst = err;
        at = n;
      }
    }
    CAPTURE(at);
    CHECK(worst <= tol);
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("stftWindow: the shapes in closed form, periodic (#1041)") {
    constexpr UInt n = 16;
    CHECK(stftWindow::value(stftWindow::SQUARE, 3, n) == 1.f);
    CHECK(stftWindow::value(stftWindow::HANNING, 0, n) == doctest::Approx(0.0));
    CHECK(stftWindow::value(stftWindow::HANNING, 4, n) == doctest::Approx(0.5));
    CHECK(stftWindow::value(stftWindow::HANNING, 8, n) == doctest::Approx(1.0));
    CHECK(stftWindow::value(stftWindow::HAMMING, 0, n) == doctest::Approx(0.08));
    CHECK(stftWindow::value(stftWindow::HAMMING, 8, n) == doctest::Approx(1.0));
    CHECK(stftWindow::value(stftWindow::BLACKMAN, 0, n) == doctest::Approx(0.0).epsilon(1e-6));
    CHECK(stftWindow::value(stftWindow::BLACKMAN, 8, n) == doctest::Approx(1.0));
    CHECK(stftWindow::value(stftWindow::TRIANGLE, 0, n) == doctest::Approx(0.0));
    CHECK(stftWindow::value(stftWindow::TRIANGLE, 4, n) == doctest::Approx(0.5));
    CHECK(stftWindow::value(stftWindow::TRIANGLE, 8, n) == doctest::Approx(1.0));
    // Periodic: sample n is sample n + size.
    CHECK(stftWindow::value(stftWindow::HANNING, 19, n) ==
          stftWindow::value(stftWindow::HANNING, 3, n));
    // A Hann window at an overlap of 2 sums to 1, everywhere.
    for (UInt k = 0; k < n / 2; ++k)
      CHECK(stftWindow::value(stftWindow::HANNING, k, n) +
                stftWindow::value(stftWindow::HANNING, k + n / 2, n) ==
            doctest::Approx(1.0));
    std::vector<float> w(n);
    stftWindow::fill(stftWindow::BLACKMAN, w.data(), n);
    for (UInt k = 0; k < n; ++k)
      CHECK(w[k] == stftWindow::value(stftWindow::BLACKMAN, k, n));
  }

  TEST_CASE("stftSynthesis: the overlap-add gain in closed form (#1041)") {
    // Hann x Hann at an overlap of 4: the products sum to 1.5 at every
    // sample, so the gain is 1 / (1.5 size).
    stftSynthesis hann(64, 4);
    for (UInt p = 0; p < 64; ++p)
      CHECK(hann.gain(p) == doctest::Approx(1.0 / (1.5 * 64.0)));
    // Hann x square at an overlap of 2: the Hann window sums to 1.
    stftSynthesis half(64, 2, false, stftWindow::SQUARE, stftWindow::HANNING);
    for (UInt p = 0; p < 64; ++p)
      CHECK(half.gain(p) == doctest::Approx(1.0 / 64.0));
    // Hann x Hann at an overlap of 2: sin^4 + cos^4, not constant; the gain
    // follows it sample by sample.
    stftSynthesis ripple(64, 2);
    for (UInt p = 0; p < 32; ++p) {
      const double a = stftWindow::value(stftWindow::HANNING, p, 64);
      const double b = stftWindow::value(stftWindow::HANNING, p + 32, 64);
      CHECK(ripple.gain(p) == doctest::Approx(1.0 / (64.0 * (a * a + b * b))));
    }
    // Hann x Hann with no overlap: the sum reaches 0 at the frame edges, so
    // the gain is the mean's, 1 / (size * 3/8 * size) ... over a hop of size.
    stftSynthesis none(64, 1);
    double sum = 0.0;
    for (UInt k = 0; k < 64; ++k) {
      const double w = stftWindow::value(stftWindow::HANNING, k, 64);
      sum += w * w;
    }
    for (UInt p = 0; p < 64; ++p)
      CHECK(none.gain(p) == doctest::Approx(1.0 / (64.0 * sum / 64.0)));
    CHECK(sum == doctest::Approx(64.0 * 3.0 / 8.0));
  }

  TEST_CASE("stft: fitting, shape and defaults (#1041)") {
    CHECK(stft::fitSize(0) == 16);
    CHECK(stft::fitSize(100) == 128);
    CHECK(stft::fitSize(1u << 20) == stft::MAXIMUM_SIZE);
    CHECK(stft::fitOverlap(0, 64) == 1);
    CHECK(stft::fitOverlap(3, 64) == 4);
    CHECK(stft::fitOverlap(1000, 64) == 64);

    stft fx;
    CHECK(fx.size() == 512);
    CHECK(fx.overlap() == 4);
    CHECK(fx.hop() == 128);
    CHECK(fx.bins() == 257);
    CHECK_FALSE(fx.fullSpectrum());
    CHECK(fx.latency() == 512);
    CHECK(fx.analysisWindow() == stftWindow::HANNING);
    CHECK(fx.synthesisWindow() == stftWindow::HANNING);

    stft full(100, 3, true, 70);
    CHECK(full.size() == 128);
    CHECK(full.overlap() == 4);
    CHECK(full.hop() == 32);
    CHECK(full.bins() == 128);
    CHECK(full.offset() == 70 % 32);
  }

  TEST_CASE("stft: an unchanged spectrum gives the input back exactly, size samples later "
            "(#1041)") {
    struct setting {
      UInt size;
      UInt overlap;
      stftWindow::TYPE analysis;
      stftWindow::TYPE synthesis;
      bool full;
      UInt offset;
    };
    const setting settings[] = {
        {16, 2, stftWindow::HANNING, stftWindow::HANNING, false, 0},
        {64, 4, stftWindow::HANNING, stftWindow::HANNING, false, 0},
        {256, 2, stftWindow::HANNING, stftWindow::HANNING, false, 0},
        {512, 4, stftWindow::HANNING, stftWindow::HANNING, false, 5},
        {1024, 8, stftWindow::HANNING, stftWindow::HANNING, true, 0},
        {2048, 4, stftWindow::HAMMING, stftWindow::HAMMING, false, 0},
        {256, 4, stftWindow::BLACKMAN, stftWindow::BLACKMAN, false, 0},
        {256, 2, stftWindow::TRIANGLE, stftWindow::SQUARE, false, 0},
        {128, 1, stftWindow::SQUARE, stftWindow::SQUARE, false, 0},
        {128, 1, stftWindow::SQUARE, stftWindow::SQUARE, true, 37},
        {4096, 2, stftWindow::SQUARE, stftWindow::HANNING, true, 0},
    };
    const std::vector<float> in = Signal(12000);
    for (const setting& s : settings) {
      CAPTURE(s.size);
      CAPTURE(s.overlap);
      CAPTURE(static_cast<int>(s.analysis));
      CAPTURE(static_cast<int>(s.synthesis));
      CAPTURE(s.full);
      CAPTURE(s.offset);
      stft fx(s.size, s.overlap, s.full, s.offset);
      fx.window(s.analysis, s.synthesis);
      CheckDelayed(in, Run(fx, in), s.size, 2e-5f);
    }
  }

  TEST_CASE("stft: the callback sees each frame's spectrum, one per hop, at the sample it "
            "completes (#1041)") {
    constexpr UInt size = 64;
    constexpr UInt overlap = 4;
    for (unsigned int block : {1u, 7u, 16u, 100u, 128u}) {
      CAPTURE(block);
      stft fx(size, overlap);
      fx.window(stftWindow::SQUARE, stftWindow::SQUARE);
      std::vector<U64> indexes;
      std::vector<std::size_t> at; // the sample count when each frame came
      std::size_t taken = 0;
      bool shapeOk = true;
      auto f = [&](stftFrame& frame) {
        shapeOk = shapeOk && frame.firstBin == 0 && frame.count == size / 2 + 1 &&
                  frame.bins == size / 2 + 1 && frame.size == size && frame.hop == size / overlap &&
                  !frame.fullSpectrum;
        indexes.push_back(frame.index);
        at.push_back(taken);
      };
      // Count the samples taken so far by feeding one block at a time.
      std::vector<float> ones(640, 1.f);
      buffer b(block);
      for (std::size_t start = 0; start < ones.size(); start += block) {
        const auto length =
            static_cast<unsigned int>(std::min<std::size_t>(block, ones.size() - start));
        b.resize(length);
        b = 1.f;
        // Frames complete inside the block; record by block, check by count.
        const std::size_t before = indexes.size();
        fx(b, f);
        taken += length;
        for (std::size_t k = before; k < at.size(); ++k)
          at[k] = taken;
      }
      CHECK(shapeOk);
      // A frame completes every hop: after 16, 32, ... samples.
      REQUIRE(indexes.size() == 640 / (size / overlap));
      for (std::size_t k = 0; k < indexes.size(); ++k) {
        CHECK(indexes[k] == k);
        // Completed somewhere inside the block that ended at at[k].
        const std::size_t completes = (k + 1) * (size / overlap);
        CHECK(completes <= at[k]);
        CHECK(completes > at[k] - block);
      }
      CHECK(fx.frames() == indexes.size());
    }
  }

  TEST_CASE("stft: the bins of a constant and of a cosine in closed form (#1041)") {
    constexpr UInt size = 64;
    {
      // A constant 1 under a square window: size in bin 0, 0 elsewhere,
      // once the frame is all signal (from the 4th frame at overlap 4).
      stft fx(size, 4);
      fx.window(stftWindow::SQUARE, stftWindow::SQUARE);
      std::vector<float> dc, other;
      auto f = [&](stftFrame& frame) {
        if (frame.index < 3) return;
        dc.push_back(frame.real[0]);
        float most = 0.f;
        for (UInt k = 1; k < frame.count; ++k)
          most = std::max(most, std::hypot(frame.real[k], frame.imaginary[k]));
        other.push_back(most);
      };
      std::vector<float> ones(1024, 1.f);
      Run(fx, ones, BLOCK, f);
      REQUIRE_FALSE(dc.empty());
      for (float v : dc)
        CHECK(v == doctest::Approx(64.0));
      for (float v : other)
        CHECK(v < 1e-4f);
    }
    {
      // A cosine of 5 cycles per frame under a Hann window: magnitude
      // size / 4 in bin 5, size / 8 in bins 4 and 6, ~0 elsewhere.
      stft fx(size, 2);
      std::vector<float> mags;
      auto f = [&](stftFrame& frame) {
        if (frame.index < 2) return;
        for (UInt k = 0; k < frame.count; ++k)
          mags.push_back(std::hypot(frame.real[k], frame.imaginary[k]));
      };
      std::vector<float> in(1024);
      for (std::size_t n = 0; n < in.size(); ++n)
        in[n] = static_cast<float>(std::cos(kTwoPi * 5.0 * static_cast<double>(n) / size));
      Run(fx, in, BLOCK, f);
      REQUIRE(mags.size() >= 33);
      for (std::size_t j = 0; j < mags.size(); ++j) {
        const UInt k = static_cast<UInt>(j % 33);
        CAPTURE(k);
        const double expected = k == 5 ? size / 4.0 : (k == 4 || k == 6 ? size / 8.0 : 0.0);
        CHECK(mags[j] == doctest::Approx(expected).epsilon(1e-3).scale(64.0));
      }
    }
  }

  TEST_CASE("stft: processing in the callback — a gain, a gate, a brick-wall lowpass (#1041)") {
    const std::vector<float> in = Signal(8000);
    {
      stft fx(256, 4);
      auto half = [](stftFrame& frame) {
        for (UInt k = 0; k < frame.count; ++k) {
          frame.real[k] *= 0.5f;
          frame.imaginary[k] *= 0.5f;
        }
      };
      CheckDelayed(in, Run(fx, in, BLOCK, half), 256, 2e-5f, 0.5f);
    }
    {
      stft fx(256, 4);
      auto gate = [](stftFrame& frame) {
        for (UInt k = 0; k < frame.count; ++k)
          frame.real[k] = frame.imaginary[k] = 0.f;
      };
      const std::vector<float> out = Run(fx, in, BLOCK, gate);
      for (float v : out)
        CHECK(v == 0.f);
    }
    {
      // Bins 8 and up zeroed, no overlap, square windows: each frame of the
      // output is the frame's lowpassed DFT resynthesis, computed directly.
      constexpr UInt size = 64;
      stft fx(size, 1);
      fx.window(stftWindow::SQUARE, stftWindow::SQUARE);
      auto lowpass = [](stftFrame& frame) {
        for (UInt k = 8; k < frame.count; ++k)
          frame.real[k] = frame.imaginary[k] = 0.f;
      };
      const std::vector<float> out = Run(fx, in, BLOCK, lowpass);
      // Frame m covers input m*64 .. m*64+63 and comes out 64 later.
      for (std::size_t m = 0; m + 2 < in.size() / size; ++m) {
        for (UInt n = 0; n < size; n += 5) {
          double y = 0.0;
          for (UInt k = 0; k < 8; ++k) {
            double re = 0.0, im = 0.0;
            for (UInt t = 0; t < size; ++t) {
              const double a = -kTwoPi * k * t / size;
              re += in[m * size + t] * std::cos(a);
              im += in[m * size + t] * std::sin(a);
            }
            const double b = kTwoPi * k * n / size;
            const double term = re * std::cos(b) - im * std::sin(b);
            y += (k == 0 ? 1.0 : 2.0) * term;
          }
          y /= size;
          CAPTURE(m);
          CAPTURE(n);
          CHECK(out[(m + 1) * size + n] == doctest::Approx(y).epsilon(1e-4).scale(1.0));
        }
      }
    }
  }

  TEST_CASE("stft: the output does not depend on the block size (#1041)") {
    const std::vector<float> in = Signal(5000);
    auto tilt = [](stftFrame& frame) {
      for (UInt k = 0; k < frame.count; ++k) {
        const float g = 1.f / (1.f + 0.05f * static_cast<float>(k));
        frame.real[k] *= g;
        frame.imaginary[k] *= g;
      }
    };
    stft reference(256, 8);
    const std::vector<float> expected = Run(reference, in, BLOCK, tilt);
    for (unsigned int block : {1u, 7u, 33u, 100u}) {
      CAPTURE(block);
      stft fx(256, 8);
      const std::vector<float> out = Run(fx, in, block, tilt);
      REQUIRE(out.size() == expected.size());
      bool same = true;
      for (std::size_t n = 0; n < out.size(); ++n)
        same = same && out[n] == expected[n];
      CHECK(same);
    }
  }

  TEST_CASE("stft: reset starts again from sample 0 (#1041)") {
    const std::vector<float> in = Signal(3000);
    stft fx(128, 4);
    const std::vector<float> first = Run(fx, in);
    fx.reset();
    CHECK(fx.frames() == 0);
    const std::vector<float> again = Run(fx, in);
    CHECK(first == again);
  }

  TEST_CASE("stftAnalysis / stftSynthesis: the halves on their own (#1041)") {
    // An analysis of an impulse at the oldest sample, square window: every
    // bin 1. The synthesis of that spectrum, square window, overlap 1: the
    // impulse again, from the next output sample.
    stftAnalysis a(32, false, stftWindow::SQUARE);
    CHECK(a.bins() == 17);
    std::vector<float> x(32, 0.f);
    x[0] = 1.f;
    a.write(x.data(), 32);
    a.analyse();
    for (UInt k = 0; k < a.bins(); ++k) {
      CHECK(a.real()[k] == doctest::Approx(1.0));
      CHECK(a.imaginary()[k] == doctest::Approx(0.0).scale(1.0));
    }
    stftSynthesis s(32, 1, false, stftWindow::SQUARE, stftWindow::SQUARE);
    for (UInt k = 0; k < s.bins(); ++k) {
      s.real()[k] = a.real()[k];
      s.imaginary()[k] = a.imaginary()[k];
    }
    s.synthesise();
    for (UInt k = 0; k < 32; ++k)
      CHECK(s.real()[k] == 0.f); // the bins are cleared
    std::vector<float> y(32, 9.f);
    s.read(y.data(), 32);
    CHECK(y[0] == doctest::Approx(1.0));
    for (UInt n = 1; n < 32; ++n)
      CHECK(y[n] == doctest::Approx(0.0).scale(1.0));
    // Read again: the output was consumed.
    s.read(y.data(), 32);
    for (UInt n = 0; n < 32; ++n)
      CHECK(y[n] == 0.f);
  }

  TEST_CASE("stft: processing, windows and reset allocate nothing (#1041)") {
    stft fx(1024, 4);
    buffer b(BLOCK);
    b = 0.25f;
    auto f = [](stftFrame& frame) { frame.real[1] = 0.f; };
    fx(b, f); // warm up
    {
      TestHelpers::ProbeScope probe;
      for (int i = 0; i < 64; ++i)
        fx(b, f);
      fx(b);
      fx.window(stftWindow::BLACKMAN, stftWindow::HAMMING);
      fx(b, f);
      fx.offset(17);
      fx.reset();
      fx(b, f);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }
}
