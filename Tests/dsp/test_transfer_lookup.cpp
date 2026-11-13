// Tests for YSE::DSP::transferLookup (YseEngine/dsp/sampling/transferLookup.hpp),
// issue #1147: part of a sample buffer used as a transfer function, what
// ~lookup does. The -1 to 1 mapping and its interpolation, silence beyond
// -1 and 1, the offset and size (Max's "size minus offset" table), the
// channel, a closed-form waveshaper, block lengths, the sample rate, and
// allocation-free blocks. The view is a sampleBufferReader's, tested in
// test_sampleBuffer.cpp.

#include <doctest/doctest.h>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <memory>
#include <utility>
#include <vector>
#include "dsp/buffer.hpp"
#include "dsp/sampling/sampleBuffer.hpp"
#include "dsp/sampling/transferLookup.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

using YSE::DSP::buffer;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferReader;
using YSE::DSP::sampleView;
using YSE::DSP::transferLookup;

namespace {

  struct SampleRateGuard {
    UInt saved;
    SampleRateGuard() : saved(YSE::SAMPLERATE) {}
    ~SampleRateGuard() {
      YSE::SAMPLERATE = saved;
    }
  };

  std::shared_ptr<sampleBuffer> Table(std::initializer_list<float> values) {
    auto b = sampleBuffer::create(1, static_cast<UInt>(values.size()));
    std::vector<float> v(values);
    b->setSamples(0, 0, v.data(), static_cast<UInt>(v.size()));
    return b;
  }

  // Frame n of channel c is (c + 1) * 10 * n: channel 0 is 10 * n.
  std::shared_ptr<sampleBuffer> Tens(UInt frames, UInt channels = 1) {
    auto b = sampleBuffer::create(channels, frames);
    for (UInt c = 0; c < channels; ++c) {
      std::vector<float> v(frames);
      for (UInt n = 0; n < frames; ++n)
        v[n] = static_cast<float>((c + 1) * 10 * n);
      b->setSamples(c, 0, v.data(), frames);
    }
    return b;
  }

  buffer Signal(std::initializer_list<float> values) {
    buffer out(static_cast<UInt>(values.size()));
    UInt i = 0;
    for (float v : values)
      out.getPtr()[i++] = v;
    return out;
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("transferLookup: -1 reads the first entry, 1 the last, a line between (#1147)") {
    // A straight line from -1 to 1 is the identity: the signal through
    // unchanged, between the entries too.
    auto b = Table({-1.f, -0.5f, 0.f, 0.5f, 1.f});
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    transferLookup t;
    for (float x : {-1.f, -0.75f, -0.5f, -0.1f, 0.f, 0.25f, 0.6f, 0.875f, 1.f}) {
      CAPTURE(x);
      CHECK(t.read(view, x) == doctest::Approx(x).epsilon(1e-6));
    }
    // Position (x + 1) / 2 * (entries - 1): 0.25 is entry 2.5.
    auto tens = Tens(5);
    sampleBufferReader tensReader(tens);
    sampleView tensView = tensReader.pin();
    CHECK(t.read(tensView, -1.f) == 0.f);
    CHECK(t.read(tensView, 0.f) == 20.f);
    CHECK(t.read(tensView, 0.25f) == 25.f);
    CHECK(t.read(tensView, 1.f) == 40.f);
    buffer in = Signal({-1.f, -0.5f, 0.f, 0.5f, 1.f});
    buffer& out = t(tensView, in);
    REQUIRE(out.getLength() == 5);
    for (UInt i = 0; i < 5; ++i)
      CHECK(out.getPtr()[i] == 10.f * static_cast<float>(i));
  }

  TEST_CASE("transferLookup: beyond -1 or 1, or not a number, outputs 0 (#1147)") {
    auto b = Table({0.5f, 0.75f, 0.9f});
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    transferLookup t;
    CHECK(t.read(view, -1.f) == 0.5f);
    CHECK(t.read(view, 1.f) == 0.9f);
    CHECK(t.read(view, -1.0001f) == 0.f);
    CHECK(t.read(view, 1.0001f) == 0.f);
    CHECK(t.read(view, 7.f) == 0.f);
    CHECK(t.read(view, std::numeric_limits<float>::quiet_NaN()) == 0.f);
    CHECK(t.read(view, std::numeric_limits<float>::infinity()) == 0.f);
    CHECK(t.read(view, -std::numeric_limits<float>::infinity()) == 0.f);
    buffer in = Signal({2.f, 1.f, -3.f, std::numeric_limits<float>::quiet_NaN(), 0.f});
    buffer& out = t(view, in);
    CHECK(out.getPtr()[0] == 0.f);
    CHECK(out.getPtr()[1] == 0.9f);
    CHECK(out.getPtr()[2] == 0.f);
    CHECK(out.getPtr()[3] == 0.f);
    CHECK(out.getPtr()[4] == 0.75f);
  }

  TEST_CASE("transferLookup: the table runs from offset up to size, size counted from the "
            "buffer's start (#1147)") {
    auto b = Tens(20);
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    transferLookup t;
    CHECK(t.offset() == 0);
    CHECK(t.size() == transferLookup::DEFAULT_SIZE);
    CHECK(t.size() == 512);
    // Frames 4 .. 8: five entries, 40 to 80. A larger offset is a shorter
    // table, as Max documents: "size minus the offset".
    t.offset(4).size(9);
    CHECK(t.read(view, -1.f) == 40.f);
    CHECK(t.read(view, 0.f) == 60.f);
    CHECK(t.read(view, 0.25f) == 65.f);
    CHECK(t.read(view, 1.f) == 80.f);
    t.offset(6);
    CHECK(t.read(view, -1.f) == 60.f);
    CHECK(t.read(view, 0.f) == 70.f);
    CHECK(t.read(view, 1.f) == 80.f);
    // A size past the buffer's end stops at its last frame: the default
    // 512 uses a 20-frame buffer whole.
    t.offset(0).size(512);
    CHECK(t.read(view, -1.f) == 0.f);
    CHECK(t.read(view, 1.f) == 190.f);
    CHECK(t.read(view, 0.f) == 95.f);
    // One entry reads it for every input in range.
    t.offset(3).size(4);
    CHECK(t.read(view, -1.f) == 30.f);
    CHECK(t.read(view, 0.3f) == 30.f);
    CHECK(t.read(view, 1.f) == 30.f);
    CHECK(t.read(view, 1.5f) == 0.f);
  }

  TEST_CASE("transferLookup: no entries, no channel or no buffer read silence (#1147)") {
    auto b = Tens(16);
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    transferLookup t;
    buffer in = Signal({-1.f, 0.f, 1.f});
    // offset == size, offset past size, offset past the buffer, size 0.
    for (const auto& range : std::vector<std::pair<Int, Int>>{{5, 5}, {9, 4}, {16, 512}, {0, 0}}) {
      CAPTURE(range.first);
      CAPTURE(range.second);
      t.offset(range.first).size(range.second);
      CHECK(t.read(view, 0.f) == 0.f);
      buffer& out = t(view, in);
      CHECK(out.getPtr()[0] == 0.f);
      CHECK(out.getPtr()[2] == 0.f);
    }
    // Below 0 is 0.
    t.offset(-3).size(-1);
    CHECK(t.offset() == 0);
    CHECK(t.size() == 0);
    t.size(16);
    CHECK(t.read(view, 1.f) == 150.f);

    // A channel the buffer lacks.
    t.channel(1);
    CHECK(t.read(view, 1.f) == 0.f);
    CHECK(t(view, in).getPtr()[2] == 0.f);

    // An unbound reader, and an empty buffer.
    sampleBufferReader unbound;
    sampleView none = unbound.pin();
    t.channel(0);
    CHECK(t.read(none, 0.f) == 0.f);
    CHECK(t(none, in).getPtr()[1] == 0.f);
    auto empty = sampleBuffer::create(1, 0);
    sampleBufferReader emptyReader(empty);
    sampleView emptyView = emptyReader.pin();
    CHECK(t(emptyView, in).getPtr()[1] == 0.f);
  }

  TEST_CASE("transferLookup: the channel, from 0 (#1147)") {
    auto b = Tens(5, 3);
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    transferLookup t;
    CHECK(t.channel() == 0);
    CHECK(t.read(view, 1.f) == 40.f);
    t.channel(2);
    CHECK(t.channel() == 2);
    CHECK(t.read(view, 1.f) == 120.f);
    CHECK(t.read(view, 0.f) == 60.f);
  }

  TEST_CASE("transferLookup: a squaring curve doubles a sine's frequency, closed form "
            "(#1147)") {
    // The table holds f(t) = t^2 at 1025 points across -1 to 1. A sine read
    // through it is sin^2 = (1 - cos(2 w n)) / 2, within the linear
    // interpolation bound h^2 / 8 * max|f''| = (2 / 1024)^2 / 8 * 2.
    constexpr UInt POINTS = 1025;
    auto b = sampleBuffer::create(1, POINTS);
    std::vector<float> curve(POINTS);
    for (UInt n = 0; n < POINTS; ++n) {
      const double t = -1.0 + 2.0 * static_cast<double>(n) / static_cast<double>(POINTS - 1);
      curve[n] = static_cast<float>(t * t);
    }
    b->setSamples(0, 0, curve.data(), POINTS);
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    transferLookup t;
    t.size(static_cast<Int>(POINTS));

    const double w =
        2.0 * 3.14159265358979323846 * 7.0 / static_cast<double>(YSE::STANDARD_BUFFERSIZE);
    buffer in(YSE::STANDARD_BUFFERSIZE);
    for (UInt n = 0; n < YSE::STANDARD_BUFFERSIZE; ++n)
      in.getPtr()[n] = static_cast<float>(std::sin(w * static_cast<double>(n)));
    buffer& out = t(view, in);
    REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
    const double h = 2.0 / static_cast<double>(POINTS - 1);
    const double bound = h * h / 8.0 * 2.0 + 1e-6;
    for (UInt n = 0; n < YSE::STANDARD_BUFFERSIZE; ++n) {
      CAPTURE(n);
      const double expected = (1.0 - std::cos(2.0 * w * static_cast<double>(n))) / 2.0;
      CHECK(std::fabs(static_cast<double>(out.getPtr()[n]) - expected) <= bound);
    }
  }

  TEST_CASE("transferLookup: one input for a block, and block lengths (#1147)") {
    auto b = Tens(5);
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    transferLookup t;
    buffer& constant = t(view, 0.5f, 10);
    REQUIRE(constant.getLength() == 10);
    CHECK(constant.getPtr()[0] == 30.f);
    CHECK(constant.getPtr()[9] == 30.f);
    CHECK(t(view, 0.f).getLength() == YSE::STANDARD_BUFFERSIZE);
    CHECK(t.out().getPtr()[YSE::STANDARD_BUFFERSIZE - 1] == 20.f);
    CHECK(t(view, 3.f).getPtr()[0] == 0.f);
    // A longer input is cut to STANDARD_BUFFERSIZE.
    buffer longer(YSE::STANDARD_BUFFERSIZE * 2);
    longer = 1.f;
    CHECK(t(view, longer).getLength() == YSE::STANDARD_BUFFERSIZE);
    CHECK(t.out().getPtr()[0] == 40.f);
  }

  TEST_CASE("transferLookup: no state follows the sample rate (#1147)") {
    SampleRateGuard guard;
    auto b = Tens(9);
    sampleBufferReader reader(b);
    transferLookup t;
    t.offset(1).size(9);
    buffer half = Signal({0.5f});
    YSE::SAMPLERATE = 22050;
    {
      sampleView view = reader.pin();
      CHECK(t(view, half).getPtr()[0] == 62.5f); // frames 1 .. 8: entry 6.25
    }
    YSE::SAMPLERATE = 96000;
    sampleView view = reader.pin();
    CHECK(t(view, half).getPtr()[0] == 62.5f);
    CHECK(t.read(view, -1.f) == 10.f);
  }

  TEST_CASE("transferLookup: processing allocates nothing (#1147)") {
    auto b = Tens(1024);
    sampleBufferReader reader(b);
    transferLookup t;
    buffer in(YSE::STANDARD_BUFFERSIZE);
    buffer shorter(16);
    for (UInt i = 0; i < YSE::STANDARD_BUFFERSIZE; ++i)
      in.getPtr()[i] = static_cast<float>(i % 5) * 0.5f - 1.f;
    shorter = 0.5f;
    float last = 0.f;
    {
      TestHelpers::ProbeScope probe;
      for (int block = 0; block < 6; ++block) {
        sampleView view = reader.pin();
        t.channel(static_cast<UInt>(block % 2)).offset(block * 3).size(512 + block * 100);
        t(view, in);
        t(view, shorter); // down and back up, within capacity
        t(view, 0.25f);
        last += t.read(view, -1.f);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(std::isfinite(last));
  }

} // TEST_SUITE
