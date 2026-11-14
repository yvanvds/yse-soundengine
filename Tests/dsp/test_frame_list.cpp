// Tests for YSE::DSP::frameSource and YSE::DSP::frameSnapshot
// (YseEngine/dsp/spectral/frameList.hpp), what ~frame and ~framesnap turn
// lists into frames and frames into lists with (issue #1163): the values,
// the fill value, offsets that build a frame from pieces or skip bins, the
// capacity and non-finite values; runs of bins equal the whole frame; the
// bins above Nyquist of a full-spectrum frame, mirrored or 0; the snapshot
// of whole frames and runs, a gap that drops a frame, the capacity and
// clear; the block as a frame; both on DSP::stft without a patcher (a frame
// captured and put back is the frame, a spectrum frozen from a snapshot);
// sample-rate independence; allocation freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>
#include "dsp/spectral/frameList.hpp"
#include "dsp/spectral/stft.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::frameSnapshot;
using YSE::DSP::frameSource;
using YSE::DSP::stft;
using YSE::DSP::stftFrame;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  std::vector<float> Noise(unsigned int count, unsigned int seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> d(-3.f, 3.f);
    std::vector<float> v(count);
    for (float& x : v)
      x = d(rng);
    return v;
  }

  stftFrame Run(UInt firstBin, UInt count, UInt bins, U64 index = 0) {
    stftFrame f;
    f.firstBin = firstBin;
    f.count = count;
    f.bins = bins;
    f.size = 2 * bins;
    f.index = index;
    return f;
  }

  // A whole frame of `bins` from a source, put out in runs of `step`.
  std::vector<float> Put(frameSource& s, UInt bins, UInt step, bool full = false, UInt size = 0) {
    std::vector<float> out(bins, -99.f);
    for (UInt first = 0; first < bins; first += step) {
      stftFrame f = Run(first, std::min(step, bins - first), bins);
      f.fullSpectrum = full;
      f.size = size > 0 ? size : 2 * bins;
      s(f, out.data() + first);
    }
    return out;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("frameSource: a list's values bin by bin, the fill value past them (#1163)") {
    frameSource s;
    CHECK(s.capacity() == frameSource::DEFAULT_CAPACITY);
    CHECK(s.length() == 0);
    // Nothing set: the fill value, 0.
    for (float v : Put(s, 300, 300))
      CHECK(v == 0.f);

    const float values[] = {1.f, -2.f, 3.5f, 0.25f};
    s.set(values, 4);
    CHECK(s.length() == 4);
    std::vector<float> out = Put(s, 300, 300);
    for (UInt k = 0; k < 4; ++k)
      CHECK(out[k] == values[k]);
    for (UInt k = 4; k < 300; ++k)
      REQUIRE(out[k] == 0.f);

    // fill: the unset bins only.
    s.fill(0.5f);
    CHECK(s.fill() == 0.5f);
    out = Put(s, 300, 300);
    CHECK(out[3] == 0.25f);
    CHECK(out[4] == 0.5f);
    CHECK(out[299] == 0.5f);
    s.fill(std::numeric_limits<float>::infinity());
    CHECK(s.fill() == 0.f);

    // A shorter list from 0 replaces the frame.
    const float two[] = {7.f, 8.f};
    s.set(two, 2);
    s.fill(-1.f);
    out = Put(s, 10, 10);
    CHECK(out[0] == 7.f);
    CHECK(out[1] == 8.f);
    CHECK(out[2] == -1.f);

    // clear: the whole frame is the fill value.
    s.clear();
    CHECK(s.length() == 0);
    CHECK(s.value(0) == -1.f);
  }

  TEST_CASE("frameSource: offsets build a frame from pieces and skip bins (#1163)") {
    // 700 values in pieces of 256, 256 and 188, each from where the last
    // ended: the frame of all 700.
    const std::vector<float> all = Noise(700, 1);
    frameSource s;
    s.set(all.data(), 256, 0);
    s.set(all.data() + 256, 256, 256);
    s.set(all.data() + 512, 188, 512);
    CHECK(s.length() == 700);
    s.fill(9.f);
    std::vector<float> out = Put(s, 1024, 128);
    for (UInt k = 0; k < 700; ++k)
      REQUIRE(out[k] == all[k]);
    for (UInt k = 700; k < 1024; ++k)
      REQUIRE(out[k] == 9.f);

    // A piece from the middle keeps the bins before it and unsets the ones
    // after.
    const float mid[] = {-5.f, -6.f};
    s.set(mid, 2, 100);
    CHECK(s.length() == 102);
    CHECK(s.value(99) == all[99]);
    CHECK(s.value(100) == -5.f);
    CHECK(s.value(101) == -6.f);
    CHECK(s.value(102) == 9.f);

    // An offset past the last bin set skips the bins between: they put out
    // the fill value, whatever it is later.
    const float far[] = {4.f};
    s.set(far, 1, 200);
    CHECK(s.length() == 201);
    CHECK(s.value(101) == -6.f);
    CHECK(s.value(102) == 9.f);
    CHECK(s.value(199) == 9.f);
    CHECK(s.value(200) == 4.f);
    s.fill(2.f);
    CHECK(s.value(150) == 2.f);
    CHECK(s.value(201) == 2.f);
    // Setting them later sets them.
    const float back[] = {1.f, 1.f};
    s.set(back, 2, 150);
    CHECK(s.value(150) == 1.f);
    CHECK(s.value(152) == 2.f);
  }

  TEST_CASE("frameSource: the capacity, non-finite values, copy (#1163)") {
    frameSource s(10);
    CHECK(s.capacity() == 10);
    CHECK(frameSource(0).capacity() == 1);
    CHECK(frameSource(1u << 20).capacity() == frameSource::MAXIMUM_CAPACITY);
    const std::vector<float> v = Noise(16, 2);
    s.set(v.data(), 16);
    CHECK(s.length() == 10);
    s.fill(0.5f);
    std::vector<float> out = Put(s, 16, 16);
    for (UInt k = 0; k < 10; ++k)
      CHECK(out[k] == v[k]);
    for (UInt k = 10; k < 16; ++k)
      CHECK(out[k] == 0.5f);
    // Past the capacity, an offset keeps nothing.
    s.set(v.data(), 3, 12);
    CHECK(s.length() == 10);
    CHECK(s.value(9) == v[9]);

    const float odd[] = {std::numeric_limits<float>::quiet_NaN(),
                         std::numeric_limits<float>::infinity(), 3.f};
    s.set(odd, 3);
    CHECK(s.value(0) == 0.f);
    CHECK(s.value(1) == 0.f);
    CHECK(s.value(2) == 3.f);

    // copy: values, fill and mirror, within the capacity.
    frameSource big;
    big.set(v.data(), 16);
    big.fill(-3.f);
    big.mirror(true);
    s.copy(big);
    CHECK(s.length() == 10);
    CHECK(s.value(9) == v[9]);
    CHECK(s.fill() == -3.f);
    CHECK(s.mirror());
    frameSource wide;
    wide.copy(s);
    CHECK(wide.length() == 10);
    CHECK(wide.value(10) == -3.f);
  }

  TEST_CASE("frameSource: runs of any length give the whole frame (#1163)") {
    const std::vector<float> v = Noise(600, 3);
    frameSource s;
    s.set(v.data(), 600);
    s.fill(0.75f);
    const std::vector<float> whole = Put(s, 1000, 1000);
    for (UInt step : {1u, 7u, 128u, 256u, 999u}) {
      CAPTURE(step);
      CHECK(Put(s, 1000, step) == whole);
    }
  }

  TEST_CASE("frameSource: the bins above Nyquist of a full-spectrum frame are 0, or mirror "
            "(#1163)") {
    // A full spectrum of 512: bins 0 .. 511, Nyquist at 256.
    const std::vector<float> v = Noise(512, 4);
    frameSource s;
    s.set(v.data(), 512);
    std::vector<float> out = Put(s, 512, 128, true, 512);
    for (UInt k = 0; k <= 256; ++k)
      REQUIRE(out[k] == v[k]);
    for (UInt k = 257; k < 512; ++k)
      REQUIRE(out[k] == 0.f);
    s.mirror(true);
    CHECK(s.mirror());
    out = Put(s, 512, 100, true, 512);
    for (UInt k = 0; k <= 256; ++k)
      REQUIRE(out[k] == v[k]);
    for (UInt k = 257; k < 512; ++k)
      REQUIRE(out[k] == v[512 - k]);
    // A half spectrum has nothing above Nyquist: mirror changes nothing.
    out = Put(s, 256, 128);
    for (UInt k = 0; k < 256; ++k)
      REQUIRE(out[k] == v[k]);
    // Mirrored bins of a short list mirror the fill value.
    s.set(v.data(), 4);
    s.fill(2.f);
    out = Put(s, 512, 512, true, 512);
    CHECK(out[511] == v[1]);
    CHECK(out[300] == 2.f);
  }

  TEST_CASE("frameSource: a block is a frame of its length (#1163)") {
    frameSource s;
    const std::vector<float> v = Noise(200, 5);
    s.set(v.data(), 200);
    buffer& b = s();
    CHECK(b.getLength() == N);
    for (UInt i = 0; i < N; ++i)
      REQUIRE(b.getPtr()[i] == v[i]);
    // Every block the same.
    CHECK(TestHelpers::buffersNearlyEqual(s(), b, 0.f));
    CHECK(s(40).getLength() == 40);
    CHECK(s(N + 10).getLength() == N);
    // A run into the class's own output, as long as the run.
    buffer& r = s(Run(130, 30, 256));
    CHECK(r.getLength() == 30);
    CHECK(r.getPtr()[0] == v[130]);
    CHECK(r.getPtr()[29] == v[159]);
  }

  TEST_CASE("frameSnapshot: keeps the last whole frame, given whole or in runs (#1163)") {
    frameSnapshot s;
    CHECK(s.capacity() == frameSnapshot::DEFAULT_CAPACITY);
    CHECK(s.frames() == 0);
    CHECK(s.size() == 0);
    CHECK(s.bins() == 0);

    const std::vector<float> a = Noise(512, 6);
    s(Run(0, 512, 512), a.data());
    CHECK(s.frames() == 1);
    CHECK(s.size() == 512);
    CHECK(s.bins() == 512);
    for (UInt k = 0; k < 512; ++k)
      REQUIRE(s.value(k) == a[k]);
    CHECK(s.value(512) == 0.f);

    // In runs of 1 .. 256 bins: the frame once it is whole,
    // the one before until then.
    U64 index = 1;
    for (UInt step : {1u, 5u, 128u, 256u}) {
      CAPTURE(step);
      const std::vector<float> b = Noise(512, 7 + step);
      const std::vector<float> before(s.values(), s.values() + 512);
      for (UInt first = 0; first < 512; first += step) {
        const UInt count = std::min(step, 512 - first);
        REQUIRE(std::equal(before.begin(), before.end(), s.values()));
        s(Run(first, count, 512, index), b.data() + first);
      }
      index++;
      CHECK(s.frames() == index);
      for (UInt k = 0; k < 512; ++k)
        REQUIRE(s.value(k) == b[k]);
    }

    std::vector<float> copied(600, -1.f);
    CHECK(s.copy(copied.data(), 600) == 512);
    CHECK(std::equal(copied.begin(), copied.begin() + 512, s.values()));
    CHECK(copied[512] == -1.f);
    CHECK(s.copy(copied.data(), 10) == 10);
  }

  TEST_CASE("frameSnapshot: a gap or a new frame drops the frame being taken (#1163)") {
    frameSnapshot s;
    const std::vector<float> a = Noise(256, 8);
    const std::vector<float> b = Noise(256, 9);
    s(Run(0, 256, 256, 0), a.data());
    REQUIRE(s.frames() == 1);
    // A gap: bins 128 .. 255 without 0 .. 127.
    s(Run(128, 128, 256, 1), b.data() + 128);
    CHECK(s.frames() == 1);
    CHECK(s.value(200) == a[200]);
    // Half a frame, then a new frame starts: the half is dropped.
    s(Run(0, 128, 256, 2), b.data());
    s(Run(0, 256, 256, 3), b.data());
    CHECK(s.frames() == 2);
    CHECK(s.value(0) == b[0]);
    // A skipped bin inside a frame.
    s(Run(0, 100, 256, 4), a.data());
    s(Run(101, 155, 256, 4), a.data() + 101);
    CHECK(s.frames() == 2);
    CHECK(s.value(0) == b[0]);
    // Another frame size is a new frame.
    s(Run(0, 100, 256, 5), a.data());
    s(Run(0, 64, 64, 5), a.data());
    CHECK(s.frames() == 3);
    CHECK(s.size() == 64);
    CHECK(s.bins() == 64);
    // An empty run changes nothing.
    s(Run(0, 0, 64, 6), a.data());
    CHECK(s.frames() == 3);
  }

  TEST_CASE("frameSnapshot: capacity, non-finite input, clear, the block as a frame (#1163)") {
    frameSnapshot s(300);
    CHECK(s.capacity() == 300);
    CHECK(frameSnapshot(0).capacity() == 1);
    CHECK(frameSnapshot(1u << 20).capacity() == frameSnapshot::MAXIMUM_CAPACITY);
    std::vector<float> v = Noise(512, 10);
    v[3] = std::numeric_limits<float>::quiet_NaN();
    v[4] = -std::numeric_limits<float>::infinity();
    for (UInt first = 0; first < 512; first += 128)
      s(Run(first, 128, 512), v.data() + first);
    CHECK(s.frames() == 1);
    CHECK(s.size() == 300);
    CHECK(s.bins() == 512);
    CHECK(s.value(3) == 0.f);
    CHECK(s.value(4) == 0.f);
    CHECK(s.value(299) == v[299]);
    CHECK(s.value(300) == 0.f);

    s.clear();
    CHECK(s.frames() == 0);
    CHECK(s.size() == 0);
    CHECK(s.bins() == 0);
    // A frame half taken before a clear is dropped.
    s(Run(0, 256, 512, 1), v.data());
    s.clear();
    s(Run(256, 256, 512, 1), v.data() + 256);
    CHECK(s.frames() == 0);

    // Blocks: each a whole frame of its length.
    frameSnapshot blocks;
    buffer in(N);
    for (unsigned int b = 0; b < 3; ++b) {
      for (unsigned int i = 0; i < N; ++i)
        in.getPtr()[i] = static_cast<float>(b * N + i);
      blocks(in);
      CHECK(blocks.frames() == b + 1);
      CHECK(blocks.size() == N);
      CHECK(blocks.value(5) == static_cast<float>(b * N + 5));
    }
    buffer shorter(40);
    blocks(shorter);
    CHECK(blocks.size() == 40);
  }

  TEST_CASE("frameSnapshot / frameSource: on DSP::stft without a patcher, a frame captured and "
            "put back is the frame, and a captured spectrum freezes (#1163)") {
    constexpr UInt SIZE = 1024;
    // Captured and put back through a frameSource, every frame: the output
    // is DSP::stft's untouched, bit for bit.
    stft plain(SIZE, 4);
    stft through(SIZE, 4);
    frameSnapshot real;
    frameSnapshot imaginary;
    frameSource outReal;
    frameSource outImaginary;
    auto copyBack = [&](stftFrame& f) {
      real(f, f.real);
      imaginary(f, f.imaginary);
      REQUIRE(real.size() == f.bins);
      outReal.set(real.values(), real.size());
      outImaginary.set(imaginary.values(), imaginary.size());
      outReal(f, f.real);
      outImaginary(f, f.imaginary);
    };
    auto nothing = [](stftFrame&) {};
    buffer b(N);
    std::size_t at = 0;
    for (unsigned int block = 0; block < 40; ++block) {
      for (unsigned int i = 0; i < N; ++i, ++at)
        b.getPtr()[i] = static_cast<float>(0.5 * std::sin(0.05 * static_cast<double>(at)) +
                                           0.2 * std::sin(0.71 * static_cast<double>(at)));
      buffer& p = plain(b, nothing);
      buffer& t = through(b, copyBack);
      REQUIRE(TestHelpers::buffersNearlyEqual(p, t, 0.f));
    }
    CHECK(real.frames() > 10);

    // Frozen: the spectrum of a sine captured once, then put out every frame
    // with the input gone. Each frame is the same, so after the overlap the
    // output repeats with the hop, and it is not silent.
    stft freeze(SIZE, 4);
    bool frozen = false;
    auto hold = [&](stftFrame& f) {
      if (!frozen) {
        real(f, f.real);
        imaginary(f, f.imaginary);
        return;
      }
      outReal.set(real.values(), real.size());
      outImaginary.set(imaginary.values(), imaginary.size());
      outReal(f, f.real);
      outImaginary(f, f.imaginary);
    };
    std::vector<float> out;
    at = 0;
    for (unsigned int block = 0; block < 64; ++block) {
      for (unsigned int i = 0; i < N; ++i, ++at)
        b.getPtr()[i] =
            block < 16 ? static_cast<float>(0.5 * std::sin(0.2 * static_cast<double>(at))) : 0.f;
      if (block == 16) frozen = true;
      buffer& o = freeze(b, hold);
      out.insert(out.end(), o.getPtr(), o.getPtr() + N);
    }
    const std::size_t hop = SIZE / 4;
    float worst = 0.f;
    float level = 0.f;
    for (std::size_t n = 40 * N; n + hop < out.size(); ++n) {
      worst = std::max(worst, std::abs(out[n] - out[n + hop]));
      level = std::max(level, std::abs(out[n]));
    }
    CHECK(worst < 1e-4f);
    CHECK(level > 0.1f);
  }

  TEST_CASE("frameSource / frameSnapshot: the output does not depend on the sample rate "
            "(#1163)") {
    const std::vector<float> v = Noise(300, 11);
    std::vector<float> want;
    std::vector<float> got;
    {
      frameSource s;
      s.set(v.data(), 300);
      want = Put(s, 512, 128);
    }
    for (unsigned int rate : {22050u, 96000u}) {
      TestHelpers::ScopedSampleRate scoped(rate);
      frameSource s;
      s.set(v.data(), 300);
      got = Put(s, 512, 128);
      CHECK(got == want);
      frameSnapshot snap;
      snap(Run(0, 300, 300), v.data());
      CHECK(std::equal(v.begin(), v.end(), snap.values()));
    }
  }

  TEST_CASE("frameSource / frameSnapshot: setting, processing, reading and clearing allocate "
            "nothing (#1163)") {
    frameSource s;
    frameSource other(64);
    frameSnapshot snap;
    const std::vector<float> v = Noise(1024, 12);
    std::vector<float> out(1024);
    buffer in(N);
    s.set(v.data(), 1024);
    snap(Run(0, 1024, 1024), v.data());
    {
      TestHelpers::ProbeScope probe;
      for (int m = 0; m < 4; ++m) {
        s.set(v.data(), 300, static_cast<UInt>(m) * 10);
        s.set(v.data(), 5, 900);
        s.fill(0.5f);
        s.mirror(m % 2 == 0);
        other.copy(s);
        s.copy(other);
        for (UInt first = 0; first < 1024; first += 128) {
          stftFrame f = Run(first, 128, 1024, static_cast<U64>(m));
          f.fullSpectrum = true;
          f.size = 1024;
          s(f, out.data() + first);
          s(f);
          snap(f, v.data() + first);
        }
        s();
        snap(in);
        snap.copy(out.data(), 1024);
        snap.clear();
        s.clear();
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
