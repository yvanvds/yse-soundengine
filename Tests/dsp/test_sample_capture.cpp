// Tests for YSE::DSP::sampleCapture (YseEngine/dsp/analysis/sampleCapture.hpp),
// issue #1128. Reference output is closed form: in LAST mode the store holds
// the last `capacity` samples it was given, in FIRST mode the first
// `capacity` since the last clear; with block positions set, only the
// samples at those positions of each block are given to it.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/analysis/sampleCapture.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::DSP::buffer;
using YSE::DSP::sampleCapture;

namespace {

  constexpr UInt N = YSE::STANDARD_BUFFERSIZE;

  // A ramp of distinct values: sample n is n * 0.25 - 3.
  std::vector<float> Ramp(std::size_t length) {
    std::vector<float> x(length);
    for (std::size_t n = 0; n < length; ++n)
      x[n] = static_cast<float>(n) * 0.25f - 3.f;
    return x;
  }

  // Runs `x` through `d` in blocks of `block` samples.
  void Blocks(sampleCapture& d, const std::vector<float>& x, UInt block) {
    for (UInt start = 0; start < x.size(); start += block) {
      const UInt len = std::min<UInt>(block, static_cast<UInt>(x.size()) - start);
      buffer b(len);
      for (UInt i = 0; i < len; ++i)
        b.getPtr()[i] = x[start + i];
      d(b);
    }
  }

  std::vector<float> Held(const sampleCapture& d) {
    std::vector<float> out(d.size());
    for (UInt i = 0; i < d.size(); ++i)
      out[i] = d.sample(i);
    // copy() reads the same samples in one go.
    std::vector<float> copied(d.size() + 3, -99.f);
    CHECK(d.copy(copied.data(), static_cast<UInt>(copied.size())) == d.size());
    copied.resize(d.size());
    CHECK(copied == out);
    return out;
  }

  std::vector<float> Slice(const std::vector<float>& x, std::size_t from, std::size_t to) {
    return std::vector<float>(x.begin() + static_cast<std::ptrdiff_t>(from),
                              x.begin() + static_cast<std::ptrdiff_t>(to));
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("sampleCapture: defaults (#1128)") {
    sampleCapture d;
    CHECK(d.capacity() == sampleCapture::DEFAULT_CAPACITY);
    CHECK(d.capacity() == 4096);
    CHECK(d.size() == 0);
    CHECK_FALSE(d.full());
    CHECK(d.mode() == sampleCapture::LAST);
    CHECK(d.indexCount() == 0);
    CHECK(d.sample(0) == 0.f);
    float out = -1.f;
    CHECK(d.copy(&out, 1) == 0);
    CHECK(out == -1.f);
    CHECK(d.copy(nullptr, 4) == 0);
  }

  TEST_CASE("sampleCapture: the capacity is clamped to 1 .. MAXIMUM_CAPACITY (#1128)") {
    CHECK(sampleCapture(0).capacity() == 1);
    CHECK(sampleCapture(1).capacity() == 1);
    CHECK(sampleCapture(sampleCapture::MAXIMUM_CAPACITY + 1).capacity() ==
          sampleCapture::MAXIMUM_CAPACITY);
  }

  TEST_CASE("sampleCapture: LAST keeps the most recent samples, oldest first, across block "
            "splits (#1128)") {
    const std::vector<float> x = Ramp(3 * N + 17);
    for (const UInt block : {N, 64u, 13u, 1u}) {
      CAPTURE(block);
      // Not yet full: everything so far.
      sampleCapture partial(static_cast<UInt>(x.size()) + 5);
      Blocks(partial, x, block);
      CHECK_FALSE(partial.full());
      CHECK(Held(partial) == x);
      // Full and wrapped: the last 100.
      sampleCapture last(100);
      Blocks(last, x, block);
      CHECK(last.full());
      CHECK(Held(last) == Slice(x, x.size() - 100, x.size()));
      // Exactly full, not wrapped.
      sampleCapture exact(static_cast<UInt>(x.size()));
      Blocks(exact, x, block);
      CHECK(exact.full());
      CHECK(Held(exact) == x);
    }
  }

  TEST_CASE("sampleCapture: FIRST keeps the first samples and ignores the rest until clear() "
            "(#1128)") {
    const std::vector<float> x = Ramp(2 * N);
    for (const UInt block : {N, 64u, 13u, 1u}) {
      CAPTURE(block);
      sampleCapture d(100);
      d.mode(sampleCapture::FIRST);
      Blocks(d, x, block);
      CHECK(d.full());
      CHECK(Held(d) == Slice(x, 0, 100));
      d.clear();
      CHECK(d.size() == 0);
      CHECK(d.capacity() == 100);
      CHECK(d.mode() == sampleCapture::FIRST);
      const std::vector<float> y = Ramp(150);
      Blocks(d, Slice(y, 50, 150), block);
      CHECK(Held(d) == Slice(y, 50, 150));
    }
  }

  TEST_CASE("sampleCapture: switching the mode keeps what is stored (#1128)") {
    const std::vector<float> x = Ramp(10);
    sampleCapture d(4);
    for (float v : x)
      d.process(v);
    CHECK(Held(d) == Slice(x, 6, 10));
    d.mode(sampleCapture::FIRST);
    d.process(100.f); // full: ignored
    CHECK(Held(d) == Slice(x, 6, 10));
    d.mode(sampleCapture::LAST);
    d.process(100.f); // replaces the oldest
    CHECK(Held(d) == std::vector<float>{x[7], x[8], x[9], 100.f});
  }

  TEST_CASE("sampleCapture: indices store only those block positions, sorted and once "
            "(#1128)") {
    const UInt list[] = {64, 3, 0, 3, 200};
    sampleCapture d(1000);
    d.indices(list, 5);
    REQUIRE(d.indexCount() == 4);
    CHECK(d.index(0) == 0);
    CHECK(d.index(1) == 3);
    CHECK(d.index(2) == 64);
    CHECK(d.index(3) == 200);
    CHECK(d.index(4) == 0);

    const std::vector<float> x = Ramp(2 * N);
    Blocks(d, x, N);
    std::vector<float> want;
    for (UInt start = 0; start < 2 * N; start += N) {
      for (UInt at : {0u, 3u, 64u, 200u}) {
        if (at < N) want.push_back(x[start + at]);
      }
    }
    CHECK(Held(d) == want);

    // A block shorter than a position stores nothing there.
    d.clear();
    buffer shortBlock(10);
    for (UInt i = 0; i < 10; ++i)
      shortBlock.getPtr()[i] = static_cast<float>(i);
    d(shortBlock);
    CHECK(Held(d) == std::vector<float>{0.f, 3.f});

    // process() stores its sample whatever the positions.
    d.process(7.f);
    CHECK(d.size() == 3);

    // More than MAX_INDICES: the first ten.
    const UInt many[] = {11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
    d.indices(many, 12);
    CHECK(d.indexCount() == sampleCapture::MAX_INDICES);
    CHECK(d.index(0) == 2);
    CHECK(d.index(9) == 11);

    // None again: every sample.
    d.indices(nullptr, 3);
    CHECK(d.indexCount() == 0);
    d.clear();
    d(shortBlock);
    CHECK(d.size() == 10);
  }

  TEST_CASE("sampleCapture: samples are stored as they are, NaN and infinities too (#1128)") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    sampleCapture d(3);
    d.process(nan);
    d.process(-inf);
    d.process(inf);
    CHECK(std::isnan(d.sample(0)));
    CHECK(d.sample(1) == -inf);
    CHECK(d.sample(2) == inf);
  }

  TEST_CASE("sampleCapture: no state depends on the sample rate (#1128)") {
    const std::vector<float> x = Ramp(3 * N);
    std::vector<float> first;
    {
      ScopedSampleRate rate(44100);
      sampleCapture d(500);
      Blocks(d, x, 64);
      first = Held(d);
    }
    ScopedSampleRate rate(96000);
    sampleCapture d(500);
    Blocks(d, x, 64);
    CHECK(Held(d) == first);
  }

  TEST_CASE("sampleCapture: storing, reading, clear() and settings allocate nothing (#1128)") {
    sampleCapture d(300);
    buffer b(N);
    buffer shortBlock(N / 2);
    for (UInt i = 0; i < N; ++i)
      b.getPtr()[i] = static_cast<float>(i);
    shortBlock = 0.25f;
    std::vector<float> out(300);
    const UInt list[] = {5, 1};
    {
      TestHelpers::ProbeScope probe;
      d(b);
      d(shortBlock);
      d(b);
      d(b); // wraps
      d.process(0.5f);
      CHECK(d.copy(out.data(), 300) == 300);
      CHECK(d.sample(299) == 0.5f);
      d.mode(sampleCapture::FIRST);
      d.clear();
      d.indices(list, 2);
      d(b);
      d.indices(nullptr, 0);
      d(b);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(d.size() == 2 + N); // two positions, then a whole block
    CHECK(d.sample(0) == 1.f);
    CHECK(d.sample(1) == 5.f);
  }

} // TEST_SUITE("dsp")
