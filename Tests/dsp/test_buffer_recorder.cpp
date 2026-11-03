// Tests for YSE::DSP::bufferRecorder (YseEngine/dsp/sampling/bufferRecorder.hpp)
// and YSE::DSP::sampleRegion (sampleRegion.hpp), issue #1143: signals recorded
// into a sample buffer between loop points given in milliseconds, once or in
// a loop, with append and a 0-1 sync output. The buffers here run at 1000 Hz
// unless a test says otherwise, so a millisecond is a frame and every
// expected value is in closed form. The writer the class records through is
// tested in test_sampleBuffer.cpp.

#include <doctest/doctest.h>
#include <limits>
#include <memory>
#include <vector>
#include "dsp/buffer.hpp"
#include "dsp/sampling/bufferRecorder.hpp"
#include "dsp/sampling/sampleBuffer.hpp"
#include "dsp/sampling/sampleRegion.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::buffer;
using YSE::DSP::bufferRecorder;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferWriter;
using YSE::DSP::sampleRegion;
using YSE::DSP::sampleWriteView;

namespace {

  struct SampleRateGuard {
    UInt saved;
    SampleRateGuard() : saved(YSE::SAMPLERATE) {}
    ~SampleRateGuard() {
      YSE::SAMPLERATE = saved;
    }
    SampleRateGuard(const SampleRateGuard&) = delete;
    SampleRateGuard& operator=(const SampleRateGuard&) = delete;
  };

  // A block of `length` samples: from + 0, from + 1, ...
  buffer Counting(UInt length, float from) {
    buffer out(length);
    for (UInt i = 0; i < length; ++i)
      out.getPtr()[i] = from + static_cast<float>(i);
    return out;
  }

  float At(const sampleBuffer& b, UInt c, UInt frame) {
    float out = 0.f;
    b.getSamples(c, frame, &out, 1);
    return out;
  }

  // One block of `in` through `rec` into what `writer` holds; a copy of the
  // sync output.
  std::vector<float> Block(bufferRecorder& rec, sampleBufferWriter& writer, buffer& in) {
    sampleWriteView view = writer.pin();
    buffer& sync = rec(view, in);
    return std::vector<float>(sync.getPtr(), sync.getPtr() + sync.getLength());
  }

  constexpr float RATE = 1000.f; // a frame per millisecond

} // namespace

TEST_SUITE("dsp") {

  // ─── sampleRegion ──────────────────────────────────────────────────────────

  TEST_CASE("sampleRegion: milliseconds to frames, kept inside the buffer (#1143)") {
    sampleRegion r = sampleRegion::fromMs(2.f, 5.f, RATE, 10);
    CHECK(r.start == 2);
    CHECK(r.end == 5);
    CHECK(r.length() == 3);
    CHECK_FALSE(r.empty());
    CHECK(r.contains(2.0));
    CHECK(r.contains(4.0));
    CHECK_FALSE(r.contains(5.0));
    CHECK_FALSE(r.contains(1.0));

    // An end of 0, below 0, not a number or past the buffer: the buffer's end.
    CHECK(sampleRegion::fromMs(2.f, 0.f, RATE, 10).end == 10);
    CHECK(sampleRegion::fromMs(2.f, -3.f, RATE, 10).end == 10);
    CHECK(sampleRegion::fromMs(2.f, std::numeric_limits<float>::quiet_NaN(), RATE, 10).end == 10);
    CHECK(sampleRegion::fromMs(2.f, 50.f, RATE, 10).end == 10);
    CHECK(sampleRegion::fromMs(2.f, std::numeric_limits<float>::infinity(), RATE, 10).end == 10);
    // A start below 0 or not a number is 0; past the end, the end (empty).
    CHECK(sampleRegion::fromMs(-4.f, 5.f, RATE, 10).start == 0);
    CHECK(sampleRegion::fromMs(std::numeric_limits<float>::quiet_NaN(), 5.f, RATE, 10).start == 0);
    CHECK(sampleRegion::fromMs(40.f, 0.f, RATE, 10).empty());
    // An end at or before the start holds nothing.
    CHECK(sampleRegion::fromMs(5.f, 5.f, RATE, 10).empty());
    CHECK(sampleRegion::fromMs(6.f, 3.f, RATE, 10).empty());
    CHECK(sampleRegion::fromMs(6.f, 3.f, RATE, 10).length() == 0);
    // An empty buffer.
    CHECK(sampleRegion::fromMs(0.f, 0.f, RATE, 0).empty());

    // The rate: 10 ms at 44100 Hz is 441 frames; 1 ms is 44.1, rounded to 44,
    // and 1.5 ms at 1000 Hz rounds up to 2.
    CHECK(sampleRegion::fromMs(1.f, 10.f, 44100.f, 100000).start == 44);
    CHECK(sampleRegion::fromMs(1.f, 10.f, 44100.f, 100000).end == 441);
    CHECK(sampleRegion::fromMs(1.5f, 0.f, RATE, 10).start == 2);
  }

  TEST_CASE("sampleRegion: the phase rises from 0 at the start to 1 at the end (#1143)") {
    const sampleRegion r = sampleRegion::fromFrames(10, 20, 100);
    CHECK(r.phase(10.0) == 0.f);
    CHECK(r.phase(15.0) == doctest::Approx(0.5f));
    CHECK(r.phase(19.0) == doctest::Approx(0.9f));
    CHECK(r.phase(20.0) == doctest::Approx(1.f));
    CHECK(r.phase(5.0) == doctest::Approx(-0.5f));
    CHECK(sampleRegion{}.phase(3.0) == 0.f);

    // fromFrames keeps the same rules as fromMs.
    CHECK(sampleRegion::fromFrames(10, 0, 100).end == 100);
    CHECK(sampleRegion::fromFrames(10, 500, 100).end == 100);
    CHECK(sampleRegion::fromFrames(500, 0, 100).empty());
  }

  // ─── bufferRecorder ────────────────────────────────────────────────────────

  TEST_CASE("bufferRecorder: does nothing until started; the sync output is 0 (#1143)") {
    auto b = sampleBuffer::create(1, 300, RATE);
    b->fill(-1.f);
    sampleBufferWriter writer(b);
    bufferRecorder rec;
    CHECK_FALSE(rec.recording());
    CHECK_FALSE(rec.append());
    CHECK_FALSE(rec.loop());
    CHECK(rec.loopStart() == 0.f);
    CHECK(rec.loopEnd() == 0.f);

    buffer in = Counting(STANDARD_BUFFERSIZE, 0.f);
    const std::vector<float> sync = Block(rec, writer, in);
    REQUIRE(sync.size() == STANDARD_BUFFERSIZE);
    for (float v : sync)
      CHECK(v == 0.f);
    CHECK(At(*b, 0, 0) == -1.f);
    CHECK(At(*b, 0, 299) == -1.f);
  }

  TEST_CASE("bufferRecorder: records from the start and stops at the end of the buffer "
            "(#1143)") {
    // 300 frames take two whole blocks and 44 samples of a third.
    const UInt frames = 2 * STANDARD_BUFFERSIZE + 44;
    auto b = sampleBuffer::create(1, frames, RATE);
    sampleBufferWriter writer(b);
    bufferRecorder rec;
    rec.record(true);
    CHECK(rec.recording());

    for (UInt block = 0; block < 3; ++block) {
      CAPTURE(block);
      buffer in = Counting(STANDARD_BUFFERSIZE, static_cast<float>(block * STANDARD_BUFFERSIZE));
      const std::vector<float> sync = Block(rec, writer, in);
      for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
        const UInt frame = block * STANDARD_BUFFERSIZE + i;
        CAPTURE(frame);
        if (frame < frames) {
          CHECK(sync[i] == doctest::Approx(static_cast<float>(frame) / static_cast<float>(frames)));
        } else {
          CHECK(sync[i] == 0.f); // stopped at the end, mid-block
        }
      }
    }
    CHECK_FALSE(rec.recording()); // stopped the block it reached the end
    CHECK(rec.position() == frames);
    for (UInt n = 0; n < frames; ++n) {
      CAPTURE(n);
      CHECK(At(*b, 0, n) == static_cast<float>(n));
    }
  }

  TEST_CASE("bufferRecorder: between the loop points, in milliseconds (#1143)") {
    auto b = sampleBuffer::create(1, 200, RATE);
    b->fill(-1.f);
    sampleBufferWriter writer(b);
    bufferRecorder rec;
    rec.loopPoints(10.f, 50.f).record(true);
    {
      sampleWriteView view = writer.pin();
      const sampleRegion r = rec.region(view);
      CHECK(r.start == 10);
      CHECK(r.end == 50);
    }
    buffer in = Counting(STANDARD_BUFFERSIZE, 100.f);
    const std::vector<float> sync = Block(rec, writer, in);
    CHECK(At(*b, 0, 9) == -1.f);
    CHECK(At(*b, 0, 10) == 100.f);
    CHECK(At(*b, 0, 49) == 139.f);
    CHECK(At(*b, 0, 50) == -1.f);
    CHECK(sync[0] == 0.f);
    CHECK(sync[20] == doctest::Approx(0.5f));
    CHECK(sync[39] == doctest::Approx(39.f / 40.f));
    CHECK(sync[40] == 0.f);
    CHECK_FALSE(rec.recording());

    // reset: the whole buffer again; a new start begins at frame 0.
    rec.reset();
    CHECK(rec.loopStart() == 0.f);
    CHECK(rec.loopEnd() == 0.f);
    rec.record(true);
    buffer again = Counting(STANDARD_BUFFERSIZE, 1000.f);
    Block(rec, writer, again);
    CHECK(At(*b, 0, 0) == 1000.f);
    CHECK(At(*b, 0, 127) == 1127.f);
    CHECK(rec.recording());
  }

  TEST_CASE("bufferRecorder: loop carries on from the start point (#1143)") {
    auto b = sampleBuffer::create(1, 64, RATE);
    sampleBufferWriter writer(b);
    bufferRecorder rec;
    rec.loop(true).loopPoints(10.f, 20.f).record(true);
    // 128 samples over a 10-frame loop: 12 whole passes and 8 samples.
    buffer in = Counting(STANDARD_BUFFERSIZE, 0.f);
    const std::vector<float> sync = Block(rec, writer, in);
    CHECK(rec.recording());
    for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
      CAPTURE(i);
      CHECK(sync[i] == doctest::Approx(static_cast<float>(i % 10) / 10.f));
    }
    // The last pass wrote 120 .. 127 into frames 10 .. 17; 18 and 19 keep
    // the pass before.
    for (UInt n = 0; n < 8; ++n)
      CHECK(At(*b, 0, 10 + n) == 120.f + static_cast<float>(n));
    CHECK(At(*b, 0, 18) == 118.f);
    CHECK(At(*b, 0, 19) == 119.f);
    CHECK(At(*b, 0, 9) == 0.f);
    CHECK(At(*b, 0, 20) == 0.f);
    CHECK(rec.position() == 18);
  }

  TEST_CASE("bufferRecorder: append resumes where recording stopped (#1143)") {
    auto b = sampleBuffer::create(1, 200, RATE);
    sampleBufferWriter writer(b);
    bufferRecorder rec;

    buffer first = Counting(50, 0.f);
    rec.record(true);
    Block(rec, writer, first);
    rec.record(false);
    CHECK(rec.position() == 50);
    buffer idle = Counting(50, 900.f);
    Block(rec, writer, idle); // stopped: nothing written
    CHECK(At(*b, 0, 50) == 0.f);

    // With append, a start carries on at frame 50.
    rec.append(true).record(true);
    buffer second = Counting(50, 50.f);
    std::vector<float> sync = Block(rec, writer, second);
    CHECK(sync[0] == doctest::Approx(50.f / 200.f));
    for (UInt n = 0; n < 100; ++n)
      CHECK(At(*b, 0, n) == static_cast<float>(n));

    // Without it, a start, even while recording, begins at the start point.
    rec.append(false).record(true);
    buffer third = Counting(10, 500.f);
    sync = Block(rec, writer, third);
    CHECK(sync[0] == 0.f);
    CHECK(At(*b, 0, 0) == 500.f);
    CHECK(At(*b, 0, 9) == 509.f);
    CHECK(At(*b, 0, 10) == 10.f);

    // Append from a position outside the loop points starts at the start
    // point: here the recording that filled the buffer.
    rec.loopPoints(150.f, 0.f).record(true);
    buffer fill = Counting(STANDARD_BUFFERSIZE, 0.f);
    Block(rec, writer, fill);
    CHECK_FALSE(rec.recording());
    CHECK(rec.position() == 200);
    rec.append(true).record(true);
    buffer more = Counting(10, 7000.f);
    Block(rec, writer, more);
    CHECK(At(*b, 0, 150) == 7000.f);
  }

  TEST_CASE("bufferRecorder: a start point moved past the position moves the recording "
            "(#1143)") {
    auto b = sampleBuffer::create(1, 300, RATE);
    sampleBufferWriter writer(b);
    bufferRecorder rec;
    rec.record(true);
    buffer a = Counting(10, 0.f);
    Block(rec, writer, a);
    CHECK(rec.position() == 10);
    rec.loopStart(100.f);
    buffer c = Counting(10, 100.f);
    Block(rec, writer, c);
    CHECK(At(*b, 0, 10) == 0.f);
    CHECK(At(*b, 0, 100) == 100.f);
    CHECK(rec.position() == 110);
    // Loop points that hold no frame write nothing, but the recording stays
    // started.
    rec.loopEnd(50.f); // before the start point
    buffer d = Counting(10, 1.f);
    std::vector<float> sync = Block(rec, writer, d);
    CHECK(rec.recording());
    CHECK(sync[0] == 0.f);
    CHECK(At(*b, 0, 110) == 0.f);
    // An end point below the position stops it.
    rec.loopStart(0.f);
    sync = Block(rec, writer, d);
    CHECK_FALSE(rec.recording());
    CHECK(sync[0] == 0.f);
    CHECK(At(*b, 0, 110) == 0.f);
    CHECK(At(*b, 0, 49) == 0.f);
  }

  TEST_CASE("bufferRecorder: input i into channel i; the rest left alone (#1143)") {
    auto b = sampleBuffer::create(2, 32, RATE);
    b->fill(-1.f);
    sampleBufferWriter writer(b);
    bufferRecorder rec;
    rec.record(true);

    // Three inputs into two channels: the third is dropped.
    buffer x = Counting(16, 0.f);
    buffer y = Counting(16, 100.f);
    buffer z = Counting(16, 200.f);
    buffer* ins[3] = {&x, &y, &z};
    {
      sampleWriteView view = writer.pin();
      buffer& sync = rec(view, ins, 3);
      CHECK(sync.getLength() == 16);
    }
    CHECK(At(*b, 0, 0) == 0.f);
    CHECK(At(*b, 0, 15) == 15.f);
    CHECK(At(*b, 1, 0) == 100.f);
    CHECK(At(*b, 1, 15) == 115.f);
    CHECK(At(*b, 0, 16) == -1.f);

    // One input: channel 2 keeps what it had.
    buffer w = Counting(16, 50.f);
    Block(rec, writer, w);
    CHECK(At(*b, 0, 16) == 50.f);
    CHECK(At(*b, 1, 16) == -1.f);

    // The block is as long as the shortest input.
    rec.record(true);
    buffer shortIn = Counting(4, 7.f);
    buffer* mixed[2] = {&x, &shortIn};
    sampleWriteView view = writer.pin();
    CHECK(rec(view, mixed, 2).getLength() == 4);
    CHECK(rec(view, mixed, 0).getLength() == 0);
  }

  TEST_CASE("bufferRecorder: no buffer, or an empty one, records once there is room "
            "(#1143)") {
    sampleBufferWriter writer; // unbound
    bufferRecorder rec;
    rec.record(true);
    buffer in = Counting(STANDARD_BUFFERSIZE, 1.f);
    std::vector<float> sync = Block(rec, writer, in);
    CHECK(sync[0] == 0.f);
    CHECK(rec.recording()); // still started

    auto b = sampleBuffer::create(1, 0, RATE);
    writer.bind(b);
    sync = Block(rec, writer, in);
    CHECK(sync[5] == 0.f);
    CHECK(rec.recording());

    // Sized later: recording begins at the start point.
    b->setSize(256);
    sync = Block(rec, writer, in);
    CHECK(At(*b, 0, 0) == 1.f);
    CHECK(sync[64] == doctest::Approx(0.25f));
    // A resize between blocks moves the end with it.
    b->setSize(130);
    buffer next = Counting(STANDARD_BUFFERSIZE, 500.f);
    sync = Block(rec, writer, next);
    CHECK(At(*b, 0, 127) == 127.f + 1.f);
    CHECK(At(*b, 0, 128) == 500.f);
    CHECK(At(*b, 0, 129) == 501.f);
    CHECK(sync[1] == doctest::Approx(129.f / 130.f));
    CHECK(sync[2] == 0.f);
    CHECK_FALSE(rec.recording());
  }

  TEST_CASE("bufferRecorder: the loop points follow the buffer's rate and the engine's "
            "(#1143)") {
    SampleRateGuard guard;
    bufferRecorder rec;
    rec.loopPoints(5.f, 10.f);

    // A buffer at the engine rate (0) follows SAMPLERATE as it changes.
    auto engine = sampleBuffer::create(1, 1000, 0.f);
    sampleBufferWriter writer(engine);
    YSE::SAMPLERATE = 1000;
    {
      sampleWriteView view = writer.pin();
      CHECK(rec.region(view).start == 5);
      CHECK(rec.region(view).end == 10);
    }
    YSE::SAMPLERATE = 8000;
    {
      sampleWriteView view = writer.pin();
      CHECK(rec.region(view).start == 40);
      CHECK(rec.region(view).end == 80);
    }
    rec.record(true);
    buffer in = Counting(STANDARD_BUFFERSIZE, 0.f);
    Block(rec, writer, in);
    CHECK(At(*engine, 0, 39) == 0.f);
    CHECK(At(*engine, 0, 40) == 0.f);
    CHECK(At(*engine, 0, 79) == 39.f);

    // A buffer with a rate of its own keeps it, whatever the engine's.
    auto own = sampleBuffer::create(1, 1000, 2000.f);
    sampleBufferWriter ownWriter(own);
    sampleWriteView view = ownWriter.pin();
    CHECK(rec.region(view).start == 10);
    CHECK(rec.region(view).end == 20);
    YSE::SAMPLERATE = 44100;
    CHECK(rec.region(view).end == 20);
  }

  TEST_CASE("bufferRecorder: blocks allocate nothing (#1143)") {
    auto b = sampleBuffer::create(4, 1000, RATE);
    sampleBufferWriter writer(b);
    bufferRecorder rec;
    buffer x = Counting(STANDARD_BUFFERSIZE, 0.f);
    buffer y = Counting(STANDARD_BUFFERSIZE, 1.f);
    buffer shortIn = Counting(32, 2.f);
    buffer* ins[2] = {&x, &y};
    buffer* mixed[2] = {&x, &shortIn};
    {
      TestHelpers::ProbeScope probe;
      for (int pass = 0; pass < 20; ++pass) {
        if (pass == 3) rec.loop(true).loopPoints(100.f, 150.f);
        if (pass == 8) rec.append(true).record(false);
        if (pass == 10) rec.record(true);
        if (pass == 15) rec.reset().loop(false);
        if (pass == 1) rec.record(true);
        sampleWriteView view = writer.pin();
        rec(view, (pass % 4) == 1 ? mixed : ins, 2); // a shorter block, and back
        rec(view, x);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE
