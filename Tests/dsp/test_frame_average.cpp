// Tests for YSE::DSP::frameAverage and YSE::DSP::frameSmooth
// (YseEngine/dsp/spectral/frameAverage.hpp), the per-bin running average
// across frames and the trailing and centred averages along a frame behind
// ~frameaverage and ~framesmooth (issues #1162, #1280): all in closed form; a frame handed over
// whole and as runs of bins in bin order give the same output; frame count,
// smoothness, capacity, frame-size change, clear, non-finite input, in place;
// the block overload as a frame and as a stream of frames across blocks;
// precision over a long run; averaging on DSP::stft without a patcher;
// sample-rate independence; allocation freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>
#include "dsp/spectral/frameAverage.hpp"
#include "dsp/spectral/stft.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::frameAverage;
using YSE::DSP::frameSmooth;
using YSE::DSP::stft;
using YSE::DSP::stftFrame;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  using Frames = std::vector<std::vector<float>>;

  Frames Noise(unsigned int frames, unsigned int bins, unsigned int seed, float range = 4.f) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> d(-range, range);
    Frames out(frames, std::vector<float>(bins));
    for (auto& f : out)
      for (float& v : f)
        v = d(rng);
    return out;
  }

  stftFrame Run(UInt firstBin, UInt count, UInt bins, U64 index = 0) {
    stftFrame f;
    f.firstBin = firstBin;
    f.count = count;
    f.bins = bins;
    f.index = index;
    return f;
  }

  // Every frame through `fn` as runs of at most `run` bins, in bin order,
  // the frames numbered when `numbered`, else all index 0 (a new frame is
  // then told by the first bin going back).
  template <class Fn> Frames Process(Fn& fn, const Frames& in, UInt run, bool numbered = true) {
    Frames out;
    U64 index = 0;
    for (const auto& frame : in) {
      const auto bins = static_cast<UInt>(frame.size());
      std::vector<float> o(bins);
      for (UInt first = 0; first < bins; first += run) {
        const UInt count = std::min(run, bins - first);
        fn(Run(first, count, bins, numbered ? index : 0), frame.data() + first, o.data() + first);
      }
      index++;
      out.push_back(o);
    }
    return out;
  }

  // The mean of bin k over frame m and the n - 1 before it, 0 before frame 0.
  double Average(const Frames& in, std::size_t m, std::size_t k, std::size_t n) {
    double sum = 0.0;
    for (std::size_t j = 0; j < n && j <= m; ++j)
      sum += in[m - j][k];
    return sum / static_cast<double>(n);
  }

  // The mean of x[k] and the s - 1 values before it in the frame, as many
  // as there are.
  double Trailing(const std::vector<float>& frame, std::size_t k, std::size_t s) {
    const std::size_t from = k + 1 >= s ? k + 1 - s : 0;
    double sum = 0.0;
    for (std::size_t j = from; j <= k; ++j)
      sum += frame[j];
    return sum / static_cast<double>(k + 1 - from);
  }

  // The mean of x[k] over the window centred on it, s / 2 values before and
  // s - 1 - s / 2 after, cut to the frame.
  double Centred(const std::vector<float>& frame, std::size_t k, std::size_t s) {
    const std::size_t before = s / 2;
    const std::size_t from = k > before ? k - before : 0;
    const std::size_t to = std::min(frame.size(), k + s - before);
    double sum = 0.0;
    for (std::size_t j = from; j < to; ++j)
      sum += frame[j];
    return sum / static_cast<double>(to - from);
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("frameAverage: each bin's mean over the last frameCount frames (#1162)") {
    const Frames in = Noise(12, 300, 1);
    for (const UInt n : {1u, 2u, 4u, 7u}) {
      CAPTURE(n);
      frameAverage fn(n);
      CHECK(fn.frameCount() == n);
      CHECK(fn.maxFrames() == n);
      const Frames out = Process(fn, in, 300);
      for (std::size_t m = 0; m < in.size(); ++m)
        for (std::size_t k = 0; k < in[m].size(); ++k)
          REQUIRE(out[m][k] == doctest::Approx(Average(in, m, k, n)).epsilon(1e-6));
      CHECK(fn.value(7) == doctest::Approx(Average(in, in.size() - 1, 7, n)).epsilon(1e-6));
      CHECK(fn.value(fn.capacity()) == 0.f);
    }
    // One frame: the frames pass unchanged.
    frameAverage one(1);
    CHECK(Process(one, in, 128) == in);
    // The default: four frames, 4096 bins.
    frameAverage plain;
    CHECK(plain.frameCount() == frameAverage::DEFAULT_FRAMES);
    CHECK(plain.capacity() == frameAverage::DEFAULT_CAPACITY);
    CHECK(plain.frameSize() == 0);
  }

  TEST_CASE("frameSmooth: each value's mean with the smoothness - 1 before it in its frame "
            "(#1162)") {
    const Frames in = Noise(5, 300, 2);
    for (const UInt s : {1u, 2u, 3u, 8u, 64u, 400u}) {
      CAPTURE(s);
      frameSmooth fn(s);
      CHECK(fn.smoothness() == s);
      const Frames out = Process(fn, in, 300);
      for (std::size_t m = 0; m < in.size(); ++m)
        for (std::size_t k = 0; k < in[m].size(); ++k)
          REQUIRE(out[m][k] == doctest::Approx(Trailing(in[m], k, s)).epsilon(1e-6));
      // Nothing of one frame reaches into the next: bin 0 of every frame
      // comes out as it went in.
      for (std::size_t m = 0; m < in.size(); ++m)
        REQUIRE(out[m][0] == in[m][0]);
    }
    frameSmooth one(1);
    CHECK(Process(one, in, 77) == in);
    frameSmooth plain;
    CHECK(plain.smoothness() == frameSmooth::DEFAULT_SMOOTHNESS);
    CHECK(frameSmooth(0).smoothness() == 1);
    CHECK(frameSmooth(5000).smoothness() == frameSmooth::MAXIMUM_SMOOTHNESS);
  }

  TEST_CASE("frameSmooth: centred() averages each value with its neighbours on both sides, cut "
            "to the frame (#1280)") {
    const Frames in = Noise(3, 300, 12);
    for (const UInt s : {1u, 2u, 3u, 4u, 5u, 41u, 400u, 1024u}) {
      CAPTURE(s);
      frameSmooth fn(s);
      for (const auto& frame : in) {
        std::vector<float> out(frame.size());
        fn.centred(Run(0, 300, 300), frame.data(), out.data());
        for (std::size_t k = 0; k < frame.size(); ++k)
          REQUIRE(out[k] == doctest::Approx(Centred(frame, k, s)).epsilon(1e-6));
      }
    }
    // A single bin, odd smoothness: symmetric around it, exactly 1 / s.
    std::vector<float> spike(256, 0.f);
    spike[100] = 1.f;
    std::vector<float> out(256);
    frameSmooth five(5);
    five.centred(Run(0, 256, 256), spike.data(), out.data());
    for (std::size_t k = 0; k < 256; ++k) {
      const bool inside = k >= 98 && k <= 102;
      REQUIRE(out[k] == (inside ? static_cast<float>(1.0 / 5) : 0.f));
    }
    // Next to the edge the window is cut: bin 1 of a spike at bin 0 takes in
    // bins 0..3.
    std::vector<float> edge(16, 0.f);
    edge[0] = 4.f;
    std::vector<float> cut(16);
    five.centred(Run(0, 16, 16), edge.data(), cut.data());
    CHECK(cut[0] == doctest::Approx(4.0 / 3));
    CHECK(cut[1] == 1.f);
    CHECK(cut[2] == doctest::Approx(0.8));
    CHECK(cut[3] == 0.f);
    // Even smoothness: one more before than after (4: k-2 .. k+1).
    frameSmooth four(4);
    four.centred(Run(0, 256, 256), spike.data(), out.data());
    for (std::size_t k = 0; k < 256; ++k) {
      const bool inside = k >= 99 && k <= 102;
      REQUIRE(out[k] == (inside ? 0.25f : 0.f));
    }
    // In place, non-finite input as 0, and only frame.count values.
    std::vector<float> y = {1.f, std::numeric_limits<float>::quiet_NaN(), 5.f, 7.f, 99.f};
    frameSmooth three(3);
    three.centred(Run(0, 4, 4), y.data(), y.data());
    CHECK(y == std::vector<float>{0.5f, 2.f, 4.f, 6.f, 99.f});
    // It keeps nothing, and leaves the runs' trailing window alone.
    std::vector<float> ramp(20);
    for (std::size_t i = 0; i < ramp.size(); ++i)
      ramp[i] = static_cast<float>(i);
    std::vector<float> trail(20);
    frameSmooth both(4);
    both(Run(0, 10, 20), ramp.data(), trail.data());
    std::vector<float> scratch(20);
    both.centred(Run(0, 20, 20), spike.data(), scratch.data());
    both(Run(10, 10, 20), ramp.data() + 10, trail.data() + 10);
    for (std::size_t k = 0; k < 20; ++k)
      REQUIRE(trail[k] == doctest::Approx(Trailing(ramp, k, 4)).epsilon(1e-6));
    // From a block: the frame's values, the rest of the block 0.
    buffer b(N);
    for (unsigned int i = 0; i < N; ++i)
      b.getPtr()[i] = static_cast<float>(i);
    buffer& o = three.centred(Run(0, 10, 10), b);
    REQUIRE(o.getLength() == N);
    CHECK(o.getPtr()[0] == 0.5f);
    CHECK(o.getPtr()[5] == 5.f);
    CHECK(o.getPtr()[9] == 8.5f);
    CHECK(o.getPtr()[10] == 0.f);
  }

  TEST_CASE("frameAverage / frameSmooth: runs of bins in bin order equal the whole frame "
            "(#1162)") {
    const Frames in = Noise(9, 512, 3);
    frameAverage wholeAverage(5);
    frameSmooth wholeSmooth(6);
    const Frames average = Process(wholeAverage, in, 512);
    const Frames smooth = Process(wholeSmooth, in, 512);
    for (const UInt run : {1u, 7u, 100u, 128u, 256u}) {
      CAPTURE(run);
      frameAverage a(5);
      frameSmooth s(6);
      CHECK(Process(a, in, run) == average);
      CHECK(Process(s, in, run) == smooth);
      // Unnumbered frames: a run that goes back to bin 0 starts the next.
      frameAverage a2(5);
      frameSmooth s2(6);
      CHECK(Process(a2, in, run, false) == average);
      CHECK(Process(s2, in, run, false) == smooth);
    }
  }

  TEST_CASE("frameSmooth: a run that does not continue the frame starts a new window "
            "(#1162)") {
    std::vector<float> in(40);
    for (std::size_t i = 0; i < in.size(); ++i)
      in[i] = static_cast<float>(i);
    std::vector<float> out(40);
    frameSmooth fn(4);
    fn(Run(0, 20, 40, 3), in.data(), out.data());
    // Bins 20.. of the same frame: the window carries on.
    fn(Run(20, 10, 40, 3), in.data() + 20, out.data() + 20);
    CHECK(out[20] == doctest::Approx((17.0 + 18 + 19 + 20) / 4));
    // A gap (bin 35 after 29): the window starts again at 35.
    fn(Run(35, 5, 40, 3), in.data() + 35, out.data() + 35);
    CHECK(out[35] == 35.f);
    CHECK(out[36] == doctest::Approx(35.5));
    // The same bins of another frame: a new window.
    fn(Run(36, 4, 40, 4), in.data() + 36, out.data() + 36);
    CHECK(out[36] == 36.f);
    // Another frame size: a new window too.
    fn(Run(40, 1, 80, 4), in.data(), out.data());
    CHECK(out[0] == 0.f);
  }

  TEST_CASE("frameSmooth: a new smoothness takes in the values before it at once (#1162)") {
    const Frames in = Noise(1, 200, 4);
    std::vector<float> out(200);
    frameSmooth fn(3);
    fn(Run(0, 100, 200), in[0].data(), out.data());
    fn.smoothness(10);
    fn(Run(100, 50, 200), in[0].data() + 100, out.data() + 100);
    fn.smoothness(2);
    fn(Run(150, 50, 200), in[0].data() + 150, out.data() + 150);
    for (std::size_t k = 0; k < 100; ++k)
      REQUIRE(out[k] == doctest::Approx(Trailing(in[0], k, 3)).epsilon(1e-6));
    for (std::size_t k = 100; k < 150; ++k)
      REQUIRE(out[k] == doctest::Approx(Trailing(in[0], k, 10)).epsilon(1e-6));
    for (std::size_t k = 150; k < 200; ++k)
      REQUIRE(out[k] == doctest::Approx(Trailing(in[0], k, 2)).epsilon(1e-6));
    // clear: the next value starts a new window.
    fn.clear();
    std::vector<float> two = {4.f, 8.f};
    fn(Run(200, 2, 200), two.data(), two.data());
    CHECK(two == std::vector<float>{4.f, 6.f});
  }

  TEST_CASE("frameAverage: frame count, capacity, frame-size change and clear (#1162)") {
    std::vector<float> in(64, 2.f);
    std::vector<float> out(64);
    frameAverage fn(4, 32);
    CHECK(fn.capacity() == 32);
    fn(Run(0, 64, 64), in.data(), out.data());
    CHECK(out[5] == 0.5f);
    CHECK(out[31] == 0.5f);
    // Past the capacity: 0.
    CHECK(out[32] == 0.f);
    CHECK(out[63] == 0.f);
    fn(Run(0, 64, 64), in.data(), out.data());
    CHECK(out[5] == 1.f);
    // A count above the maximum is the maximum; a change clears.
    fn.frameCount(9);
    CHECK(fn.frameCount() == 4);
    fn.frameCount(2);
    CHECK(fn.frameCount() == 2);
    CHECK(fn.value(5) == 0.f);
    fn(Run(0, 64, 64), in.data(), out.data());
    CHECK(out[5] == 1.f);
    fn(Run(0, 64, 64), in.data(), out.data());
    CHECK(out[5] == 2.f);
    fn.frameCount(0);
    CHECK(fn.frameCount() == 1);
    fn.frameCount(4);
    // Another frame size: a new spectrum, the state cleared.
    fn(Run(0, 64, 64), in.data(), out.data());
    fn(Run(0, 64, 64), in.data(), out.data());
    CHECK(out[5] == 1.f);
    fn(Run(0, 64, 128), in.data(), out.data());
    CHECK(out[5] == 0.5f);
    // clear: as at the start.
    fn(Run(0, 64, 128), in.data(), out.data());
    CHECK(out[5] == 1.f);
    fn.clear();
    CHECK(fn.value(5) == 0.f);
    fn(Run(0, 64, 128), in.data(), out.data());
    CHECK(out[5] == 0.5f);
    // Clamped construction.
    CHECK(frameAverage(0, 0).maxFrames() == 1);
    CHECK(frameAverage(0, 0).capacity() == 1);
    CHECK(frameAverage(1000, 8).maxFrames() == frameAverage::MAXIMUM_FRAMES);
  }

  TEST_CASE("frameAverage / frameSmooth: non-finite input counts as 0; in place works "
            "(#1162)") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    std::vector<float> bad = {nan, inf, -inf, 4.f};
    std::vector<float> out(4);
    frameAverage a(2);
    frameSmooth s(2);
    a(Run(0, 4, 4), bad.data(), out.data());
    CHECK(out == std::vector<float>{0.f, 0.f, 0.f, 2.f});
    s(Run(0, 4, 4), bad.data(), out.data());
    CHECK(out == std::vector<float>{0.f, 0.f, 0.f, 2.f});

    // In place: the input array is the output.
    std::vector<float> x = {6.f, 6.f, 6.f, 6.f};
    a(Run(0, 4, 4, 1), x.data(), x.data());
    CHECK(x == std::vector<float>{3.f, 3.f, 3.f, 5.f});
    std::vector<float> y = {1.f, 3.f, 5.f, 7.f};
    s(Run(0, 4, 4, 1), y.data(), y.data());
    CHECK(y == std::vector<float>{1.f, 2.f, 4.f, 6.f});
  }

  TEST_CASE("frameAverage / frameSmooth: a block is a frame, or a run of a stream of frames "
            "across blocks (#1162)") {
    // framesize 0: a block is a frame of its length.
    buffer first(N);
    buffer second(N);
    for (unsigned int i = 0; i < N; ++i) {
      first.getPtr()[i] = static_cast<float>(i);
      second.getPtr()[i] = static_cast<float>(i) * 3.f + 1.f;
    }
    frameAverage a(2);
    frameSmooth s(2);
    buffer& a1 = a(first);
    REQUIRE(a1.getLength() == N);
    CHECK(a1.getPtr()[10] == 5.f);
    buffer& a2 = a(second);
    CHECK(a2.getPtr()[10] == (10.f + 31.f) / 2.f);
    buffer& s1 = s(second);
    CHECK(s1.getPtr()[0] == 1.f); // a new frame every block
    CHECK(s1.getPtr()[10] == (28.f + 31.f) / 2.f);
    buffer& s2 = s(first);
    CHECK(s2.getPtr()[0] == 0.f);
    // Longer than a standard block: the first STANDARD_BUFFERSIZE samples.
    buffer longer(N + 10);
    CHECK(a(longer).getLength() == N);
    // A run from a block: samples past the run are 0.
    frameSmooth r(2);
    buffer& part = r(Run(0, 10, 512), first);
    CHECK(part.getLength() == N);
    CHECK(part.getPtr()[9] == 8.5f);
    CHECK(part.getPtr()[10] == 0.f);

    // framesize 200 and 48: a stream cut into frames that span blocks, or
    // several to a block; sample i of a frame is bin i.
    for (const UInt size : {200u, 48u}) {
      CAPTURE(size);
      constexpr unsigned int BLOCKS = 12;
      const Frames stream = Noise(1, BLOCKS * N, 5 + size);
      const std::vector<float>& x = stream[0];
      frameAverage sa(3);
      frameSmooth ss(5);
      sa.frameSize(size);
      ss.frameSize(size);
      CHECK(sa.frameSize() == size);
      CHECK(ss.frameSize() == size);
      std::vector<float> outA;
      std::vector<float> outS;
      buffer b(N);
      for (unsigned int block = 0; block < BLOCKS; ++block) {
        std::copy(x.begin() + block * N, x.begin() + (block + 1) * N, b.getPtr());
        buffer& oa = sa(b);
        outA.insert(outA.end(), oa.getPtr(), oa.getPtr() + N);
        buffer& os = ss(b);
        outS.insert(outS.end(), os.getPtr(), os.getPtr() + N);
      }
      Frames frames;
      for (std::size_t at = 0; at + size <= x.size(); at += size)
        frames.emplace_back(x.begin() + static_cast<long>(at),
                            x.begin() + static_cast<long>(at + size));
      for (std::size_t m = 0; m < frames.size(); ++m)
        for (std::size_t k = 0; k < size; ++k) {
          REQUIRE(outA[m * size + k] == doctest::Approx(Average(frames, m, k, 3)).epsilon(1e-6));
          REQUIRE(outS[m * size + k] == doctest::Approx(Trailing(frames[m], k, 5)).epsilon(1e-6));
        }
    }
    // A clamped frame size; a new one starts the next block at bin 0.
    frameSmooth big;
    big.frameSize(1u << 20);
    CHECK(big.frameSize() == frameSmooth::MAXIMUM_FRAME_SIZE);
    frameSmooth restart(2);
    restart.frameSize(1000);
    restart(first);
    restart.frameSize(1000);
    CHECK(restart(second).getPtr()[0] == 1.f);
  }

  TEST_CASE("frameAverage: the running sums stay exact over a long run (#1162)") {
    // 20000 frames of loud noise, then silence: once framecount silent
    // frames are in, every bin is 0 again; on the way, the average matches
    // the closed form.
    constexpr UInt BINS = 64;
    constexpr UInt COUNT = 8;
    frameAverage fn(COUNT, BINS);
    const Frames noise = Noise(20000, BINS, 7, 1000.f);
    Frames last(COUNT, std::vector<float>(BINS, 0.f));
    std::vector<float> frame(BINS);
    std::vector<float> out(BINS);
    float worst = 0.f;
    for (UInt m = 0; m < 20000; ++m) {
      for (UInt k = 0; k < BINS; ++k)
        frame[k] = noise[m][k] * (m % 3 == 0 ? 1e-3f : 1.f);
      last[m % COUNT] = frame;
      fn(Run(0, BINS, BINS, m), frame.data(), out.data());
      if (m < COUNT) continue;
      for (UInt k = 0; k < BINS; ++k) {
        double sum = 0.0;
        for (const auto& f : last)
          sum += f[k];
        worst = std::max(worst, std::abs(out[k] - static_cast<float>(sum / COUNT)));
      }
    }
    CHECK(worst < 1e-3f); // against values of up to 1000
    std::fill(frame.begin(), frame.end(), 0.f);
    for (UInt m = 0; m < COUNT; ++m)
      fn(Run(0, BINS, BINS, 20000 + m), frame.data(), out.data());
    float residue = 0.f;
    for (const float v : out)
      residue = std::max(residue, std::abs(v));
    CHECK(residue < 1e-9f);
  }

  TEST_CASE("frameAverage / frameSmooth: on DSP::stft without a patcher (#1162)") {
    // A stream that repeats every frame (bin-aligned partials, no overlap)
    // gives the same spectrum every frame: averaged over three frames, the
    // first comes out at a third, the second at two thirds, then whole.
    constexpr UInt SIZE = 256;
    stft fx(SIZE, 1);
    fx.window(YSE::DSP::stftWindow::SQUARE, YSE::DSP::stftWindow::SQUARE);
    frameAverage re(3);
    frameAverage im(3);
    frameSmooth flat(5);
    auto average = [&](stftFrame& f) {
      re(f, f.real, f.real);
      im(f, f.imaginary, f.imaginary);
    };
    auto signal = [](std::size_t n) {
      const double t = static_cast<double>(n % SIZE) / SIZE;
      return static_cast<float>(0.5 * std::sin(2 * PI * 3 * t) + 0.25 * std::cos(2 * PI * 17 * t));
    };
    buffer b(N);
    std::vector<float> out;
    for (unsigned int block = 0; block < 16; ++block) {
      for (unsigned int i = 0; i < N; ++i)
        b.getPtr()[i] = signal(block * N + i);
      buffer& o = fx(b, average);
      out.insert(out.end(), o.getPtr(), o.getPtr() + N);
    }
    for (std::size_t n = SIZE; n < out.size(); ++n) {
      const std::size_t frame = n / SIZE - 1; // the frame this sample was made from
      const float scale = frame >= 2 ? 1.f : static_cast<float>(frame + 1) / 3.f;
      REQUIRE(out[n] == doctest::Approx(scale * signal(n - SIZE)).epsilon(1e-4));
    }

    // Smoothed along the frame: every frame a callback gets is the trailing
    // mean of its own real parts.
    stft noisy(SIZE, 4);
    std::vector<float> before(SIZE);
    double worst = 0.0;
    unsigned int seen = 0;
    auto smooth = [&](stftFrame& f) {
      std::copy(f.real, f.real + f.count, before.begin());
      flat(f, f.real, f.real);
      for (UInt k = 0; k < f.count; ++k)
        worst = std::max(worst, std::abs(f.real[k] - Trailing(before, k, 5)));
      seen++;
    };
    const Frames x = Noise(1, 8 * N, 9);
    for (unsigned int block = 0; block < 8; ++block) {
      std::copy(x[0].begin() + block * N, x[0].begin() + (block + 1) * N, b.getPtr());
      noisy(b, smooth);
    }
    CHECK(seen >= 8);
    CHECK(worst < 1e-3);

    // A callback gets whole frames: centred() smooths them in place.
    stft centred(SIZE, 4);
    double off = 0.0;
    unsigned int frames = 0;
    auto centre = [&](stftFrame& f) {
      const std::vector<float> was(f.real, f.real + f.count);
      flat.centred(f, f.real, f.real);
      for (UInt k = 0; k < f.count; ++k)
        off = std::max(off, std::abs(f.real[k] - Centred(was, k, 5)));
      frames++;
    };
    for (unsigned int block = 0; block < 8; ++block) {
      std::copy(x[0].begin() + block * N, x[0].begin() + (block + 1) * N, b.getPtr());
      centred(b, centre);
    }
    CHECK(frames >= 8);
    CHECK(off < 1e-3);
  }

  TEST_CASE("frameAverage / frameSmooth: the output does not depend on the sample rate "
            "(#1162)") {
    const Frames in = Noise(6, 200, 6);
    frameAverage a(3);
    frameSmooth s(4);
    const Frames average = Process(a, in, 128);
    const Frames smooth = Process(s, in, 128);
    for (const UInt rate : {22050u, 96000u}) {
      CAPTURE(rate);
      TestHelpers::ScopedSampleRate scoped(rate);
      frameAverage a2(3);
      frameSmooth s2(4);
      CHECK(Process(a2, in, 128) == average);
      CHECK(Process(s2, in, 128) == smooth);
    }
  }

  TEST_CASE("frameAverage / frameSmooth: processing, setters and clear allocate nothing "
            "(#1162)") {
    frameAverage a;
    frameSmooth s;
    buffer x(N);
    buffer shorter(100);
    std::vector<float> run(N, 0.5f);
    a(x);
    s(x);
    {
      TestHelpers::ProbeScope probe;
      for (int m = 0; m < 4; ++m) {
        a(x);
        s(x);
        a(shorter);
        s(shorter);
        a(Run(256, N, 2048, static_cast<U64>(m)), run.data(), run.data());
        s(Run(256, N, 2048, static_cast<U64>(m)), run.data(), run.data());
        a(Run(0, N, 2048), x);
        s(Run(0, N, 2048), x);
        s.centred(Run(0, N, N), run.data(), run.data());
        s.centred(Run(0, N, N), x);
      }
      a.frameCount(2);
      a.frameSize(300);
      s.smoothness(100);
      s.frameSize(300);
      a(x);
      s(x);
      a.clear();
      s.clear();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }
}
