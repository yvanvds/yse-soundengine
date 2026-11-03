// Tests for YSE::DSP::bufferPlayer (YseEngine/dsp/sampling/bufferPlayer.hpp),
// YSE::DSP::loopCrossfade (loopCrossfade.hpp) and sampleView::cubic, issue
// #1140: a sample buffer played from a start to an end point at normal speed
// or over a duration, backward, in a loop with a crossfade across the jump,
// or at the positions of a signal. The buffers hold a ramp (sample n is n) or
// a cube, which the 4-point kernel reproduces exactly, and run at 1000 Hz
// with the engine at 1000 Hz unless a test says otherwise, so a millisecond
// is a frame and a sample, and every expected value is in closed form.

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>
#include "dsp/buffer.hpp"
#include "dsp/interpolate4.hpp"
#include "dsp/sampling/bufferPlayer.hpp"
#include "dsp/sampling/loopCrossfade.hpp"
#include "dsp/sampling/sampleBuffer.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::buffer;
using YSE::DSP::bufferPlayer;
using YSE::DSP::loopCrossfade;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferReader;
using YSE::DSP::sampleView;

namespace {

  constexpr float RATE = 1000.f; // a frame per millisecond

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

  // One block played on its own; a copy of output `c`.
  std::vector<float> Block(bufferPlayer& play, sampleBufferReader& reader, UInt c = 0,
                           UInt length = STANDARD_BUFFERSIZE) {
    sampleView view = reader.pin();
    play(view, length);
    buffer& out = play.out(c);
    return std::vector<float>(out.getPtr(), out.getPtr() + out.getLength());
  }

  bool Silent(const std::vector<float>& v, std::size_t from = 0) {
    for (std::size_t i = from; i < v.size(); ++i)
      if (v[i] != 0.f) return false;
    return true;
  }

} // namespace

TEST_SUITE("dsp") {

  // ─── sampleView::cubic ────────────────────────────────────────────────────

  TEST_CASE("sampleView::cubic: exact at the samples, a cube in between, held at the ends "
            "(#1140)") {
    const UInt frames = 12;
    auto b = sampleBuffer::create(1, frames, RATE);
    std::vector<float> cube(frames);
    for (UInt n = 0; n < frames; ++n)
      cube[n] = static_cast<float>(n * n * n) * 0.01f;
    b->setSamples(0, 0, cube.data(), frames);
    sampleBufferReader reader(b);
    sampleView view = reader.pin();

    for (UInt n = 0; n < frames; ++n)
      CHECK(view.cubic(0, static_cast<double>(n)) == cube[n]);
    // A 4-point Lagrange kernel reproduces a cubic between two inner samples.
    for (double p : {1.25, 2.5, 5.75, 9.5}) {
      CAPTURE(p);
      CHECK(view.cubic(0, p) == doctest::Approx(p * p * p * 0.01).epsilon(1e-4));
    }
    // The same value interpolate4 gives on the same samples.
    buffer data(frames);
    for (UInt n = 0; n < frames; ++n)
      data.getPtr()[n] = cube[n];
    YSE::DSP::interpolate4 interp;
    interp.source(data);
    buffer at(1);
    at.getPtr()[0] = 4.3f;
    CHECK(interp(at).getPtr()[0] == doctest::Approx(view.cubic(0, 4.3f)));

    // Held inside: below 0 the first sample, past the last the last one.
    CHECK(view.cubic(0, -3.0) == cube[0]);
    CHECK(view.cubic(0, std::numeric_limits<double>::quiet_NaN()) == cube[0]);
    CHECK(view.cubic(0, 11.5) == cube[11]);
    CHECK(view.cubic(0, 400.0) == cube[11]);
    // A missing channel, an empty view.
    CHECK(view.cubic(1, 2.0) == 0.f);
    sampleBufferReader none;
    CHECK(none.pin().cubic(0, 1.0) == 0.f);

    // At the edges the neighbours repeat the end samples. On a ramp 0 1 2 3
    // the inner span stays linear; next to the first sample the repeated 0
    // bends it: 0.5 (1 - (0.5 (2 - 1 - 3) + 2) / 12) = 0.4375.
    auto ramp = Ramp(1, 4);
    sampleBufferReader r(ramp);
    sampleView v = r.pin();
    CHECK(v.cubic(0, 0.5) == doctest::Approx(0.4375f));
    CHECK(v.cubic(0, 1.5) == doctest::Approx(1.5f));
  }

  // ─── loopCrossfade ────────────────────────────────────────────────────────

  TEST_CASE("loopCrossfade: the fade is cut to the loop, the weight rises to the jump "
            "(#1140)") {
    loopCrossfade x = loopCrossfade::make(110.0, 100.0, 4.0); // either order
    CHECK(x.low == 100.0);
    CHECK(x.high == 110.0);
    CHECK(x.fade == 4.0);
    CHECK(loopCrossfade::make(100.0, 110.0, 50.0).fade == 10.0);
    CHECK(loopCrossfade::make(100.0, 110.0, -1.0).fade == 0.0);
    CHECK(loopCrossfade::make(100.0, 110.0, std::numeric_limits<double>::quiet_NaN()).fade == 0.0);

    double partner = -1.0;
    CHECK(x.weight(105.0, true, partner) == 0.f);
    CHECK(x.weight(106.0, true, partner) == 0.f);
    CHECK(x.weight(108.0, true, partner) == doctest::Approx(0.5f));
    CHECK(partner == 98.0);
    CHECK(x.weight(103.0, false, partner) == doctest::Approx(0.25f));
    CHECK(partner == 113.0);
    CHECK(x.weight(104.0, false, partner) == 0.f);
    CHECK(loopCrossfade::make(0.0, 10.0, 0.0).weight(9.9, true, partner) == 0.f);
  }

  // ─── bufferPlayer ─────────────────────────────────────────────────────────

  TEST_CASE("bufferPlayer: start() plays the whole buffer at normal speed, done in the "
            "block it ends (#1140)") {
    SampleRateGuard rate(1000);
    const UInt frames = STANDARD_BUFFERSIZE + 30;
    auto b = Ramp(1, frames);
    sampleBufferReader reader(b);
    bufferPlayer play;
    CHECK_FALSE(play.playing());
    CHECK(Silent(Block(play, reader)));
    CHECK_FALSE(play.done());

    play.start();
    CHECK(play.playing());
    std::vector<float> one = Block(play, reader);
    for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
      CAPTURE(i);
      CHECK(one[i] == static_cast<float>(i));
    }
    CHECK_FALSE(play.done());
    CHECK(play.position() == doctest::Approx(STANDARD_BUFFERSIZE));
    std::vector<float> two = Block(play, reader);
    CHECK(two[0] == static_cast<float>(STANDARD_BUFFERSIZE));
    CHECK(two[29] == static_cast<float>(frames - 1));
    CHECK(Silent(two, 30));
    CHECK(play.done());
    CHECK_FALSE(play.playing());
    // Once: the next block is silent and not done.
    CHECK(Silent(Block(play, reader)));
    CHECK_FALSE(play.done());

    // A buffer that ends on a block boundary is done in its last block.
    auto exact = Ramp(1, STANDARD_BUFFERSIZE);
    sampleBufferReader er(exact);
    play.start();
    Block(play, er);
    CHECK(play.done());
  }

  TEST_CASE("bufferPlayer: a start, an end and a duration set the span and the speed "
            "(#1140)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(1, 1000);
    sampleBufferReader reader(b);
    bufferPlayer play;

    // 100 ms of buffer over 50 ms: twice as fast.
    play.start(0.f, 100.f, 50.f);
    std::vector<float> fast = Block(play, reader);
    for (UInt i = 0; i < 50; ++i)
      CHECK(fast[i] == static_cast<float>(2 * i));
    CHECK(Silent(fast, 50));
    CHECK(play.done());

    // Over 200 ms: half speed, through the kernel (exact on a ramp).
    play.start(300.f, 400.f, 200.f);
    std::vector<float> slow = Block(play, reader);
    CHECK(slow[0] == 300.f);
    CHECK(slow[1] == doctest::Approx(300.5f));
    CHECK(slow[101] == doctest::Approx(350.5f));
    CHECK_FALSE(play.done());

    // From a start to the end of the buffer.
    play.start(990.f);
    std::vector<float> tail = Block(play, reader);
    CHECK(tail[0] == 990.f);
    CHECK(tail[9] == 999.f);
    CHECK(Silent(tail, 10));
    CHECK(play.done());

    // Points past the buffer are held at its end: nothing to play, done.
    play.start(5000.f, 6000.f);
    CHECK(Silent(Block(play, reader)));
    CHECK(play.done());
    CHECK_FALSE(play.playing());
    // A span of nothing ends at once.
    play.start(50.f, 50.f);
    Block(play, reader);
    CHECK(play.done());
    // Not a number: the defaults (0, the buffer's end, normal speed).
    const float nan = std::numeric_limits<float>::quiet_NaN();
    play.start(nan, nan, nan);
    std::vector<float> whole = Block(play, reader);
    CHECK(whole[5] == 5.f);
    CHECK(whole[127] == 127.f);
  }

  TEST_CASE("bufferPlayer: an end before the start plays backward (#1140)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(1, 1000);
    sampleBufferReader reader(b);
    bufferPlayer play;
    // Max's "500, 0 500" at 1 ms a frame: from 100 down to just above 0.
    play.start(100.f, 0.f);
    std::vector<float> back = Block(play, reader);
    for (UInt i = 0; i < 100; ++i)
      CHECK(back[i] == static_cast<float>(100 - i));
    CHECK(Silent(back, 100));
    CHECK(play.done());

    // Backward twice as fast.
    play.start(400.f, 200.f, 100.f);
    std::vector<float> fast = Block(play, reader);
    CHECK(fast[0] == 400.f);
    CHECK(fast[99] == 202.f);
    CHECK(Silent(fast, 100));
  }

  TEST_CASE("bufferPlayer: loop carries on from the start point; off, it ends at the next "
            "end (#1140)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(1, 1000);
    sampleBufferReader reader(b);
    bufferPlayer play;
    play.loop(true).start(10.f, 20.f);
    for (int pass = 0; pass < 3; ++pass) {
      std::vector<float> v = Block(play, reader);
      for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
        CAPTURE(i);
        CHECK(v[i] == static_cast<float>(10 + (pass * STANDARD_BUFFERSIZE + i) % 10));
      }
      CHECK_FALSE(play.done());
      CHECK(play.playing());
    }
    play.loop(false);
    std::vector<float> last = Block(play, reader);
    // The three blocks were 384 samples: 4 into a pass, 6 left of it.
    CHECK(last[0] == 14.f);
    CHECK(last[5] == 19.f);
    CHECK(Silent(last, 6));
    CHECK(play.done());

    // A backward loop, and at a speed that skips a whole loop per sample.
    play.loop(true).start(20.f, 10.f);
    std::vector<float> back = Block(play, reader);
    CHECK(back[0] == 20.f);
    CHECK(back[9] == 11.f);
    CHECK(back[10] == 20.f);
    play.start(0.f, 10.f, 0.5f); // 20 frames a sample: always frame 0
    std::vector<float> skip = Block(play, reader);
    CHECK(skip[0] == 0.f);
    CHECK(skip[1] == 0.f);
    CHECK(skip[100] == 0.f);
    CHECK(play.playing());
  }

  TEST_CASE("bufferPlayer: loopinterp crossfades the end of the loop into what leads up to "
            "its start (#1140)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(1, 1000);
    sampleBufferReader reader(b);
    bufferPlayer play;
    play.loop(true).loopInterp(true).interpTime(4.f).start(100.f, 110.f);
    std::vector<float> v = Block(play, reader);
    // Samples 100 .. 105 as they are; 106 .. 109 move toward 96 .. 99, ten
    // frames back: p - 10 g, g = (p - 106) / 4.
    CHECK(v[5] == 105.f);
    CHECK(v[6] == 106.f);
    CHECK(v[7] == doctest::Approx(104.5f));
    CHECK(v[8] == doctest::Approx(103.f));
    CHECK(v[9] == doctest::Approx(101.5f));
    CHECK(v[10] == 100.f); // the jump lands on the start, as the fade did
    CHECK(v[17] == doctest::Approx(104.5f));

    // Without loopinterp, or without loop, no crossfade.
    play.loopInterp(false).start(100.f, 110.f);
    CHECK(Block(play, reader)[8] == 108.f);
    play.loopInterp(true).loop(false).start(100.f, 110.f);
    CHECK(Block(play, reader)[8] == 108.f);

    // Backward: the frames above the end (just above 100) move toward the
    // ones after the start: p + 10 g, g = (104 - p) / 4.
    play.loop(true).start(110.f, 100.f);
    std::vector<float> back = Block(play, reader);
    CHECK(back[0] == 110.f);
    CHECK(back[6] == 104.f);
    CHECK(back[7] == doctest::Approx(105.5f));
    CHECK(back[9] == doctest::Approx(108.5f));
    CHECK(back[10] == 110.f);

    // A crossfade longer than the loop is the loop: p - 10 (p - 100) / 10.
    play.interpTime(500.f).start(100.f, 110.f);
    std::vector<float> all = Block(play, reader);
    CHECK(all[0] == 100.f);
    CHECK(all[5] == doctest::Approx(100.f));
    CHECK(all[9] == doctest::Approx(100.f));
    CHECK(play.interpTime() == 500.f);
    CHECK(play.interpTime(-3.f).interpTime() == 0.f);
    CHECK(bufferPlayer().interpTime() == 50.f);
  }

  TEST_CASE("bufferPlayer: pause holds, resume goes on, stop is not an end (#1140)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(1, 1000);
    sampleBufferReader reader(b);
    bufferPlayer play;
    play.start();
    Block(play, reader);
    play.pause();
    CHECK(play.paused());
    CHECK(play.playing());
    CHECK(Silent(Block(play, reader)));
    CHECK(play.position() == doctest::Approx(STANDARD_BUFFERSIZE));
    play.resume();
    CHECK(Block(play, reader)[0] == static_cast<float>(STANDARD_BUFFERSIZE));

    play.stop();
    CHECK_FALSE(play.playing());
    CHECK(Silent(Block(play, reader)));
    CHECK_FALSE(play.done());
    play.resume(); // nothing to resume
    CHECK(Silent(Block(play, reader)));
    play.pause(); // nothing to pause
    CHECK_FALSE(play.paused());

    // A start while paused plays again from its start point.
    play.start(10.f);
    Block(play, reader);
    play.pause();
    play.start(20.f);
    CHECK(Block(play, reader)[0] == 20.f);
  }

  TEST_CASE("bufferPlayer: following a position signal in ms (#1140)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(1, 300, 2000.f); // two frames a millisecond
    sampleBufferReader reader(b);
    bufferPlayer play;
    buffer positions(STANDARD_BUFFERSIZE);
    for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i)
      positions.getPtr()[i] = 100.f - 0.25f * static_cast<float>(i); // backward, half speed
    positions.getPtr()[0] = -5.f;
    positions.getPtr()[1] = 1000.f;
    {
      sampleView view = reader.pin();
      play(view, positions);
    }
    const float* out = play.out().getPtr();
    CHECK(out[0] == 0.f); // held at the first sample
    CHECK(out[1] == 299.f); // and at the last
    CHECK(out[2] == doctest::Approx(199.f));
    CHECK(out[3] == doctest::Approx(198.5f));
    CHECK(out[127] == doctest::Approx(136.5f));
    CHECK_FALSE(play.done());

    // Playback on its own neither moves nor ends meanwhile.
    play.start(10.f, 11.f);
    {
      sampleView view = reader.pin();
      play(view, positions);
      play(view, positions);
    }
    CHECK(play.playing());
    CHECK_FALSE(play.done());
    // A shorter position block gives shorter outputs.
    buffer few(16);
    few.getPtr()[3] = 2.f;
    {
      sampleView view = reader.pin();
      play(view, few);
    }
    CHECK(play.out().getLength() == 16);
    CHECK(play.out().getPtr()[3] == 4.f);
  }

  TEST_CASE("bufferPlayer: channels play in order; extra buffer channels are mixed, extra "
            "outputs silent (#1140)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(3, 500);
    sampleBufferReader reader(b);
    bufferPlayer two(2);
    CHECK(two.channels() == 2);
    two.start();
    Block(two, reader);
    CHECK(two.out(0).getPtr()[5] == 5.f + 2005.f); // channels 1 and 3
    CHECK(two.out(1).getPtr()[5] == 1005.f);

    bufferPlayer four(4);
    four.start();
    Block(four, reader);
    CHECK(four.out(2).getPtr()[7] == 2007.f);
    CHECK(Silent(std::vector<float>(four.out(3).getPtr(), four.out(3).getPtr() + 128)));
    CHECK(&four.out(9) == &four.out(3)); // past the count: the last

    CHECK(bufferPlayer(0).channels() == 1);
    CHECK(bufferPlayer(1000).channels() == bufferPlayer::MAX_CHANNELS);
  }

  TEST_CASE("bufferPlayer: no buffer waits; a resized buffer moves the end; a shorter "
            "block (#1140)") {
    SampleRateGuard rate(1000);
    sampleBufferReader none;
    bufferPlayer play;
    play.start(5.f);
    CHECK(Silent(Block(play, none)));
    CHECK(play.playing()); // waits for something to play

    auto b = Ramp(1, 1000);
    sampleBufferReader reader(b);
    CHECK(Block(play, reader)[0] == 5.f); // from its start point

    // The buffer shrinks under a play to its end: the end follows it.
    b->setSize(STANDARD_BUFFERSIZE + 10);
    std::vector<float> v = Block(play, reader);
    CHECK(v[0] == static_cast<float>(STANDARD_BUFFERSIZE + 5));
    CHECK(v[4] == static_cast<float>(STANDARD_BUFFERSIZE + 9));
    CHECK(Silent(v, 5));
    CHECK(play.done());

    auto longer = Ramp(1, 1000);
    sampleBufferReader lr(longer);
    play.start();
    std::vector<float> shortBlock = Block(play, lr, 0, 32);
    CHECK(shortBlock.size() == 32);
    CHECK(Block(play, lr, 0, 1000).size() == STANDARD_BUFFERSIZE);
    CHECK(Block(play, lr)[0] == 32.f + STANDARD_BUFFERSIZE);
  }

  TEST_CASE("bufferPlayer: normal speed follows the engine rate and the buffer's (#1140)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(1, 4000, 2000.f);
    sampleBufferReader reader(b);
    bufferPlayer play;
    play.start();
    std::vector<float> one = Block(play, reader);
    CHECK(one[10] == 20.f); // two buffer frames an engine sample
    YSE::SAMPLERATE = 4000;
    std::vector<float> two = Block(play, reader);
    CHECK(two[0] == 256.f);
    CHECK(two[2] == 257.f); // half a buffer frame an engine sample
    // A duration is in ms of the engine's time, whatever its rate.
    play.start(0.f, 1000.f, 500.f); // 2000 frames over 2000 samples
    CHECK(Block(play, reader)[10] == 10.f);
  }

  TEST_CASE("bufferPlayer: blocks allocate nothing (#1140)") {
    SampleRateGuard rate(1000);
    auto b = Ramp(3, 1000);
    sampleBufferReader reader(b);
    bufferPlayer play(2);
    buffer positions(STANDARD_BUFFERSIZE);
    for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i)
      positions.getPtr()[i] = static_cast<float>(i) * 1.5f;
    buffer few(40);
    {
      TestHelpers::ProbeScope probe;
      for (int pass = 0; pass < 30; ++pass) {
        if (pass == 1) play.start();
        if (pass == 3) play.loop(true).loopInterp(true).start(100.f, 150.f, 20.f);
        if (pass == 6) play.pause();
        if (pass == 8) play.resume();
        if (pass == 10) play.start(600.f, 300.f);
        if (pass == 15) play.interpTime(5.f).start(900.f, 0.f, 10.f);
        if (pass == 20) play.loop(false).stop();
        if (pass == 22) play.start(10.f);
        sampleView view = reader.pin();
        play(view, (pass % 5) == 2 ? 40 : STANDARD_BUFFERSIZE);
        if (pass % 7 == 0) play(view, (pass % 2) == 0 ? positions : few);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE
