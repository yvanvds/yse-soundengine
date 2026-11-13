// Tests for signal objects that follow a domain clock (issue #1278): ~phasor's
// interval in beats, `lock` and `transport`, and ~sync's `transport` mode, both
// reading the clock through patcher/time/signalClock.
//
// The DSP halves (DSP::beatSpan, DSP::phasor on a span, DSP::clockFollower's
// clock mode) are tested in Tests/dsp/test_beat_span.cpp. These are the
// patcher halves, in a real patcherImplementation with a real `.transport`
// driving a real domain clock, rendered block by block in the engine's order:
// the clock advances, then the patcher renders. Every sample of the ramp is
// compared against the clock's own beat, read from CLOCK::Manager() before and
// after the block, across tempo changes and the transport's stop and start.
//
// Registered in the `clock` suite rather than `patcher`: the cases drive
// CLOCK::Manager().update() on the test thread, which would race a live audio
// thread from another suite in the shared process. No audio device required.

#include <doctest/doctest.h>
#include <cmath>
#include <string>
#include <vector>

#include "clock/clockManager.h"
#include "dsp/buffer.hpp"
#include "headers/constants.hpp"
#include "patcher/generatorObjects/dPhasor.h"
#include "patcher/generatorObjects/dSync.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/time/clockBridge.h"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "patcher/sinks.hpp"

using TestHelpers::BufferSink;
using TestHelpers::ScopedSampleRate;
using YSE::PATCHER::patcherImplementation;

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

  // Records ints and messages, in order. With a log it keeps them as text.
  struct Recorder : TestHelpers::SinkBase {
    std::vector<int> ints;
    std::vector<std::string> messages;

    Recorder() {
      ints.reserve(4096);
      messages.reserve(256);
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) { ints.push_back(v); });
    }
    const char* Type() const override {
      return "signal_clock_recorder";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string& message, float) override {
      messages.push_back(message);
    }
    bool HandlesMessages() const override {
      return true;
    }
    int Count(int value) const {
      int n = 0;
      for (int v : ints)
        if (v == value) n++;
      return n;
    }
  };

  // A patcher with a `.transport` on clock @p name, rendered in the engine's
  // order. Before() and After() are the clock's beat around the last block.
  struct World {
    std::string name;
    patcherImplementation patch{1, nullptr};
    YSE::pHandle* transport = nullptr;
    double before = 0.0;
    double after = 0.0;

    explicit World(const std::string& clock) : name(clock) {
      // First, so the clock exists when the signal objects bind it.
      transport = patch.CreateObject(YSE::OBJ::G_TRANSPORT, name + " 120");
      REQUIRE(transport != nullptr);
      REQUIRE(YSE::CLOCK::Manager().clockExists(name));
    }
    ~World() {
      YSE::CLOCK::Manager().destroyClock(name);
      YSE::CLOCK::Manager().update(0.01f);
    }
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    void Resolve() {
      patch.Clocks()->WaitIdle();
    }
    void Send(const std::string& command) {
      transport->SetListData(0, command);
    }
    void Block() {
      before = YSE::CLOCK::Manager().beatPosition(name);
      YSE::CLOCK::Manager().update(static_cast<Flt>(N) / static_cast<Flt>(YSE::SAMPLERATE));
      patch.Calculate(YSE::T_DSP);
      after = YSE::CLOCK::Manager().beatPosition(name);
    }
    // The clock's beat at sample @p i of the last block.
    double BeatAt(unsigned int i) const {
      return before + (after - before) * static_cast<double>(i) / static_cast<double>(N);
    }
  };

  // The worst distance between a block of @p out and a ramp locked to the
  // clock at @p period beats and @p offset.
  double LockError(const World& w, YSE::DSP::buffer& out, double period, double offset) {
    double worst = 0.0;
    for (unsigned int i = 0; i < N; ++i) {
      const double d = CircleDistance(out.getPtr()[i], Frac(w.BeatAt(i) / period + offset));
      if (d > worst) worst = d;
    }
    return worst;
  }
} // namespace

TEST_SUITE("clock") {

  TEST_CASE("~phasor: lock follows a .transport clock's beat across tempo changes, stop and "
            "start (#1278)") {
    ScopedSampleRate rate(48000);
    BufferSink whole;
    YSE::pHandle wholeHandle(&whole);
    World w("sc1278.lock");
    // A quarter-note ramp into the dac, and a whole-note ramp a quarter of a
    // cycle on, into a sink: frequency, phaseoffset, periodbeats, lock,
    // syncupdate, transport.
    YSE::pHandle* quarter = w.patch.CreateObject(YSE::OBJ::D_PHASOR, "0 0 1 1 0 sc1278.lock");
    YSE::pHandle* bar = w.patch.CreateObject(YSE::OBJ::D_PHASOR, "0 0.25 4 1 0 sc1278.lock");
    YSE::pHandle* dac = w.patch.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(quarter != nullptr);
    REQUIRE(bar != nullptr);
    REQUIRE(dac != nullptr);
    w.patch.Connect(quarter, 0, dac, 0);
    w.patch.Connect(bar, 0, &wholeHandle, 0);
    w.Resolve();
    // One slot for the clock, shared by the transport and both ramps.
    CHECK(w.patch.Clocks()->BoundCount() == 1);

    double worst = 0.0;
    auto block = [&]() {
      w.Block();
      REQUIRE(whole.received != nullptr);
      worst = std::fmax(worst, LockError(w, w.patch.output[0], 1.0, 0.0));
      worst = std::fmax(worst, LockError(w, *whole.received, 4.0, 0.25));
    };

    // The transport makes its clock stopped: the ramps stand on beat 0.
    block();
    CHECK(w.patch.output[0].getPtr()[N - 1] == doctest::Approx(0.f));
    CHECK(whole.received->getPtr()[0] == doctest::Approx(0.25f));

    w.Send("start");
    for (int b = 0; b < 600; ++b) // 1.6 s at 120 BPM: 3.2 beats
      block();
    CHECK(w.after == doctest::Approx(600.0 * N / 24000.0).epsilon(1e-6));

    w.Send("tempo 90");
    for (int b = 0; b < 600; ++b)
      block();

    // Stopped: the beat holds and both ramps hold with it.
    w.Send("stop");
    block();
    const float heldQuarter = w.patch.output[0].getPtr()[N - 1];
    const float heldBar = whole.received->getPtr()[N - 1];
    for (int b = 0; b < 100; ++b) {
      block();
      CHECK(w.patch.output[0].getPtr()[0] == heldQuarter);
      CHECK(whole.received->getPtr()[N - 1] == heldBar);
    }

    // Started again: on from the same beat, at the transport's tempo.
    w.Send("start");
    for (int b = 0; b < 600; ++b)
      block();
    CHECK(worst < 1e-5);
  }

  TEST_CASE("~phasor: a note value without lock follows the clock's tempo from the ramp's own "
            "phase; a number goes back to Hz (#1278)") {
    ScopedSampleRate rate(48000);
    World w("sc1278.free");
    YSE::pHandle* ramp = w.patch.CreateObject(YSE::OBJ::D_PHASOR, "0 0.5 0 0 0 sc1278.free");
    YSE::pHandle* dac = w.patch.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(ramp != nullptr);
    w.patch.Connect(ramp, 0, dac, 0);
    w.Resolve();
    w.Send("start");

    // No interval yet: frequency 0, the ramp stays at its phaseoffset.
    w.Block();
    CHECK(w.patch.output[0].getPtr()[N - 1] == doctest::Approx(0.5f));

    // `8n`: a cycle every half beat, from where the ramp stands.
    ramp->SetListData(0, "8n");
    double expected = 0.5;
    double worst = 0.0;
    for (int b = 0; b < 900; ++b) {
      if (b == 300) w.Send("tempo 60");
      if (b == 600) w.Send("stop");
      w.Block();
      const double step = (w.after - w.before) / static_cast<double>(N) / 0.5;
      for (unsigned int i = 0; i < N; ++i) {
        worst = std::fmax(worst, CircleDistance(w.patch.output[0].getPtr()[i], Frac(expected)));
        expected += step;
      }
    }
    CHECK(worst < 1e-5);
    // Stopped for the last 300 blocks: the ramp holds.
    CHECK(w.patch.output[0].getPtr()[0] == w.patch.output[0].getPtr()[N - 1]);

    // `960 ticks` is two beats; a plain number is Hz again: 375 Hz is 1/128
    // of a cycle a sample, whatever the clock does.
    ramp->SetListData(0, "960 ticks");
    w.Send("start");
    w.Block();
    w.Block();
    const double step = (w.after - w.before) / static_cast<double>(N) / 2.0;
    CHECK(CircleDistance(w.patch.output[0].getPtr()[1] - w.patch.output[0].getPtr()[0], step) <
          1e-6);
    ramp->SetFloatData(0, 375.f);
    w.Block();
    CHECK(CircleDistance(w.patch.output[0].getPtr()[1] - w.patch.output[0].getPtr()[0],
                         1.0 / 128.0) < 1e-6);
  }

  TEST_CASE("~phasor: lock and syncupdate as messages; a clock named before it exists is "
            "followed once it does (#1278)") {
    ScopedSampleRate rate(48000);
    patcherImplementation patch(1, nullptr);
    YSE::pHandle* ramp = patch.CreateObject(YSE::OBJ::D_PHASOR, "0 0.125 1 0 0 sc1278.late");
    YSE::pHandle* dac = patch.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(ramp != nullptr);
    patch.Connect(ramp, 0, dac, 0);
    ramp->SetListData(0, "lock 1");
    ramp->SetListData(0, "syncupdate 1");
    patch.Clocks()->WaitIdle();
    CHECK(patch.Clocks()->BoundCount() == 1);
    CHECK_FALSE(patch.Clocks()->Resolved(1));

    // No such clock: the ramp holds where it started.
    for (int b = 0; b < 10; ++b) {
      YSE::CLOCK::Manager().update(static_cast<Flt>(N) / static_cast<Flt>(YSE::SAMPLERATE));
      patch.Calculate(YSE::T_DSP);
      CHECK(patch.output[0].getPtr()[N - 1] == doctest::Approx(0.125f));
    }

    // The host makes it, running. The bridge's Poll resolves the name within
    // RESOLVE_INTERVAL_BLOCKS, and from then the ramp is locked to it.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("sc1278.late", 120.f));
    bool locked = false;
    for (int b = 0; b < 200 && !locked; ++b) {
      mgr.update(static_cast<Flt>(N) / static_cast<Flt>(YSE::SAMPLERATE));
      patch.Calculate(YSE::T_DSP);
      patch.Clocks()->WaitIdle();
      locked = patch.Clocks()->Resolved(1) &&
               patch.output[0].getPtr()[N - 1] != patch.output[0].getPtr()[0];
    }
    REQUIRE(locked);
    for (int b = 0; b < 5; ++b) {
      const double before = mgr.beatPosition("sc1278.late");
      mgr.update(static_cast<Flt>(N) / static_cast<Flt>(YSE::SAMPLERATE));
      patch.Calculate(YSE::T_DSP);
      const double after = mgr.beatPosition("sc1278.late");
      // Locked (the message), with phaseoffset 0.125 on top.
      CHECK(CircleDistance(patch.output[0].getPtr()[0], Frac(before + 0.125)) < 1e-5);
      CHECK(CircleDistance(patch.output[0].getPtr()[N - 1],
                           Frac(before + (after - before) * (N - 1) / N + 0.125)) < 1e-5);
    }
    mgr.destroyClock("sc1278.late");
    mgr.update(0.01f);
  }

  TEST_CASE("~phasor and ~sync: the clock parameters survive a DumpJSON / ParseJSON round trip "
            "and bind on joining a patcher (#1278)") {
    const std::string phasorArgs = "0 0.25 4 1 1 sc1278.json";
    const std::string syncArgs = "120 0 0 24 -1 sc1278.json";
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_PHASOR, phasorArgs) != nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_SYNC, syncArgs) != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    REQUIRE(dst.GetHandleFromID(0) != nullptr);
    REQUIRE(dst.GetHandleFromID(1) != nullptr);
    CHECK(dst.GetHandleFromID(0)->GetParams() == phasorArgs);
    CHECK(dst.GetHandleFromID(1)->GetParams() == syncArgs);
    // Both name the same clock: one slot.
    CHECK(dst.Clocks()->BoundCount() == 1);
    CHECK(std::string(dst.Clocks()->NameOf(1)) == "sc1278.json");

    // Outside a patcher nothing binds; without a name nothing binds either.
    YSE::PATCHER::dPhasor loose;
    loose.SetParams(phasorArgs);
    CHECK_FALSE(loose.ClockBound());
    YSE::PATCHER::dSync none;
    none.SetParent(&dst);
    CHECK_FALSE(none.ClockBound());
    none.SetParent(nullptr);
  }

  TEST_CASE("~sync: transport follows a clock's beat, reports its tempo, and sends 252 and 251 "
            "as it stops and starts (#1278)") {
    ScopedSampleRate rate(48000);
    Recorder report;
    Recorder midi;
    BufferSink ramp;
    YSE::pHandle reportHandle(&report);
    YSE::pHandle midiHandle(&midi);
    YSE::pHandle rampHandle(&ramp);
    World w("sc1278.sync");
    // bpm 100 (not the clock's), offset 0, midioffset 0, ppq 24, no rtport.
    YSE::pHandle* sync = w.patch.CreateObject(YSE::OBJ::D_SYNC, "100 0 0 24 -1 sc1278.sync");
    REQUIRE(sync != nullptr);
    w.patch.Connect(sync, 0, &rampHandle, 0);
    w.patch.Connect(sync, 1, &reportHandle, 0);
    w.patch.Connect(sync, 2, &midiHandle, 0);
    w.Resolve();

    double worst = 0.0;
    auto block = [&]() {
      w.Block();
      REQUIRE(ramp.received != nullptr);
      worst = std::fmax(worst, LockError(w, *ramp.received, 1.0, 0.0));
    };

    // Stopped: no clock out, and the ramp stands on beat 0.
    block();
    CHECK(midi.ints.empty());
    CHECK(ramp.received->getPtr()[N - 1] == doctest::Approx(0.f));

    // Started at 120: the tempo report, and 24 ticks a beat. 1500 blocks at
    // 120 BPM are 8 beats exactly. Resuming on beat 0 is a continue, not a
    // tick, so the ticks are the 191 grid lines after it, and the one on beat
    // 8 if it lands inside the last block rather than on the next one's first
    // sample.
    w.Send("start");
    for (int b = 0; b < 1500; ++b)
      block();
    REQUIRE_FALSE(report.messages.empty());
    CHECK(report.messages.front() == "bpm 120.");
    // A domain clock that starts moving continues from its beat: 251.
    REQUIRE_FALSE(midi.ints.empty());
    CHECK(midi.ints.front() == 251);
    CHECK(midi.Count(248) >= 191);
    CHECK(midi.Count(248) <= 192);
    CHECK(midi.Count(250) == 0);

    // A tempo change is reported.
    w.Send("tempo 90");
    block();
    CHECK(report.messages.back() == "bpm 90.");

    // Taps, MIDI bytes, tempos and start/stop at the object do nothing while
    // it follows a clock.
    const std::size_t reports = report.messages.size();
    sync->SetBang(0);
    sync->SetIntData(0, 250);
    sync->SetFloatData(0, 60.f);
    sync->SetListData(0, "stop");
    for (int b = 0; b < 10; ++b)
      block();
    CHECK(report.messages.size() == reports);
    CHECK(midi.Count(252) == 0);

    // The transport stops: one 252, then silence.
    w.Send("stop");
    block();
    REQUIRE_FALSE(midi.ints.empty());
    CHECK(midi.ints.back() == 252);
    const std::size_t bytes = midi.ints.size();
    for (int b = 0; b < 100; ++b)
      block();
    CHECK(midi.ints.size() == bytes);
    CHECK(midi.Count(252) == 1);

    // And starts again: 251 (a domain clock resumes where it stopped), and
    // the ramp carries on from the same beat.
    w.Send("start");
    block();
    CHECK(midi.ints[bytes] == 251);
    for (int b = 0; b < 300; ++b)
      block();
    CHECK(midi.Count(251) == 2);
    CHECK(worst < 1e-5);
  }

  TEST_CASE("~phasor and ~sync: reading a clock in a rendered block allocates nothing (#1278)") {
    ScopedSampleRate rate(48000);
    Recorder midi;
    YSE::pHandle midiHandle(&midi);
    World w("sc1278.alloc");
    YSE::pHandle* locked = w.patch.CreateObject(YSE::OBJ::D_PHASOR, "0 0 1 1 0 sc1278.alloc");
    YSE::pHandle* unlocked = w.patch.CreateObject(YSE::OBJ::D_PHASOR, "0 0 0.5 0 1 sc1278.alloc");
    YSE::pHandle* sync = w.patch.CreateObject(YSE::OBJ::D_SYNC, "120 0 0 24 -1 sc1278.alloc");
    YSE::pHandle* dac = w.patch.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(locked != nullptr);
    REQUIRE(unlocked != nullptr);
    REQUIRE(sync != nullptr);
    w.patch.Connect(locked, 0, dac, 0);
    w.patch.Connect(unlocked, 0, dac, 0);
    w.patch.Connect(sync, 2, &midiHandle, 0);
    w.Resolve();
    w.Send("start");
    for (int b = 0; b < 10; ++b)
      w.Block();
    {
      TestHelpers::ProbeScope probe;
      for (int b = 0; b < 200; ++b) {
        YSE::CLOCK::Manager().update(static_cast<Flt>(N) / static_cast<Flt>(YSE::SAMPLERATE));
        w.patch.Calculate(YSE::T_DSP);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(midi.Count(248) > 0);
  }

} // TEST_SUITE("clock")
