// Tests for `.transport` — the patcher's remote control for a named domain
// clock (issue #513).
//
// #513 is a design gate as much as an object, so these cases are organised
// around the contract rather than around the methods:
//
//   - **ownership** cases pin who creates the clock and who does not destroy
//     it. A `.transport` creates its clock when it joins a patcher, *stopped*,
//     and leaves a clock the host already made exactly as it is; deleting the
//     object leaves the clock running. Those three sentences are the whole of
//     what #507 (`.timepoint`) and #512 (`.tempo`) get to rely on, so each is
//     an assertion rather than a comment.
//
//   - **control** cases are the user-visible object: start, stop, retempo,
//     glide, and report. They run against a real `patcherImplementation` with
//     recorders on both outlets and a real `CLOCK::domainClock` advancing
//     underneath, because "the clock the patch controls is the clock the engine
//     runs" is the claim, and only a real clock can carry it.
//
//   - **route** cases cover the half a unit test structurally cannot see: a
//     command that arrives *from inside `Calculate`* — the audio callback — has
//     to reach the clock through the bridge's wait-free write rather than
//     through `CLOCK::Manager()`'s mutex. The rig for that is a real patch (a
//     `.delay` into a `.i` into the transport), because the only way to get a
//     handler onto the audio thread is to have the patcher put it there.
//
//   - **persistence** covers the DumpJSON / ParseJSON round trip, end to end:
//     the restored object is driven and the clock is watched, so a parameter
//     that survived the JSON but not the rebuild would still fail.
//
// Registered in the `clock` suite, not `patcher`: the cases drive
// CLOCK::Manager().update() directly on the test thread to advance beats
// deterministically, which would race a live audio thread from another suite in
// the shared monolithic process. No audio device required.

#include <doctest/doctest.h>
#include <cstdint>
#include <string>
#include <vector>

#include "clock/clockManager.h"
#include "patcher/inlet.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/time/clockBridge.h"
#include "patcher/time/gTransport.h"
#include "patcher/time/messageScheduler.h"
#include "support/alloc_probe.hpp"

namespace {

  using YSE::PATCHER::patcherImplementation;

  // 0.25 s per tick at 120 BPM is exactly half a beat, and every number in that
  // sentence is exact in binary, so the cases below assert on boundaries rather
  // than around them. The same constants the #688 cases use.
  constexpr float kTickSeconds = 0.25f;
  constexpr float kTempo = 120.f;

  // One block of the whole world, in the engine's own order: advance every
  // domain clock, then let the patcher render. A tempo written during a render
  // is therefore consumed by the *next* tick, which is exactly what happens on
  // a real device.
  void Tick(patcherImplementation& p) {
    YSE::CLOCK::Manager().update(kTickSeconds);
    p.Calculate(YSE::T_DSP);
  }

  // Records every float it is sent, in order. Both of `.transport`'s outlets
  // are floats, so wiring them to one recorder is what makes the right-to-left
  // send order observable.
  struct FloatRecorder : YSE::PATCHER::pObject {
    std::vector<float> seen;

    FloatRecorder() : pObject(false) {
      // Reserved up front so the allocation-probe case measures the *object's*
      // handlers rather than this vector growing under them.
      seen.reserve(64);
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) { seen.push_back(v); });
    }
    const char* Type() const override {
      return "transport_recorder";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A `.transport` living in a real patcher, with one recorder taking both
  // outlets so the pair's order can be read off `seen`.
  struct TransportRig {
    patcherImplementation patcher{1, nullptr};
    FloatRecorder out;
    YSE::pHandle outHandle{&out};
    YSE::pHandle* transport = nullptr;

    explicit TransportRig(const std::string& args) {
      transport = patcher.CreateObject(YSE::OBJ::G_TRANSPORT, args);
      REQUIRE(transport != nullptr);
      patcher.Connect(transport, 0, &outHandle, 0);
      patcher.Connect(transport, 1, &outHandle, 0);
      // The object binds in SetParent and the clock it binds already exists by
      // then, so this join only makes the resolve *deterministic* — it cannot
      // conjure a clock that was never created.
      patcher.Clocks()->WaitIdle();
    }

    void Send(const std::string& message) {
      transport->SetListData(0, message);
    }
    void Bang() {
      transport->SetBang(0);
    }
    void Tick() {
      ::Tick(patcher);
    }
  };

  // Drop a clock and let the manager retire it, so a case's name is free again
  // for the next run of the process.
  void DropClock(const std::string& name) {
    YSE::CLOCK::Manager().destroyClock(name);
    YSE::CLOCK::Manager().update(0.01f);
  }

} // namespace

TEST_SUITE("clock") {

  // ─── the object exists ──────────────────────────────────────────────────────

  TEST_CASE("transport: the object is registered and creatable (#513)") {
    bool found = false;
    for (const std::string& name : YSE::PATCHER::Register().AllNames()) {
      if (name == YSE::OBJ::G_TRANSPORT) found = true;
    }
    CHECK(found);

    patcherImplementation patcher(1, nullptr);
    YSE::pHandle* obj = patcher.CreateObject(YSE::OBJ::G_TRANSPORT, "");
    REQUIRE(obj != nullptr);
    // Two inlets and two outlets: control / tempo in, beat / tempo out.
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 2);
  }

  // ─── ownership: who makes the clock, and who never unmakes it ───────────────

  TEST_CASE("transport: joining a patcher creates the named clock, stopped (#513)") {
    // Half of the design gate. A patch that names a clock nobody made gets one,
    // so `.transport main` + `.metro clock main` is a working patch with no host
    // code at all — and it is created *stopped*, so dropping a transport into a
    // patch never starts anything moving behind the user's back.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE_FALSE(mgr.clockExists("tr.new"));

    TransportRig rig("tr.new 120");
    CHECK(mgr.clockExists("tr.new"));
    CHECK(mgr.currentTempo("tr.new") == doctest::Approx(0.f));

    // And it really is stopped: beats do not accumulate until something starts
    // it.
    for (int i = 0; i < 4; i++)
      rig.Tick();
    CHECK(mgr.beatPosition("tr.new") == doctest::Approx(0.0));

    // The binding went into the patcher's own bridge, under the name given.
    CHECK(rig.patcher.Clocks()->BoundCount() == 1);
    CHECK(std::string(rig.patcher.Clocks()->NameOf(1)) == "tr.new");

    DropClock("tr.new");
  }

  TEST_CASE("transport: a clock the host already made is left exactly as it is (#513)") {
    // The other half. `createClock` is first-registration-wins, so a transport
    // naming the host's clock is a second hand on the same lever rather than a
    // reset of it — which is the whole reason clocks are addressed by name.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("tr.host", 90.f));

    TransportRig rig("tr.host 120");
    // Not re-created at 0, and not re-tempoed to the transport's 120: still the
    // host's clock, still running at the host's tempo.
    CHECK(mgr.currentTempo("tr.host") == doctest::Approx(90.f));
    rig.Tick();
    CHECK(mgr.beatPosition("tr.host") == doctest::Approx(0.375)); // 0.25 s at 90 BPM

    DropClock("tr.host");
  }

  TEST_CASE("transport: deleting the object leaves its clock alone (#513)") {
    // The never-destroys half of the contract. A remote control that unplugged
    // the studio clock when it was thrown away would silently stop every other
    // object bound to that name, and the patcher cannot know whether the host,
    // a clip or another patch is still on it.
    auto& mgr = YSE::CLOCK::Manager();
    TransportRig rig("tr.keep 120");
    rig.Send("start");
    rig.Tick();
    REQUIRE(mgr.beatPosition("tr.keep") == doctest::Approx(0.5));

    rig.patcher.DeleteObject(rig.transport);
    rig.transport = nullptr;

    // Still there, still running, still at the tempo the transport left it.
    CHECK(mgr.clockExists("tr.keep"));
    CHECK(mgr.currentTempo("tr.keep") == doctest::Approx(kTempo));
    rig.Tick();
    CHECK(mgr.beatPosition("tr.keep") == doctest::Approx(1.0));

    DropClock("tr.keep");
  }

  TEST_CASE("transport: with no name, or with no patcher, it controls nothing (#513)") {
    // The "what if the clock does not exist" branch of the acceptance list. A
    // transport that reported beat 0 for a clock it does not have would be
    // lying to whatever it is wired into, so it says nothing at all.
    auto& mgr = YSE::CLOCK::Manager();

    TransportRig rig("");
    rig.Send("start");
    rig.Bang();
    CHECK(rig.out.seen.empty());
    CHECK(rig.patcher.Clocks()->BoundCount() == 0);

    // No patcher: SetParent is where creation and binding happen, so an object
    // that never got one creates nothing.
    YSE::PATCHER::gTransport orphan;
    orphan.SetParams("tr.orphan 120");
    orphan.GetInlet(0)->SetList("start", YSE::T_GUI);
    CHECK_FALSE(mgr.clockExists("tr.orphan"));
    CHECK_FALSE(orphan.Bound());
  }

  // ─── control: start, stop, tempo ────────────────────────────────────────────

  TEST_CASE("transport: start runs the clock and stop pauses it where it stands (#513)") {
    // A domain clock has no run flag and no origin to rewind to — its beat is
    // the running integral of its tempo — so start and stop are a tempo write
    // and nothing else, and stop is a *pause*.
    auto& mgr = YSE::CLOCK::Manager();
    TransportRig rig("tr.run 120");

    rig.Tick();
    CHECK(mgr.beatPosition("tr.run") == doctest::Approx(0.0));

    rig.Send("start");
    rig.Tick();
    CHECK(mgr.currentTempo("tr.run") == doctest::Approx(kTempo));
    CHECK(mgr.beatPosition("tr.run") == doctest::Approx(0.5));

    rig.Send("stop");
    for (int i = 0; i < 4; i++)
      rig.Tick();
    CHECK(mgr.currentTempo("tr.run") == doctest::Approx(0.f));
    CHECK(mgr.beatPosition("tr.run") == doctest::Approx(0.5));

    // Carries on from where it stopped rather than from zero.
    rig.Send("start");
    rig.Tick();
    CHECK(mgr.beatPosition("tr.run") == doctest::Approx(1.0));

    DropClock("tr.run");
  }

  TEST_CASE("transport: a non-zero int or float starts it and 0 stops it (#513)") {
    // Max's other spelling of the same two methods, and the `.metro` rule for
    // the float: compared against zero rather than cast, so 0.5 starts where
    // `(int)0.5` would stop.
    auto& mgr = YSE::CLOCK::Manager();
    TransportRig rig("tr.toggle 120");

    rig.transport->SetIntData(0, 1);
    rig.Tick();
    CHECK(mgr.currentTempo("tr.toggle") == doctest::Approx(kTempo));

    rig.transport->SetIntData(0, 0);
    rig.Tick();
    CHECK(mgr.currentTempo("tr.toggle") == doctest::Approx(0.f));

    rig.transport->SetFloatData(0, 0.5f);
    rig.Tick();
    CHECK(mgr.currentTempo("tr.toggle") == doctest::Approx(kTempo));

    rig.transport->SetFloatData(0, 0.f);
    rig.Tick();
    CHECK(mgr.currentTempo("tr.toggle") == doctest::Approx(0.f));

    DropClock("tr.toggle");
  }

  TEST_CASE("transport: 'tempo' retempos a running clock and is remembered while stopped (#513)") {
    auto& mgr = YSE::CLOCK::Manager();
    TransportRig rig("tr.tempo 120");

    rig.Send("start");
    rig.Tick();
    REQUIRE(mgr.beatPosition("tr.tempo") == doctest::Approx(0.5));

    // Live: the next block runs at the new tempo.
    rig.Send("tempo 240");
    rig.Tick();
    CHECK(mgr.currentTempo("tr.tempo") == doctest::Approx(240.f));
    CHECK(mgr.beatPosition("tr.tempo") == doctest::Approx(1.5));

    rig.Send("stop");
    rig.Tick();
    REQUIRE(mgr.currentTempo("tr.tempo") == doctest::Approx(0.f));

    // Remembered rather than obeyed: setting a tempo must not start a transport
    // nobody started.
    rig.Send("tempo 60");
    for (int i = 0; i < 4; i++)
      rig.Tick();
    CHECK(mgr.currentTempo("tr.tempo") == doctest::Approx(0.f));
    CHECK(mgr.beatPosition("tr.tempo") == doctest::Approx(1.5));

    rig.Send("start");
    rig.Tick();
    CHECK(mgr.currentTempo("tr.tempo") == doctest::Approx(60.f));
    CHECK(mgr.beatPosition("tr.tempo") == doctest::Approx(1.75)); // 0.25 s at 60 BPM

    // A bare `tempo` is not a tempo, and neither is a word that is not one of
    // the three commands: both leave everything where it is.
    rig.Send("tempo");
    rig.Send("seek 4");
    rig.Tick();
    CHECK(mgr.currentTempo("tr.tempo") == doctest::Approx(60.f));

    DropClock("tr.tempo");
  }

  TEST_CASE("transport: the cold inlet sets the tempo, live while running (#513)") {
    auto& mgr = YSE::CLOCK::Manager();
    TransportRig rig("tr.cold 120");

    // Stopped: remembered only.
    rig.transport->SetFloatData(1, 240.f);
    rig.Tick();
    CHECK(mgr.currentTempo("tr.cold") == doctest::Approx(0.f));

    rig.Send("start");
    rig.Tick();
    CHECK(mgr.currentTempo("tr.cold") == doctest::Approx(240.f));

    // Running: takes effect at the next block.
    rig.transport->SetIntData(1, 60);
    rig.Tick();
    CHECK(mgr.currentTempo("tr.cold") == doctest::Approx(60.f));

    DropClock("tr.cold");
  }

  TEST_CASE("transport: 'tempo <bpm> <ramp>' glides instead of jumping (#513)") {
    // The one thing Max's transport cannot do, and the reason `.transport` maps
    // onto `setTempo(name, bpm, rampSeconds)` rather than onto a tempo field:
    // a domain clock's tempo is a playable, rampable control.
    auto& mgr = YSE::CLOCK::Manager();
    TransportRig rig("tr.ramp 120");

    rig.Send("start");
    rig.Tick();
    REQUIRE(mgr.currentTempo("tr.ramp") == doctest::Approx(kTempo));

    // 120 -> 240 over one second is 120 BPM per second, so a quarter-second
    // block lands on 150 exactly.
    rig.Send("tempo 240 1");
    rig.Tick();
    CHECK(mgr.currentTempo("tr.ramp") == doctest::Approx(150.f));
    rig.Tick();
    CHECK(mgr.currentTempo("tr.ramp") == doctest::Approx(180.f));
    for (int i = 0; i < 2; i++)
      rig.Tick();
    CHECK(mgr.currentTempo("tr.ramp") == doctest::Approx(240.f));

    // A stop is instant whatever the ramp says — a stop that glided to a halt
    // would be a ritardando nobody asked for.
    rig.Send("stop");
    rig.Tick();
    CHECK(mgr.currentTempo("tr.ramp") == doctest::Approx(0.f));

    DropClock("tr.ramp");
  }

  // ─── control: reporting ─────────────────────────────────────────────────────

  TEST_CASE("transport: bang reports the tempo then the beat, right to left (#513)") {
    auto& mgr = YSE::CLOCK::Manager();
    TransportRig rig("tr.report 120");

    rig.Send("start");
    rig.Tick();
    rig.Tick();
    REQUIRE(mgr.beatPosition("tr.report") == doctest::Approx(1.0));

    rig.Bang();
    REQUIRE(rig.out.seen.size() == 2);
    // Both outlets feed the one recorder, so the order in `seen` is the send
    // order: Max's right-to-left, tempo before beat.
    CHECK(rig.out.seen[0] == doctest::Approx(kTempo));
    CHECK(rig.out.seen[1] == doctest::Approx(1.0f));

    // A stopped clock still has a position, and reports it.
    rig.out.seen.clear();
    rig.Send("stop");
    rig.Tick();
    rig.Bang();
    REQUIRE(rig.out.seen.size() == 2);
    CHECK(rig.out.seen[0] == doctest::Approx(0.f));
    CHECK(rig.out.seen[1] == doctest::Approx(1.0f));

    DropClock("tr.report");
  }

  // ─── the audio-callback route ───────────────────────────────────────────────

  TEST_CASE("transport: a command delivered from inside Calculate still lands (#513)") {
    // The property a direct unit call structurally cannot show. A patcher
    // message handler runs on whichever thread dispatched it, and the deferred
    // drain at the top of `Calculate` dispatches *from the audio callback* — so
    // this whole chain (`.delay` -> `.i 1` -> `.transport`) puts a start on the
    // audio thread, where `CLOCK::Manager()`'s mutex is out of bounds and the
    // object has to take the bridge's wait-free write instead. If that route
    // were missing, the clock would simply never start.
    auto& mgr = YSE::CLOCK::Manager();
    TransportRig rig("tr.rt 120");

    YSE::pHandle* del = rig.patcher.CreateObject(YSE::OBJ::G_DELAY, "");
    YSE::pHandle* one = rig.patcher.CreateObject(YSE::OBJ::G_INT, "1");
    REQUIRE(del != nullptr);
    REQUIRE(one != nullptr);
    rig.patcher.Connect(del, 0, one, 0);
    rig.patcher.Connect(one, 0, rig.transport, 0);

    // The wait-free write needs the binding resolved; the clock was created
    // before the bind, so this join is all it takes.
    rig.patcher.Clocks()->WaitIdle();
    REQUIRE(rig.patcher.Clocks()->Resolved(1));

    del->SetBang(0);
    const std::uint64_t due = YSE::PATCHER::messageScheduler::BlocksForMillis(5);
    for (std::uint64_t block = 0; block <= due; ++block)
      rig.Tick();
    // One more tick for the clock to consume the request the render published.
    rig.Tick();

    CHECK(mgr.currentTempo("tr.rt") == doctest::Approx(kTempo));
    CHECK(mgr.beatPosition("tr.rt") > 0.0);

    DropClock("tr.rt");
  }

  TEST_CASE("transport: its commands allocate nothing (#513)") {
    // The handler may be the audio callback — there is no predicate an object
    // can ask up front — so every command word has to be matched against the
    // message in place and every number read without building a string. The
    // probe sees std::string allocations since issue #697, so this assertion is
    // not vacuous over paths that carry text.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    TransportRig rig("tr.noalloc 120");

    // Built outside the probe: it is the *handler* that must not allocate, not
    // the test's own construction of the message.
    const std::string start = "start";
    const std::string stop = "stop";
    const std::string tempo = "tempo 140 2";
    {
      TestHelpers::ProbeScope probe;
      rig.transport->SetListData(0, start);
      rig.transport->SetListData(0, tempo);
      rig.transport->SetListData(0, stop);
      rig.transport->SetIntData(0, 1);
      rig.transport->SetFloatData(1, 90.f);
      rig.transport->SetBang(0);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    // And it really did something — an assertion that only proves nothing
    // happened proves nothing. The transport is running, gliding to the 90 the
    // cold inlet handed it over the two-second ramp `tempo 140 2` left behind,
    // so eight quarter-second blocks is exactly when it gets there.
    for (int i = 0; i < 8; i++)
      rig.Tick();
    CHECK(YSE::CLOCK::Manager().currentTempo("tr.noalloc") == doctest::Approx(90.f));

    DropClock("tr.noalloc");
  }

  // ─── persistence ────────────────────────────────────────────────────────────

  TEST_CASE("transport: parameters survive a DumpJSON / ParseJSON round trip (#513)") {
    // All three of them, and checked by *driving* the restored object rather
    // than by reading the JSON back: a parameter that survived the file but not
    // the rebuild would pass a string comparison and fail here.
    auto& mgr = YSE::CLOCK::Manager();

    YSE::patcher src;
    src.create(2);
    YSE::pHandle* obj = src.CreateObject(YSE::OBJ::G_TRANSPORT, "tr.save 90 1.5");
    REQUIRE(obj != nullptr);

    const std::string json = src.DumpJSON();
    CHECK(json.find("tr.save 90 1.5") != std::string::npos);

    patcherImplementation restored(1, nullptr);
    restored.ParseJSON(json);
    REQUIRE(restored.Objects() == 1);
    // The name came back and the rebuilt object bound it.
    restored.Clocks()->WaitIdle();
    CHECK(restored.Clocks()->BoundCount() == 1);
    CHECK(std::string(restored.Clocks()->NameOf(1)) == "tr.save");

    YSE::pHandle* back = restored.GetHandleFromList(0);
    REQUIRE(back != nullptr);
    back->SetListData(0, "start");

    // Tempo 90 reached over a 1.5 s ramp from the clock's 0 is 60 BPM per
    // second, so one quarter-second block lands on 15 exactly — which is the
    // tempo *and* the ramp, both proven by one number.
    Tick(restored);
    CHECK(mgr.currentTempo("tr.save") == doctest::Approx(15.f));
    for (int i = 0; i < 5; i++)
      Tick(restored);
    CHECK(mgr.currentTempo("tr.save") == doctest::Approx(90.f));

    DropClock("tr.save");
  }

} // TEST_SUITE
