// Tests for `.timepoint` — a bang at an absolute beat position on a named
// domain clock (issue #507).
//
// The cases are organised around the three things that make this object what it
// is, rather than around its methods:
//
//   - **grammar** cases run against a standalone object, which has no patcher
//     and so no clock at all. That is exactly what makes them the right place
//     for everything that is not timing: a command must not be read as a time
//     value, and an object with nothing to watch must not bang.
//
//   - **timing** cases run a real `patcherImplementation` with a real
//     `.transport` driving a real `CLOCK::domainClock` underneath, because the
//     claim is "the clock the patch watches is the clock the engine runs" and
//     only a real clock can carry it. They also pin the decision the object
//     turns on: reaching a time is a *crossing*, so a target already in the past
//     is spent rather than fired.
//
//   - **contract** cases pin the half of #513 this object had to inherit
//     without breaking: `.transport` creates clocks and `.timepoint` never does.
//     A patch that watched a name into existence would leave every object bound
//     to it sitting on a clock nobody started.
//
//   - **route** covers what a direct call structurally cannot see: a time value
//     arriving *from inside `Calculate`* — the audio callback — has to re-arm
//     through the bridge's wait-free pair. The rig for that is a real patch
//     (`.delay` -> `.i` -> `.timepoint`), because the only way to get a handler
//     onto the audio thread is to have the patcher put it there.
//
//   - **persistence** covers the DumpJSON / ParseJSON round trip end to end: the
//     restored object is wired up and driven, so a parameter that survived the
//     JSON but not the rebuild would still fail.
//
// Registered in the `clock` suite, not `patcher`: the cases drive
// CLOCK::Manager().update() directly on the test thread to advance beats
// deterministically, which would race a live audio thread from another suite in
// the shared monolithic process. No audio device required.

#include <doctest/doctest.h>
#include <cstdint>
#include <string>

#include "clock/clockManager.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "patcher/time/clockBridge.h"
#include "patcher/time/gTimepoint.h"
#include "patcher/time/messageScheduler.h"
#include "support/alloc_probe.hpp"

namespace {

  using TestHelpers::Wire;
  using YSE::PATCHER::clockBridge;
  using YSE::PATCHER::gTimepoint;
  using YSE::PATCHER::patcherImplementation;

  // 0.25 s per tick at 120 BPM is exactly half a beat, and every number in that
  // sentence is exact in binary, so the cases below assert on boundaries rather
  // than around them. The same constants the #513 and #688 cases use.
  constexpr float kTickSeconds = 0.25f;
  constexpr float kTempo = 120.f;

  // One block of the whole world, in the engine's own order: advance every
  // domain clock, then let the patcher render. A tempo written during a render
  // is therefore consumed by the *next* tick, which is what happens on a real
  // device.
  void Tick(patcherImplementation& p) {
    YSE::CLOCK::Manager().update(kTickSeconds);
    p.Calculate(YSE::T_DSP);
  }

  // Counts the bangs it receives. That is the entire observable surface of this
  // object: one outlet, one kind of message.
  struct Counter : YSE::PATCHER::pObject {
    int bangs = 0;

    Counter() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { bangs++; });
    }
    const char* Type() const override {
      return "timepoint_counter";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A standalone `.timepoint` with a counter on its outlet. Standalone means no
  // patcher, so no bridge, no scheduler and no clock — which is what makes this
  // rig the right place for everything that is not timing.
  struct Rig {
    gTimepoint obj;
    Counter out;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) obj.SetParams(args);
      Wire(obj, 0, out);
    }

    void Int(int value) {
      obj.GetInlet(0)->SetInt(value, YSE::T_GUI);
    }
    void Float(float value) {
      obj.GetInlet(0)->SetFloat(value, YSE::T_GUI);
    }
    void List(const std::string& message) {
      obj.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
  };

  // A `.timepoint` in a real patcher, optionally with a `.transport` driving the
  // clock it watches, and a counter on its outlet.
  struct ClockRig {
    patcherImplementation patcher{1, nullptr};
    Counter out;
    YSE::pHandle outHandle{&out};
    YSE::pHandle* transport = nullptr;
    YSE::pHandle* point = nullptr;

    // An empty `transportArgs` builds no `.transport` at all, so the clock is
    // whatever the case made — or nothing, which is its own test.
    ClockRig(const std::string& transportArgs, const std::string& pointArgs) {
      if (!transportArgs.empty()) {
        transport = patcher.CreateObject(YSE::OBJ::G_TRANSPORT, transportArgs);
        REQUIRE(transport != nullptr);
      }
      point = patcher.CreateObject(YSE::OBJ::G_TIMEPOINT, pointArgs);
      REQUIRE(point != nullptr);
      patcher.Connect(point, 0, &outHandle, 0);
      // The objects bind in SetParent; this only makes the resolve *deterministic*
      // — it cannot conjure a clock that was never created.
      patcher.Clocks()->WaitIdle();
    }

    void Drive(const std::string& message) {
      transport->SetListData(0, message);
    }
    void Send(const std::string& message) {
      point->SetListData(0, message);
    }
    void Target(float beat) {
      point->SetFloatData(0, beat);
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
  // for the next run of the process.
  void DropClock(const std::string& name) {
    YSE::CLOCK::Manager().destroyClock(name);
    YSE::CLOCK::Manager().update(0.01f);
  }

} // namespace

TEST_SUITE("clock") {

  // ─── the object exists ──────────────────────────────────────────────────────

  TEST_CASE("timepoint: the object is registered and creatable (#507)") {
    bool found = false;
    for (const std::string& name : YSE::PATCHER::Register().AllNames()) {
      if (name == YSE::OBJ::G_TIMEPOINT) found = true;
    }
    CHECK(found);

    patcherImplementation patcher(1, nullptr);
    YSE::pHandle* obj = patcher.CreateObject(YSE::OBJ::G_TIMEPOINT, "");
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == ".timepoint");
    // Max's shape: one inlet, one outlet.
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 1);

    gTimepoint standalone;
    CHECK(standalone.GetCategory() == YSE::PATCHER::pCategory::TIME);
  }

  // ─── grammar, which needs no clock ──────────────────────────────────────────

  TEST_CASE("timepoint: the creation arguments are the clock, the beat and active (#507)") {
    Rig rig("tp.args 32 0");
    CHECK(rig.obj.Target() == 32.0);
    CHECK_FALSE(rig.obj.IsActive());
    // No patcher means no bridge: nothing is bound and nothing is armed, and a
    // standalone object never bangs however far a clock somewhere else has run.
    CHECK_FALSE(rig.obj.Bound());
    CHECK(std::string(rig.obj.ClockName()).empty());
    CHECK_FALSE(rig.obj.Armed());

    Rig fresh;
    CHECK(fresh.obj.Target() == 0.0);
    CHECK(fresh.obj.IsActive());
  }

  TEST_CASE("timepoint: a number sets the time and the commands are not numbers (#507)") {
    Rig rig("tp.grammar 4");

    rig.Float(12.5f);
    CHECK(rig.obj.Target() == 12.5);
    rig.Int(7);
    CHECK(rig.obj.Target() == 7.0);
    // A list with a leading number means what a bare number means.
    rig.List("9");
    CHECK(rig.obj.Target() == 9.0);

    // `active` is a command. A bare `active` is not a setting, so it changes
    // nothing rather than picking a value.
    rig.List("active 0");
    CHECK_FALSE(rig.obj.IsActive());
    rig.List("active");
    CHECK_FALSE(rig.obj.IsActive());
    rig.List("active 1");
    CHECK(rig.obj.IsActive());

    // `clock` is a command too, and inert here: a standalone object has no
    // bridge to bind through. It must still not be read as a time.
    rig.List("clock elsewhere");
    CHECK_FALSE(rig.obj.Bound());
    CHECK(rig.obj.Target() == 9.0);

    // Everything Max spells that this patcher cannot: quantize needs a meter, a
    // bars.beats.units figure needs one as well as an origin, and an unknown
    // word is a word. None of them may become a number.
    rig.List("quantize 4n");
    rig.List("1.1.0");
    rig.List("seek 4");
    rig.List("wibble");
    CHECK(rig.obj.Target() == 9.0);

    // And nothing above emitted anything: there is no clock to have reached.
    CHECK(rig.out.bangs == 0);
  }

  // ─── timing: reaching the beat ──────────────────────────────────────────────

  TEST_CASE("timepoint: bangs when the clock reaches the beat, once (#507)") {
    // The object's whole reason for existing, end to end: a `.transport` runs
    // the clock, and the timepoint fires at the beat it names — not a block
    // early, not a block late, and not twice.
    auto& mgr = YSE::CLOCK::Manager();
    ClockRig rig("tp.reach 120", "tp.reach 2");

    // The first block delivers the baseline probe: the binding was unresolved
    // when the object joined, so this is its first sight of the clock.
    rig.Tick();
    CHECK(rig.out.bangs == 0);
    // Created stopped, per #513, so nothing has moved yet.
    CHECK(mgr.beatPosition("tp.reach") == doctest::Approx(0.0));

    rig.Drive("start");
    rig.Ticks(3); // 0.5, 1.0, 1.5
    CHECK(rig.out.bangs == 0);

    rig.Tick(); // 2.0
    CHECK(rig.out.bangs == 1);
    CHECK(mgr.beatPosition("tp.reach") == doctest::Approx(2.0));

    // Spent: a crossing happens once, and the clock going on past the target is
    // not a second one.
    rig.Ticks(8);
    CHECK(rig.out.bangs == 1);

    DropClock("tp.reach");
  }

  TEST_CASE("timepoint: a target already in the past never bangs (#507)") {
    // The decision the object turns on. A patch loaded, or an object dropped in,
    // while the clock stands at bar 40 must not fire its whole score in one
    // block — so reaching a time is a crossing and not a condition, and a
    // crossing that happened before anything was watching is gone.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("tp.past", kTempo));
    for (int i = 0; i < 6; i++)
      mgr.update(kTickSeconds);
    REQUIRE(mgr.beatPosition("tp.past") == doctest::Approx(3.0));

    ClockRig rig("", "tp.past 2");
    rig.Ticks(12); // the clock sails on from 3 to 9
    CHECK(rig.out.bangs == 0);
    CHECK(mgr.beatPosition("tp.past") == doctest::Approx(9.0));

    DropClock("tp.past");
  }

  TEST_CASE("timepoint: a new time value re-arms a spent object (#507)") {
    // What makes the crossing rule livable: the object is re-usable, and the way
    // to re-use it is to name the next moment.
    ClockRig rig("tp.rearm 120", "tp.rearm 1");
    rig.Tick();
    rig.Drive("start");

    rig.Ticks(2); // 0.5, 1.0
    CHECK(rig.out.bangs == 1);
    rig.Ticks(4); // 1.5 .. 3.0, still spent
    CHECK(rig.out.bangs == 1);

    rig.Target(4.f);
    rig.Ticks(2); // 3.5, 4.0
    CHECK(rig.out.bangs == 2);

    // A time value the clock has already passed leaves it spent rather than
    // banging at once — the same rule, reached from the inlet instead of from
    // the creation argument.
    rig.Target(1.f);
    rig.Ticks(8);
    CHECK(rig.out.bangs == 2);

    DropClock("tp.rearm");
  }

  TEST_CASE("timepoint: it follows the clock's tempo, and holds while it is paused (#507)") {
    // The whole point of measuring on a domain clock rather than in
    // milliseconds: the target arrives sooner, later, or never, and it does so
    // in step with every other object and every clip on that domain.
    auto& mgr = YSE::CLOCK::Manager();
    ClockRig rig("tp.tempo 120", "tp.tempo 2");
    rig.Tick();
    rig.Drive("start");

    rig.Ticks(2); // 1.0
    CHECK(rig.out.bangs == 0);

    // Paused short of the target: a domain clock at tempo 0 never reaches it,
    // however long the engine runs.
    rig.Drive("stop");
    rig.Ticks(10);
    CHECK(rig.out.bangs == 0);
    CHECK(mgr.beatPosition("tp.tempo") == doctest::Approx(1.0));

    // Half the tempo, so twice the blocks — and the target is still the same
    // beat, which is what "absolute position" means.
    rig.Drive("tempo 60");
    rig.Drive("start");
    rig.Ticks(3); // 1.25, 1.5, 1.75
    CHECK(rig.out.bangs == 0);
    rig.Tick(); // 2.0
    CHECK(rig.out.bangs == 1);

    DropClock("tp.tempo");
  }

  TEST_CASE("timepoint: 'active' arms and disarms the object (#507)") {
    // Max's active attribute, as a creation argument and as a message. An
    // inactive timepoint arms nothing, so a target passes under it unremarked;
    // `active 1` arms it afresh from wherever the clock now stands.
    ClockRig rig("tp.active 120", "tp.active 2 0");
    rig.Tick();
    rig.Drive("start");

    rig.Ticks(8); // 0.5 .. 4.0, well past the target of 2
    CHECK(rig.out.bangs == 0);

    // A time value on an inactive object sets the time and arms nothing.
    rig.Target(6.f);
    rig.Ticks(6); // 4.5 .. 7.0, past 6
    CHECK(rig.out.bangs == 0);

    rig.Target(9.f);
    rig.Send("active 1");
    rig.Ticks(3); // 7.5, 8.0, 8.5
    CHECK(rig.out.bangs == 0);
    rig.Tick(); // 9.0
    CHECK(rig.out.bangs == 1);

    // And back off again.
    rig.Send("active 0");
    rig.Target(12.f);
    rig.Ticks(10);
    CHECK(rig.out.bangs == 1);

    DropClock("tp.active");
  }

  TEST_CASE("timepoint: 'clock <name>' re-points it and a bare 'clock' stops it (#507)") {
    // `.metro`'s port of Max's own `clock` message, on an object whose position
    // is only meaningful against a named clock. A bare `clock` therefore stops
    // the object watching rather than falling back to milliseconds: an absolute
    // beat has no meaning on a clock that does not count beats.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("tp.a", 0.f));
    REQUIRE(mgr.createClock("tp.b", 0.f));

    ClockRig rig("", "tp.a 2");
    rig.Tick();
    CHECK(rig.patcher.Clocks()->BoundCount() == 1);

    rig.Send("clock tp.b");
    rig.patcher.Clocks()->WaitIdle();
    rig.Tick(); // the new binding's baseline probe comes back
    CHECK(rig.patcher.Clocks()->BoundCount() == 2);

    // The clock it used to watch reaches the target and nothing happens.
    mgr.setTempo("tp.a", kTempo, 0.f);
    rig.Ticks(8);
    CHECK(mgr.beatPosition("tp.a") == doctest::Approx(4.0));
    CHECK(rig.out.bangs == 0);

    // The one it watches now does.
    mgr.setTempo("tp.b", kTempo, 0.f);
    rig.Ticks(3); // 0.5, 1.0, 1.5
    CHECK(rig.out.bangs == 0);
    rig.Tick(); // 2.0
    CHECK(rig.out.bangs == 1);

    // A bare `clock` unbinds, so a target it would otherwise reach goes by.
    rig.Send("clock");
    rig.Target(4.f);
    rig.Ticks(10);
    CHECK(rig.out.bangs == 1);

    DropClock("tp.a");
    DropClock("tp.b");
  }

  // ─── the ownership contract inherited from #513 ─────────────────────────────

  TEST_CASE("timepoint: it never creates a clock, and starts watching when one appears (#507)") {
    // The half of #513 this object had to inherit without breaking.
    // `.transport` is the sole creator; a `.timepoint` binds a name and waits.
    // A patch that watched a name into existence would leave every object bound
    // to it sitting on a clock nobody started.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE_FALSE(mgr.clockExists("tp.never"));

    ClockRig rig("", "tp.never 1");
    CHECK_FALSE(mgr.clockExists("tp.never"));
    // Bound, though — the name is claimed in the patcher's bridge, unresolved.
    CHECK(rig.patcher.Clocks()->BoundCount() == 1);
    CHECK(std::string(rig.patcher.Clocks()->NameOf(1)) == "tp.never");
    CHECK_FALSE(rig.patcher.Clocks()->Resolved(1));

    rig.Ticks(20);
    CHECK(rig.out.bangs == 0);
    CHECK_FALSE(mgr.clockExists("tp.never"));

    // The clock appears — stopped, which is how `.transport` makes one — and the
    // object starts watching. Calculate's poll of the bridge retries an
    // unresolved binding only every RESOLVE_INTERVAL_BLOCKS blocks, so this many
    // ticks is what it takes for the retry to come round. The join after it is
    // only for determinism — the resolve job it waits on is the one Calculate
    // itself armed. Ticks alone cannot stand in for it: the lookup runs on the
    // background pool, and this loop is synchronous with no wall-clock wait in
    // it, so a fixed budget of blocks bounds nothing about when a pool thread
    // gets scheduled (issue #740). The clock is created at tempo 0, so no
    // number of ticks moves its beat position while the wait plays out.
    REQUIRE(mgr.createClock("tp.never", 0.f));
    for (std::uint64_t i = 0; i < clockBridge::RESOLVE_INTERVAL_BLOCKS + 2; i++)
      rig.Tick();
    rig.patcher.Clocks()->WaitIdle();
    REQUIRE(rig.patcher.Clocks()->Resolved(1));
    rig.Tick(); // the baseline probe comes back
    CHECK(rig.out.bangs == 0);

    mgr.setTempo("tp.never", kTempo, 0.f);
    rig.Tick(); // 0.5
    CHECK(rig.out.bangs == 0);
    rig.Tick(); // 1.0
    CHECK(rig.out.bangs == 1);

    DropClock("tp.never");
  }

  TEST_CASE("timepoint: with no name it watches nothing and binds nothing (#507)") {
    ClockRig rig("", "");
    CHECK(rig.patcher.Clocks()->BoundCount() == 0);
    rig.Target(1.f);
    rig.Send("active 1");
    rig.Ticks(10);
    CHECK(rig.out.bangs == 0);
  }

  // ─── the audio-callback route ───────────────────────────────────────────────

  TEST_CASE("timepoint: a time value delivered from inside Calculate still arms (#507)") {
    // The property a direct call structurally cannot show. A patcher message
    // handler runs on whichever thread dispatched it, and the deferred drain at
    // the top of `Calculate` dispatches *from the audio callback* — so this chain
    // (`.delay` -> `.i 6` -> `.timepoint`) re-arms the object on the audio
    // thread, where every route it takes has to be wait-free.
    auto& mgr = YSE::CLOCK::Manager();
    ClockRig rig("tp.rt 120", "tp.rt 1");

    YSE::pHandle* del = rig.patcher.CreateObject(YSE::OBJ::G_DELAY, "");
    YSE::pHandle* six = rig.patcher.CreateObject(YSE::OBJ::G_INT, "6");
    REQUIRE(del != nullptr);
    REQUIRE(six != nullptr);
    rig.patcher.Connect(del, 0, six, 0);
    rig.patcher.Connect(six, 0, rig.point, 0);

    rig.Tick();
    rig.Drive("start");
    rig.Ticks(2); // 0.5, 1.0 — the creation-argument target
    REQUIRE(rig.out.bangs == 1);

    del->SetBang(0);
    for (int i = 0; i < 24 && rig.out.bangs < 2; i++)
      rig.Tick();
    CHECK(rig.out.bangs == 2);
    // The tick that produced the second bang is the tick the clock reached 6.
    CHECK(mgr.beatPosition("tp.rt") == doctest::Approx(6.0));

    DropClock("tp.rt");
  }

  TEST_CASE("timepoint: its inlet allocates nothing (#507)") {
    // The handler may be the audio callback — there is no predicate an object
    // can ask up front — so every command word has to be matched against the
    // message in place and every number read without building a string. The
    // probe sees std::string allocations since issue #697, so this assertion is
    // not vacuous over paths that carry text.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    ClockRig rig("tp.noalloc 120", "tp.noalloc 5");
    rig.Tick();

    // Built outside the probe: it is the *handler* that must not allocate, not
    // the test's own construction of the message.
    const std::string off = "active 0";
    const std::string on = "active 1";
    const std::string clock = "clock tp.noalloc";
    const std::string unknown = "quantize 4n";
    {
      TestHelpers::ProbeScope probe;
      rig.point->SetListData(0, off);
      rig.point->SetListData(0, on);
      rig.point->SetListData(0, clock);
      rig.point->SetListData(0, unknown);
      rig.point->SetFloatData(0, 3.f);
      rig.point->SetIntData(0, 2);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    // And it really did something — an assertion that only proves nothing
    // happened proves nothing. The object is watching beat 2 now, not the 5 it
    // was created with.
    rig.Drive("start");
    rig.Ticks(3); // 0.5, 1.0, 1.5
    CHECK(rig.out.bangs == 0);
    rig.Tick(); // 2.0
    CHECK(rig.out.bangs == 1);

    DropClock("tp.noalloc");
  }

  // ─── persistence ────────────────────────────────────────────────────────────

  TEST_CASE("timepoint: parameters survive a DumpJSON / ParseJSON round trip (#507)") {
    // Checked by *driving* the restored object rather than by reading the JSON
    // back: a parameter that survived the file but not the rebuild would pass a
    // string comparison and fail here.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("tp.save", 0.f));

    YSE::patcher src;
    src.create(2);
    YSE::pHandle* obj = src.CreateObject(YSE::OBJ::G_TIMEPOINT, "tp.save 3 1");
    REQUIRE(obj != nullptr);

    const std::string json = src.DumpJSON();
    CHECK(json.find("tp.save 3 1") != std::string::npos);

    patcherImplementation restored(1, nullptr);
    restored.ParseJSON(json);
    REQUIRE(restored.Objects() == 1);
    restored.Clocks()->WaitIdle();
    // The name came back and the rebuilt object bound it — without creating it,
    // the clock this case made being the one it found.
    CHECK(restored.Clocks()->BoundCount() == 1);
    CHECK(std::string(restored.Clocks()->NameOf(1)) == "tp.save");

    YSE::pHandle* back = restored.GetHandleFromList(0);
    REQUIRE(back != nullptr);
    CHECK(back->GetParams() == "tp.save 3 1");

    Counter out;
    YSE::pHandle outHandle(&out);
    restored.Connect(back, 0, &outHandle, 0);

    Tick(restored); // the baseline probe
    mgr.setTempo("tp.save", kTempo, 0.f);
    for (int i = 0; i < 5; i++)
      Tick(restored); // 0.5 .. 2.5
    CHECK(out.bangs == 0);
    Tick(restored); // 3.0 — the restored time, on the restored clock
    CHECK(out.bangs == 1);

    DropClock("tp.save");
  }

} // TEST_SUITE
