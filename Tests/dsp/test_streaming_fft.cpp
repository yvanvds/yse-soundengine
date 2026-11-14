// Tests for DSP::frameClock, DSP::streamingFft and DSP::streamingInverseFft
// (dsp/spectral/streamingFft.hpp, issue #1160): the frame schedule in
// closed form; the bins of an impulse, a cosine and a complex input against
// a direct DFT; every frame against the block classes DSP::fft /
// DSP::inverseFft; intervals above the size, offsets and live offset
// changes; the fft -> ifft round trip (size times larger, 2 * size samples
// later); block-size and sample-rate independence; reset; the fitting of
// sizes and intervals; transforms on two threads at once; allocation
// freedom.

#include <doctest/doctest.h>
#include <atomic>
#include <cmath>
#include <thread>
#include <vector>
#include "dsp/fourier/fft.hpp"
#include "dsp/spectral/frameClock.hpp"
#include "dsp/spectral/streamingFft.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::frameClock;
using YSE::DSP::streamingFft;
using YSE::DSP::streamingInverseFft;
using YSE::DSP::streamingTransform;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double kTwoPi = 6.283185307179586476925286766559;

  struct Output {
    std::vector<float> real, imaginary, sync;
  };

  // The whole input through `fn` in blocks of `block` samples; `im` empty
  // means the real-only call.
  Output Run(streamingTransform& fn, const std::vector<float>& re, const std::vector<float>& im,
             unsigned int block) {
    Output out;
    buffer r(block);
    buffer i(block);
    for (std::size_t start = 0; start < re.size(); start += block) {
      const auto length =
          static_cast<unsigned int>(std::min<std::size_t>(block, re.size() - start));
      r.resize(length);
      i.resize(length);
      for (unsigned int n = 0; n < length; ++n) {
        r.getPtr()[n] = re[start + n];
        if (!im.empty()) i.getPtr()[n] = im[start + n];
      }
      buffer& real = im.empty() ? fn(r) : fn(r, i);
      REQUIRE(real.getLength() == length);
      REQUIRE(&real == &fn.real());
      for (unsigned int n = 0; n < length; ++n) {
        out.real.push_back(fn.real().getPtr()[n]);
        out.imaginary.push_back(fn.imaginary().getPtr()[n]);
        out.sync.push_back(fn.sync().getPtr()[n]);
      }
    }
    return out;
  }

  std::vector<float> Noise(std::size_t length, unsigned int seed) {
    std::vector<float> v(length);
    unsigned int state = seed * 2654435761u + 1u;
    for (auto& x : v) {
      state = state * 1664525u + 1013904223u;
      x = static_cast<float>(state >> 8) / static_cast<float>(1u << 24) * 2.f - 1.f;
    }
    return v;
  }

  // A direct DFT (inverse: the conjugate kernel, not divided) of one frame.
  void Dft(const float* re, const float* im, unsigned int size, bool inverse,
           std::vector<double>& outRe, std::vector<double>& outIm) {
    outRe.assign(size, 0.0);
    outIm.assign(size, 0.0);
    const double sign = inverse ? 1.0 : -1.0;
    for (unsigned int k = 0; k < size; ++k)
      for (unsigned int n = 0; n < size; ++n) {
        const double a = sign * kTwoPi *
                         static_cast<double>((static_cast<unsigned long long>(k) * n) % size) /
                         size;
        const double x = re[n];
        const double y = im == nullptr ? 0.0 : im[n];
        outRe[k] += x * std::cos(a) - y * std::sin(a);
        outIm[k] += x * std::sin(a) + y * std::cos(a);
      }
  }

  // Frames of `size` starting at `start` in `re`/`im` (zeros before 0)
  // transformed by the block classes, compared with `out` from `at` on.
  void CheckFrame(const std::vector<float>& re, const std::vector<float>& im, long start,
                  unsigned int size, bool inverse, const Output& out, std::size_t at) {
    buffer fr(size);
    buffer fi(size);
    for (unsigned int n = 0; n < size; ++n) {
      const long s = start + static_cast<long>(n);
      const bool inside = s >= 0 && static_cast<std::size_t>(s) < re.size();
      fr.getPtr()[n] = inside ? re[static_cast<std::size_t>(s)] : 0.f;
      fi.getPtr()[n] = inside && !im.empty() ? im[static_cast<std::size_t>(s)] : 0.f;
    }
    YSE::DSP::fft forward;
    YSE::DSP::inverseFft backward;
    buffer& r = inverse ? backward(fr, fi) : forward(fr, fi);
    buffer& i = inverse ? backward.getImaginary() : forward.getImaginary();
    for (unsigned int k = 0; k < size; ++k) {
      CAPTURE(start);
      CAPTURE(k);
      REQUIRE(at + k < out.real.size());
      CHECK(out.real[at + k] == r.getPtr()[k]);
      CHECK(out.imaginary[at + k] == i.getPtr()[k]);
      CHECK(out.sync[at + k] == static_cast<float>(k));
    }
  }

  void CheckSilent(const Output& out, std::size_t from, std::size_t to) {
    for (std::size_t n = from; n < to && n < out.real.size(); ++n) {
      CAPTURE(n);
      CHECK(out.real[n] == 0.f);
      CHECK(out.imaginary[n] == 0.f);
      CHECK(out.sync[n] == 0.f);
    }
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("frameClock: frames complete on schedule, whatever the runs (#1160)") {
    // Every (size, interval, offset): the samples at which a frame
    // completes are offset + size - 1 + m * interval, m any whole number,
    // that are >= 0, walking in runs of every length the clock allows.
    struct Case {
      UInt size, interval, offset;
    };
    for (const Case c :
         {Case{16, 16, 0}, Case{16, 16, 5}, Case{16, 64, 0}, Case{16, 64, 40}, Case{16, 64, 60},
          Case{64, 16, 0}, Case{64, 16, 3}, Case{1, 1, 0}, Case{5, 3, 7}}) {
      CAPTURE(c.size);
      CAPTURE(c.interval);
      CAPTURE(c.offset);
      for (const UInt step : {1u, 3u, 1000u}) {
        CAPTURE(step);
        frameClock clock(c.size, c.interval, c.offset);
        CHECK(clock.size() == c.size);
        CHECK(clock.interval() == c.interval);
        CHECK(clock.offset() == c.offset % c.interval);
        std::vector<long> completions;
        long sample = 0;
        while (sample < 400) {
          const long position =
              ((sample - static_cast<long>(clock.offset())) % static_cast<long>(c.interval) +
               static_cast<long>(c.interval)) %
              static_cast<long>(c.interval);
          CHECK(clock.position() == static_cast<UInt>(position));
          const UInt run = std::min(step, clock.untilFrame());
          REQUIRE(run >= 1);
          REQUIRE(run <= c.interval);
          if (clock.advance(run)) completions.push_back(sample + static_cast<long>(run) - 1);
          sample += static_cast<long>(run);
        }
        std::vector<long> expected;
        for (long m = -64; m < 400; ++m) {
          const long last = static_cast<long>(c.offset % c.interval) + static_cast<long>(c.size) -
                            1 + m * static_cast<long>(c.interval);
          if (last >= 0 && last < sample) expected.push_back(last);
        }
        CHECK(completions == expected);
      }
    }
  }

  TEST_CASE("frameClock: a new offset keeps the stream's place; reset restarts it (#1160)") {
    frameClock clock(16, 32, 0);
    CHECK(clock.advance(10) == false);
    CHECK(clock.position() == 10);
    clock.offset(4); // sample 10 is now at position 6
    CHECK(clock.offset() == 4);
    CHECK(clock.position() == 6);
    CHECK(clock.untilFrame() == 10); // the frame ends at sample 4 + 15 = 19
    clock.offset(36); // modulo 32: 4 again
    CHECK(clock.offset() == 4);
    CHECK(clock.position() == 6);
    clock.offset(20); // sample 10 at position 22: not taking samples in
    CHECK(clock.position() == 22);
    CHECK(clock.untilFrame() == 26);
    clock.reset(); // sample 0 next, at position -20 mod 32
    CHECK(clock.position() == 12);
    CHECK(clock.untilFrame() == 4);
    // Out-of-range settings are held to 1.
    frameClock tiny(0, 0, 9);
    CHECK(tiny.size() == 1);
    CHECK(tiny.interval() == 1);
    CHECK(tiny.offset() == 0);
    CHECK(tiny.untilFrame() == 1);
  }

  TEST_CASE("streamingFft: sizes and intervals are fitted to powers of two (#1160)") {
    CHECK(streamingTransform::fitSize(0) == 16);
    CHECK(streamingTransform::fitSize(16) == 16);
    CHECK(streamingTransform::fitSize(17) == 32);
    CHECK(streamingTransform::fitSize(512) == 512);
    CHECK(streamingTransform::fitSize(600) == 1024);
    CHECK(streamingTransform::fitSize(1u << 16) == 1u << 16);
    CHECK(streamingTransform::fitSize(1u << 20) == 1u << 16);
    CHECK(streamingTransform::fitInterval(0, 512) == 512);
    CHECK(streamingTransform::fitInterval(100, 512) == 512);
    CHECK(streamingTransform::fitInterval(513, 512) == 1024);
    CHECK(streamingTransform::fitInterval(0, 600) == 1024);
    CHECK(streamingTransform::fitInterval(1u << 30, 512) == streamingTransform::MAXIMUM_INTERVAL);

    streamingFft fn(100, 300, 700);
    CHECK(fn.size() == 128);
    CHECK(fn.interval() == 512);
    CHECK(fn.offset() == 188);
    CHECK(fn.latency() == 128);
    streamingInverseFft defaults;
    CHECK(defaults.size() == 512);
    CHECK(defaults.interval() == 512);
    CHECK(defaults.offset() == 0);
  }

  TEST_CASE("streamingFft: an impulse and a cosine give their bins in closed form, one frame "
            "later (#1160)") {
    constexpr unsigned int SIZE = 64;
    // An impulse at sample 5 of frame 0: bin k = e^(-2 pi i k 5 / 64).
    std::vector<float> in(4 * SIZE, 0.f);
    in[5] = 1.f;
    // Frame 1: a cosine of 3 cycles per frame, amplitude 2: bins 3 and 61
    // are 64, the rest 0.
    for (unsigned int n = 0; n < SIZE; ++n)
      in[SIZE + n] =
          2.f * static_cast<float>(std::cos(kTwoPi * 3.0 * static_cast<double>(n) / SIZE));
    streamingFft fn(SIZE);
    const Output out = Run(fn, in, {}, N);

    // Nothing comes out while the first frame is taken in.
    CheckSilent(out, 0, SIZE);
    for (unsigned int k = 0; k < SIZE; ++k) {
      CAPTURE(k);
      const double a = -kTwoPi * 5.0 * k / SIZE;
      CHECK(out.real[SIZE + k] == doctest::Approx(std::cos(a)).epsilon(1e-5));
      CHECK(out.imaginary[SIZE + k] == doctest::Approx(std::sin(a)).epsilon(1e-5));
      CHECK(out.sync[SIZE + k] == static_cast<float>(k));

      const double expected = (k == 3 || k == SIZE - 3) ? 64.0 : 0.0;
      CHECK(std::abs(out.real[2 * SIZE + k] - expected) < 1e-4);
      CHECK(std::abs(out.imaginary[2 * SIZE + k]) < 1e-4);
      CHECK(out.sync[2 * SIZE + k] == static_cast<float>(k));
    }
    // The frame after the cosine is silent: every bin 0, the sync still
    // counting.
    for (unsigned int k = 0; k < SIZE; ++k) {
      CHECK(out.real[3 * SIZE + k] == 0.f);
      CHECK(out.sync[3 * SIZE + k] == static_cast<float>(k));
    }
  }

  TEST_CASE("streamingFft / streamingInverseFft: every frame matches a direct DFT and the "
            "block classes (#1160)") {
    for (const unsigned int size : {16u, 64u, 512u, 2048u}) {
      CAPTURE(size);
      const auto re = Noise(5 * size, size);
      const auto im = Noise(5 * size, size + 1);
      for (const bool inverse : {false, true}) {
        CAPTURE(inverse);
        streamingFft forward(size);
        streamingInverseFft backward(size);
        streamingTransform& fn = inverse ? static_cast<streamingTransform&>(backward) : forward;
        const Output out = Run(fn, re, im, N);
        CheckSilent(out, 0, size);
        for (unsigned int m = 0; m + 1 < 5; ++m)
          CheckFrame(re, im, static_cast<long>(m) * static_cast<long>(size), size, inverse, out,
                     (m + 1) * size);

        // Frame 2 against a DFT in double.
        std::vector<double> dr, di;
        Dft(re.data() + 2 * size, im.data() + 2 * size, size, inverse, dr, di);
        const double tolerance = 1e-4 * size;
        for (unsigned int k = 0; k < size; ++k) {
          CAPTURE(k);
          CHECK(std::abs(out.real[3 * size + k] - dr[k]) < tolerance);
          CHECK(std::abs(out.imaginary[3 * size + k] - di[k]) < tolerance);
        }
      }
    }
    // The real-only call is the complex one with a zero imaginary input.
    const auto re = Noise(4 * 256, 9);
    const std::vector<float> zeros(re.size(), 0.f);
    streamingFft a(256);
    streamingFft b(256);
    const Output real = Run(a, re, {}, N);
    const Output complex = Run(b, re, zeros, N);
    CHECK(real.real == complex.real);
    CHECK(real.imaginary == complex.imaginary);
    CHECK(real.sync == complex.sync);
  }

  TEST_CASE("streamingFft: the largest size transforms an impulse (#1160)") {
    const UInt size = streamingTransform::MAXIMUM_SIZE;
    std::vector<float> in(2 * size, 0.f);
    in[1] = 1.f; // bin k = e^(-2 pi i k / size)
    streamingFft fn(size);
    const Output out = Run(fn, in, {}, N);
    for (UInt k = 0; k < size; k += 97) {
      CAPTURE(k);
      const double a = -kTwoPi * k / size;
      CHECK(std::abs(out.real[size + k] - std::cos(a)) < 1e-3);
      CHECK(std::abs(out.imaginary[size + k] - std::sin(a)) < 1e-3);
    }
  }

  TEST_CASE("streamingFft: an interval above the size uses the first size samples of each "
            "period, then is silent (#1160)") {
    constexpr unsigned int SIZE = 32;
    constexpr unsigned int INTERVAL = 128;
    const auto re = Noise(6 * INTERVAL, 4);
    const auto im = Noise(6 * INTERVAL, 5);
    for (const unsigned int offset : {0u, 50u, 100u, 127u}) {
      CAPTURE(offset);
      streamingFft fn(SIZE, INTERVAL, offset);
      const Output out = Run(fn, re, im, N);
      // Frame m starts at offset + (m - 1) * interval (the one before the
      // stream reads zeros) and comes out the size samples after it.
      std::size_t last = 0;
      for (long m = 0; m < 6; ++m) {
        const long start = static_cast<long>(offset) + (m - 1) * static_cast<long>(INTERVAL);
        const long at = start + static_cast<long>(SIZE);
        if (at < 0 || static_cast<std::size_t>(at) + SIZE > re.size()) continue;
        CheckSilent(out, last, static_cast<std::size_t>(at));
        CheckFrame(re, im, start, SIZE, false, out, static_cast<std::size_t>(at));
        last = static_cast<std::size_t>(at) + SIZE;
      }
      CHECK(last > 4 * INTERVAL);
    }
  }

  TEST_CASE("streamingFft: an offset staggers frames, the stream reading 0 before its start "
            "(#1160)") {
    constexpr unsigned int SIZE = 64;
    const auto re = Noise(6 * SIZE, 6);
    for (const unsigned int offset : {1u, 32u, 63u}) {
      CAPTURE(offset);
      streamingFft fn(SIZE, SIZE, offset);
      const Output out = Run(fn, re, {}, N);
      // The first frame started offset - size samples before the stream.
      CheckSilent(out, 0, offset);
      for (long m = 0; m < 5; ++m) {
        const long start = static_cast<long>(offset) + (m - 1) * static_cast<long>(SIZE);
        CheckFrame(re, {}, start, SIZE, false, out, static_cast<std::size_t>(start + SIZE));
      }
    }
  }

  TEST_CASE("streamingFft: a live offset moves the frames from then on (#1160)") {
    constexpr unsigned int SIZE = 64;
    const auto re = Noise(8 * SIZE, 7);
    streamingFft fn(SIZE, 2 * SIZE, 0);
    Output out = Run(fn, std::vector<float>(re.begin(), re.begin() + 2 * SIZE), {}, 32);
    // Sample 128 would start a frame at offset 0. Under offset 100 it sits
    // at position 28: samples 128..163 fill positions 28..63 of a frame that
    // still holds samples 0..27, which completes at 163. The first whole
    // frame under the new offset starts at 228, the next at 356.
    fn.offset(100);
    CHECK(fn.offset() == 100);
    const Output rest = Run(fn, std::vector<float>(re.begin() + 2 * SIZE, re.end()), {}, 32);
    out.real.insert(out.real.end(), rest.real.begin(), rest.real.end());
    out.imaginary.insert(out.imaginary.end(), rest.imaginary.begin(), rest.imaginary.end());
    out.sync.insert(out.sync.end(), rest.sync.begin(), rest.sync.end());
    for (const long start : {228L, 356L}) {
      CAPTURE(start);
      CheckFrame(re, {}, start, SIZE, false, out, static_cast<std::size_t>(start + SIZE));
      CheckSilent(out, static_cast<std::size_t>(start + 2 * SIZE),
                  static_cast<std::size_t>(start + 2 * SIZE + 64));
    }
  }

  TEST_CASE("streamingFft -> streamingInverseFft gives the input back, size times larger, two "
            "frames later (#1160)") {
    constexpr unsigned int SIZE = 256;
    const auto in = Noise(8 * SIZE, 8);
    struct Case {
      UInt interval, inverseOffset;
    };
    // Back to back, the inverse on the same schedule; with an interval of
    // twice the size, the inverse offset by the size and every other frame
    // of the input used.
    for (const Case c : {Case{SIZE, 0}, Case{2 * SIZE, SIZE}}) {
      CAPTURE(c.interval);
      streamingFft forward(SIZE, c.interval, 0);
      streamingInverseFft backward(SIZE, c.interval, c.inverseOffset);
      std::vector<float> out;
      buffer block(N);
      for (std::size_t start = 0; start < in.size(); start += N) {
        for (unsigned int n = 0; n < N; ++n)
          block.getPtr()[n] = in[start + n];
        forward(block);
        backward(forward.real(), forward.imaginary());
        for (unsigned int n = 0; n < N; ++n) {
          out.push_back(backward.real().getPtr()[n]);
          CHECK(std::abs(backward.imaginary().getPtr()[n]) < 1e-3f * SIZE);
        }
      }
      for (std::size_t n = 2 * SIZE; n < out.size(); ++n) {
        CAPTURE(n);
        const std::size_t source = n - 2 * SIZE;
        const bool used = (source % c.interval) < SIZE;
        const float expected = used ? static_cast<float>(SIZE) * in[source] : 0.f;
        CHECK(std::abs(out[n] - expected) < 2e-3f * SIZE);
      }
      for (std::size_t n = 0; n < 2 * SIZE; ++n)
        CHECK(out[n] == 0.f);
    }
  }

  TEST_CASE("streamingFft: the output does not depend on the block size or the sample rate "
            "(#1160)") {
    const auto re = Noise(3000, 10);
    const auto im = Noise(3000, 11);
    streamingFft reference(128, 256, 77);
    const Output expected = Run(reference, re, im, N);
    for (const unsigned int block : {1u, 7u, 100u, N - 1}) {
      CAPTURE(block);
      streamingFft fn(128, 256, 77);
      const Output out = Run(fn, re, im, block);
      CHECK(out.real == expected.real);
      CHECK(out.imaginary == expected.imaginary);
      CHECK(out.sync == expected.sync);
    }
    for (const UInt rate : {22050u, 96000u}) {
      CAPTURE(rate);
      streamingFft fn(128, 256, 77);
      TestHelpers::ScopedSampleRate scoped(rate);
      const Output out = Run(fn, re, im, N);
      CHECK(out.real == expected.real);
      CHECK(out.sync == expected.sync);
    }
    // A block longer than STANDARD_BUFFERSIZE is cut to it; a block is as
    // long as its shorter input.
    streamingFft fn(128);
    buffer longIn(N + 30);
    CHECK(fn(longIn).getLength() == N);
    buffer a(40);
    buffer b(30);
    CHECK(fn(a, b).getLength() == 30);
  }

  TEST_CASE("streamingFft: reset clears the frame and the output and restarts the stream "
            "(#1160)") {
    const auto re = Noise(1000, 12);
    streamingFft fn(64, 128, 30);
    const Output first = Run(fn, re, {}, N);
    Run(fn, Noise(333, 13), {}, 50); // anything
    fn.reset();
    const Output again = Run(fn, re, {}, N);
    CHECK(again.real == first.real);
    CHECK(again.imaginary == first.imaginary);
    CHECK(again.sync == first.sync);
  }

  TEST_CASE("streamingFft: transforms on two threads at once agree with one at a time "
            "(#1160)") {
    // The Mayer FFT kept its trig generator's work tables in statics, so
    // two transforms on two render threads overwrote each other's twiddles.
    const auto re = Noise(16 * 4096, 14);
    const auto expectedSmall = [&] {
      streamingFft fn(4096);
      return Run(fn, re, {}, N);
    }();
    const auto expectedLarge = [&] {
      streamingFft fn(16384);
      return Run(fn, re, {}, N);
    }();
    std::atomic<int> mismatches{0};
    const auto work = [&](UInt size, const Output& expected) {
      for (int round = 0; round < 6; ++round) {
        streamingFft fn(size);
        const Output out = Run(fn, re, {}, N);
        if (out.real != expected.real || out.imaginary != expected.imaginary) mismatches++;
      }
    };
    std::thread a(work, 4096u, std::cref(expectedSmall));
    std::thread b(work, 16384u, std::cref(expectedLarge));
    a.join();
    b.join();
    CHECK(mismatches.load() == 0);
  }

  TEST_CASE("streamingFft / streamingInverseFft: processing, offset and reset allocate "
            "nothing (#1160)") {
    streamingFft forward(1024, 2048, 0);
    streamingInverseFft backward(1024, 2048, 1024);
    buffer re(N);
    buffer im(N);
    buffer shortRe(50);
    for (unsigned int n = 0; n < N; ++n) {
      re.getPtr()[n] = std::sin(static_cast<float>(n) * 0.1f);
      im.getPtr()[n] = 0.f;
    }
    forward(re, im); // first call
    backward(forward.real(), forward.imaginary());
    {
      TestHelpers::ProbeScope probe;
      for (int block = 0; block < 40; ++block) { // frames complete inside blocks
        forward(re, im);
        backward(forward.real(), forward.imaginary());
      }
      forward(shortRe);
      forward.offset(300);
      forward(re);
      forward.reset();
      backward.reset();
      forward(re);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
