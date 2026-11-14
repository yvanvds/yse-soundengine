// Tests for YSE::DSP::frameDelta and YSE::DSP::frameAccumulate
// (YseEngine/dsp/spectral/frameDelta.hpp), the per-bin difference and
// running sum behind ~framedelta and ~frameaccum (issue #1161): both in
// closed form over frames; a frame handed over whole and as runs of bins in
// bin order give the same output; accumulate after delta gives the frames
// back; the wrapped sum against phaseWrap of the unwrapped one; capacity,
// frame-size change, clear, non-finite input, in place; the block as a frame;
// a phase vocoder on DSP::stft without a patcher; block-size and sample-rate
// independence; allocation freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>
#include "dsp/spectral/frameDelta.hpp"
#include "dsp/spectral/stft.hpp"
#include "dsp/operators/polar.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::frameAccumulate;
using YSE::DSP::frameDelta;
using YSE::DSP::phaseWrap;
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

  stftFrame Run(UInt firstBin, UInt count, UInt bins) {
    stftFrame f;
    f.firstBin = firstBin;
    f.count = count;
    f.bins = bins;
    return f;
  }

  // Every frame through `fn` as runs of at most `run` bins, in bin order.
  template <class Fn> Frames Process(Fn& fn, const Frames& in, UInt run) {
    Frames out;
    for (const auto& frame : in) {
      const auto bins = static_cast<UInt>(frame.size());
      std::vector<float> o(bins);
      for (UInt first = 0; first < bins; first += run) {
        const UInt count = std::min(run, bins - first);
        fn(Run(first, count, bins), frame.data() + first, o.data() + first);
      }
      out.push_back(o);
    }
    return out;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("frameDelta: each bin minus the same bin of the frame before (#1161)") {
    const Frames in = Noise(6, 300, 1);
    frameDelta fn;
    const Frames out = Process(fn, in, 300);
    for (std::size_t m = 0; m < in.size(); ++m)
      for (std::size_t k = 0; k < in[m].size(); ++k) {
        const float previous = m == 0 ? 0.f : in[m - 1][k];
        REQUIRE(out[m][k] == in[m][k] - previous);
      }
    CHECK(fn.previous(7) == in.back()[7]);
    CHECK(fn.previous(fn.capacity()) == 0.f);
  }

  TEST_CASE("frameAccumulate: each bin plus its sum over the frames before (#1161)") {
    const Frames in = Noise(6, 300, 2);
    frameAccumulate fn;
    CHECK_FALSE(fn.wrap());
    const Frames out = Process(fn, in, 300);
    std::vector<float> sum(300, 0.f);
    for (std::size_t m = 0; m < in.size(); ++m)
      for (std::size_t k = 0; k < in[m].size(); ++k) {
        sum[k] += in[m][k];
        REQUIRE(out[m][k] == sum[k]);
      }
    CHECK(fn.value(11) == sum[11]);
  }

  TEST_CASE("frameDelta / frameAccumulate: runs of bins in bin order equal the whole frame "
            "(#1161)") {
    const Frames in = Noise(8, 512, 3);
    frameDelta wholeDelta;
    frameAccumulate wholeSum;
    const Frames delta = Process(wholeDelta, in, 512);
    const Frames sum = Process(wholeSum, in, 512);
    for (const UInt run : {1u, 7u, 100u, 128u, 256u}) {
      CAPTURE(run);
      frameDelta d;
      frameAccumulate a;
      CHECK(Process(d, in, run) == delta);
      CHECK(Process(a, in, run) == sum);
    }
  }

  TEST_CASE("frameAccumulate after frameDelta gives the frames back (#1161)") {
    const Frames in = Noise(40, 256, 4, 3.14f);
    frameDelta d;
    frameAccumulate a;
    const Frames delta = Process(d, in, 128);
    const Frames back = Process(a, delta, 128);
    float worst = 0.f;
    for (std::size_t m = 0; m < in.size(); ++m)
      for (std::size_t k = 0; k < in[m].size(); ++k)
        worst = std::max(worst, std::abs(back[m][k] - in[m][k]));
    CHECK(worst < 1e-5f);

    // Wrapped, phases come back modulo a turn.
    frameDelta d2;
    frameAccumulate wrapped(frameAccumulate::DEFAULT_CAPACITY, true);
    const Frames turns = Process(wrapped, Process(d2, in, 128), 128);
    worst = 0.f;
    for (std::size_t m = 0; m < in.size(); ++m)
      for (std::size_t k = 0; k < in[m].size(); ++k) {
        const double off = static_cast<double>(turns[m][k]) - static_cast<double>(in[m][k]);
        const double turn = off - 2.0 * PI * std::round(off / (2.0 * PI));
        worst = std::max(worst, static_cast<float>(std::abs(turn)));
      }
    CHECK(worst < 1e-5f);
  }

  TEST_CASE("frameAccumulate: wrapped sums stay in -pi .. pi and follow the unwrapped one "
            "(#1161)") {
    // A phase that keeps advancing: a bin's deviation every frame.
    constexpr unsigned int FRAMES = 2000;
    constexpr unsigned int BINS = 64;
    frameAccumulate fn(BINS, true);
    frameAccumulate unwrapped(BINS);
    CHECK(fn.wrap());
    std::vector<double> exact(BINS, 0.0);
    std::vector<float> in(BINS);
    std::vector<float> out(BINS);
    std::vector<float> grown(BINS);
    std::vector<float> previous(BINS, 0.f);
    const auto turnOff = [](double value, double reference) {
      const double off = value - reference;
      return static_cast<float>(std::abs(off - 2.0 * PI * std::round(off / (2.0 * PI))));
    };
    float worst = 0.f;
    float worstUnwrapped = 0.f;
    for (unsigned int m = 0; m < FRAMES; ++m) {
      for (unsigned int k = 0; k < BINS; ++k)
        in[k] = 0.05f * static_cast<float>(k) + 0.01f;
      fn(Run(0, BINS, BINS), in.data(), out.data());
      unwrapped(Run(0, BINS, BINS), in.data(), grown.data());
      for (unsigned int k = 0; k < BINS; ++k) {
        REQUIRE(out[k] >= -static_cast<float>(PI));
        REQUIRE(out[k] <= static_cast<float>(PI));
        // Each step is phaseWrap of the stored sum plus the input.
        REQUIRE(out[k] == phaseWrap::process(previous[k] + in[k]));
        previous[k] = out[k];
        exact[k] += static_cast<double>(in[k]);
        worst = std::max(worst, turnOff(out[k], exact[k]));
        worstUnwrapped = std::max(worstUnwrapped, turnOff(grown[k], exact[k]));
      }
    }
    // Wrapped, the running phase keeps its precision; unwrapped, the sum
    // grows into the thousands and loses it.
    CHECK(worst < 1e-3f);
    CHECK(worstUnwrapped > 4.f * worst);

    // pi itself gives -pi, as phaseWrap.
    frameAccumulate edge(4, true);
    const float half[1] = {static_cast<float>(PI)};
    float o[1] = {};
    edge(Run(0, 1, 4), half, o);
    CHECK(o[0] == phaseWrap::process(static_cast<float>(PI)));

    // Switched off, the sums grow again from where they are.
    fn.wrap(false);
    const float step[1] = {10.f};
    float stored[1] = {};
    const float before = fn.value(0);
    fn(Run(0, 1, BINS), step, stored);
    CHECK(stored[0] == before + 10.f);
  }

  TEST_CASE("frameDelta / frameAccumulate: bins past the capacity put out 0 (#1161)") {
    frameDelta d(100);
    frameAccumulate a(100);
    CHECK(d.capacity() == 100);
    std::vector<float> in(128, 2.f);
    std::vector<float> out(128, 9.f);
    // A run straddling the capacity, then one past it.
    for (int frame = 0; frame < 2; ++frame) {
      d(Run(64, 64, 256), in.data(), out.data());
      for (unsigned int i = 0; i < 64; ++i)
        REQUIRE(out[i] == (i < 36 ? (frame == 0 ? 2.f : 0.f) : 0.f));
      a(Run(64, 64, 256), in.data(), out.data());
      for (unsigned int i = 0; i < 64; ++i)
        REQUIRE(out[i] == (i < 36 ? 2.f * static_cast<float>(frame + 1) : 0.f));
      std::fill(out.begin(), out.end(), 9.f);
      d(Run(128, 128, 256), in.data(), out.data());
      a(Run(128, 128, 256), in.data(), out.data());
      for (float v : out)
        REQUIRE(v == 0.f);
    }
    // Capacities are clamped.
    CHECK(frameDelta(0).capacity() == 1);
    CHECK(frameAccumulate(1u << 20).capacity() == frameAccumulate::MAXIMUM_CAPACITY);
  }

  TEST_CASE("frameDelta / frameAccumulate: a new frame size and clear start again (#1161)") {
    std::vector<float> in(64, 1.5f);
    std::vector<float> out(64);
    frameDelta d;
    frameAccumulate a;
    for (int m = 0; m < 3; ++m) {
      d(Run(0, 64, 64), in.data(), out.data());
      a(Run(0, 64, 64), in.data(), out.data());
    }
    CHECK(a.value(3) == 4.5f);
    // Same bins, another frame size: a new spectrum, the state cleared.
    d(Run(0, 64, 128), in.data(), out.data());
    CHECK(out[5] == 1.5f);
    a(Run(0, 64, 128), in.data(), out.data());
    CHECK(out[5] == 1.5f);
    // clear: the next frame comes out as it goes in.
    a(Run(0, 64, 128), in.data(), out.data());
    CHECK(out[5] == 3.f);
    d.clear();
    a.clear();
    CHECK(d.previous(5) == 0.f);
    CHECK(a.value(5) == 0.f);
    d(Run(0, 64, 128), in.data(), out.data());
    CHECK(out[5] == 1.5f);
    a(Run(0, 64, 128), in.data(), out.data());
    CHECK(out[5] == 1.5f);
  }

  TEST_CASE("frameDelta / frameAccumulate: non-finite input counts as 0; in place works "
            "(#1161)") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    std::vector<float> in = {1.f, 2.f, 3.f};
    std::vector<float> out(3);
    frameDelta d;
    frameAccumulate a;
    d(Run(0, 3, 3), in.data(), out.data());
    a(Run(0, 3, 3), in.data(), out.data());
    std::vector<float> bad = {nan, inf, -inf};
    d(Run(0, 3, 3), bad.data(), out.data());
    CHECK(out == std::vector<float>{-1.f, -2.f, -3.f});
    a(Run(0, 3, 3), bad.data(), out.data());
    CHECK(out == std::vector<float>{1.f, 2.f, 3.f});

    // In place: the input array is the output.
    std::vector<float> x = {5.f, 6.f, 7.f};
    d(Run(0, 3, 3), x.data(), x.data());
    CHECK(x == std::vector<float>{5.f, 6.f, 7.f}); // previous was 0 after the bad frame
    std::vector<float> y = {1.f, 1.f, 1.f};
    a(Run(0, 3, 3), y.data(), y.data());
    CHECK(y == std::vector<float>{2.f, 3.f, 4.f});
  }

  TEST_CASE("frameDelta / frameAccumulate: a block is a frame of its own length (#1161)") {
    buffer first(N);
    buffer second(N);
    for (unsigned int i = 0; i < N; ++i) {
      first.getPtr()[i] = static_cast<float>(i);
      second.getPtr()[i] = static_cast<float>(i) * 3.f + 1.f;
    }
    frameDelta d;
    frameAccumulate a;
    CHECK(TestHelpers::buffersNearlyEqual(d(first), first, 0.f));
    CHECK(TestHelpers::buffersNearlyEqual(a(first), first, 0.f));
    buffer& delta = d(second);
    buffer& sum = a(second);
    REQUIRE(delta.getLength() == N);
    for (unsigned int i = 0; i < N; ++i) {
      CHECK(delta.getPtr()[i] == second.getPtr()[i] - first.getPtr()[i]);
      CHECK(sum.getPtr()[i] == second.getPtr()[i] + first.getPtr()[i]);
    }
    // Another block length is another frame size: the state starts again.
    buffer shorter(64);
    for (unsigned int i = 0; i < 64; ++i)
      shorter.getPtr()[i] = 2.f;
    buffer& s = d(shorter);
    CHECK(s.getLength() == 64);
    CHECK(s.getPtr()[10] == 2.f);
    // Longer than a standard block: the first STANDARD_BUFFERSIZE samples.
    buffer longer(N + 10);
    CHECK(a(longer).getLength() == N);

    // A run from a block: samples past the run are 0.
    frameDelta r;
    buffer& part = r(Run(0, 10, 512), first);
    CHECK(part.getLength() == N);
    CHECK(part.getPtr()[9] == 9.f);
    CHECK(part.getPtr()[10] == 0.f);
  }

  TEST_CASE("frameDelta / frameAccumulate: a phase vocoder on DSP::stft without a patcher "
            "(#1161)") {
    // Phases differenced, then accumulated, then turned back into bins: the
    // spectrum is unchanged, so the input comes back `size` samples later.
    constexpr UInt SIZE = 1024;
    stft fx(SIZE, 4);
    frameDelta d;
    frameAccumulate a(frameAccumulate::DEFAULT_CAPACITY, true);
    std::vector<float> phase(SIZE);
    std::vector<float> magnitude(SIZE);
    auto vocoder = [&](stftFrame& f) {
      for (UInt k = 0; k < f.count; ++k) {
        magnitude[k] = std::hypot(f.real[k], f.imaginary[k]);
        phase[k] = std::atan2(f.imaginary[k], f.real[k]);
      }
      d(f, phase.data(), phase.data());
      a(f, phase.data(), phase.data());
      for (UInt k = 0; k < f.count; ++k) {
        f.real[k] = magnitude[k] * std::cos(phase[k]);
        f.imaginary[k] = magnitude[k] * std::sin(phase[k]);
      }
    };
    buffer b(N);
    std::vector<float> in;
    std::vector<float> out;
    for (unsigned int block = 0; block < 64; ++block) {
      for (unsigned int i = 0; i < N; ++i) {
        const auto n = static_cast<double>(in.size());
        const auto x = static_cast<float>(0.5 * std::sin(0.031 * n) + 0.25 * std::sin(0.4 * n));
        b.getPtr()[i] = x;
        in.push_back(x);
      }
      buffer& o = fx(b, vocoder);
      for (unsigned int i = 0; i < N; ++i)
        out.push_back(o.getPtr()[i]);
    }
    float worst = 0.f;
    for (std::size_t n = 2 * SIZE; n < out.size(); ++n)
      worst = std::max(worst, std::abs(out[n] - in[n - SIZE]));
    CHECK(worst < 1e-4f);
  }

  TEST_CASE("frameDelta / frameAccumulate: the output does not depend on the sample rate "
            "(#1161)") {
    const Frames in = Noise(5, 200, 6);
    frameDelta d;
    frameAccumulate a(frameAccumulate::DEFAULT_CAPACITY, true);
    const Frames delta = Process(d, in, 128);
    const Frames sum = Process(a, in, 128);
    for (const UInt rate : {22050u, 96000u}) {
      CAPTURE(rate);
      TestHelpers::ScopedSampleRate scoped(rate);
      frameDelta d2;
      frameAccumulate a2(frameAccumulate::DEFAULT_CAPACITY, true);
      CHECK(Process(d2, in, 128) == delta);
      CHECK(Process(a2, in, 128) == sum);
    }
  }

  TEST_CASE("frameDelta / frameAccumulate: processing and clear allocate nothing (#1161)") {
    frameDelta d;
    frameAccumulate a;
    buffer x(N);
    buffer shorter(100);
    std::vector<float> run(N, 0.5f);
    d(x);
    a(x);
    {
      TestHelpers::ProbeScope probe;
      for (int m = 0; m < 4; ++m) {
        d(x);
        a(x);
        d(shorter);
        a(shorter);
        d(Run(256, N, 2048), run.data(), run.data());
        a(Run(256, N, 2048), run.data(), run.data());
        d(Run(0, N, 2048), x);
        a(Run(0, N, 2048), x);
      }
      a.wrap(true);
      a(x);
      d.clear();
      a.clear();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }
}
