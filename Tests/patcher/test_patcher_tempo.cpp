// Tests for `.tempo` — a counting metronome on a named domain clock (#512).
//
// The cases are organised around the four things that make this object what it
// is, rather than around its methods:
//
//   - **grammar** cases run against a standalone object, which has no patcher
//     and so no bridge and no clock at all. That is what makes them the right
//     place for everything that is not timing: the creation arguments, the
//     clamps on the note value, and the rule that a command must never be read
//     as a number.
//
//   - **counting** cases run a real `patcherImplementation` over a real
//     `CLOCK::domainClock`, because the claim is "the grid the patch counts on
//     is the clock the engine runs" and only a real clock can carry it. They
//     pin the two halves of the object: the *interval* is Max's
//     4 x multiplier / division beats, and the *number* is where in the cycle
//     the tick is — 0 to division - 1, wrapping.
//
//   - **contract** cases pin what #512 inherits from #513 and where it
//     deliberately parts from it: `.transport` creates clocks and `.tempo`
//     never does, a `.tempo` start *writes* its tempo onto the clock, and a
//     `.tempo` stop never does — a metronome switching off must not stop every
//     clip on the domain with it.
//
//   - **route** covers what a direct call structurally cannot see: a start
//     arriving *from inside `Calculate`* — the audio callback — has to reach
//     the clock through the bridge's wait-free write rather than through the
//     manager's mutex. The rig for that is a real patch (`.delay` ->
//     `.tempo`), because the only way to get a handler onto the audio thread is
//     to have the patcher put it there.
//
//   - **flow** and **persistence** drive the object the way a patch would: the
//     count into a `.sel` to make a downbeat trigger, and a DumpJSON /
//     ParseJSON round trip whose restored object is wired up and counted.
//
// Registered in the `clock` suite, not `patcher`: the cases drive
// CLOCK::Manager().update() directly on the test thread to advance beats
// deterministically, which would race a live audio thread from another suite in
// the shared monolithic process. No audio device required.

#include <doctest/doctest.h>
#include <string>
#include <vector>

#include "clock/clockManager.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/time/clockBridge.h"
#include "patcher/time/gTempo.h"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

namespace {

  using YSE::PATCHER::gTempo;
  using YSE::PATCHER::patcherImplementation;

  // 0.25 s per tick at 120 BPM is exactly half a beat, and every number in that
  // sentence is exact in binary, so the cases below assert on boundaries rather
  // than around them. The same constants the #513, #688 and #507 cases use.
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

  // Records every number this object ever sent, in order. That sequence is the
  // entire observable surface of a `.tempo`: one outlet, one kind of message,
  // and the *values* matter as much as the count of them.
  struct Recorder : YSE::PATCHER::pObject {
    std::vector<int> values;

    Recorder() : pObject(false) {
      // Reserved up front so the allocation probe below measures the *object*
      // rather than this sink's own vector growing under it.
      values.reserve(1024);
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) { values.push_back(v); });
    }
    const char* Type() const override {
      return "tempo_recorder";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A standalone `.tempo` with a recorder on its outlet. Standalone means no
  // patcher, so no bridge, no scheduler and no clock — which is what makes this
  // rig the right place for everything that is not timing.
  struct Rig {
    gTempo obj;
    Recorder out;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) obj.SetParams(args);
      obj.ConnectOutlet(out.GetInlet(0), 0);
    }

    void Int(int value, int inlet = 0) {
      obj.GetInlet(inlet)->SetInt(value, YSE::T_GUI);
    }
    void Float(float value, int inlet = 0) {
      obj.GetInlet(inlet)->SetFloat(value, YSE::T_GUI);
    }
    void Bang() {
      obj.GetInlet(0)->SetBang(YSE::T_GUI);
    }
    void List(const std::string& message) {
      obj.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
  };

  // A `.tempo` in a real patcher with a recorder on its outlet. No `.transport`
  // is built: this object writes its own tempo onto the clock when it starts,
  // which is half of what these cases are here to pin.
  struct ClockRig {
    patcherImplementation patcher{1, nullptr};
    Recorder out;
    YSE::pHandle outHandle{&out};
    YSE::pHandle* obj = nullptr;

    explicit ClockRig(const std::string& args) {
      obj = patcher.CreateObject(YSE::OBJ::G_TEMPO, args);
      REQUIRE(obj != nullptr);
      patcher.Connect(obj, 0, &outHandle, 0);
      // The object binds in SetParent; this only makes the resolve
      // *deterministic* — it cannot conjure a clock that was never created.
      patcher.Clocks()->WaitIdle();
    }

    void Start() {
      obj->SetIntData(0, 1);
    }
    void Send(const std::string& message) {
      obj->SetListData(0, message);
    }
    void Set(int inlet, int value) {
      obj->SetIntData(inlet, value);
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

  // A clock nobody is driving yet, which is the state `.transport` leaves a
  // fresh one in and the state every case here starts its own clock in.
  void MakeStoppedClock(const std::string& name) {
    REQUIRE(YSE::CLOCK::Manager().createClock(name, 0.f));
  }

} // namespace

TEST_SUITE("clock") {

  // ─── the object exists ──────────────────────────────────────────────────────

  TEST_CASE("tempo: the object is registered and creatable (#512)") {
    bool found = false;
    for (const std::string& name : YSE::PATCHER::Register().AllNames()) {
      if (name == YSE::OBJ::G_TEMPO) found = true;
    }
    CHECK(found);

    patcherImplementation patcher(1, nullptr);
    YSE::pHandle* obj = patcher.CreateObject(YSE::OBJ::G_TEMPO, "");
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == ".tempo");
    // Max's shape: four inlets — on/off, tempo, multiplier, division — and one
    // outlet carrying the count.
    CHECK(obj->GetInputs() == 4);
    CHECK(obj->GetOutputs() == 1);

    gTempo standalone;
    CHECK(standalone.GetCategory() == YSE::PATCHER::pCategory::TIME);
  }

  // ─── grammar, which needs no clock ──────────────────────────────────────────

  TEST_CASE("tempo: the creation arguments are the clock, the tempo and the note value (#512)") {
    Rig rig("tc.args 90 2 8");
    CHECK(rig.obj.WantedTempo() == doctest::Approx(90.f));
    CHECK(rig.obj.Multiplier() == 2);
    CHECK(rig.obj.Division() == 8);
    // Max's unit: 4 x multiplier / division beats.
    CHECK(rig.obj.UnitBeats() == doctest::Approx(1.0));
    // No patcher means no bridge: nothing is bound, and the metronome cannot
    // run however far a clock somewhere else has gone.
    CHECK_FALSE(rig.obj.Bound());
    CHECK(std::string(rig.obj.ClockName()).empty());
    CHECK_FALSE(rig.obj.Running());

    // Max's defaults, and a sixteenth note is a quarter of a beat.
    Rig fresh;
    CHECK(fresh.obj.WantedTempo() == doctest::Approx(120.f));
    CHECK(fresh.obj.Multiplier() == 1);
    CHECK(fresh.obj.Division() == 16);
    CHECK(fresh.obj.UnitBeats() == doctest::Approx(0.25));
  }

  TEST_CASE("tempo: the multiplier is floored and the division keeps Max's 1-96 (#512)") {
    // The two numbers that shape the grid keep Max's ranges, unlike the tempo:
    // a division of 0 would be a grid with no step to divide by, and one of
    // 1000 is not a note value Max has a name for.
    Rig rig("tc.clamp 120 1 16");

    rig.Int(0, 3);
    CHECK(rig.obj.Division() == 1);
    rig.Int(-5, 3);
    CHECK(rig.obj.Division() == 1);
    rig.Int(1000, 3);
    CHECK(rig.obj.Division() == 96);
    rig.Int(8, 3);
    CHECK(rig.obj.Division() == 8);

    rig.Int(0, 2);
    CHECK(rig.obj.Multiplier() == 1);
    rig.Int(-4, 2);
    CHECK(rig.obj.Multiplier() == 1);
    rig.Float(3.7f, 2);
    CHECK(rig.obj.Multiplier() == 3);

    // Whatever the two are, the unit is finite and positive — the invariant the
    // grid divides by on the audio thread.
    CHECK(rig.obj.UnitBeats() == doctest::Approx(4.0 * 3 / 8));
  }

  TEST_CASE("tempo: with no clock it counts nothing, and its commands are not numbers (#512)") {
    Rig rig("tc.none 120 1 4");

    // A standalone object binds nothing, so there is nothing to count on and a
    // start does not even begin: an object that emitted a 0 for a grid it does
    // not have would be lying to whatever it is wired into.
    rig.Bang();
    CHECK_FALSE(rig.obj.Running());
    rig.Int(1);
    CHECK_FALSE(rig.obj.Running());
    rig.Float(0.5f);
    CHECK_FALSE(rig.obj.Running());
    CHECK(rig.out.values.empty());

    // `tempo` is a command: it sets the tempo the object will assert and, with
    // nothing bound, writes nothing anywhere. Not clamped — Max's 5-300 range
    // goes with the tempo implementation this object does not have.
    rig.List("tempo 400");
    CHECK(rig.obj.WantedTempo() == doctest::Approx(400.f));
    rig.List("tempo -30");
    CHECK(rig.obj.WantedTempo() == doctest::Approx(-30.f));
    // A bare `tempo` is not a tempo, so it changes nothing rather than picking
    // a number.
    rig.List("tempo");
    CHECK(rig.obj.WantedTempo() == doctest::Approx(-30.f));

    // `clock` is a command too, and inert here: a standalone object has no
    // bridge to bind through. It must still not be read as a start.
    rig.List("clock elsewhere");
    CHECK_FALSE(rig.obj.Bound());
    CHECK_FALSE(rig.obj.Running());

    // Everything Max spells that this patcher cannot: quantize needs a meter, a
    // bars.beats.units figure needs one as well as an origin, and an unknown
    // word is a word. None of them may become a start or a number.
    rig.List("quantize 4n");
    rig.List("1.1.0");
    rig.List("wibble");
    CHECK_FALSE(rig.obj.Running());
    CHECK(rig.out.values.empty());
  }

  // ─── counting ───────────────────────────────────────────────────────────────

  TEST_CASE("tempo: it counts a quarter-note cycle on the clock it names (#512)") {
    // The object's whole reason for existing, end to end: a stopped clock, a
    // `.tempo` started on it, and a number out the outlet on every quarter note
    // — 0 the moment it starts, then 1, 2, 3 and round again.
    auto& mgr = YSE::CLOCK::Manager();
    MakeStoppedClock("tc.count");

    ClockRig rig("tc.count 120 1 4");
    CHECK(rig.patcher.Clocks()->BoundCount() == 1);
    rig.Ticks(4);
    // Created stopped: dropping a `.tempo` into a patch neither counts nor
    // retempos anything.
    CHECK(rig.out.values.empty());
    CHECK(mgr.currentTempo("tc.count") == doctest::Approx(0.f));

    rig.Start();
    // Max's metronomes emit the moment they are started, and for this one that
    // is the 0 the cycle begins on.
    CHECK(rig.out.values == std::vector<int>{0});

    rig.Tick(); // 0.5
    CHECK(rig.out.values == std::vector<int>{0});
    rig.Tick(); // 1.0
    CHECK(rig.out.values == std::vector<int>{0, 1});
    rig.Ticks(6); // 2.0, 3.0, 4.0
    CHECK(rig.out.values == std::vector<int>{0, 1, 2, 3, 0});
    CHECK(mgr.beatPosition("tc.count") == doctest::Approx(4.0));

    DropClock("tc.count");
  }

  TEST_CASE("tempo: the count cycles from 0 to division - 1 (#512)") {
    // Max: "cycles continuously from 0 to (rhythmic value - 1)". An eighth-note
    // division is half a beat, which at 120 BPM is exactly one tick, so this
    // reads back as one number per block for as long as it runs.
    MakeStoppedClock("tc.wrap");

    ClockRig rig("tc.wrap 120 1 8");
    rig.Start();
    rig.Ticks(10);

    CHECK(rig.out.values == std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2});

    DropClock("tc.wrap");
  }

  TEST_CASE("tempo: the multiplier and the division set the interval, keeping the phase (#512)") {
    // Max's 4 x multiplier / division, from both inlets, under a running
    // metronome. A change rebases the grid at the current beat rather than
    // jumping the phase (#625's rule for `.metro`), and the *count* carries on
    // from where it stood: a change of rate is not a change of position in the
    // cycle.
    MakeStoppedClock("tc.unit");

    ClockRig rig("tc.unit 120 1 8"); // half a beat: one tick
    rig.Start();
    rig.Ticks(2);
    CHECK(rig.out.values == std::vector<int>{0, 1, 2});

    // Multiplier 2 halves the rate to one beat, two ticks — and the count goes
    // on to 3 rather than restarting.
    rig.Set(2, 2);
    rig.Tick(); // 1.5
    CHECK(rig.out.values == std::vector<int>{0, 1, 2});
    rig.Tick(); // 2.0
    CHECK(rig.out.values == std::vector<int>{0, 1, 2, 3});

    // Division 16 against multiplier 2 is half a beat again, so one tick — and
    // the cycle is now sixteen long, which 3 already fits inside.
    rig.Set(3, 16);
    rig.Tick(); // 2.5
    CHECK(rig.out.values == std::vector<int>{0, 1, 2, 3, 4});

    DropClock("tc.unit");
  }

  TEST_CASE("tempo: it follows the clock's tempo, and holds while it is paused (#512)") {
    // The whole point of counting on a domain clock rather than in
    // milliseconds: the grid stretches with the tempo and stops with it, in
    // step with every other object and every clip on that domain.
    auto& mgr = YSE::CLOCK::Manager();
    MakeStoppedClock("tc.follow");

    ClockRig rig("tc.follow 120 1 4"); // one beat per tick pair
    rig.Start();
    rig.Ticks(2); // 1.0
    CHECK(rig.out.values == std::vector<int>{0, 1});

    // Paused: a domain clock at tempo 0 never reaches the next grid point,
    // however long the engine runs.
    mgr.setTempo("tc.follow", 0.f, 0.f);
    rig.Ticks(10);
    CHECK(rig.out.values == std::vector<int>{0, 1});
    CHECK(mgr.beatPosition("tc.follow") == doctest::Approx(1.0));

    // Half the tempo, so twice the blocks per unit — and the unit is still one
    // beat, which is what counting in beats means.
    mgr.setTempo("tc.follow", 60.f, 0.f);
    rig.Ticks(3); // 1.25, 1.5, 1.75
    CHECK(rig.out.values == std::vector<int>{0, 1});
    rig.Tick(); // 2.0
    CHECK(rig.out.values == std::vector<int>{0, 1, 2});

    DropClock("tc.follow");
  }

  // ─── the contract: what a start and a stop do to the clock ──────────────────

  TEST_CASE("tempo: starting writes its tempo onto the clock, stopping never does (#512)") {
    // The place `.tempo` deliberately parts from `.transport`. A start asserts
    // the object's BPM, which is Max's metronome exactly; a stop retires this
    // object's grid and *nothing else*, because a metronome switching off must
    // not stop every clip on the domain with it.
    auto& mgr = YSE::CLOCK::Manager();
    MakeStoppedClock("tc.drive");

    ClockRig rig("tc.drive 120 1 4");
    rig.Start();
    rig.Ticks(2); // 1.0
    CHECK(mgr.currentTempo("tc.drive") == doctest::Approx(120.f));
    CHECK(rig.out.values == std::vector<int>{0, 1});

    rig.Send("stop");
    rig.Ticks(10);
    // The count stopped...
    CHECK(rig.out.values == std::vector<int>{0, 1});
    // ...and the clock did not: it is still at 120 BPM and still moving, so
    // every clip and every other object on that domain ran on untouched.
    CHECK(mgr.currentTempo("tc.drive") == doctest::Approx(120.f));
    CHECK(mgr.beatPosition("tc.drive") == doctest::Approx(6.0));

    // A restart re-phases the grid at the beat the message landed on and puts
    // the cycle back to 0, which is Max's "starts or restarts".
    rig.Start();
    CHECK(rig.out.values == std::vector<int>{0, 1, 0});
    rig.Ticks(2); // 7.0
    CHECK(rig.out.values == std::vector<int>{0, 1, 0, 1});

    DropClock("tc.drive");
  }

  TEST_CASE("tempo: the tempo message and the tempo inlet write the clock, unclamped (#512)") {
    // "Bind to a domain clock rather than reimplementing tempo" taken to its
    // conclusion: the BPM is not this object's private state to convert into
    // milliseconds, it is the clock's, so setting it moves the whole domain —
    // rampably, and without Max's 5-300 clamp.
    auto& mgr = YSE::CLOCK::Manager();
    MakeStoppedClock("tc.bpm");

    ClockRig rig("tc.bpm 120 1 4");

    rig.Send("tempo 400"); // past Max's ceiling of 300
    rig.Tick();
    CHECK(mgr.currentTempo("tc.bpm") == doctest::Approx(400.f));

    rig.Set(1, 90); // the tempo inlet, the one a slider goes into
    rig.Tick();
    CHECK(mgr.currentTempo("tc.bpm") == doctest::Approx(90.f));

    rig.Send("tempo 0"); // 0 is how the engine spells "paused"
    rig.Tick();
    CHECK(mgr.currentTempo("tc.bpm") == doctest::Approx(0.f));
    const double held = mgr.beatPosition("tc.bpm");
    rig.Ticks(4);
    CHECK(mgr.beatPosition("tc.bpm") == doctest::Approx(held));

    // The glide Max's tempo cannot do: 120 BPM over one second, which is 120
    // BPM per second from a standstill.
    rig.Send("tempo 120 1");
    rig.Tick(); // 0.25 s in
    CHECK(mgr.currentTempo("tc.bpm") == doctest::Approx(30.f));
    rig.Ticks(3); // 1.0 s in, exactly on the target
    CHECK(mgr.currentTempo("tc.bpm") == doctest::Approx(120.f));

    // Nothing above started the metronome: setting a tempo must not.
    CHECK(rig.out.values.empty());

    DropClock("tc.bpm");
  }

  TEST_CASE("tempo: it never creates a clock, and starts counting when one appears (#512)") {
    // The half of #513 this object inherits without breaking. `.transport` is
    // the sole creator; a `.tempo` binds a name and counts on it. A metronome
    // that started a clock into existence would leave every object bound to
    // that name sitting on a clock nobody asked for — and a tempo written at a
    // name nothing has claimed is not a tempo.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE_FALSE(mgr.clockExists("tc.never"));

    ClockRig rig("tc.never 120 1 4");
    // Bound, though — the name is claimed in the patcher's bridge, unresolved.
    CHECK(rig.patcher.Clocks()->BoundCount() == 1);
    CHECK(std::string(rig.patcher.Clocks()->NameOf(1)) == "tc.never");
    CHECK_FALSE(rig.patcher.Clocks()->Resolved(1));

    rig.Start();
    // Max's metronome emits on start whatever its clock is doing, exactly as
    // `.metro` bangs on start before its clock resolves — and then waits.
    CHECK(rig.out.values == std::vector<int>{0});
    CHECK_FALSE(mgr.clockExists("tc.never"));

    rig.Ticks(20);
    CHECK(rig.out.values == std::vector<int>{0});
    CHECK_FALSE(mgr.clockExists("tc.never"));

    // The clock appears — stopped, which is how `.transport` makes one — and
    // the object starts counting. clockBridge::Poll retries an unresolved
    // binding every RESOLVE_INTERVAL_BLOCKS blocks.
    REQUIRE(mgr.createClock("tc.never", 0.f));
    for (int i = 0; i < 400 && !rig.patcher.Clocks()->Resolved(1); i++)
      rig.Tick();
    REQUIRE(rig.patcher.Clocks()->Resolved(1));
    CHECK(rig.out.values == std::vector<int>{0});

    mgr.setTempo("tc.never", kTempo, 0.f);
    // The first wakeup that finds a clock takes the baseline rather than
    // emitting, so the run begins where the clock did.
    rig.Ticks(2); // 1.0 — the baseline
    CHECK(rig.out.values == std::vector<int>{0});
    rig.Ticks(2); // 2.0 — one unit past it
    CHECK(rig.out.values == std::vector<int>{0, 1});

    DropClock("tc.never");
  }

  TEST_CASE("tempo: 'clock <name>' re-points the count and a bare 'clock' stops it (#512)") {
    // `.metro`'s and `.timepoint`'s port of Max's own `clock` message. A bare
    // `clock` stops the object counting rather than falling back to
    // milliseconds: a tempo and a note value have no meaning without a beat.
    auto& mgr = YSE::CLOCK::Manager();
    MakeStoppedClock("tc.a");
    MakeStoppedClock("tc.b");

    ClockRig rig("tc.a 120 1 4");
    rig.Start();
    rig.Ticks(2); // tc.a at 1.0
    CHECK(rig.out.values == std::vector<int>{0, 1});

    rig.Send("clock tc.b");
    rig.patcher.Clocks()->WaitIdle();
    CHECK(rig.patcher.Clocks()->BoundCount() == 2);

    // The clock it used to count on runs on and produces nothing.
    rig.Ticks(10);
    CHECK(mgr.beatPosition("tc.a") == doctest::Approx(6.0));
    CHECK(rig.out.values == std::vector<int>{0, 1});

    // The one it counts on now does — and the cycle carried on rather than
    // restarting, a change of clock not being a restart.
    mgr.setTempo("tc.b", kTempo, 0.f);
    for (int i = 0; i < 10 && rig.out.values.size() < 3; i++)
      rig.Tick();
    CHECK(rig.out.values == std::vector<int>{0, 1, 2});

    // A bare `clock` unbinds, so the grid it would otherwise reach goes by.
    rig.Send("clock");
    rig.Ticks(20);
    CHECK(rig.out.values == std::vector<int>{0, 1, 2});

    DropClock("tc.a");
    DropClock("tc.b");
  }

  TEST_CASE("tempo: with no name it counts nothing and binds nothing (#512)") {
    ClockRig rig("");
    CHECK(rig.patcher.Clocks()->BoundCount() == 0);
    rig.Start();
    rig.Send("tempo 120");
    rig.Ticks(10);
    CHECK(rig.out.values.empty());
  }

  // ─── the audio-callback route ───────────────────────────────────────────────

  TEST_CASE("tempo: a start delivered from inside Calculate still runs (#512)") {
    // The property a direct call structurally cannot show. A patcher message
    // handler runs on whichever thread dispatched it, and the deferred drain at
    // the top of `Calculate` dispatches *from the audio callback* — so this
    // chain (`.delay` -> `.tempo`) starts the metronome on the audio thread,
    // where the tempo write may not take the clock manager's mutex and has to
    // go through the bridge's wait-free pair instead.
    auto& mgr = YSE::CLOCK::Manager();
    MakeStoppedClock("tc.rt");

    ClockRig rig("tc.rt 120 1 4");
    YSE::pHandle* del = rig.patcher.CreateObject(YSE::OBJ::G_DELAY, "");
    REQUIRE(del != nullptr);
    rig.patcher.Connect(del, 0, rig.obj, 0);

    rig.Tick();
    CHECK(rig.out.values.empty());
    CHECK(mgr.currentTempo("tc.rt") == doctest::Approx(0.f));

    // The bang comes back out of the scheduler on the audio thread and starts
    // the metronome there.
    del->SetBang(0);
    for (int i = 0; i < 8 && rig.out.values.empty(); i++)
      rig.Tick();
    CHECK(rig.out.values == std::vector<int>{0});
    // The tempo written through the bridge reached the clock, which is the half
    // of the start that could not take a lock.
    rig.Tick();
    CHECK(mgr.currentTempo("tc.rt") == doctest::Approx(120.f));

    // And it goes on counting from there, on the grid the audio-thread start
    // laid down.
    for (int i = 0; i < 8 && rig.out.values.size() < 2; i++)
      rig.Tick();
    CHECK(rig.out.values == std::vector<int>{0, 1});

    DropClock("tc.rt");
  }

  TEST_CASE("tempo: its inlets allocate nothing (#512)") {
    // The handler may be the audio callback — there is no predicate an object
    // can ask up front — so every command word has to be matched against the
    // message in place and every number read without building a string. The
    // probe sees std::string allocations since issue #697, so this assertion is
    // not vacuous over paths that carry text.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    MakeStoppedClock("tc.noalloc");
    ClockRig rig("tc.noalloc 120 1 4");
    rig.Tick();

    // Built outside the probe: it is the *handler* that must not allocate, not
    // the test's own construction of the message.
    const std::string stop = "stop";
    const std::string clock = "clock tc.noalloc";
    const std::string bpm = "tempo 120";
    const std::string unknown = "quantize 4n";
    {
      TestHelpers::ProbeScope probe;
      rig.obj->SetListData(0, stop);
      rig.obj->SetListData(0, unknown);
      rig.obj->SetListData(0, bpm);
      rig.obj->SetListData(0, clock);
      rig.obj->SetIntData(2, 2);
      rig.obj->SetIntData(3, 8);
      // The start is the interesting one: it arms a wakeup *and* writes the
      // clock, and the by-name write reuses the creation argument's own string
      // rather than building the bound name into a new one.
      rig.obj->SetIntData(0, 1);
      rig.obj->SetFloatData(0, 0.f);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    // And it really did something — an assertion that only proves nothing
    // happened proves nothing. The grid is the eighth note the probed messages
    // left behind (multiplier 2, division 8 is one beat; multiplier 1 halves
    // it), not the quarter note the object was created with.
    rig.out.values.clear();
    rig.obj->SetIntData(2, 1);
    rig.Start();
    rig.Ticks(2);
    CHECK(rig.out.values == std::vector<int>{0, 1, 2});

    DropClock("tc.noalloc");
  }

  // ─── a patch that uses the count ────────────────────────────────────────────

  TEST_CASE("tempo: the count drives a '.sel' as a downbeat trigger (#512)") {
    // What the number is *for*, and the whole difference between this object
    // and a `.metro`: a bang says "now", a count says "now, and it is the first
    // beat of the bar". One `.tempo` and one `.sel` is a downbeat.
    MakeStoppedClock("tc.sel");

    patcherImplementation patcher(1, nullptr);
    TestHelpers::BangSink downbeat;
    YSE::pHandle sinkHandle(&downbeat);

    YSE::pHandle* tempo = patcher.CreateObject(YSE::OBJ::G_TEMPO, "tc.sel 120 1 4");
    YSE::pHandle* sel = patcher.CreateObject(YSE::OBJ::G_SEL, "0");
    REQUIRE(tempo != nullptr);
    REQUIRE(sel != nullptr);
    patcher.Connect(tempo, 0, sel, 0);
    patcher.Connect(sel, 0, &sinkHandle, 0);
    patcher.Clocks()->WaitIdle();

    tempo->SetIntData(0, 1);
    CHECK(downbeat.bangCount == 1); // the 0 the run starts on

    for (int i = 0; i < 6; i++) // beats 1, 2, 3
      Tick(patcher);
    CHECK(downbeat.bangCount == 1);

    for (int i = 0; i < 2; i++) // beat 4 — the cycle comes round to 0
      Tick(patcher);
    CHECK(downbeat.bangCount == 2);

    for (int i = 0; i < 8; i++) // beat 8
      Tick(patcher);
    CHECK(downbeat.bangCount == 3);

    DropClock("tc.sel");
  }

  // ─── persistence ────────────────────────────────────────────────────────────

  TEST_CASE("tempo: parameters survive a DumpJSON / ParseJSON round trip (#512)") {
    // Checked by *driving* the restored object rather than by reading the JSON
    // back: a parameter that survived the file but not the rebuild would pass a
    // string comparison and fail here.
    auto& mgr = YSE::CLOCK::Manager();
    MakeStoppedClock("tc.save");

    YSE::patcher src;
    src.create(2);
    YSE::pHandle* obj = src.CreateObject(YSE::OBJ::G_TEMPO, "tc.save 90 2 8");
    REQUIRE(obj != nullptr);

    const std::string json = src.DumpJSON();
    CHECK(json.find("tc.save 90 2 8") != std::string::npos);

    patcherImplementation restored(1, nullptr);
    restored.ParseJSON(json);
    REQUIRE(restored.Objects() == 1);
    restored.Clocks()->WaitIdle();
    // The name came back and the rebuilt object bound it — without creating it,
    // the clock this case made being the one it found.
    CHECK(restored.Clocks()->BoundCount() == 1);
    CHECK(std::string(restored.Clocks()->NameOf(1)) == "tc.save");

    YSE::pHandle* back = restored.GetHandleFromList(0);
    REQUIRE(back != nullptr);
    CHECK(back->GetParams() == "tc.save 90 2 8");

    Recorder out;
    YSE::pHandle outHandle(&out);
    restored.Connect(back, 0, &outHandle, 0);

    // The restored tempo drives the restored clock, and the restored note value
    // — 4 x 2 / 8, one beat — is the grid. 90 BPM is 0.375 beats per tick, so
    // the first unit lands on the third one.
    back->SetIntData(0, 1);
    CHECK(out.values == std::vector<int>{0});
    Tick(restored);
    CHECK(mgr.currentTempo("tc.save") == doctest::Approx(90.f));
    Tick(restored); // 0.75
    CHECK(out.values == std::vector<int>{0});
    Tick(restored); // 1.125 — past the one-beat unit
    CHECK(out.values == std::vector<int>{0, 1});

    DropClock("tc.save");
  }

} // TEST_SUITE
