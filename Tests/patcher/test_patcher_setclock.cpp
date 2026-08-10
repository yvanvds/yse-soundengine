// Tests for `.setclock` — the patcher's *maker* of a named domain clock
// (issue #515).
//
// The patcher now has three objects on the clock surface and they differ by
// exactly one thing each, so almost every case here is about the thing that
// makes this one a separate object rather than a synonym:
//
//   - `.when` (#514) only looks: it binds a name and creates nothing.
//   - `.transport` (#513) creates its clock **stopped**, at tempo 0, and spends
//     its int and float on `start` and `stop`. It is a play button.
//   - `.setclock` creates its clock **running**, at its `tempo` argument, and
//     its int and float are the *speed*. It is a time source — the object that
//     gives part of a patch a tempo of its own without a loadbang.
//
// Two cases below are written so that they would fail if `.setclock` were
// `gTransport` under a new name: `creates it running`, which compares the two
// objects side by side on the same argument, and the end-to-end
// `second tempo domain` case, where a `.timepoint` fires on a `.setclock`'s
// clock and does not fire on a `.transport`'s.
//
// The rest covers what the object shares with its siblings and therefore has to
// re-prove for itself: first-registration-wins, the lifetime rule #515 asked
// for (the clock outlives the object), the `tempo <bpm> <ramp>` grammar, the
// silence when there is no clock, the audio-callback route that a direct unit
// call structurally cannot reach, and the JSON round trip.
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
#include "patcher/time/gSetClock.h"
#include "patcher/time/messageScheduler.h"
#include "support/alloc_probe.hpp"

namespace {

  using YSE::PATCHER::patcherImplementation;

  // 0.25 s per tick at 120 BPM is exactly half a beat, and at 240 exactly one,
  // and every number in those two sentences is exact in binary — so the cases
  // below assert on boundaries rather than around them. `.transport`'s and
  // `.when`'s constants, so the three files' numbers line up side by side.
  constexpr float kTickSeconds = 0.25f;
  constexpr float kTempo = 120.f;
  constexpr float kFastTempo = 240.f;

  // One block of the whole world, in the engine's own order: advance every
  // domain clock, then let the patcher render. A tempo written during a render
  // is therefore consumed by the *next* tick, which is what happens on a real
  // device.
  void Tick(patcherImplementation& p) {
    YSE::CLOCK::Manager().update(kTickSeconds);
    p.Calculate(YSE::T_DSP);
  }

  // Records every float it is sent, in order.
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
      return "setclock_recorder";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // Counts bangs. The end-to-end case wires a `.timepoint` into one of these.
  struct BangCounter : YSE::PATCHER::pObject {
    int bangs = 0;

    BangCounter() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { bangs++; });
    }
    const char* Type() const override {
      return "setclock_bangs";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A `.setclock` living in a real patcher, with a recorder on its one outlet.
  struct SetClockRig {
    patcherImplementation patcher{1, nullptr};
    FloatRecorder out;
    YSE::pHandle outHandle{&out};
    YSE::pHandle* obj = nullptr;

    explicit SetClockRig(const std::string& args) {
      obj = patcher.CreateObject(YSE::OBJ::G_SETCLOCK, args);
      REQUIRE(obj != nullptr);
      patcher.Connect(obj, 0, &outHandle, 0);
      // Makes the resolve attempt deterministic.
      patcher.Clocks()->WaitIdle();
    }

    void Bang() {
      obj->SetBang(0);
    }
    void Int(int value) {
      obj->SetIntData(0, value);
    }
    void Float(float value) {
      obj->SetFloatData(0, value);
    }
    void Send(const std::string& message) {
      obj->SetListData(0, message);
    }
    void Tick() {
      ::Tick(patcher);
    }
    void Ticks(int n) {
      for (int i = 0; i < n; i++)
        Tick();
    }
  };

  // Drop a clock and let the manager retire it, so a case's name is free again
  // for the next run of the process. Every case here has to do this by hand:
  // the object under test deliberately never destroys anything.
  void DropClock(const std::string& name) {
    YSE::CLOCK::Manager().destroyClock(name);
    YSE::CLOCK::Manager().update(0.01f);
  }

} // namespace

