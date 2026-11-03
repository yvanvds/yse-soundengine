// Tests for YSE::DSP::sampleAccess and sampleIndexWriter
// (YseEngine/dsp/sampling/sampleAccess.hpp), issue #1144: one sample read or
// written by index (~peek), and a signal written at signal-rate indexes
// (~poke). Both work on the view a sampleBufferWriter pins; the writer itself
// is tested in test_sampleBuffer.cpp.

#include <doctest/doctest.h>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <memory>
#include <vector>
#include "dsp/buffer.hpp"
#include "dsp/sampling/sampleAccess.hpp"
#include "dsp/sampling/sampleBuffer.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

using YSE::DSP::buffer;
using YSE::DSP::sampleAccess;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferWriter;
using YSE::DSP::sampleIndexWriter;
using YSE::DSP::sampleWriteView;

namespace {

  struct SampleRateGuard {
    UInt saved;
    SampleRateGuard() : saved(YSE::SAMPLERATE) {}
    ~SampleRateGuard() {
      YSE::SAMPLERATE = saved;
    }
  };

  // Sample n of channel c is (c + 1) * 10 + n.
  std::shared_ptr<sampleBuffer> Counting(UInt channels, UInt frames) {
    auto b = sampleBuffer::create(channels, frames, 1000.f);
    for (UInt c = 0; c < channels; ++c) {
      std::vector<float> values(frames);
      for (UInt n = 0; n < frames; ++n)
        values[n] = static_cast<float>((c + 1) * 10 + n);
      b->setSamples(c, 0, values.data(), frames);
    }
    return b;
  }

  float At(const sampleBuffer& b, UInt c, UInt frame) {
    float out = 0.f;
    b.getSamples(c, frame, &out, 1);
    return out;
  }

  buffer Ramp(std::initializer_list<float> values) {
    buffer out(static_cast<UInt>(values.size()));
    UInt i = 0;
    for (float v : values)
      out.getPtr()[i++] = v;
    return out;
  }

} // namespace

TEST_SUITE("dsp") {

  // ─── sampleAccess ─────────────────────────────────────────────────────────

  TEST_CASE("sampleAccess: reads the sample at an index, nothing outside (#1144)") {
    auto b = Counting(2, 5);
    sampleBufferWriter writer(b);
    sampleWriteView view = writer.pin();
    sampleAccess access;
    CHECK(access.channel() == 0);
    CHECK(access.clip());

    float out = -1.f;
    CHECK(access.read(view, 3, out));
    CHECK(out == 13.f);
    access.channel(1);
    CHECK(access.read(view, 0, out));
    CHECK(out == 20.f);

    out = -1.f;
    CHECK_FALSE(access.read(view, 5, out));
    CHECK_FALSE(access.read(view, -1, out));
    access.channel(2);
    CHECK_FALSE(access.read(view, 0, out));
    CHECK(out == -1.f); // untouched
  }

  TEST_CASE("sampleAccess: writes at an index, clipped unless clipping is off (#1144)") {
    auto b = sampleBuffer::create(2, 4);
    sampleBufferWriter writer(b);
    sampleAccess access;
    {
      sampleWriteView view = writer.pin();
      CHECK(access.write(view, 1, 0.5f));
      CHECK(access.write(view, 2, 3.f)); // clipped to 1
      CHECK(access.write(view, 3, -7.f)); // clipped to -1
      access.clip(false);
      access.channel(1);
      CHECK(access.write(view, 0, 3.f)); // as it is
      CHECK_FALSE(access.write(view, 4, 1.f));
      CHECK_FALSE(access.write(view, -1, 1.f));
      access.channel(5);
      CHECK_FALSE(access.write(view, 0, 1.f));
    }
    CHECK(At(*b, 0, 1) == 0.5f);
    CHECK(At(*b, 0, 2) == 1.f);
    CHECK(At(*b, 0, 3) == -1.f);
    CHECK(At(*b, 1, 0) == 3.f);
    CHECK(At(*b, 1, 1) == 0.f);
  }

  TEST_CASE("sampleAccess: an empty buffer reads and writes nothing (#1144)") {
    sampleBufferWriter unbound;
    sampleWriteView view = unbound.pin();
    sampleAccess access;
    float out = 2.f;
    CHECK_FALSE(access.read(view, 0, out));
    CHECK_FALSE(access.write(view, 0, 1.f));
    CHECK(out == 2.f);
  }

  // ─── sampleIndexWriter ────────────────────────────────────────────────────

  TEST_CASE("sampleIndexWriter: each value goes to its index, truncated (#1144)") {
    auto b = sampleBuffer::create(1, 8);
    sampleBufferWriter writer(b);
    sampleIndexWriter poke;
    buffer values = Ramp({0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 5.f});
    // 2.9 truncates to 2; -1 pauses; 8 is past the end; NaN writes nothing.
    buffer indexes = Ramp({0.f, 2.9f, -1.f, 7.f, 8.f, std::numeric_limits<float>::quiet_NaN()});
    sampleWriteView view = writer.pin();
    CHECK(poke(view, values, indexes) == 3);
    CHECK(At(*b, 0, 0) == 0.1f);
    CHECK(At(*b, 0, 2) == 0.2f);
    CHECK(At(*b, 0, 7) == 0.4f);
    CHECK(At(*b, 0, 1) == 0.f);
    CHECK(At(*b, 0, 3) == 0.f);
  }

  TEST_CASE("sampleIndexWriter: values are not clipped (#1144)") {
    auto b = sampleBuffer::create(1, 2);
    sampleBufferWriter writer(b);
    sampleIndexWriter poke;
    sampleWriteView view = writer.pin();
    CHECK(poke(view, 12.5f, 1.f) == 1);
    CHECK(At(*b, 0, 1) == 12.5f);
  }

  TEST_CASE("sampleIndexWriter: a constant value at signal indexes (#1144)") {
    auto b = sampleBuffer::create(2, 6);
    sampleBufferWriter writer(b);
    sampleIndexWriter poke;
    poke.channel(1);
    CHECK(poke.channel() == 1);
    buffer indexes = Ramp({5.f, 4.f, -1.f, 0.f});
    sampleWriteView view = writer.pin();
    CHECK(poke(view, 0.25f, indexes) == 3);
    CHECK(At(*b, 1, 5) == 0.25f);
    CHECK(At(*b, 1, 4) == 0.25f);
    CHECK(At(*b, 1, 0) == 0.25f);
    CHECK(At(*b, 0, 5) == 0.f); // the other channel is untouched
  }

  TEST_CASE("sampleIndexWriter: signal values at one index leave the last (#1144)") {
    auto b = sampleBuffer::create(1, 4);
    sampleBufferWriter writer(b);
    sampleIndexWriter poke;
    buffer values = Ramp({1.f, 2.f, 3.f});
    sampleWriteView view = writer.pin();
    CHECK(poke(view, values, 2.f) == 1);
    CHECK(At(*b, 0, 2) == 3.f);
    CHECK(poke(view, values, -1.f) == 0);
    buffer none(0);
    CHECK(poke(view, none, 1.f) == 0);
  }

  TEST_CASE("sampleIndexWriter: two signals run as long as the shorter (#1144)") {
    auto b = sampleBuffer::create(1, 8);
    sampleBufferWriter writer(b);
    sampleIndexWriter poke;
    buffer values = Ramp({1.f, 2.f, 3.f, 4.f});
    buffer indexes = Ramp({4.f, 5.f});
    sampleWriteView view = writer.pin();
    CHECK(poke(view, values, indexes) == 2);
    CHECK(At(*b, 0, 4) == 1.f);
    CHECK(At(*b, 0, 5) == 2.f);
  }

  TEST_CASE("sampleIndexWriter: a channel the buffer lacks writes nothing (#1144)") {
    auto b = sampleBuffer::create(1, 4);
    sampleBufferWriter writer(b);
    sampleIndexWriter poke;
    poke.channel(1);
    buffer values = Ramp({1.f, 2.f});
    buffer indexes = Ramp({0.f, 1.f});
    sampleWriteView view = writer.pin();
    CHECK(poke(view, values, indexes) == 0);
    CHECK(poke(view, 1.f, indexes) == 0);
    CHECK(poke(view, 1.f, 0.f) == 0);
    CHECK(At(*b, 0, 0) == 0.f);
  }

  TEST_CASE("sampleIndexWriter: a recorder loop, written and read back (#1144)") {
    // Closed form: a ramp of indexes 0..63 writes a sine into the buffer
    // sample for sample, which reads back exactly.
    auto b = sampleBuffer::create(1, 64);
    sampleBufferWriter writer(b);
    sampleIndexWriter poke;
    buffer values(64);
    buffer indexes(64);
    for (UInt i = 0; i < 64; ++i) {
      values.getPtr()[i] = std::sin(0.1f * static_cast<float>(i));
      indexes.getPtr()[i] = static_cast<float>(i);
    }
    {
      sampleWriteView view = writer.pin();
      CHECK(poke(view, values, indexes) == 64);
    }
    for (UInt i = 0; i < 64; ++i)
      CHECK(At(*b, 0, i) == std::sin(0.1f * static_cast<float>(i)));
  }

  TEST_CASE("sampleAccess / sampleIndexWriter: no state follows the sample rate (#1144)") {
    // Indexes are in samples of the buffer, whatever the engine rate.
    SampleRateGuard guard;
    auto b = sampleBuffer::create(1, 4, 0.f);
    sampleBufferWriter writer(b);
    sampleIndexWriter poke;
    sampleAccess access;
    YSE::SAMPLERATE = 22050;
    {
      sampleWriteView view = writer.pin();
      CHECK(poke(view, 0.5f, 1.f) == 1);
    }
    YSE::SAMPLERATE = 96000;
    sampleWriteView view = writer.pin();
    CHECK(view.sampleRate() == doctest::Approx(96000.f));
    float out = 0.f;
    CHECK(access.read(view, 1, out));
    CHECK(out == 0.5f);
  }

  TEST_CASE("sampleAccess / sampleIndexWriter: processing allocates nothing (#1144)") {
    auto b = sampleBuffer::create(2, 1024);
    sampleBufferWriter writer(b);
    sampleIndexWriter poke;
    sampleAccess access;
    buffer values(YSE::STANDARD_BUFFERSIZE);
    buffer indexes(YSE::STANDARD_BUFFERSIZE);
    for (UInt i = 0; i < YSE::STANDARD_BUFFERSIZE; ++i) {
      values.getPtr()[i] = 0.25f;
      indexes.getPtr()[i] = static_cast<float>(i);
    }
    float out = 0.f;
    {
      TestHelpers::ProbeScope probe;
      for (int block = 0; block < 4; ++block) {
        sampleWriteView view = writer.pin();
        poke(view, values, indexes);
        poke(view, 0.5f, indexes);
        poke(view, values, 3.f);
        poke(view, 1.f, 2.f);
        access.write(view, 7, 0.75f);
        access.read(view, 7, out);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(out == 0.75f);
  }

} // TEST_SUITE
