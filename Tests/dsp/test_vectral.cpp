// Tests for YSE::DSP::vectral (YseEngine/dsp/spectral/vectral.hpp), the
// per-index smoothing of frames behind ~vectral (issue #1132): the defaults
// pass frames through; rampsmooth, slide and deltaclip in closed form across
// frames, never across the samples of one frame; every index matches the
// time-domain class run on that index alone; the input index shifting the
// frame; indexes outside the frame, fractional and NaN; non-finite settings
// and input; mode switches keeping the stored values; size and clear;
// block-size and sample-rate independence; allocation freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>
#include "dsp/spectral/vectral.hpp"
#include "dsp/filters/rampSmooth.hpp"
#include "dsp/filters/slewLimiter.hpp"
#include "dsp/filters/slide.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::vectral;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // A stream of frames: the values of `frames`, one after another, with both
  // indexes counting 0 .. size - 1 within each frame (the input index offset
  // by `shift`). Processed in blocks of `block` samples; returns the output
  // cut back into frames.
  std::vector<std::vector<float>> Run(vectral& fn, const std::vector<std::vector<float>>& frames,
                                      unsigned int block = N, int shift = 0) {
    std::vector<float> outIndex;
    std::vector<float> inIndex;
    std::vector<float> values;
    for (const std::vector<float>& frame : frames)
      for (std::size_t k = 0; k < frame.size(); ++k) {
        outIndex.push_back(static_cast<float>(k));
        inIndex.push_back(static_cast<float>(static_cast<int>(k) + shift));
        values.push_back(frame[k]);
      }
    std::vector<float> out;
    for (std::size_t start = 0; start < values.size(); start += block) {
      const auto length =
          static_cast<unsigned int>(std::min<std::size_t>(block, values.size() - start));
      buffer o(length);
      buffer i(length);
      buffer x(length);
      for (unsigned int n = 0; n < length; ++n) {
        o.getPtr()[n] = outIndex[start + n];
        i.getPtr()[n] = inIndex[start + n];
        x.getPtr()[n] = values[start + n];
      }
      buffer& result = fn(o, i, x);
      REQUIRE(result.getLength() == length);
      for (unsigned int n = 0; n < length; ++n)
        out.push_back(result.getPtr()[n]);
    }
    std::vector<std::vector<float>> cut;
    std::size_t at = 0;
    for (const std::vector<float>& frame : frames) {
      cut.emplace_back(out.begin() + static_cast<std::ptrdiff_t>(at),
                       out.begin() + static_cast<std::ptrdiff_t>(at + frame.size()));
      at += frame.size();
    }
    return cut;
  }

  // `count` frames of `size` values, every one `value`.
  std::vector<std::vector<float>> Constant(std::size_t count, std::size_t size, float value) {
    return std::vector<std::vector<float>>(count, std::vector<float>(size, value));
  }

  std::vector<std::vector<float>> Noise(std::size_t count, std::size_t size, unsigned int seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-1.f, 1.f);
    std::vector<std::vector<float>> frames(count, std::vector<float>(size));
    for (std::vector<float>& frame : frames)
      for (float& v : frame)
        v = dist(rng);
    return frames;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("vectral: the defaults pass the frames through (#1132)") {
    vectral fn;
    CHECK(fn.capacity() == 512);
    CHECK(fn.size() == 512);
    CHECK(fn.mode() == vectral::RAMPSMOOTH);
    CHECK(fn.rampUp() == 1.f);
    CHECK(fn.rampDown() == 1.f);
    const std::vector<std::vector<float>> in = Noise(4, 512, 1);
    CHECK(Run(fn, in) == in);
  }

  TEST_CASE("vectral: rampsmooth ramps every index in a straight line over frames (#1132)") {
    vectral fn(8);
    fn.rampsmooth(4.f, 8.f);
    // Index k steps from 0 to k + 1 in frame 0 and stays there: it rises
    // over 4 frames, a quarter of the way each, then holds. Within one frame
    // the indexes do not smooth into each other.
    std::vector<float> high(8);
    for (std::size_t k = 0; k < 8; ++k)
      high[k] = static_cast<float>(k + 1);
    std::vector<std::vector<float>> frames(6, high);
    const auto rising = Run(fn, frames);
    for (std::size_t m = 0; m < 6; ++m)
      for (std::size_t k = 0; k < 8; ++k) {
        CAPTURE(m);
        CAPTURE(k);
        const double expected = high[k] * std::min<double>(static_cast<double>(m + 1), 4.0) / 4.0;
        CHECK(rising[m][k] == doctest::Approx(expected).epsilon(1e-6));
      }
    // Falling back to 0 over 8 frames, landing exactly.
    const auto falling = Run(fn, Constant(10, 8, 0.f));
    for (std::size_t m = 0; m < 10; ++m)
      for (std::size_t k = 0; k < 8; ++k) {
        CAPTURE(m);
        CAPTURE(k);
        const double expected =
            high[k] * (1.0 - std::min<double>(static_cast<double>(m + 1), 8.0) / 8.0);
        CHECK(std::fabs(falling[m][k] - expected) <= 1e-5);
      }
    for (std::size_t k = 0; k < 8; ++k)
      CHECK(fn.value(static_cast<UInt>(k)) == 0.f);
  }

  TEST_CASE("vectral: slide moves every index part of the way each frame (#1132)") {
    vectral fn(4);
    fn.slide(4.f, 2.f);
    CHECK(fn.mode() == vectral::SLIDE);
    CHECK(fn.slideUp() == 4.f);
    CHECK(fn.slideDown() == 2.f);
    // Rising to 1 by a quarter of the distance per frame:
    // y(m) = 1 - (3/4)^(m + 1).
    const auto rising = Run(fn, Constant(5, 4, 1.f));
    double y = 0.0;
    for (std::size_t m = 0; m < 5; ++m) {
      y += (1.0 - y) / 4.0;
      for (std::size_t k = 0; k < 4; ++k) {
        CAPTURE(m);
        CHECK(rising[m][k] ==
              doctest::Approx(1.0 - std::pow(0.75, static_cast<double>(m + 1))).epsilon(1e-6));
      }
    }
    // Falling to -1 by half the distance per frame.
    const auto falling = Run(fn, Constant(5, 4, -1.f));
    for (std::size_t m = 0; m < 5; ++m) {
      y += (-1.0 - y) / 2.0;
      for (std::size_t k = 0; k < 4; ++k) {
        CAPTURE(m);
        CHECK(falling[m][k] == doctest::Approx(y).epsilon(1e-6));
      }
    }
  }

  TEST_CASE("vectral: deltaclip limits the change of every index per frame (#1132)") {
    vectral fn(4);
    fn.deltaclip(-0.1f, 0.25f);
    CHECK(fn.mode() == vectral::DELTACLIP);
    CHECK(fn.clipLowest() == -0.1f);
    CHECK(fn.clipHighest() == 0.25f);
    // 0 to 1 at 0.25 per frame, then held at the input exactly.
    const auto rising = Run(fn, Constant(6, 4, 1.f));
    const float up[6] = {0.25f, 0.5f, 0.75f, 1.f, 1.f, 1.f};
    for (std::size_t m = 0; m < 6; ++m)
      for (std::size_t k = 0; k < 4; ++k) {
        CAPTURE(m);
        CHECK(rising[m][k] == up[m]);
      }
    // Back to 0.75 at 0.1 per frame: 0.9, 0.8, then the input.
    const auto falling = Run(fn, Constant(4, 4, 0.75f));
    const double down[4] = {0.9, 0.8, 0.75, 0.75};
    for (std::size_t m = 0; m < 4; ++m)
      for (std::size_t k = 0; k < 4; ++k) {
        CAPTURE(m);
        CHECK(falling[m][k] == doctest::Approx(down[m]).epsilon(1e-6));
      }
  }

  TEST_CASE("vectral: every index is the time-domain class run on that index alone (#1132)") {
    constexpr std::size_t SIZE = 37; // frames that straddle block boundaries
    const auto in = Noise(40, SIZE, 7);

    SUBCASE("rampsmooth") {
      vectral fn(SIZE);
      fn.rampsmooth(3.f, 7.f);
      std::vector<YSE::DSP::rampSmooth> ref(SIZE);
      for (auto& r : ref)
        r.up(3.f).down(7.f);
      const auto out = Run(fn, in);
      for (std::size_t m = 0; m < in.size(); ++m)
        for (std::size_t k = 0; k < SIZE; ++k)
          REQUIRE(out[m][k] == ref[k].process(in[m][k]));
    }
    SUBCASE("slide") {
      vectral fn(SIZE);
      fn.slide(2.5f, 9.f);
      std::vector<YSE::DSP::slide> ref(SIZE);
      for (auto& r : ref)
        r.up(2.5f).down(9.f);
      const auto out = Run(fn, in);
      for (std::size_t m = 0; m < in.size(); ++m)
        for (std::size_t k = 0; k < SIZE; ++k)
          REQUIRE(out[m][k] == ref[k].process(in[m][k]));
    }
    SUBCASE("deltaclip") {
      vectral fn(SIZE);
      fn.deltaclip(-0.05f, 0.2f);
      std::vector<YSE::DSP::slewLimiter> ref(SIZE);
      for (auto& r : ref)
        r.minimum(-0.05f).maximum(0.2f);
      const auto out = Run(fn, in);
      for (std::size_t m = 0; m < in.size(); ++m)
        for (std::size_t k = 0; k < SIZE; ++k)
          REQUIRE(out[m][k] == ref[k].process(in[m][k]));
    }
  }

  TEST_CASE("vectral: an input index ahead of the output index shifts the frame (#1132)") {
    vectral fn(8);
    std::vector<float> frame(8);
    for (std::size_t k = 0; k < 8; ++k)
      frame[k] = static_cast<float>(10 + k);
    // The input index is k + 2: value k is stored at k + 2, and output index
    // k reads what was stored there. Within one frame, indexes 0 and 1 are
    // read before anything is stored at them this frame.
    const auto first = Run(fn, {frame}, N, 2);
    CHECK(first[0][0] == 0.f);
    CHECK(first[0][1] == 0.f);
    for (std::size_t k = 2; k < 8; ++k)
      CHECK(first[0][k] == frame[k - 2]);
    // Values 6 and 7 fell off the end and nothing is ever stored at 0 and
    // 1: the next frame is the same.
    const auto second = Run(fn, {frame}, N, 2);
    CHECK(second[0][0] == 0.f);
    CHECK(second[0][1] == 0.f);
    for (std::size_t k = 2; k < 8; ++k)
      CHECK(second[0][k] == frame[k - 2]);
  }

  TEST_CASE("vectral: indexes outside the frame, fractional and NaN (#1132)") {
    vectral fn(4);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    // Fractional indexes use their whole part.
    CHECK(fn.process(2.9f, 2.9f, 5.f) == 5.f);
    CHECK(fn.value(2) == 5.f);
    // An input index outside the frame stores nothing; an output index
    // outside reads 0.
    CHECK(fn.process(2.f, 4.f, 9.f) == 5.f);
    CHECK(fn.process(2.f, -0.5f, 9.f) == 5.f);
    CHECK(fn.process(2.f, nan, 9.f) == 5.f);
    CHECK(fn.process(2.f, std::numeric_limits<float>::infinity(), 9.f) == 5.f);
    CHECK(fn.process(4.f, 2.f, 5.f) == 0.f);
    CHECK(fn.process(-1.f, 2.f, 5.f) == 0.f);
    CHECK(fn.process(nan, 2.f, 5.f) == 0.f);
    for (UInt k = 0; k < 4; ++k)
      CHECK(fn.value(k) == (k == 2 ? 5.f : 0.f));
    CHECK(fn.value(4) == 0.f);
  }

  TEST_CASE("vectral: non-finite settings are ignored, a non-finite input stores nothing "
            "(#1132)") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    vectral fn(2);
    fn.rampsmooth(4.f, 5.f).rampsmooth(nan, inf);
    CHECK(fn.rampUp() == 4.f);
    CHECK(fn.rampDown() == 5.f);
    fn.slide(3.f, 6.f).slide(inf, nan);
    CHECK(fn.slideUp() == 3.f);
    CHECK(fn.slideDown() == 6.f);
    // Infinite limits are limits: the change passes whole.
    fn.deltaclip(-inf, inf).deltaclip(nan, nan);
    CHECK(fn.clipLowest() == -inf);
    CHECK(fn.clipHighest() == inf);
    CHECK(fn.process(0.f, 0.f, 0.75f) == 0.75f);

    for (const auto mode : {vectral::RAMPSMOOTH, vectral::SLIDE, vectral::DELTACLIP}) {
      CAPTURE(static_cast<int>(mode));
      if (mode == vectral::RAMPSMOOTH) fn.rampsmooth(1.f, 1.f);
      if (mode == vectral::SLIDE) fn.slide(1.f, 1.f);
      if (mode == vectral::DELTACLIP) fn.deltaclip(-inf, inf);
      CHECK(fn.process(0.f, 0.f, 0.25f) == 0.25f);
      CHECK(fn.process(0.f, 0.f, nan) == 0.25f);
      CHECK(fn.process(0.f, 0.f, inf) == 0.25f);
    }
  }

  TEST_CASE("vectral: a new mode carries on from the stored values (#1132)") {
    vectral fn(1);
    fn.slide(2.f, 2.f);
    CHECK(fn.process(0.f, 0.f, 1.f) == 0.5f);
    CHECK(fn.process(0.f, 0.f, 1.f) == 0.75f);
    // rampsmooth from 0.75: the input it heads for differs from the stored
    // value, so a ramp of 2 frames starts from 0.75.
    fn.rampsmooth(2.f, 2.f);
    CHECK(fn.process(0.f, 0.f, 1.f) == doctest::Approx(0.875));
    CHECK(fn.process(0.f, 0.f, 1.f) == 1.f);
    // A value equal to the stored one after a slide starts no ramp.
    fn.slide(1.f, 1.f);
    CHECK(fn.process(0.f, 0.f, 0.5f) == 0.5f);
    fn.rampsmooth(4.f, 4.f);
    CHECK(fn.process(0.f, 0.f, 0.5f) == 0.5f);
    CHECK(fn.process(0.f, 0.f, 0.5f) == 0.5f);
    // deltaclip from the middle of a ramp.
    CHECK(fn.process(0.f, 0.f, 1.5f) == 0.75f);
    fn.deltaclip(-1.f, 0.1f);
    CHECK(fn.process(0.f, 0.f, 1.5f) == doctest::Approx(0.85));
  }

  TEST_CASE("vectral: size and clear (#1132)") {
    CHECK(vectral(0).capacity() == 1);
    CHECK(vectral(vectral::MAXIMUM_SIZE + 5).capacity() == vectral::MAXIMUM_SIZE);

    vectral fn(16);
    CHECK(fn.process(5.f, 5.f, 3.f) == 3.f);
    // The same size keeps the frame.
    fn.size(16);
    CHECK(fn.value(5) == 3.f);
    // A new size clears it and limits the indexes.
    fn.size(8);
    CHECK(fn.size() == 8);
    CHECK(fn.value(5) == 0.f);
    CHECK(fn.process(8.f, 8.f, 1.f) == 0.f);
    CHECK(fn.process(7.f, 7.f, 1.f) == 1.f);
    // Clamped to 1 .. the capacity.
    fn.size(100);
    CHECK(fn.size() == 16);
    fn.size(0);
    CHECK(fn.size() == 1);
    fn.size(16);
    CHECK(fn.process(15.f, 15.f, 2.f) == 2.f);
    // clear sets every value to 0, also mid-ramp: the ramp starts again
    // from 0.
    fn.rampsmooth(4.f, 4.f);
    CHECK(fn.process(3.f, 3.f, 4.f) == 1.f);
    fn.clear();
    CHECK(fn.value(15) == 0.f);
    CHECK(fn.value(3) == 0.f);
    CHECK(fn.process(3.f, 3.f, 4.f) == 1.f);
  }

  TEST_CASE("vectral: blocks are as long as the shortest input, at most a standard buffer "
            "(#1132)") {
    vectral fn(4);
    buffer o(10);
    buffer i(7);
    buffer x(9);
    CHECK(fn(o, i, x).getLength() == 7);
    buffer longO(N + 30);
    buffer longI(N + 30);
    buffer longX(N + 30);
    CHECK(fn(longO, longI, longX).getLength() == N);
  }

  TEST_CASE("vectral: the output does not depend on the block size or the sample rate "
            "(#1132)") {
    const auto in = Noise(30, 100, 3);
    vectral reference(100);
    reference.slide(3.f, 5.f);
    const auto expected = Run(reference, in, N);

    for (const unsigned int block : {1u, 37u, 100u, N - 1}) {
      CAPTURE(block);
      vectral fn(100);
      fn.slide(3.f, 5.f);
      CHECK(Run(fn, in, block) == expected);
    }
    for (const UInt rate : {22050u, 96000u}) {
      CAPTURE(rate);
      vectral fn(100);
      TestHelpers::ScopedSampleRate scoped(rate);
      fn.slide(3.f, 5.f);
      CHECK(Run(fn, in, N) == expected);
    }
  }

  TEST_CASE("vectral: processing, settings, size and clear allocate nothing (#1132)") {
    vectral fn(1024);
    buffer o(N);
    buffer i(N);
    buffer x(N);
    for (unsigned int n = 0; n < N; ++n) {
      o.getPtr()[n] = static_cast<float>(n);
      i.getPtr()[n] = static_cast<float>(n);
      x.getPtr()[n] = std::sin(static_cast<float>(n) * 0.1f);
    }
    buffer shortO(100);
    buffer shortI(100);
    buffer shortX(100);
    fn(o, i, x); // first call
    {
      TestHelpers::ProbeScope probe;
      fn.rampsmooth(4.f, 8.f);
      fn(o, i, x);
      fn.slide(2.f, 10.f);
      fn(o, i, x);
      fn.deltaclip(-0.1f, 0.1f);
      fn(shortO, shortI, shortX);
      fn(o, i, x);
      fn.size(256);
      fn(o, i, x);
      fn.size(1024);
      fn.clear();
      fn(o, i, x);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
