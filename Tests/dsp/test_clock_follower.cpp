// Tests for YSE::DSP::clockFollower (YseEngine/dsp/generators/clockFollower.hpp),
// issue #1131. Reference output is closed form: at a tempo of b BPM the ramp
// rises by b / (60 * SAMPLERATE) a sample, the MIDI clock out ticks every
// 1 / ppq of it, taps and clicks at a regular interval T set the tempo to
// 60 / T, and regular MIDI ticks every t seconds set it to 60 / (t * ppq).
// Events are timed in whole blocks (or, for clicks, whole samples) so the
// expected values are exact.

#include <doctest/doctest.h>
#include <cmath>
#include <vector>
#include "dsp/generators/clockFollower.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::DSP::buffer;
using YSE::DSP::clockFollower;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // How far @p value is from @p target on the 0..1 circle.
  double CircleDistance(double value, double target) {
    double d = value - target;
    d -= std::floor(d + 0.5);
    return std::fabs(d);
  }

  // Run @p blocks plain blocks and return the first sample of the last one.
  float Run(clockFollower& cf, int blocks) {
    float first = 0.f;
    for (int b = 0; b < blocks; ++b)
      first = cf().getPtr()[0];
    return first;
  }

  // The clock bytes of the last report.
  std::vector<int> Bytes(const clockFollower& cf) {
    std::vector<int> out;
    out.reserve(cf.clockBytes());
    for (UInt i = 0; i < cf.clockBytes(); ++i)
      out.push_back(cf.clockByte(i));
    return out;
  }

  // Sample positions of the 248s over @p samples one-sample blocks.
  std::vector<unsigned int> TickPositions(clockFollower& cf, unsigned int samples) {
    std::vector<unsigned int> at;
    for (unsigned int n = 0; n < samples; ++n) {
      cf(1);
      for (UInt i = 0; i < cf.clockBytes(); ++i)
        if (cf.clockByte(i) == clockFollower::MIDI_TICK) at.push_back(n);
    }
    return at;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("clockFollower: at 120 BPM the ramp rises once per beat, in closed form (#1131)") {
    ScopedSampleRate rate(48000);
    clockFollower cf;
    CHECK(cf.tempo() == 120.f);
    CHECK(cf.running());
    unsigned int n = 0;
    for (int block = 0; block < 400; ++block) { // 51200 samples: past two beats
      buffer& out = cf();
      REQUIRE(out.getLength() == N);
      for (unsigned int i = 0; i < N; ++i, ++n) {
        const double p = static_cast<double>(n) / 24000.0;
        CAPTURE(n);
        CHECK(CircleDistance(out.getPtr()[i], p - std::floor(p)) < 1e-6);
        CHECK(out.getPtr()[i] >= 0.f);
        CHECK(out.getPtr()[i] < 1.f);
      }
    }
    CHECK_FALSE(cf.tempoChanged());
  }

  TEST_CASE("clockFollower: the MIDI clock out ticks every 1/ppq of the ramp, shifted by the "
            "clock offset (#1131)") {
    ScopedSampleRate rate(48000);
    {
      clockFollower cf; // 24000 samples a beat, a tick every 1000
      const std::vector<unsigned int> at = TickPositions(cf, 48000);
      REQUIRE(at.size() == 48);
      for (std::size_t k = 0; k < at.size(); ++k) {
        CAPTURE(k);
        CHECK(std::abs(static_cast<int>(at[k]) - static_cast<int>(k * 1000)) <= 1);
      }
    }
    {
      clockFollower cf;
      cf.ppq(48);
      const std::vector<unsigned int> at = TickPositions(cf, 24000);
      CHECK(at.size() == 48);
      CHECK(std::abs(static_cast<int>(at[1]) - 500) <= 1);
    }
    {
      // Half a tick early: the first tick still comes with the first sample.
      clockFollower cf;
      cf.clockOffset(1.f / 48.f);
      CHECK(cf.clockOffset() == doctest::Approx(1.0 / 48.0));
      const std::vector<unsigned int> at = TickPositions(cf, 24000);
      REQUIRE(at.size() == 25);
      CHECK(at[0] == 0);
      for (std::size_t k = 1; k < at.size(); ++k) {
        CAPTURE(k);
        CHECK(std::abs(static_cast<int>(at[k]) - static_cast<int>(k * 1000 - 500)) <= 1);
      }
    }
    {
      // Over many blocks: one tick per 1/24 beat, none lost or doubled.
      clockFollower cf;
      int ticks = 0;
      for (int b = 0; b < 755; ++b) { // 96640 samples: ticks at 0 .. 96000
        cf();
        for (UInt i = 0; i < cf.clockBytes(); ++i)
          if (cf.clockByte(i) == clockFollower::MIDI_TICK) ticks++;
      }
      CHECK(ticks == 97);
    }
  }

  TEST_CASE("clockFollower: tempo() sets, holds and reports the tempo (#1131)") {
    ScopedSampleRate rate(48000);
    clockFollower cf;
    cf.tempo(90.f);
    cf();
    CHECK(cf.tempoChanged());
    CHECK(cf.tempo() == 90.f);
    cf();
    CHECK_FALSE(cf.tempoChanged());
    cf.tempo(90.f); // unchanged: nothing to report
    cf();
    CHECK_FALSE(cf.tempoChanged());

    cf.tempo(0.f);
    CHECK(cf.tempo() == clockFollower::MIN_TEMPO);
    cf.tempo(5000.f);
    CHECK(cf.tempo() == clockFollower::MAX_TEMPO);
    cf.tempo(std::nanf(""));
    CHECK(cf.tempo() == clockFollower::MAX_TEMPO);

    // The ramp follows: 90 BPM is 32000 samples a beat.
    cf.tempo(90.f);
    buffer& out = cf();
    CHECK(out.getPtr()[1] - out.getPtr()[0] == doctest::Approx(1.0 / 32000.0).epsilon(1e-6));
  }

  TEST_CASE("clockFollower: offset() shifts the output, wrapped into 0..1 (#1131)") {
    ScopedSampleRate rate(48000);
    clockFollower plain;
    clockFollower shifted;
    shifted.offset(1.25f);
    CHECK(shifted.offset() == doctest::Approx(0.25));
    for (int b = 0; b < 300; ++b) {
      buffer& a = plain();
      buffer& s = shifted();
      for (unsigned int i = 0; i < N; ++i)
        CHECK(CircleDistance(s.getPtr()[i], a.getPtr()[i] + 0.25) < 1e-6);
    }
    shifted.offset(std::nanf(""));
    CHECK(shifted.offset() == doctest::Approx(0.25));
  }

  TEST_CASE("clockFollower: stop holds the ramp and start restarts it from 0, with 252 and 250 "
            "out (#1131)") {
    ScopedSampleRate rate(48000);
    clockFollower cf;
    Run(cf, 50);
    const float held = cf.phase();
    cf.stop();
    CHECK_FALSE(cf.running());
    buffer& out = cf();
    CHECK(Bytes(cf) == std::vector<int>{252});
    for (unsigned int i = 0; i < N; ++i)
      CHECK(out.getPtr()[i] == held);
    Run(cf, 10);
    CHECK(cf.phase() == held);
    CHECK(cf.clockBytes() == 0);

    cf.start();
    buffer& again = cf();
    CHECK(cf.running());
    CHECK(Bytes(cf) == std::vector<int>{250, 248});
    CHECK(again.getPtr()[0] == 0.f);
    CHECK(again.getPtr()[N - 1] > 0.f);
  }

  TEST_CASE("clockFollower: three taps that agree set the tempo; the beat is absorbed or jumped "
            "to (#1131)") {
    ScopedSampleRate rate(48000);
    {
      // 180 blocks = 23040 samples = 0.48 s: 125 BPM. At 120 BPM the ramp
      // stands at 0.92 at the third tap, 0.08 from the beat: absorbed over
      // the next beat.
      clockFollower cf;
      cf.tap();
      cf();
      CHECK(cf.taps() == 1);
      Run(cf, 179);
      cf.tap();
      Run(cf, 180);
      CHECK(cf.tempo() == 120.f); // two taps are not enough
      cf.tap();
      buffer& out = cf();
      CHECK(cf.taps() == 1);
      CHECK(cf.tempoChanged());
      CHECK(cf.tempo() == doctest::Approx(125.0).epsilon(1e-9));
      CHECK(CircleDistance(out.getPtr()[0], 0.92) < 1e-6); // no jump
      const float beat = Run(cf, 179); // block 180 after the tap starts a beat
      CHECK(CircleDistance(cf.phase(), 0.0) < 1e-6);
      (void)beat;
      // A fourth tap in time: the tempo holds, nothing to absorb.
      cf.tap();
      cf();
      CHECK_FALSE(cf.tempoChanged());
      CHECK(cf.tempo() == doctest::Approx(125.0).epsilon(1e-9));
    }
    {
      // At 100 BPM the ramp stands at 0.6 at the third tap: too far, it
      // jumps to the beat.
      clockFollower cf;
      cf.tempo(100.f);
      cf.tap();
      Run(cf, 180);
      cf.tap();
      Run(cf, 180);
      cf.tap();
      buffer& out = cf();
      CHECK(cf.tempo() == doctest::Approx(125.0).epsilon(1e-9));
      CHECK(out.getPtr()[0] == 0.f);
    }
  }

  TEST_CASE("clockFollower: taps that disagree, or come too slowly, set nothing; the count "
            "slides (#1131)") {
    ScopedSampleRate rate(48000);
    clockFollower cf;
    cf.tap();
    Run(cf, 180); // 0.48 s
    cf.tap();
    Run(cf, 100); // 0.2667 s: disagrees
    cf.tap();
    cf();
    CHECK(cf.tempo() == 120.f);
    Run(cf, 99);
    cf.tap(); // 0.2667 s again: the last two agree
    cf();
    CHECK(cf.tempo() == doctest::Approx(60.0 / (12800.0 / 48000.0)).epsilon(1e-9));

    clockFollower slow;
    for (int t = 0; t < 4; ++t) {
      slow.tap();
      Run(slow, 788); // 2.1 s: longer than a 30 BPM beat
    }
    CHECK(slow.tempo() == 120.f);
  }

  TEST_CASE("clockFollower: a click track sets the tempo to the sample and locks the ramp to "
            "its clicks (#1131)") {
    ScopedSampleRate rate(48000);
    // 100 BPM: a click every 28800 samples, from sample 1000.
    const auto clickAt = [](std::uint64_t n, unsigned int every, float level) {
      return n >= 1000 && (n - 1000) % every == 0 ? level : 0.f;
    };
    clockFollower cf;
    buffer click(N);
    std::uint64_t n = 0;
    int clicks = 0;
    int changes = 0;
    std::vector<double> atClick;
    for (int b = 0; b < 1200; ++b) { // 153600 samples: five clicks
      for (unsigned int i = 0; i < N; ++i)
        click.getPtr()[i] = clickAt(n + i, 28800, b % 2 == 0 ? 0.5f : -0.5f);
      buffer& out = cf(click);
      for (unsigned int i = 0; i < N; ++i)
        if (click.getPtr()[i] != 0.f) atClick.push_back(out.getPtr()[i]);
      clicks += static_cast<int>(cf.clicks());
      if (cf.tempoChanged()) changes++;
      n += N;
    }
    CHECK(clicks == 6);
    CHECK(changes == 1);
    CHECK(cf.tempo() == doctest::Approx(100.0).epsilon(1e-9));
    REQUIRE(atClick.size() == 6);
    // From the fourth click on, the ramp is on the beat at every click.
    CHECK(CircleDistance(atClick[4], 0.0) < 1e-6);
    CHECK(CircleDistance(atClick[5], 0.0) < 1e-6);
  }

  TEST_CASE("clockFollower: clicks below 0.1, inside the refractory time or outside 30-240 BPM "
            "set nothing (#1131)") {
    ScopedSampleRate rate(48000);
    buffer click(N);
    {
      clockFollower cf;
      for (int b = 0; b < 1200; ++b) {
        click = 0.f;
        if (b % 225 == 0) click.getPtr()[3] = 0.09f; // 100 BPM, too quiet
        cf(click);
        CHECK(cf.clicks() == 0);
      }
      CHECK(cf.tempo() == 120.f);
    }
    {
      // A ringing click: two peaks with a zero between count once.
      clockFollower cf;
      click = 0.f;
      click.getPtr()[10] = 0.5f;
      click.getPtr()[12] = 0.5f;
      click.getPtr()[100] = std::nanf("");
      cf(click);
      CHECK(cf.clicks() == 1);
    }
    {
      // 300 BPM: counted as clicks, too fast for a tempo.
      clockFollower cf;
      std::uint64_t n = 0;
      unsigned int clicks = 0;
      for (int b = 0; b < 600; ++b) {
        for (unsigned int i = 0; i < N; ++i)
          click.getPtr()[i] = (n + i) % 9600 == 0 ? 0.5f : 0.f;
        cf(click);
        clicks += cf.clicks();
        n += N;
      }
      CHECK(clicks == 8);
      CHECK(cf.tempo() == 120.f);
    }
  }

  TEST_CASE("clockFollower: MIDI start holds at 0 until the first tick; regular ticks give the "
            "tempo and lock the ramp (#1131)") {
    ScopedSampleRate rate(48000);
    // A tick every 8 blocks (1024 samples): 60 / (1024 / 48000 * 24) BPM.
    const double expected = 60.0 / (1024.0 / 48000.0 * 24.0);
    clockFollower cf;
    Run(cf, 37);
    cf.midi(250);
    buffer& held = cf();
    CHECK(Bytes(cf) == std::vector<int>{250});
    for (unsigned int i = 0; i < N; ++i)
      CHECK(held.getPtr()[i] == 0.f);
    Run(cf, 3);
    CHECK(cf.phase() == 0.f);

    int changes = 0;
    for (int tick = 0; tick < 96; ++tick) {
      cf.midi(248);
      buffer& out = cf();
      CHECK(cf.ticks() == 1);
      if (cf.tempoChanged()) changes++;
      if (tick == 0) {
        CHECK(out.getPtr()[0] == 0.f);
        CHECK(Bytes(cf) == std::vector<int>{248});
      }
      if (tick >= 24) {
        CAPTURE(tick);
        CHECK(CircleDistance(out.getPtr()[0], static_cast<double>(tick % 24) / 24.0) < 1e-6);
      }
      Run(cf, 7);
    }
    CHECK(changes == 1); // exact ticks: the first estimate is the tempo
    CHECK(cf.tempo() == doctest::Approx(expected).epsilon(1e-9));
  }

  TEST_CASE("clockFollower: jittered ticks give the mean tempo and the ramp stays on them "
            "(#1131)") {
    ScopedSampleRate rate(48000);
    // 7 and 8 blocks in turn: 960 samples a tick on average, 125 BPM.
    clockFollower cf;
    cf.midi(250);
    for (int tick = 0; tick < 240; ++tick) {
      cf.midi(248);
      buffer& out = cf();
      if (tick >= 72) {
        CAPTURE(tick);
        CHECK(CircleDistance(out.getPtr()[0], static_cast<double>(tick % 24) / 24.0) < 0.01);
      }
      Run(cf, tick % 2 == 0 ? 6 : 7);
    }
    CHECK(cf.tempo() == doctest::Approx(125.0).epsilon(2e-3));
  }

  TEST_CASE("clockFollower: ticks without a start lock to the nearest tick; stop, continue and "
            "a gap (#1131)") {
    ScopedSampleRate rate(48000);
    clockFollower cf;
    Run(cf, 13); // somewhere between two ticks
    for (int tick = 0; tick < 72; ++tick) {
      cf.midi(248);
      buffer& out = cf();
      if (tick >= 24) {
        const double steps = static_cast<double>(out.getPtr()[0]) * 24.0;
        CHECK(std::fabs(steps - std::round(steps)) < 1e-4);
      }
      Run(cf, 7);
    }

    // Stopped: ticks at another rate set the tempo, the ramp holds.
    cf.midi(252);
    cf();
    CHECK(Bytes(cf) == std::vector<int>{252});
    const float held = cf.phase();
    for (int tick = 0; tick < 60; ++tick) {
      cf.midi(248);
      buffer& out = cf();
      CHECK(out.getPtr()[N - 1] == held);
      CHECK(cf.clockBytes() == 0);
      Run(cf, 5); // 768 samples a tick: 156.25 BPM
    }
    // Read while the window slid from the old ticks to these: within the
    // hysteresis.
    CHECK(cf.tempo() == doctest::Approx(156.25).epsilon(1e-3));

    // Continue: running again from where it held, 251 out.
    cf.midi(251);
    buffer& out = cf();
    CHECK(cf.running());
    REQUIRE(cf.clockBytes() >= 1);
    CHECK(cf.clockByte(0) == 251);
    CHECK(out.getPtr()[0] == held);
    CHECK(out.getPtr()[N - 1] > held);

    // A gap longer than TICK_TIMEOUT: the old ticks are forgotten, so the
    // new tempo is read from the new ticks alone.
    Run(cf, 1200); // 3.2 s
    for (int tick = 0; tick < 12; ++tick) {
      cf.midi(248);
      Run(cf, 10); // 1280 samples a tick: 93.75 BPM
    }
    CHECK(cf.tempo() == doctest::Approx(93.75).epsilon(1e-9));

    // Other bytes do nothing.
    const float before = cf.tempo();
    cf.midi(0);
    cf.midi(144);
    cf.midi(255);
    cf();
    CHECK(cf.ticks() == 0);
    for (UInt i = 0; i < cf.clockBytes(); ++i)
      CHECK(cf.clockByte(i) == 248);
    CHECK(cf.tempo() == before);
  }

  TEST_CASE("clockFollower: ppq sets the ticks per beat in; a change forgets the ticks "
            "(#1131)") {
    ScopedSampleRate rate(48000);
    clockFollower cf;
    cf.ppq(0);
    CHECK(cf.ppq() == 1);
    cf.ppq(1000);
    CHECK(cf.ppq() == clockFollower::MAX_PPQ);
    cf.ppq(12);
    // A tick every 8 blocks at 12 ppq is twice as slow as at 24.
    for (int tick = 0; tick < 30; ++tick) {
      cf.midi(248);
      Run(cf, 8);
    }
    CHECK(cf.tempo() == doctest::Approx(60.0 / (1024.0 / 48000.0 * 12.0)).epsilon(1e-9));
  }

  TEST_CASE("clockFollower: follows SAMPLERATE changed after construction (#1131)") {
    ScopedSampleRate rate(48000);
    clockFollower cf;
    buffer& a = cf();
    CHECK(a.getPtr()[1] - a.getPtr()[0] == doctest::Approx(1.0 / 24000.0).epsilon(1e-6));
    YSE::SAMPLERATE = 96000;
    buffer& b = cf();
    CHECK(b.getPtr()[1] - b.getPtr()[0] == doctest::Approx(1.0 / 48000.0).epsilon(1e-6));
    // Taps are timed in seconds: 0.5 s at 96 kHz is 375 blocks.
    cf.tempo(100.f);
    cf.tap();
    Run(cf, 375);
    cf.tap();
    Run(cf, 375);
    cf.tap();
    cf();
    CHECK(cf.tempo() == doctest::Approx(120.0).epsilon(1e-9));
  }

  TEST_CASE("clockFollower: reset starts over without a report; block lengths (#1131)") {
    ScopedSampleRate rate(48000);
    clockFollower cf;
    cf.offset(0.5f);
    cf.tap();
    Run(cf, 100);
    cf.stop();
    cf.reset(90.f);
    CHECK(cf.tempo() == 90.f);
    CHECK(cf.running());
    CHECK(cf.phase() == 0.f);
    CHECK(cf.offset() == doctest::Approx(0.5));
    buffer& out = cf(32);
    CHECK(out.getLength() == 32);
    CHECK_FALSE(cf.tempoChanged());
    CHECK(cf.taps() == 0);
    CHECK(out.getPtr()[0] == 0.5f);
    CHECK(cf(4 * N).getLength() == N);
  }

  TEST_CASE("clockFollower: blocks, events and settings allocate nothing (#1131)") {
    ScopedSampleRate rate(48000);
    clockFollower cf;
    buffer click(N);
    click = 0.f;
    cf();
    {
      TestHelpers::ProbeScope probe;
      cf.tempo(100.f).offset(0.1f).clockOffset(0.2f).ppq(48);
      cf.midi(250);
      for (int b = 0; b < 400; ++b) {
        if (b % 8 == 0) cf.midi(248);
        if (b % 100 == 0) cf.tap();
        click.getPtr()[5] = b % 200 == 0 ? 1.f : 0.f;
        cf(click);
      }
      cf.stop();
      cf();
      cf.midi(251);
      cf(17);
      cf.start();
      cf.reset(120.f);
      cf();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
