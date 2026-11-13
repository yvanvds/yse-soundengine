// Tests for YSE::DSP::beatSpan (YseEngine/dsp/generators/beatSpan.hpp) and the
// two classes that follow a beat clock through it, issue #1278: DSP::phasor
// with a period in beats (unlocked and locked) and DSP::clockFollower's clock
// mode.
//
// The clock is a real CLOCK::domainClock advanced one block at a time, read
// the way the patcher reads it: after the clock's update, so the reading is
// the end of the block. Reference output is closed form: at a tempo of b BPM
// a sample is b / (60 * SAMPLERATE) beats, the block starts where the reading
// before it ended, and a ramp with a period of P beats is beat / P, wrapped.

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <vector>
#include "clock/domainClock.h"
#include "dsp/generators/beatSpan.hpp"
#include "dsp/generators/clockFollower.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::CLOCK::domainClock;
using YSE::DSP::beatSpan;
using YSE::DSP::buffer;
using YSE::DSP::clockFollower;
using YSE::DSP::phasor;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  double Frac(double x) {
    return x - std::floor(x);
  }

  // How far @p value is from @p target on the 0..1 circle.
  double CircleDistance(double value, double target) {
    double d = value - target;
    d -= std::floor(d + 0.5);
    return std::fabs(d);
  }

  // One engine block: advance the clock, then read it, in the engine's order.
  struct Clock {
    domainClock clock;
    beatSpan span;
    double before = 0.0; // the beat at the start of the last block

    explicit Clock(float bpm) : clock("beatspan.test", bpm) {}

    const beatSpan& Block() {
      before = clock.beatPosition();
      clock.update(static_cast<Flt>(N) / static_cast<Flt>(YSE::SAMPLERATE));
      span.read(clock.beatPosition(), clock.currentTempo(), N);
      return span;
    }
    void Tempo(float bpm) {
      clock.requestTempo(bpm, 0.f);
    }
  };

  // The clock bytes of the last report.
  std::vector<int> Bytes(const clockFollower& cf) {
    std::vector<int> out;
    out.reserve(cf.clockBytes());
    for (UInt i = 0; i < cf.clockBytes(); ++i)
      out.push_back(cf.clockByte(i));
    return out;
  }

  int Count(const std::vector<int>& bytes, int value) {
    int n = 0;
    for (int b : bytes)
      if (b == value) n++;
    return n;
  }
} // namespace

TEST_SUITE("dsp") {

  // ─── beatSpan ────────────────────────────────────────────────────────────

  TEST_CASE("beatSpan: the reading is the end of the block, which starts where the last one "
            "ended (#1278)") {
    ScopedSampleRate rate(48000);
    beatSpan span;
    CHECK_FALSE(span.valid());
    CHECK_FALSE(span.moving());

    // 120 BPM: 1/24000 of a beat a sample, 128/24000 a block.
    const double block = 128.0 / 24000.0;
    span.read(10.0, 120.f, N);
    REQUIRE(span.valid());
    // The first reading has no block before it: it starts a block's worth of
    // the tempo back.
    CHECK(span.end() == doctest::Approx(10.0));
    CHECK(span.start() == doctest::Approx(10.0 - block));
    CHECK(span.length() == N);
    CHECK(span.tempo() == doctest::Approx(120.f));
    CHECK(span.at(0) == doctest::Approx(span.start()));
    CHECK(span.at(64) == doctest::Approx(10.0 - block / 2.0));

    // The next block starts exactly where this one ended, whatever the tempo
    // says, so a clock that moved by a hair more is followed without a jump.
    span.read(10.0 + block * 1.001, 120.f, N);
    CHECK(span.start() == 10.0);
    CHECK(span.step() == doctest::Approx(block * 1.001 / 128.0));
    CHECK(span.moving());
  }

  TEST_CASE("beatSpan: a stopped clock, a glide and a stop within one block continue; skipped "
            "blocks and a lost reading start over (#1278)") {
    ScopedSampleRate rate(48000);
    const double block = 128.0 / 24000.0;
    beatSpan span;
    span.read(4.0, 0.f, N);
    CHECK(span.start() == 4.0);
    CHECK_FALSE(span.moving()); // stopped: start and end are the same beat
    span.read(4.0, 0.f, N);
    CHECK(span.start() == 4.0);
    CHECK_FALSE(span.moving());

    // Started with a glide: the clock moves half what its new tempo says.
    span.read(4.0 + block / 2.0, 120.f, N);
    CHECK(span.start() == 4.0);
    CHECK(span.moving());
    span.read(4.0 + block * 1.5, 120.f, N);
    CHECK(span.start() == doctest::Approx(4.0 + block / 2.0));

    // Stopped by a glide that ends inside the block: the tempo already says
    // 0, the clock still moved half a block. Still the same line.
    span.read(4.0 + block * 2.0, 0.f, N);
    CHECK(span.start() == doctest::Approx(4.0 + block * 1.5));

    // Running again, then three blocks go by unrendered: the reading after
    // them starts a block back instead of racing through three.
    span.read(4.0 + block * 3.0, 120.f, N);
    span.read(4.0 + block * 7.0, 120.f, N);
    CHECK(span.start() == doctest::Approx(4.0 + block * 6.0));

    // A lost reading, and a reading that is not a number, start over too.
    span.lose();
    CHECK_FALSE(span.valid());
    CHECK_FALSE(span.moving());
    span.read(4.0 + block * 8.0, 120.f, N);
    CHECK(span.start() == doctest::Approx(4.0 + block * 7.0));
    span.read(std::numeric_limits<double>::quiet_NaN(), 120.f, N);
    CHECK_FALSE(span.valid());

    // A clock running backwards ends before it starts.
    span.read(2.0, -120.f, N);
    CHECK(span.start() == doctest::Approx(2.0 + block));
    CHECK(span.step() < 0.0);

    // A block longer than STANDARD_BUFFERSIZE is held to it.
    span.read(3.0, 120.f, N * 4);
    CHECK(span.length() == N);
  }

  // ─── phasor on a clock ───────────────────────────────────────────────────

  TEST_CASE("phasor: locked, the phase is the clock's beat over the period, through tempo "
            "changes, a stop and a restart (#1278)") {
    ScopedSampleRate rate(48000);
    Clock c(120.f);
    phasor ramp;
    phasor quarter;
    double worst = 0.0;
    auto checkBlock = [&](double period, phasor& r, float offset) {
      const beatSpan& span = c.span;
      buffer& out = r(span, period, true, offset);
      REQUIRE(out.getLength() == N);
      for (unsigned int i = 0; i < N; ++i) {
        // The block runs from the beat before the update to the beat after.
        const double beat =
            c.before + (c.clock.beatPosition() - c.before) * static_cast<double>(i) / N;
        const double d = CircleDistance(out.getPtr()[i], Frac(beat / period + offset));
        if (d > worst) worst = d;
      }
    };

    for (int b = 0; b < 400; ++b) { // 120 BPM
      c.Block();
      checkBlock(4.0, ramp, 0.f); // 1n
      checkBlock(1.0, quarter, 0.25f); // 4n, a quarter of a cycle on
    }
    c.Tempo(90.f);
    for (int b = 0; b < 400; ++b) {
      c.Block();
      checkBlock(4.0, ramp, 0.f);
      checkBlock(1.0, quarter, 0.25f);
    }
    // Stopped (tempo 0): the clock holds its beat and so does the ramp.
    c.Tempo(0.f);
    c.Block();
    checkBlock(4.0, ramp, 0.f);
    const float held = ramp.phase();
    for (int b = 0; b < 50; ++b) {
      c.Block();
      checkBlock(4.0, ramp, 0.f);
      checkBlock(1.0, quarter, 0.25f);
    }
    CHECK(ramp.phase() == held);
    // And carries on from where the clock does, at the new tempo.
    c.Tempo(200.f);
    for (int b = 0; b < 400; ++b) {
      c.Block();
      checkBlock(4.0, ramp, 0.f);
      checkBlock(1.0, quarter, 0.25f);
    }
    CHECK(worst < 1e-5);
  }

  TEST_CASE("phasor: unlocked, the speed follows the clock and the phase is the ramp's own "
            "(#1278)") {
    ScopedSampleRate rate(48000);
    Clock c(120.f);
    phasor ramp;
    ramp.phase(0.5f);
    // 4n at 120 BPM: two cycles a second, from phase 0.5.
    double expected = 0.5;
    double worst = 0.0;
    for (int b = 0; b < 300; ++b) {
      if (b == 100) c.Tempo(60.f);
      if (b == 200) c.Tempo(0.f);
      const beatSpan& span = c.Block();
      buffer& out = ramp(span, 1.0, false);
      for (unsigned int i = 0; i < N; ++i) {
        const double d = CircleDistance(out.getPtr()[i], Frac(expected));
        if (d > worst) worst = d;
        expected += span.step();
      }
    }
    CHECK(worst < 1e-5);
    // Stopped for the last hundred blocks: the last block did not move.
    CHECK_FALSE(c.span.moving());

    // A span without a reading, or a period that is not above 0, holds.
    const float now = ramp.phase();
    beatSpan none;
    buffer& held = ramp(none, 1.0, false);
    CHECK(held.getPtr()[0] == now);
    CHECK(held.getPtr()[N - 1] == now);
    c.Tempo(120.f);
    buffer& zero = ramp(c.Block(), 0.0, true);
    CHECK(zero.getPtr()[N - 1] == now);
    CHECK(ramp.phase() == now);
  }

  TEST_CASE("phasor: sync update holds a new frequency until the ramp wraps; update() takes it "
            "at once (#1278)") {
    ScopedSampleRate rate(48000);
    phasor ramp;
    ramp.syncUpdate(true);
    CHECK(ramp.syncUpdate());
    // 375 Hz is 1/128 of a cycle a sample at 48 kHz and 750 Hz 1/64: both
    // exact, so the wrap lands on a known sample.
    const float slow = 1.f / 128.f;
    const float fast = 1.f / 64.f;
    ramp.phase(0.5f);
    buffer& first = ramp(375.f);
    CHECK(first.getPtr()[63] == doctest::Approx(0.5f + 63.f * slow));
    CHECK(first.getPtr()[64] == 0.f); // the wrap; ends back at 0.5

    // Ask for 750 Hz: the cycle under way finishes at 375.
    buffer& second = ramp(750.f);
    CHECK(second.getPtr()[0] == doctest::Approx(0.5f));
    CHECK(second.getPtr()[1] - second.getPtr()[0] == doctest::Approx(slow));
    CHECK(second.getPtr()[63] == doctest::Approx(0.5f + 63.f * slow));
    CHECK(second.getPtr()[64] == 0.f); // the wrap: 750 Hz from here
    CHECK(second.getPtr()[65] == doctest::Approx(fast));
    CHECK(second.getPtr()[127] == doctest::Approx(63.f * fast));

    // update() takes the next frequency without waiting for the wrap.
    ramp.update();
    buffer& third = ramp(375.f);
    CHECK(third.getPtr()[1] - third.getPtr()[0] == doctest::Approx(slow));

    // Off, a new frequency is taken at once (the #1045 behaviour).
    phasor plain;
    plain(375.f);
    buffer& now = plain(750.f);
    CHECK(now.getPtr()[1] - now.getPtr()[0] == doctest::Approx(fast));
  }

  TEST_CASE("phasor: sync update holds a new period in beats until the ramp wraps (#1278)") {
    ScopedSampleRate rate(48000);
    Clock c(120.f);
    phasor ramp;
    ramp.syncUpdate(true);
    ramp(c.Block(), 1.0, false); // one cycle a beat
    const beatSpan& span = c.Block();
    // Ask for a two-beat cycle mid-way: the step stays a one-beat step.
    buffer& out = ramp(span, 2.0, false);
    CHECK(out.getPtr()[1] - out.getPtr()[0] == doctest::Approx(span.step()).epsilon(1e-6));
  }

  TEST_CASE("phasor: following a clock allocates nothing (#1278)") {
    ScopedSampleRate rate(48000);
    Clock c(120.f);
    phasor ramp;
    ramp(c.Block(), 1.0, true);
    TestHelpers::ProbeScope probe;
    for (int b = 0; b < 50; ++b) {
      ramp(c.Block(), 1.0, b % 2 == 0, 0.25f);
      ramp(480.f);
    }
    CHECK(TestHelpers::g_alloc_count.load() == 0);
  }

  // ─── clockFollower on a clock ────────────────────────────────────────────

  TEST_CASE("clockFollower: on a clock, the ramp is the clock's beat and the MIDI clock ticks "
            "ppq times a beat (#1278)") {
    ScopedSampleRate rate(48000);
    Clock c(120.f);
    clockFollower cf;
    cf.offset(0.5f);
    CHECK_FALSE(cf.following());
    int ticks = 0;
    double worst = 0.0;
    // 1500 blocks at 120 BPM: 4000 ms, 8 beats exactly.
    for (int b = 0; b < 1500; ++b) {
      const beatSpan& span = c.Block();
      buffer& out = cf(span);
      for (unsigned int i = 0; i < N; ++i) {
        const double d = CircleDistance(out.getPtr()[i], Frac(span.at(i) + 0.5));
        if (d > worst) worst = d;
      }
      const std::vector<int> bytes = Bytes(cf);
      CHECK(Count(bytes, clockFollower::MIDI_START) == 0); // following is not a start
      ticks += Count(bytes, clockFollower::MIDI_TICK);
    }
    CHECK(cf.following());
    CHECK(worst < 1e-5);
    // The first tick goes out with the first sample; 8 beats is 192 ticks
    // after it, and the last lands at most a sample from the block's end.
    CHECK(ticks >= 192);
    CHECK(ticks <= 193);
    CHECK(cf.tempo() == doctest::Approx(120.f));
  }

  TEST_CASE("clockFollower: on a clock, the tempo is the clock's, its stop is 252 and its "
            "restart 251 with no tick of its own (#1278)") {
    ScopedSampleRate rate(48000);
    Clock c(120.f);
    clockFollower cf;
    cf.reset(100.f);
    cf(c.Block());
    CHECK(cf.tempoChanged()); // 100 -> the clock's 120
    CHECK(cf.tempo() == doctest::Approx(120.f));
    cf(c.Block());
    CHECK_FALSE(cf.tempoChanged());

    c.Tempo(90.f);
    cf(c.Block());
    CHECK(cf.tempoChanged());
    CHECK(cf.tempo() == doctest::Approx(90.f));

    // The clock stops: one 252, the tempo is kept, the ramp holds.
    c.Tempo(0.f);
    cf(c.Block());
    CHECK(Bytes(cf) == std::vector<int>{clockFollower::MIDI_STOP});
    CHECK_FALSE(cf.running());
    CHECK(cf.tempo() == doctest::Approx(90.f));
    const float held = cf.phase();
    for (int b = 0; b < 20; ++b) {
      cf(c.Block());
      CHECK(cf.clockBytes() == 0);
    }
    CHECK(cf.phase() == held);

    // It moves again: 251, and the next tick only when the grid is crossed.
    c.Tempo(90.f);
    cf(c.Block());
    const std::vector<int> bytes = Bytes(cf);
    REQUIRE_FALSE(bytes.empty());
    CHECK(bytes[0] == clockFollower::MIDI_CONTINUE);
    CHECK(Count(bytes, clockFollower::MIDI_CONTINUE) == 1);
    CHECK(cf.running());

    // A clock running backwards still has a tempo, by size.
    c.Tempo(-60.f);
    cf(c.Block());
    CHECK(cf.tempo() == doctest::Approx(60.f));
  }

  TEST_CASE("clockFollower: on a clock, taps, MIDI, clicks, tempos, start and stop are ignored "
            "until reset (#1278)") {
    ScopedSampleRate rate(48000);
    Clock c(120.f);
    clockFollower cf;
    clockFollower ref;
    cf(c.Block());
    beatSpan copy = c.span;
    ref(copy);
    for (int b = 0; b < 200; ++b) {
      cf.tap().midi(clockFollower::MIDI_TICK).midi(clockFollower::MIDI_START).tempo(60.f);
      cf.start().stop();
      buffer click(N);
      click = 1.f;
      const beatSpan& span = c.Block();
      // A click track given to the click-track overload says nothing either.
      cf(click);
      cf(span);
      ref(span);
      CHECK(cf.taps() == 0);
      CHECK(cf.ticks() == 0);
      CHECK(cf.clicks() == 0);
      CHECK(cf.phase() == ref.phase());
      CHECK(cf.tempo() == doctest::Approx(120.f));
    }
    // A span without a reading holds and sends nothing.
    beatSpan none;
    const float now = cf.phase();
    buffer& held = cf(none);
    CHECK(held.getPtr()[N - 1] == doctest::Approx(now));
    CHECK(cf.clockBytes() == 0);

    // reset() leaves clock mode: the class's own sources count again.
    cf.reset(120.f);
    CHECK_FALSE(cf.following());
    cf.tempo(60.f);
    cf();
    CHECK(cf.tempo() == doctest::Approx(60.f));
  }

  TEST_CASE("clockFollower: following a clock allocates nothing (#1278)") {
    ScopedSampleRate rate(48000);
    Clock c(120.f);
    clockFollower cf;
    cf(c.Block());
    TestHelpers::ProbeScope probe;
    for (int b = 0; b < 200; ++b) {
      if (b == 50) c.Tempo(0.f);
      if (b == 100) c.Tempo(140.f);
      cf(c.Block());
    }
    CHECK(TestHelpers::g_alloc_count.load() == 0);
  }

} // TEST_SUITE("dsp")