TEST_SUITE("clock") {

  // ─── the object exists ──────────────────────────────────────────────────────

  TEST_CASE("setclock: the object is registered and creatable (#515)") {
    bool found = false;
    for (const std::string& name : YSE::PATCHER::Register().AllNames()) {
      if (name == YSE::OBJ::G_SETCLOCK) found = true;
    }
    CHECK(found);

    patcherImplementation patcher(1, nullptr);
    YSE::pHandle* obj = patcher.CreateObject(YSE::OBJ::G_SETCLOCK, "");
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == ".setclock");
    // Max's shape minus the inlet it cannot have: one inlet, because the right
    // one sets a reporting interval and nothing here polls a clock, and one
    // outlet, because Max's bang outputs the time and nothing else.
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 1);

    YSE::PATCHER::gSetClock standalone;
    CHECK(standalone.GetCategory() == YSE::PATCHER::pCategory::TIME);
    // The defaults: no clock, 120 BPM, no glide.
    CHECK(std::string(standalone.ClockName()).empty());
    CHECK(standalone.WantedTempo() == doctest::Approx(120.f));
    CHECK(standalone.WantedRamp() == doctest::Approx(0.f));
  }

  // ─── ownership: it makes the clock, and makes it turn ───────────────────────

  TEST_CASE("setclock: joining a patcher creates the clock, and creates it running (#515)") {
    // **The object.** A `.transport` creates its clock stopped and waits to be
    // started (#513); a `.when` creates nothing at all (#514). This one creates
    // a clock that is already turning at its argument, which is what makes a
    // second tempo domain a single object rather than an object plus a
    // loadbang. Both halves are checked here, side by side on the same
    // argument, so the case fails if this object were `gTransport` renamed.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE_FALSE(mgr.clockExists("sc.make"));

    SetClockRig rig("sc.make 240");
    REQUIRE(mgr.clockExists("sc.make"));
    CHECK(mgr.currentTempo("sc.make") == doctest::Approx(kFastTempo));
    CHECK(rig.patcher.Clocks()->BoundCount() == 1);
    CHECK(std::string(rig.patcher.Clocks()->NameOf(1)) == "sc.make");

    // And it is really advancing, with nothing sent to it: one quarter-second
    // block at 240 BPM is one beat exactly.
    rig.Tick();
    CHECK(mgr.beatPosition("sc.make") == doctest::Approx(1.0));
    rig.Ticks(3);
    CHECK(mgr.beatPosition("sc.make") == doctest::Approx(4.0));

    // The same creation argument given to a `.transport` produces a clock that
    // is not turning at all, which is the difference in one line.
    patcherImplementation other(1, nullptr);
    REQUIRE(other.CreateObject(YSE::OBJ::G_TRANSPORT, "sc.make.tr 240") != nullptr);
    REQUIRE(mgr.clockExists("sc.make.tr"));
    CHECK(mgr.currentTempo("sc.make.tr") == doctest::Approx(0.f));
    Tick(other);
    CHECK(mgr.beatPosition("sc.make.tr") == doctest::Approx(0.0));

    DropClock("sc.make.tr");
    DropClock("sc.make");
  }

  TEST_CASE("setclock: a name someone already claimed is left exactly as it is (#515)") {
    // First registration wins, `.transport`'s rule. The clock the host made is
    // *not* re-tempoed to this object's argument — a patch that names an
    // existing clock is joining it, not seizing it — and the object says which
    // of the two happened.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("sc.taken", kTempo));

    SetClockRig rig("sc.taken 240");
    CHECK(mgr.currentTempo("sc.taken") == doctest::Approx(kTempo));
    rig.Tick();
    CHECK(mgr.beatPosition("sc.taken") == doctest::Approx(0.5));

    // Bound all the same, and still able to drive it — joining is not seizing,
    // but it is not being locked out either.
    CHECK(rig.patcher.Clocks()->BoundCount() == 1);
    rig.Float(kFastTempo);
    rig.Tick();
    CHECK(mgr.currentTempo("sc.taken") == doctest::Approx(kFastTempo));

    DropClock("sc.taken");
  }

  TEST_CASE("setclock: deleting the object leaves its clock running (#515)") {
    // The lifetime rule #515 asked to have decided, pinned. Max destroys a
    // setclock's clock with the object and its clients revert to the default
    // one; that outcome is not reachable here, because a clockBridge binding
    // owns a share of its clock and is never released (#707) — a destroy would
    // freeze every `.metro clock <name>` on a clock that has stopped advancing
    // rather than hand it back. So the clock outlives the object, and teardown
    // belongs to the host.
    auto& mgr = YSE::CLOCK::Manager();
    SetClockRig rig("sc.keep 240");
    rig.Tick();
    REQUIRE(mgr.beatPosition("sc.keep") == doctest::Approx(1.0));

    rig.patcher.DeleteObject(rig.obj);
    rig.obj = nullptr;

    CHECK(mgr.clockExists("sc.keep"));
    CHECK(mgr.currentTempo("sc.keep") == doctest::Approx(kFastTempo));
    rig.Tick();
    CHECK(mgr.beatPosition("sc.keep") == doctest::Approx(2.0));

    DropClock("sc.keep");
  }

  TEST_CASE("setclock: with no name it does nothing, and with no patcher it creates nothing "
            "(#515)") {
    auto& mgr = YSE::CLOCK::Manager();

    SetClockRig rig("");
    rig.Bang();
    rig.Float(150.f);
    rig.Send("tempo 150");
    CHECK(rig.out.seen.empty());
    CHECK(rig.patcher.Clocks()->BoundCount() == 0);

    // No patcher: SetParent is where creating and binding both happen, so a
    // standalone object does neither — however good its argument looks.
    REQUIRE_FALSE(mgr.clockExists("sc.orphan"));
    YSE::PATCHER::gSetClock orphan;
    orphan.SetParams("sc.orphan 240");
    orphan.GetInlet(0)->SetBang(YSE::T_GUI);
    orphan.GetInlet(0)->SetFloat(90.f, YSE::T_GUI);
    CHECK_FALSE(mgr.clockExists("sc.orphan"));
    CHECK_FALSE(orphan.Bound());
    CHECK_FALSE(orphan.CreatedClock());
    // It parsed its arguments all the same, and remembers the number it was
    // sent — the clock is what is missing, not the object.
    CHECK(std::string(orphan.ClockName()) == "sc.orphan");
    CHECK(orphan.WantedTempo() == doctest::Approx(90.f));
  }

  // ─── the inlet is the speed ─────────────────────────────────────────────────

  TEST_CASE("setclock: the number inlet is the speed, not a start and stop (#515)") {
    // The other half of the difference from `.transport`, where these exact
    // values would be start, stop, start. Here a slider on this inlet sweeps
    // the tempo, which is the thing you want to sweep.
    auto& mgr = YSE::CLOCK::Manager();
    SetClockRig rig("sc.speed 120");
    rig.Tick();
    REQUIRE(mgr.beatPosition("sc.speed") == doctest::Approx(0.5));

    rig.Int(240);
    rig.Tick();
    CHECK(mgr.currentTempo("sc.speed") == doctest::Approx(kFastTempo));
    CHECK(mgr.beatPosition("sc.speed") == doctest::Approx(1.5));

    // 0 is a tempo like any other, and tempo 0 is what "stopped" means to a
    // domain clock — so this is the stop, and there is no second piece of state
    // remembering a tempo to come back to.
    rig.Int(0);
    rig.Tick();
    CHECK(mgr.currentTempo("sc.speed") == doctest::Approx(0.f));
    CHECK(mgr.beatPosition("sc.speed") == doctest::Approx(1.5));
    rig.Ticks(4);
    CHECK(mgr.beatPosition("sc.speed") == doctest::Approx(1.5));

    // And negative runs it backwards, because a domain clock's tempo is not
    // clamped and this object does not invent a clamp of its own.
    rig.Float(-120.f);
    rig.Tick();
    CHECK(mgr.beatPosition("sc.speed") == doctest::Approx(1.0));
    CHECK(rig.out.seen.empty()); // setting a tempo emits nothing

    DropClock("sc.speed");
  }

  TEST_CASE("setclock: 'tempo <bpm> <ramp>' glides, and nothing else is a message (#515)") {
    auto& mgr = YSE::CLOCK::Manager();
    SetClockRig rig("sc.msg 120");

    // Max's word for the same setting, plus the thing Max's setclock cannot do:
    // 120 to 240 over 1 s is 120 BPM per second, so one quarter-second block
    // lands on 150 exactly — the tempo *and* the ramp, proven by one number.
    rig.Send("tempo 240 1");
    rig.Tick();
    CHECK(mgr.currentTempo("sc.msg") == doctest::Approx(150.f));
    rig.Ticks(3);
    CHECK(mgr.currentTempo("sc.msg") == doctest::Approx(kFastTempo));

    // The ramp sticks, as `.transport`'s does: a bare number glides too.
    rig.Float(120.f);
    rig.Tick();
    CHECK(mgr.currentTempo("sc.msg") == doctest::Approx(210.f));
    rig.Ticks(3);
    CHECK(mgr.currentTempo("sc.msg") == doctest::Approx(kTempo));

    // Everything that is not a `tempo <number>` leaves the clock alone: a bare
    // word with no number, a word that merely starts with the right letters,
    // Max's modes and its reporting interval, and the `clock <name>` the
    // clock-reading objects take but this one — being the clock — does not.
    const float before = mgr.currentTempo("sc.msg");
    rig.Send("tempo");
    rig.Send("tempo later");
    rig.Send("tempos 240");
    rig.Send("mode mul 4");
    rig.Send("resolution 10");
    rig.Send("set 480");
    rig.Send("clock sc.msg.other");
    rig.Ticks(2);
    CHECK(mgr.currentTempo("sc.msg") == doctest::Approx(before));
    CHECK(rig.out.seen.empty());
    // And it is still the clock it was created with, not re-pointed.
    CHECK(std::string(rig.patcher.Clocks()->NameOf(1)) == "sc.msg");
    CHECK(rig.patcher.Clocks()->BoundCount() == 1);

    DropClock("sc.msg");
  }

  // ─── reporting ──────────────────────────────────────────────────────────────

  TEST_CASE("setclock: bang reports the beat position, and silence when there is none (#515)") {
    auto& mgr = YSE::CLOCK::Manager();
    SetClockRig rig("sc.report 240");

    rig.Ticks(2);
    REQUIRE(mgr.beatPosition("sc.report") == doctest::Approx(2.0));
    rig.Bang();
    REQUIRE(rig.out.seen.size() == 1);
    CHECK(rig.out.seen[0] == doctest::Approx(2.f));

    // A live read, not a value cached when the object was built. One outlet,
    // Max's, so the tempo is not sent beside it — `.when` reads that back.
    rig.out.seen.clear();
    rig.Tick();
    rig.Bang();
    REQUIRE(rig.out.seen.size() == 1);
    CHECK(rig.out.seen[0] == doctest::Approx(3.f));

    DropClock("sc.report");

    // Nothing at all once the clock is gone — not a zero, which would be
    // indistinguishable from a clock genuinely sitting at beat 0.
    rig.out.seen.clear();
    rig.Bang();
    CHECK(rig.out.seen.empty());
  }

  // ─── the audio-callback route ───────────────────────────────────────────────

  TEST_CASE("setclock: a tempo delivered from inside Calculate lands, through the bridge (#515)") {
    // The property a direct unit call structurally cannot show, and the whole
    // reason `Push` has two routes. A patcher message handler runs on whichever
    // thread dispatched it, and the deferred drain at the top of `Calculate`
    // dispatches *from the audio callback* — so this chain (`.pipe` ->
    // `.setclock`) writes a tempo on the audio thread, where
    // `CLOCK::Manager()`'s mutex is out of bounds.
    auto& mgr = YSE::CLOCK::Manager();
    SetClockRig rig("sc.rt 120");

    YSE::pHandle* pipe = rig.patcher.CreateObject(YSE::OBJ::G_PIPE, "5");
    REQUIRE(pipe != nullptr);
    rig.patcher.Connect(pipe, 0, rig.obj, 0);

    // The wait-free write needs the binding resolved; the clock was created
    // before the bind, so this join is all it takes.
    rig.patcher.Clocks()->WaitIdle();
    REQUIRE(rig.patcher.Clocks()->Resolved(1));

    const std::uint64_t due = YSE::PATCHER::messageScheduler::BlocksForMillis(5);
    pipe->SetFloatData(0, kFastTempo);
    for (std::uint64_t block = 0; block <= due + 1; ++block)
      rig.Tick();
    CHECK(mgr.currentTempo("sc.rt") == doctest::Approx(kFastTempo));

    // Which route it took, pinned exactly. Destroy the clock and make a fresh
    // one under the same name: the manager now answers with the new clock,
    // while the binding stays attached to the old, frozen one forever — `Poll`
    // only ever retries bindings that never resolved (#707). So a write that
    // reaches the *new* clock came through `CLOCK::Manager()`, and one that
    // does not came through the bridge.
    mgr.destroyClock("sc.rt");
    mgr.update(0.01f);
    REQUIRE_FALSE(mgr.clockExists("sc.rt"));
    REQUIRE(mgr.createClock("sc.rt", kTempo));

    pipe->SetFloatData(0, 33.f);
    for (std::uint64_t block = 0; block <= due + 1; ++block)
      rig.Tick();
    // Untouched: the write from inside Calculate went to the binding, not to
    // the name. A by-name write on the audio thread would have moved this.
    CHECK(mgr.currentTempo("sc.rt") == doctest::Approx(kTempo));

    // And off the callback it is the other route, on the same object in the
    // same breath — the manager, by name, reaching the clock that now holds it.
    rig.Float(77.f);
    rig.Tick();
    CHECK(mgr.currentTempo("sc.rt") == doctest::Approx(77.f));

    DropClock("sc.rt");
  }

  TEST_CASE("setclock: its handlers allocate nothing (#515)") {
    // The handler may be the audio callback — there is no predicate an object
    // can ask up front — so nothing may be read out of the message into a
    // string. The probe sees std::string allocations since issue #697, so this
    // assertion is not vacuous over the paths that carry text.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    auto& mgr = YSE::CLOCK::Manager();
    SetClockRig rig("sc.noalloc 120");
    rig.Tick();

    // Built outside the probe: it is the *handler* that must not allocate, not
    // the test's own construction of the message.
    const std::string command = "tempo 180 0.5";
    const std::string ignored = "mode interp some other patcher message 1 2 3";
    {
      TestHelpers::ProbeScope probe;
      rig.obj->SetBang(0);
      rig.obj->SetIntData(0, 200);
      rig.obj->SetFloatData(0, 200.5f);
      rig.obj->SetListData(0, command);
      rig.obj->SetListData(0, ignored);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    // And it really did something — an assertion that only proves nothing
    // happened proves nothing.
    CHECK(rig.out.seen.size() == 1); // the bang
    // The last command was `tempo 180 0.5`, so the clock is on its way there.
    rig.Ticks(3);
    CHECK(mgr.currentTempo("sc.noalloc") == doctest::Approx(180.f));

    DropClock("sc.noalloc");
  }

  // ─── the use case, end to end ───────────────────────────────────────────────

  TEST_CASE("setclock: a patch gets a second tempo domain that is turning on load (#515)") {
    // Issue #515's use case, driven through the objects a patch would actually
    // use: a `.setclock` makes the clock and a `.timepoint` bound to the same
    // name fires on it, with nothing sent to either object. The second half is
    // the same patch built on a `.transport` instead — the object a reader
    // might reach for — where the timepoint never fires, because a transport's
    // clock is created stopped and is waiting for a `start` nobody sent.
    auto& mgr = YSE::CLOCK::Manager();

    patcherImplementation live(1, nullptr);
    BangCounter fired;
    YSE::pHandle firedHandle{&fired};
    REQUIRE(live.CreateObject(YSE::OBJ::G_SETCLOCK, "sc.poly 240") != nullptr);
    YSE::pHandle* point = live.CreateObject(YSE::OBJ::G_TIMEPOINT, "sc.poly 2");
    REQUIRE(point != nullptr);
    live.Connect(point, 0, &firedHandle, 0);
    live.Clocks()->WaitIdle();

    // One quarter-second block is one beat at 240 BPM, so four blocks sail past
    // beat 2 — and a crossing fires once.
    for (int i = 0; i < 4; i++)
      Tick(live);
    CHECK(mgr.beatPosition("sc.poly") == doctest::Approx(4.0));
    CHECK(fired.bangs == 1);

    patcherImplementation stopped(1, nullptr);
    BangCounter never;
    YSE::pHandle neverHandle{&never};
    REQUIRE(stopped.CreateObject(YSE::OBJ::G_TRANSPORT, "sc.poly.tr 240") != nullptr);
    YSE::pHandle* other = stopped.CreateObject(YSE::OBJ::G_TIMEPOINT, "sc.poly.tr 2");
    REQUIRE(other != nullptr);
    stopped.Connect(other, 0, &neverHandle, 0);
    stopped.Clocks()->WaitIdle();

    for (int i = 0; i < 4; i++)
      Tick(stopped);
    CHECK(mgr.beatPosition("sc.poly.tr") == doctest::Approx(0.0));
    CHECK(never.bangs == 0);

    DropClock("sc.poly.tr");
    DropClock("sc.poly");
  }

  // ─── persistence ────────────────────────────────────────────────────────────

  TEST_CASE("setclock: parameters survive a DumpJSON / ParseJSON round trip (#515)") {
    // All three of them, and checked by *driving* the restored object rather
    // than by reading the JSON back: a parameter that survived the file but not
    // the rebuild would pass a string comparison and fail here.
    auto& mgr = YSE::CLOCK::Manager();

    YSE::patcher src;
    src.create(2);
    YSE::pHandle* obj = src.CreateObject(YSE::OBJ::G_SETCLOCK, "sc.save 90 1.5");
    REQUIRE(obj != nullptr);

    const std::string json = src.DumpJSON();
    CHECK(json.find("sc.save 90 1.5") != std::string::npos);
    // The clock came into being at the saved tempo, already running.
    REQUIRE(mgr.clockExists("sc.save"));
    CHECK(mgr.currentTempo("sc.save") == doctest::Approx(90.f));

    patcherImplementation restored(1, nullptr);
    restored.ParseJSON(json);
    REQUIRE(restored.Objects() == 1);
    restored.Clocks()->WaitIdle();
    CHECK(restored.Clocks()->BoundCount() == 1);
    CHECK(std::string(restored.Clocks()->NameOf(1)) == "sc.save");

    YSE::pHandle* back = restored.GetHandleFromList(0);
    REQUIRE(back != nullptr);
    back->SetIntData(0, 150);

    // 90 to 150 over the restored 1.5 s ramp is 40 BPM per second, so one
    // quarter-second block lands on 100 exactly — the ramp proven by a number
    // that only the saved 1.5 produces.
    Tick(restored);
    CHECK(mgr.currentTempo("sc.save") == doctest::Approx(100.f));
    for (int i = 0; i < 5; i++)
      Tick(restored);
    CHECK(mgr.currentTempo("sc.save") == doctest::Approx(150.f));

    DropClock("sc.save");
  }

} // TEST_SUITE
