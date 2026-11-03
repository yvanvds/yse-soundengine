// Tests for YSE::DSP::groovePlayer (YseEngine/dsp/sampling/groovePlayer.hpp)
// and YSE::DSP::playerOutputs (playerOutputs.hpp), issue #1141: a sample
// buffer played at the speed of a signal, forward and backward, between loop
// points it jumps across (with a crossfade) or swings over, with a sync output,
// and the loop read where a phase says (#1142).
// The buffers hold a ramp (sample n is n + 1000 c), which the 4-point kernel
// reproduces exactly away from the ends, and run at 1000 Hz with the engine at
// 1000 Hz unless a test says otherwise, so a millisecond is a frame and a
// sample, and every expected value is in closed form.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>
#include "dsp/buffer.hpp"
#include "dsp/sampling/groovePlayer.hpp"
#include "dsp/sampling/loopCrossfade.hpp"
#include "dsp/sampling/playerOutputs.hpp"
#include "dsp/sampling/sampleBuffer.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::buffer;
using YSE::DSP::GROOVE_LOOP;
using YSE::DSP::groovePlayer;
using YSE::DSP::loopCrossfade;
using YSE::DSP::playerOutputs;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferReader;
using YSE::DSP::sampleView;

namespace {

  constexpr float RATE = 1000.f; // a frame per millisecond
  constexpr UInt N = STANDARD_BUFFERSIZE;

  struct SampleRateGuard {
    UInt saved;
    explicit SampleRateGuard(UInt rate) : saved(YSE::SAMPLERATE) {
      YSE::SAMPLERATE = rate;
    }
    ~SampleRateGuard() {
      YSE::SAMPLERATE = saved;
    }
    SampleRateGuard(const SampleRateGuard&) = delete;
    SampleRateGuard& operator=(const SampleRateGuard&) = delete;
  };

  // A buffer of `frames` frames at `rate`, channel c's sample n = n + 1000 c.
  std::shared_ptr<sampleBuffer> Ramp(UInt channels, UInt frames, float rate = RATE) {
    auto b = sampleBuffer::create(channels, frames, rate);
    std::vector<float> values(frames);
    for (UInt c = 0; c < channels; ++c) {
      for (UInt n = 0; n < frames; ++n)
        values[n] = static_cast<float>(n) + 1000.f * static_cast<float>(c);
      b->setSamples(c, 0, values.data(), frames);
    }
    return b;
  }

  std::vector<float> Copy(buffer& b) {
    return std::vector<float>(b.getPtr(), b.getPtr() + b.getLength());
  }

  // One block at a constant speed; a copy of output `c`.
  std::vector<float> Block(groovePlayer& g, sampleBufferReader& reader, float speed, UInt c = 0,
                           UInt length = N) {
    sampleView view = reader.pin();
    g(view, speed, length);
    return Copy(g.out(c));
  }

  bool Silent(const std::vector<float>& v, std::size_t from = 0) {
    for (std::size_t i = from; i < v.size(); ++i)
      if (v[i] != 0.f) return false;
    return true;
  }

} // namespace

TEST_SUITE("dsp") {

  // ─── playerOutputs ────────────────────────────────────────────────────────

  TEST_CASE("playerOutputs: channel k into output k, extra channels mixed, a crossfaded "
            "read (#1141)") {
    auto b = Ramp(3, 100);
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    playerOutputs outs(2);
    CHECK(outs.channels() == 2);
    CHECK(outs.clear(500) == N); // cut to the standard block
    CHECK(outs.clear(10) == 10);
    CHECK(outs.out(0).getLength() == 10);
    outs.read(view, 3, 40.0, nullptr, true);
    CHECK(outs.out(0).getPtr()[3] == 40.f + 2040.f); // channel 3 mixed into output 1
    CHECK(outs.out(1).getPtr()[3] == 1040.f);
    CHECK(outs.out(0).getPtr()[2] == 0.f);
    CHECK(&outs.out(7) == &outs.out(1)); // past the count: the last

    // A loop 20 .. 30 with a 4-frame fade: frame 28 is halfway to 18.
    const loopCrossfade x = loopCrossfade::make(20.0, 30.0, 4.0);
    outs.clear(10);
    outs.read(view, 0, 28.0, &x, true);
    CHECK(outs.out(1).getPtr()[0] == doctest::Approx(1023.f));
    outs.channels(0);
    CHECK(outs.channels() == 1);
    outs.channels(1000);
    CHECK(outs.channels() == playerOutputs::MAX_CHANNELS);
  }

  // ─── speed ────────────────────────────────────────────────────────────────

  TEST_CASE("groovePlayer: speed 1 plays at normal speed from position 0, the sync ramps over "
            "the whole buffer (#1141)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(1, 1000);
    sampleBufferReader reader(b);
    groovePlayer g;
    CHECK(g.playing());
    CHECK(g.loop() == GROOVE_LOOP::OFF);
    CHECK(g.loopStart() == 0.f);
    CHECK(g.loopEnd() == 0.f);
    std::vector<float> v = Block(g, reader, 1.f);
    std::vector<float> sync = Copy(g.sync());
    REQUIRE(v.size() == N);
    for (UInt i = 0; i < N; ++i) {
      CAPTURE(i);
      CHECK(v[i] == static_cast<float>(i));
      CHECK(sync[i] == doctest::Approx(static_cast<float>(i) / 1000.f));
    }
    CHECK(g.position() == doctest::Approx(static_cast<double>(N)));
    CHECK(Block(g, reader, 1.f)[3] == static_cast<float>(N + 3));
  }

  TEST_CASE("groovePlayer: faster, slower, backward, standing still, and a speed signal "
            "(#1141)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(1, 1000);
    sampleBufferReader reader(b);
    groovePlayer g;

    g.position(10.f);
    std::vector<float> fast = Block(g, reader, 2.f);
    CHECK(fast[0] == 10.f);
    CHECK(fast[50] == 110.f);
    g.position(10.f);
    std::vector<float> slow = Block(g, reader, 0.5f);
    CHECK(slow[1] == doctest::Approx(10.5f));
    CHECK(slow[40] == doctest::Approx(30.f));

    g.position(500.f);
    std::vector<float> back = Block(g, reader, -1.f);
    for (UInt i = 0; i < N; ++i) {
      CAPTURE(i);
      CHECK(back[i] == static_cast<float>(500 - i));
    }
    CHECK(g.position() == doctest::Approx(500.0 - N));

    // Speed 0 holds the sample at the position; not a stop.
    g.position(42.f);
    std::vector<float> still = Block(g, reader, 0.f);
    CHECK(still[0] == 42.f);
    CHECK(still[N - 1] == 42.f);
    CHECK(g.playing());
    // A speed that is not a number is 0.
    CHECK(Block(g, reader, std::numeric_limits<float>::quiet_NaN())[9] == 42.f);

    // A speed signal: 1 for 10 samples, then -2.
    buffer speed(N);
    for (UInt i = 0; i < N; ++i)
      speed.getPtr()[i] = i < 10 ? 1.f : -2.f;
    g.position(300.f);
    {
      sampleView view = reader.pin();
      g(view, speed);
    }
    std::vector<float> wave = Copy(g.out());
    CHECK(wave[9] == 309.f);
    CHECK(wave[10] == 310.f);
    CHECK(wave[11] == 308.f);
    CHECK(wave[20] == 290.f);
    // As long as the speed signal.
    buffer shortSpeed(40);
    shortSpeed.getPtr()[0] = 1.f;
    sampleView view = reader.pin();
    g(view, shortSpeed);
    CHECK(g.out().getLength() == 40);
    CHECK(g.sync().getLength() == 40);
  }

  TEST_CASE("groovePlayer: the position is kept inside the buffer; not a number or below 0 is 0 "
            "(#1141)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(1, 1000);
    sampleBufferReader reader(b);
    groovePlayer g;
    g.position(-50.f);
    CHECK(g.position() == 0.0);
    g.position(std::numeric_limits<float>::quiet_NaN());
    CHECK(g.position() == 0.0);
    g.position(5000.f); // past the end: at the end, backward plays down from it
    std::vector<float> v = Block(g, reader, -1.f);
    CHECK(v[0] == 999.f); // the end holds the last sample
    CHECK(v[1] == 999.f);
    CHECK(v[2] == 998.f);
  }

  // ─── the loop ─────────────────────────────────────────────────────────────

  TEST_CASE("groovePlayer: loop off plays through the loop points and stops off an end of the "
            "buffer until the next jump (#1141)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(1, 200);
    sampleBufferReader reader(b);
    groovePlayer g;
    g.loopPoints(20.f, 30.f).position(150.f);
    std::vector<float> v = Block(g, reader, 1.f);
    CHECK(v[49] == 199.f);
    CHECK(Silent(v, 50));
    CHECK_FALSE(g.playing());
    CHECK(Silent(Block(g, reader, 1.f)));
    CHECK(Silent(Copy(g.sync())));
    CHECK(Silent(Block(g, reader, -1.f))); // stopped either way

    // A jump plays again: through the loop points, with the sync inside them.
    g.position(15.f);
    std::vector<float> through = Block(g, reader, 1.f);
    std::vector<float> sync = Copy(g.sync());
    CHECK(through[20] == 35.f);
    CHECK(sync[4] == 0.f); // frame 19: before the loop
    CHECK(sync[5] == 0.f); // frame 20: the loop start
    CHECK(sync[10] == doctest::Approx(0.5f));
    CHECK(sync[15] == doctest::Approx(1.f)); // frame 30: the loop end
    CHECK(sync[16] == 0.f);

    // Backward it reads sample 0, then stops.
    g.position(5.f);
    std::vector<float> back = Block(g, reader, -1.f);
    CHECK(back[5] == 0.f);
    CHECK(back[4] == 1.f);
    CHECK(Silent(back, 5));
    CHECK_FALSE(g.playing());
    CHECK(g.position() == 0.0);
    // Ending on the last sample of a block stops there, not a block later.
    g.position(static_cast<float>(N - 1));
    Block(g, reader, -1.f);
    CHECK_FALSE(g.playing());
  }

  TEST_CASE("groovePlayer: loop on jumps between the loop points, forward and backward, and "
            "the sync ramps 0 to 1 over the loop (#1141)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(1, 1000);
    sampleBufferReader reader(b);
    groovePlayer g;
    g.loop(GROOVE_LOOP::ON).loopPoints(100.f, 110.f).position(100.f);
    std::vector<float> v = Block(g, reader, 1.f);
    std::vector<float> sync = Copy(g.sync());
    for (UInt i = 0; i < N; ++i) {
      CAPTURE(i);
      CHECK(v[i] == static_cast<float>(100 + i % 10));
      CHECK(sync[i] == doctest::Approx(static_cast<float>(i % 10) / 10.f));
    }
    CHECK(g.playing());

    // Twice as fast, the overshoot carries over the jump: 100 102 .. 108 100.
    g.position(100.f);
    std::vector<float> fast = Block(g, reader, 2.f);
    CHECK(fast[4] == 108.f);
    CHECK(fast[5] == 100.f);
    g.position(101.f);
    CHECK(Block(g, reader, 3.f)[3] == 100.f); // 101 104 107 (110 -> 100)

    // Before the loop, forward: plays into it.
    g.position(90.f);
    std::vector<float> into = Block(g, reader, 1.f);
    CHECK(into[9] == 99.f);
    CHECK(into[20] == 100.f);
    // After the loop, forward: on into the loop at once.
    g.position(500.f);
    CHECK(Block(g, reader, 1.f)[0] == 100.f);

    // Backward: below the start goes on from the end.
    g.position(105.f);
    std::vector<float> back = Block(g, reader, -1.f);
    CHECK(back[5] == 100.f);
    CHECK(back[6] == 109.f);
    CHECK(back[15] == 100.f);
    // After the loop, backward: plays down into it.
    g.position(120.f);
    std::vector<float> down = Block(g, reader, -1.f);
    CHECK(down[10] == 110.f);
    CHECK(down[11] == 109.f);
    CHECK(down[20] == 100.f);
    CHECK(down[21] == 109.f);

    // A loop whose end is not after its start holds nothing: played through.
    g.loopPoints(300.f, 200.f).position(250.f);
    std::vector<float> none = Block(g, reader, 1.f);
    CHECK(none[60] == 310.f);
    CHECK(Silent(Copy(g.sync())));
  }

  TEST_CASE("groovePlayer: pendulum turns round at each loop point (#1141)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(1, 1000);
    sampleBufferReader reader(b);
    groovePlayer g;
    g.loop(GROOVE_LOOP::PENDULUM).loopPoints(100.f, 110.f).position(100.f);
    std::vector<float> v = Block(g, reader, 1.f);
    std::vector<float> sync = Copy(g.sync());
    // A triangle between the loop points: 100 .. 110 .. 100 .., period 20.
    for (UInt i = 0; i < N; ++i) {
      CAPTURE(i);
      const UInt t = i % 20;
      const float expected = 100.f + static_cast<float>(t <= 10 ? t : 20 - t);
      CHECK(v[i] == expected);
      CHECK(sync[i] == doctest::Approx((expected - 100.f) / 10.f));
    }
    // A negative speed starts down and turns at the start point.
    g.position(105.f);
    std::vector<float> back = Block(g, reader, -1.f);
    CHECK(back[5] == 100.f);
    CHECK(back[6] == 101.f);
    CHECK(back[15] == 110.f);
    CHECK(back[16] == 109.f);
    // Turned, the speed flips with it: 3 frames a sample, reflected off 110.
    g.position(104.f);
    std::vector<float> fast = Block(g, reader, 3.f);
    CHECK(fast[2] == 110.f);
    CHECK(fast[3] == 107.f);
    // Past the loop: reflected back into it.
    g.position(112.f);
    CHECK(Block(g, reader, 1.f)[0] == 108.f);
  }

  TEST_CASE("groovePlayer: loopinterp crossfades a loop across its jump, in the direction it "
            "plays (#1141)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(1, 1000);
    sampleBufferReader reader(b);
    groovePlayer g;
    g.loop(GROOVE_LOOP::ON).loopInterp(true).interpTime(4.f).loopPoints(100.f, 110.f);
    g.position(100.f);
    std::vector<float> v = Block(g, reader, 1.f);
    // 106 .. 109 move toward 96 .. 99, ten frames back.
    CHECK(v[5] == 105.f);
    CHECK(v[6] == 106.f);
    CHECK(v[7] == doctest::Approx(104.5f));
    CHECK(v[8] == doctest::Approx(103.f));
    CHECK(v[9] == doctest::Approx(101.5f));
    CHECK(v[10] == 100.f); // the jump lands on the start, as the fade did
    CHECK(v[17] == doctest::Approx(104.5f));

    // Backward the frames above the start move toward those after the end.
    g.position(109.f);
    std::vector<float> back = Block(g, reader, -1.f);
    CHECK(back[5] == 104.f);
    CHECK(back[6] == doctest::Approx(105.5f));
    CHECK(back[8] == doctest::Approx(108.5f));
    CHECK(back[9] == doctest::Approx(110.f));
    CHECK(back[10] == 109.f);

    // Outside the loop, no crossfade; nor in a pendulum, nor with loop off.
    g.position(98.f);
    CHECK(Block(g, reader, 1.f)[0] == 98.f);
    g.loop(GROOVE_LOOP::PENDULUM).position(100.f);
    CHECK(Block(g, reader, 1.f)[8] == 108.f);
    g.loop(GROOVE_LOOP::OFF).position(100.f);
    CHECK(Block(g, reader, 1.f)[8] == 108.f);
    g.loop(GROOVE_LOOP::ON).loopInterp(false).position(100.f);
    CHECK(Block(g, reader, 1.f)[8] == 108.f);

    CHECK(g.interpTime(-3.f).interpTime() == 0.f);
    CHECK(groovePlayer().interpTime() == 50.f);
    CHECK_FALSE(groovePlayer().loopInterp());
  }

  // ─── jumps and stop ───────────────────────────────────────────────────────

  TEST_CASE("groovePlayer: startLoop, endLoop and position play after a stop; reset is the "
            "whole buffer (#1141)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(1, 1000);
    sampleBufferReader reader(b);
    groovePlayer g;
    g.loop(GROOVE_LOOP::ON).loopPoints(200.f, 300.f);
    Block(g, reader, 1.f);
    g.stop();
    CHECK_FALSE(g.playing());
    CHECK(Silent(Block(g, reader, 1.f)));
    CHECK(Silent(Copy(g.sync())));
    CHECK(g.position() == doctest::Approx(static_cast<double>(N))); // it stays

    g.startLoop();
    CHECK(g.playing());
    CHECK(Block(g, reader, 1.f)[0] == 200.f);
    g.stop().endLoop();
    std::vector<float> back = Block(g, reader, -1.f);
    CHECK(back[0] == 300.f);
    CHECK(back[1] == 299.f);
    g.stop().position(40.f);
    CHECK(Block(g, reader, 1.f)[0] == 40.f);

    // reset: both points 0, the loop the whole buffer.
    g.reset();
    CHECK(g.loopStart() == 0.f);
    CHECK(g.loopEnd() == 0.f);
    g.endLoop();
    Block(g, reader, 1.f); // from the end, forward: on from the start
    CHECK(g.out().getPtr()[0] == 0.f);
    CHECK(g.out().getPtr()[5] == 5.f);
    // With no loop points startLoop is the beginning.
    g.loop(GROOVE_LOOP::OFF).startLoop();
    CHECK(Block(g, reader, 1.f)[7] == 7.f);
    g.loopStart(12.f).loopEnd(20.f);
    CHECK(g.loopStart() == 12.f);
    CHECK(g.loopEnd() == 20.f);
  }

  // ─── buffers, channels and rates ──────────────────────────────────────────

  TEST_CASE("groovePlayer: channels play in order; extra buffer channels are mixed, extra "
            "outputs are silent (#1141)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(3, 1000);
    sampleBufferReader reader(b);
    groovePlayer two(2);
    CHECK(two.channels() == 2);
    std::vector<float> a = Block(two, reader, 1.f, 0);
    CHECK(a[5] == 5.f + 2005.f);
    CHECK(two.out(1).getPtr()[5] == 1005.f);
    groovePlayer four(4);
    Block(four, reader, 1.f);
    CHECK(four.out(2).getPtr()[5] == 2005.f);
    CHECK(Silent(Copy(four.out(3))));
    CHECK(groovePlayer(0).channels() == 1);
    CHECK(groovePlayer(500).channels() == groovePlayer::MAX_CHANNELS);
  }

  TEST_CASE("groovePlayer: no buffer is silence and keeps the position; the loop follows a "
            "resized buffer (#1141)") {
    SampleRateGuard rate(1000);
    groovePlayer g;
    sampleBufferReader none;
    g.position(30.f).endLoop();
    {
      sampleView view = none.pin();
      g(view, 1.f);
    }
    CHECK(Silent(Copy(g.out())));
    CHECK(Silent(Copy(g.sync())));
    CHECK(g.position() == doctest::Approx(30.0));

    // The jump waited for a buffer: its loop end (0: the buffer's end).
    auto b = Ramp(1, 500);
    sampleBufferReader reader(b);
    g.loop(GROOVE_LOOP::ON);
    std::vector<float> v = Block(g, reader, -1.f);
    CHECK(v[1] == 499.f);
    // Grown, the loop end grows with it.
    b->setSize(2000);
    g.position(495.f);
    CHECK(Block(g, reader, 1.f)[10] == 0.f); // the new frames are 0
    CHECK(g.playing());
    CHECK(g.sync().getPtr()[10] == doctest::Approx(505.f / 2000.f));
    // Shrunk under the position: held at the end.
    b->setSize(100);
    g.position(900.f);
    CHECK(Block(g, reader, 1.f)[0] == 0.f); // the end wraps to the loop start
  }

  TEST_CASE("groovePlayer: normal speed follows the engine rate and the buffer's (#1141)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(1, 4000, 2000.f);
    sampleBufferReader reader(b);
    groovePlayer g;
    std::vector<float> one = Block(g, reader, 1.f);
    CHECK(one[10] == 20.f); // two buffer frames an engine sample
    YSE::SAMPLERATE = 4000;
    std::vector<float> two = Block(g, reader, 1.f);
    CHECK(two[0] == 256.f);
    CHECK(two[2] == 257.f); // half a buffer frame an engine sample
    // The loop points are ms of the buffer: 100 ms is 200 frames here.
    g.loop(GROOVE_LOOP::ON).loopPoints(100.f, 150.f).startLoop();
    CHECK(Block(g, reader, 1.f)[0] == 200.f);
    CHECK(g.position() == doctest::Approx(100.0 + N * 0.25));
  }

  TEST_CASE("groovePlayer: blocks allocate nothing (#1141)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(3, 1000);
    sampleBufferReader reader(b);
    groovePlayer g(2);
    buffer speed(N);
    for (UInt i = 0; i < N; ++i)
      speed.getPtr()[i] = std::sin(static_cast<float>(i) * 0.1f) * 3.f;
    buffer few(40);
    few.getPtr()[3] = -1.f;
    {
      TestHelpers::ProbeScope probe;
      for (int pass = 0; pass < 40; ++pass) {
        if (pass == 1) g.position(500.f);
        if (pass == 3) g.loop(GROOVE_LOOP::ON).loopInterp(true).loopPoints(100.f, 150.f);
        if (pass == 8) g.interpTime(5.f).endLoop();
        if (pass == 12) g.loop(GROOVE_LOOP::PENDULUM).startLoop();
        if (pass == 18) g.loop(GROOVE_LOOP::OFF).position(990.f);
        if (pass == 22) g.stop();
        if (pass == 25) g.reset().position(10.f);
        sampleView view = reader.pin();
        if (pass % 3 == 0) {
          g(view, (pass % 2) == 0 ? speed : few);
        } else {
          g(view, (pass % 5) == 2 ? -1.5f : 2.f, (pass % 4) == 1 ? 40 : N);
        }
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  // The phase-driven block (#1142). Loop 100 to 200 ms: frame 100 + 100 p.

  TEST_CASE("groovePlayer::phase: a ramp reads the loop in a straight line, sync the phase "
            "(#1142)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(1, 1000);
    sampleBufferReader reader(b);
    groovePlayer g;
    g.loopPoints(100.f, 200.f);
    buffer ramp(N);
    for (UInt i = 0; i < N; ++i)
      ramp.getPtr()[i] = static_cast<float>(i) / static_cast<float>(N);
    sampleView view = reader.pin();
    buffer& out = g.phase(view, ramp);
    CHECK(&out == &g.out(0));
    for (UInt i = 0; i < N; ++i) {
      CAPTURE(i);
      const float p = ramp.getPtr()[i];
      CHECK(out.getPtr()[i] == doctest::Approx(100.f + 100.f * p));
      CHECK(g.sync().getPtr()[i] == doctest::Approx(p));
    }
    CHECK(g.position() == doctest::Approx(100.0 + 100.0 * (N - 1.0) / N));

    // A loop of another length: the same ramp plays it once, in the same time.
    g.loopPoints(10.f, 20.f);
    g.phase(view, ramp);
    for (UInt i = 0; i < N; ++i)
      CHECK(g.out().getPtr()[i] == doctest::Approx(10.f + 10.f * ramp.getPtr()[i]));

    // By default the loop is the whole buffer.
    g.reset();
    g.phase(view, 0.5f);
    CHECK(g.out().getPtr()[0] == doctest::Approx(500.f));
  }

  TEST_CASE("groovePlayer::phase: outside 0 to 1 wraps round the loop; 1 is the end; not a "
            "number holds (#1142)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(1, 1000);
    sampleBufferReader reader(b);
    groovePlayer g;
    g.loopPoints(100.f, 200.f);
    sampleView view = reader.pin();
    const auto at = [&](float phase) {
      g.phase(view, phase, 4);
      CHECK(g.out().getLength() == 4);
      return g.out().getPtr()[3];
    };
    CHECK(at(0.f) == 100.f);
    CHECK(at(0.25f) == doctest::Approx(125.f));
    CHECK(at(1.25f) == doctest::Approx(125.f));
    CHECK(g.sync().getPtr()[3] == doctest::Approx(0.25f));
    CHECK(at(-0.75f) == doctest::Approx(125.f));
    CHECK(at(1.f) == doctest::Approx(200.f)); // a one-shot ramp at rest
    CHECK(g.sync().getPtr()[3] == 1.f);
    CHECK(at(2.f) == doctest::Approx(100.f));
    CHECK(at(0.5f) == doctest::Approx(150.f));
    CHECK(at(std::numeric_limits<float>::quiet_NaN()) == doctest::Approx(150.f));
    CHECK(at(std::numeric_limits<float>::infinity()) == doctest::Approx(150.f));
    CHECK(g.sync().getPtr()[3] == doctest::Approx(0.5f));
  }

  TEST_CASE("groovePlayer::phase: a falling ramp plays backward, a held phase holds, loop mode "
            "plays no part (#1142)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(1, 1000);
    sampleBufferReader reader(b);
    groovePlayer g;
    g.loopPoints(100.f, 200.f);
    buffer falling(N);
    for (UInt i = 0; i < N; ++i)
      falling.getPtr()[i] = 1.f - static_cast<float>(i) / static_cast<float>(N);
    sampleView view = reader.pin();
    std::vector<float> off;
    for (GROOVE_LOOP mode : {GROOVE_LOOP::OFF, GROOVE_LOOP::ON, GROOVE_LOOP::PENDULUM}) {
      g.loop(mode);
      g.phase(view, falling);
      std::vector<float> now = Copy(g.out());
      CHECK(now[0] == doctest::Approx(200.f));
      CHECK(now[N - 1] < now[0]);
      if (off.empty()) off = now;
      CHECK(now == off);
      CHECK(g.playing()); // never runs off: no speed to carry it
    }
    g.phase(view, 0.3f);
    for (UInt i = 0; i < N; ++i)
      CHECK(g.out().getPtr()[i] == doctest::Approx(130.f));
  }

  TEST_CASE("groovePlayer::phase: the crossfade follows the ramp's direction, also across its "
            "wrap (#1142)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(1, 1000);
    sampleBufferReader reader(b);
    groovePlayer g;
    g.loopPoints(100.f, 200.f).loopInterp(true).interpTime(10.f);
    sampleView view = reader.pin();

    // Rising: the last 10 frames fade to the 10 frames before the loop start.
    // Frame 196 at 0.6 of the fade gives 196 + 0.6 (96 - 196) = 136. The wrap
    // reads 0 at frame 100: still rising, no fade there.
    buffer rising(5);
    const float up[5] = {0.5f, 0.75f, 0.96f, 0.f, 0.02f};
    std::copy(up, up + 5, rising.getPtr());
    g.phase(view, rising);
    CHECK(g.out().getPtr()[0] == doctest::Approx(150.f));
    CHECK(g.out().getPtr()[1] == doctest::Approx(175.f));
    CHECK(g.out().getPtr()[2] == doctest::Approx(136.f));
    CHECK(g.out().getPtr()[3] == doctest::Approx(100.f));
    CHECK(g.out().getPtr()[4] == doctest::Approx(102.f));

    // Falling: the first 10 frames fade to the 10 frames after the loop end.
    // Frame 104 at 0.6 of the fade gives 104 + 0.6 (204 - 104) = 164. The wrap
    // reads 0.98 at frame 198: still falling, no fade there.
    buffer down(5);
    const float dn[5] = {0.5f, 0.25f, 0.04f, 0.98f, 0.9f};
    std::copy(dn, dn + 5, down.getPtr());
    g.phase(view, down);
    CHECK(g.out().getPtr()[0] == doctest::Approx(150.f));
    CHECK(g.out().getPtr()[1] == doctest::Approx(125.f));
    CHECK(g.out().getPtr()[2] == doctest::Approx(164.f));
    CHECK(g.out().getPtr()[3] == doctest::Approx(198.f));
    CHECK(g.out().getPtr()[4] == doctest::Approx(190.f));

    // A held phase keeps the last direction: still falling.
    buffer settle(2);
    settle.getPtr()[0] = 0.5f;
    settle.getPtr()[1] = 0.04f;
    g.phase(view, settle);
    g.phase(view, 0.04f, 4);
    CHECK(g.out().getPtr()[3] == doctest::Approx(164.f));

    // Off, or no length: the plain read.
    g.interpTime(0.f);
    g.phase(view, 0.04f, 4);
    CHECK(g.out().getPtr()[3] == doctest::Approx(104.f));
    g.interpTime(10.f).loopInterp(false);
    g.phase(view, 0.04f, 4);
    CHECK(g.out().getPtr()[3] == doctest::Approx(104.f));
  }

  TEST_CASE("groovePlayer::phase: silence without a loop, a buffer, or after a stop; a speed "
            "takes over where it read (#1142)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(1, 1000);
    sampleBufferReader reader(b);
    groovePlayer g;
    sampleView view = reader.pin();

    // A loop that holds nothing.
    g.loopPoints(300.f, 300.f);
    g.phase(view, 0.5f);
    CHECK(Silent(Copy(g.out())));
    CHECK(Silent(Copy(g.sync())));

    // A stop lasts until a jump; the jump only starts it again.
    g.loopPoints(100.f, 200.f).stop();
    g.phase(view, 0.5f);
    CHECK(Silent(Copy(g.out())));
    CHECK_FALSE(g.playing());
    g.position(700.f);
    g.phase(view, 0.5f);
    CHECK(g.out().getPtr()[0] == doctest::Approx(150.f));
    g.stop().endLoop();
    g.phase(view, 0.5f);
    CHECK(g.out().getPtr()[0] == doctest::Approx(150.f));

    // A speed goes on from where the phase read.
    g.phase(view, 0.5f, 1);
    CHECK(g.position() == doctest::Approx(150.0));
    g.loop(GROOVE_LOOP::OFF);
    g(view, 1.f, 3);
    CHECK(g.out().getPtr()[0] == doctest::Approx(150.f));
    CHECK(g.out().getPtr()[2] == doctest::Approx(152.f));

    // No buffer, and an empty one.
    sampleBufferReader none;
    {
      sampleView nothing = none.pin();
      g.phase(nothing, 0.5f);
      CHECK(Silent(Copy(g.out())));
      CHECK(Silent(Copy(g.sync())));
    }
    auto empty = sampleBuffer::create(1, 0, RATE);
    sampleBufferReader emptyReader(empty);
    {
      sampleView nothing = emptyReader.pin();
      g.phase(nothing, 0.5f);
      CHECK(Silent(Copy(g.out())));
    }
  }

  TEST_CASE("groovePlayer::phase: follows the buffer as it is, not the engine's rate (#1142)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(1, 1000);
    sampleBufferReader reader(b);
    groovePlayer g;
    g.loopPoints(100.f, 200.f);
    {
      sampleView view = reader.pin();
      g.phase(view, 0.5f);
      CHECK(g.out().getPtr()[0] == doctest::Approx(150.f));
      YSE::SAMPLERATE = 48000; // the ramp carries the tempo
      g.phase(view, 0.5f);
      CHECK(g.out().getPtr()[0] == doctest::Approx(150.f));
    }

    // The points are ms of the buffer: 100 ms is 200 frames at 2000 Hz.
    b->setSampleRate(2000.f);
    {
      sampleView view = reader.pin();
      g.phase(view, 0.5f);
      CHECK(g.out().getPtr()[0] == doctest::Approx(300.f));
    }

    // A loop end past the buffer is its end, and follows a resize.
    b->setSampleRate(RATE);
    g.loopPoints(0.f, 0.f);
    {
      sampleView view = reader.pin();
      g.phase(view, 0.5f);
      CHECK(g.out().getPtr()[0] == doctest::Approx(500.f));
    }
    b->setSize(500);
    {
      sampleView view = reader.pin();
      g.phase(view, 0.5f);
      CHECK(g.out().getPtr()[0] == doctest::Approx(250.f));
    }
  }

  TEST_CASE("groovePlayer::phase: channels as playerOutputs; a block as long as its phases, cut "
            "to STANDARD_BUFFERSIZE (#1142)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(3, 1000);
    sampleBufferReader reader(b);
    groovePlayer g(2);
    g.loopPoints(100.f, 200.f);
    sampleView view = reader.pin();
    buffer few(40);
    std::fill(few.getPtr(), few.getPtr() + 40, 0.5f);
    g.phase(view, few);
    CHECK(g.out(0).getLength() == 40);
    CHECK(g.sync().getLength() == 40);
    // Channels 0 and 2 mix into output 0; channel 1 is output 1.
    CHECK(g.out(0).getPtr()[39] == doctest::Approx(150.f + 2150.f));
    CHECK(g.out(1).getPtr()[39] == doctest::Approx(1150.f));
    g.phase(view, 0.5f, N * 4);
    CHECK(g.out(0).getLength() == N);
    g.channels(3);
    g.phase(view, 0.5f);
    CHECK(g.out(2).getPtr()[0] == doctest::Approx(2150.f));
  }

  TEST_CASE("groovePlayer::phase: blocks allocate nothing (#1142)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(3, 1000);
    sampleBufferReader reader(b);
    groovePlayer g(2);
    buffer ramp(N);
    for (UInt i = 0; i < N; ++i)
      ramp.getPtr()[i] = static_cast<float>(i) * 0.03f - 1.f;
    buffer few(40);
    few.getPtr()[3] = 0.5f;
    {
      TestHelpers::ProbeScope probe;
      for (int pass = 0; pass < 40; ++pass) {
        if (pass == 3) g.loopInterp(true).loopPoints(100.f, 150.f);
        if (pass == 8) g.interpTime(5.f).endLoop();
        if (pass == 22) g.stop();
        if (pass == 25) g.reset().position(10.f);
        sampleView view = reader.pin();
        if (pass % 3 == 0) {
          g.phase(view, (pass % 2) == 0 ? ramp : few);
        } else {
          g.phase(view, 0.25f, (pass % 4) == 1 ? 40 : N);
        }
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE
