// Tests for `.timer` — Max's timer, "report elapsed time between two events"
// (issue #506).
//
// The object makes three separable claims, and the cases are grouped by which
// one they carry:
//
//   - **grammar** cases drive a standalone object, which has no patcher and so
//     no clock at all. That is what makes them the right place for everything
//     about the *shape* of the object: which bang does what, that a report
//     before any start emits nothing, and that the right inlet measures rather
//     than stops — Max's page says "sends out the time elapsed", not "stops",
//     so a second report gives a second, larger split from the one start.
//
//   - **milliseconds** cases are the first half of #506: the interval is read
//     off the engine's monotonic clock at each of the two events. They sleep
//     real time between the bangs, which is what makes the claim falsifiable.
//
//   - **beats** cases are the second half — #506's "second outlet reporting the
//     interval in beats against a bound domain clock". They run a real
//     `patcherImplementation` with a real `.transport` driving a real
//     `CLOCK::domainClock`, and they advance that clock *instantly* on the test
//     thread. So wall-clock time barely moves while whole beats go by, which
//     pins the claim that matters: the two outlets are independent quantities
//     and the beats one is not the milliseconds one divided by a tempo. The
//     tempo-change case says the same thing a second way.
//
// Plus the usual per-object obligations: the registry entry, the #513 ownership
// contract (this object binds a clock and never creates one), an allocation
// probe over both inlets, a DumpJSON / ParseJSON round trip, and a route case
// that delivers both events from inside `Calculate` — the audio callback —
// through a real `.delay` -> `.timer` patch.
//
// Registered in the `clock` suite, not `patcher`: the cases drive
// CLOCK::Manager().update() directly on the test thread to advance beats
// deterministically, which would race a live audio thread from another suite in
// the shared monolithic process. No audio device required.

#include <doctest/doctest.h>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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
#include "patcher/time/gTimer.h"
#include "support/alloc_probe.hpp"

using namespace std::chrono_literals;

namespace {

  using TestHelpers::Wire;
  using YSE::PATCHER::clockBridge;
  using YSE::PATCHER::gTimer;
  using YSE::PATCHER::patcherImplementation;

  // 0.25 s per tick at 120 BPM is exactly half a beat, and every number in that
  // sentence is exact in binary, so the cases below assert on boundaries rather
  // than around them. The same constants the #513, #507 and #688 cases use.
  constexpr float kTickSeconds = 0.25f;
  constexpr float kTempo = 120.f;

  // One block of the whole world, in the engine's own order: advance every
  // domain clock, then let the patcher render.
  void Tick(patcherImplementation& p) {
    YSE::CLOCK::Manager().update(kTickSeconds);
    p.Calculate(YSE::T_DSP);
  }

  // Records every float this object receives together with the inlet it came
  // in on, in order. Both facts matter: the values are the measurement, and the
  // order is Max's right-to-left outlet rule.
  struct Sink : YSE::PATCHER::pObject {
    std::vector<std::pair<int, float>> events;

    Sink() : pObject(false) {
      // Reserved up front so the allocation probe measures the *object* rather
      // than this sink's own vector growing under it.
      events.reserve(256);
      for (int i = 0; i < 2; i++) {
        inputs.emplace_back(this, i == 0, i);
        inputs.back().RegisterFloat(
            [this, i](float v, int, YSE::THREAD) { events.emplace_back(i, v); });
      }
    }
    const char* Type() const override {
      return "timer_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    // Everything that arrived on one inlet, in order. Inlet 0 is wired to the
    // milliseconds outlet and inlet 1 to the beats outlet.
    std::vector<float> on(int inlet) const {
      std::vector<float> out;
      for (const auto& e : events) {
        if (e.first == inlet) out.push_back(e.second);
      }
      return out;
    }
    std::size_t count(int inlet) const {
      return on(inlet).size();
    }
  };

  // A standalone `.timer` with a sink on both outlets. Standalone means no
  // patcher, hence no clock bridge, which is what makes this rig the right
  // place for everything that is not about beats.
  struct Rig {
    gTimer obj;
    Sink out;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) obj.SetParams(args);
      Wire(obj, 0, out);
      Wire(obj, 1, out, 1);
    }

    void Start() {
      obj.GetInlet(0)->SetBang(YSE::T_GUI);
    }
    void Report() {
      obj.GetInlet(1)->SetBang(YSE::T_GUI);
    }
  };

  // A `.timer` in a real patcher, optionally with a `.transport` driving the
  // clock it measures against, and a sink on both outlets.
  struct ClockRig {
    patcherImplementation patcher{1, nullptr};
    Sink out;
    YSE::pHandle outHandle{&out};
    YSE::pHandle* transport = nullptr;
    YSE::pHandle* timer = nullptr;

    // An empty `transportArgs` builds no `.transport` at all, so the clock is
    // whatever the case made — or nothing, which is its own test.
    ClockRig(const std::string& transportArgs, const std::string& timerArgs) {
      if (!transportArgs.empty()) {
        transport = patcher.CreateObject(YSE::OBJ::G_TRANSPORT, transportArgs);
        REQUIRE(transport != nullptr);
      }
      timer = patcher.CreateObject(YSE::OBJ::G_TIMER, timerArgs);
      REQUIRE(timer != nullptr);
      patcher.Connect(timer, 0, &outHandle, 0);
      patcher.Connect(timer, 1, &outHandle, 1);
      // The object binds in SetParent; this only makes the resolve *deterministic*
      // — it cannot conjure a clock that was never created.
      patcher.Clocks()->WaitIdle();
    }

    void Drive(const std::string& message) {
      transport->SetListData(0, message);
    }
    void Start() {
      timer->SetBang(0);
    }
    void Report() {
      timer->SetBang(1);
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

  TEST_CASE("timer: the object is registered and creatable (#506)") {
    bool found = false;
    for (const std::string& name : YSE::PATCHER::Register().AllNames()) {
      if (name == YSE::OBJ::G_TIMER) found = true;
    }
    CHECK(found);

    patcherImplementation patcher(1, nullptr);
    YSE::pHandle* obj = patcher.CreateObject(YSE::OBJ::G_TIMER, "");
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == ".timer");
    // Max's shape, plus #506's second outlet: two inlets (start, report) and
    // two outlets (milliseconds, beats).
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 2);
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::FLOAT);

    gTimer standalone;
    CHECK(standalone.GetCategory() == YSE::PATCHER::pCategory::TIME);
  }

  // ─── grammar: the two events ────────────────────────────────────────────────

  TEST_CASE("timer: a report before any start emits nothing (#506)") {
    // There is no interval between one event and no event. A 0 here would hand
    // whatever this is wired into a duration no pair of events produced.
    Rig rig;
    CHECK_FALSE(rig.obj.Started());
    CHECK(rig.obj.ElapsedMs() == 0.0);

    rig.Report();
    rig.Report();
    CHECK(rig.out.events.empty());

    // And it works normally afterwards, so the silence above was the missing
    // start and not a broken object.
    rig.Start();
    CHECK(rig.obj.Started());
    rig.Report();
    CHECK(rig.out.count(0) == 1);
  }

  TEST_CASE("timer: the right inlet measures without stopping, so splits work (#506)") {
    // Max: "in right inlet: sends out the time elapsed since the timer was
    // started" — it takes a reading and leaves the mark where it was. The
    // natural "start / stop" reading of the object is subtly wrong, and this is
    // where that is pinned.
    Rig rig;

    rig.Start();
    std::this_thread::sleep_for(30ms);
    rig.Report();
    std::this_thread::sleep_for(30ms);
    rig.Report();

    const std::vector<float> ms = rig.out.on(0);
    REQUIRE(ms.size() == 2);
    // The second split is measured from the same start, not from the first
    // report: a timer that reset itself on a report would give ~30 twice.
    CHECK(ms[1] > ms[0]);
    CHECK(ms[0] >= 20.f);
    CHECK(ms[1] >= 50.f);
    CHECK(rig.obj.Started());
  }

  TEST_CASE("timer: a second start bang re-starts the interval (#506)") {
    // Max: "starts or restarts the timer."
    Rig rig;

    rig.Start();
    std::this_thread::sleep_for(40ms);
    rig.Report();
    REQUIRE(rig.out.on(0).size() == 1);
    REQUIRE(rig.out.on(0)[0] >= 30.f);

    rig.Start();
    rig.Report();
    const std::vector<float> ms = rig.out.on(0);
    REQUIRE(ms.size() == 2);
    CHECK(ms[1] < 20.f);
  }

  TEST_CASE("timer: with no clock name the beats outlet stays silent (#506)") {
    // A pure millisecond timer is the default, and the beats outlet says
    // nothing rather than 0 — a 0 would be indistinguishable from two events
    // that really did land on the same beat.
    Rig rig;
    CHECK(std::string(rig.obj.ClockName()).empty());
    CHECK_FALSE(rig.obj.Bound());

    rig.Start();
    rig.Report();
    CHECK(rig.out.count(0) == 1);
    CHECK(rig.out.count(1) == 0);
  }

  TEST_CASE("timer: a standalone object keeps its clock name but binds nothing (#506)") {
    // No patcher means no bridge. The name is remembered — it is a parameter,
    // and it has to survive a save — but nothing is bound and no beats are
    // reported, the by-name route being a bridge over resolve latency rather
    // than a way around having no binding at all.
    Rig rig("ti.standalone");
    CHECK(std::string(rig.obj.ClockName()) == "ti.standalone");
    CHECK_FALSE(rig.obj.Bound());

    REQUIRE(YSE::CLOCK::Manager().createClock("ti.standalone", kTempo));
    YSE::CLOCK::Manager().update(kTickSeconds);

    rig.Start();
    YSE::CLOCK::Manager().update(kTickSeconds);
    rig.Report();
    CHECK(rig.out.count(0) == 1);
    CHECK(rig.out.count(1) == 0);

    DropClock("ti.standalone");
  }

  // ─── milliseconds: the interval is measured ─────────────────────────────────

  TEST_CASE("timer: the interval is the measured time between the two events (#506)") {
    Rig rig;

    rig.Start();
    std::this_thread::sleep_for(40ms);
    // The accessor and the outlet answer the same question, so they are checked
    // against each other rather than only against a constant.
    const double live = rig.obj.ElapsedMs();
    rig.Report();

    const std::vector<float> ms = rig.out.on(0);
    REQUIRE(ms.size() == 1);
    CHECK(ms[0] >= 30.f);
    // Generous: a loaded CI runner can oversleep by a lot, but not by seconds.
    CHECK(ms[0] < 5000.f);
    CHECK(live >= 30.0);
    CHECK(ms[0] >= (float)live);
  }

  // ─── beats: the second outlet ───────────────────────────────────────────────

  TEST_CASE("timer: the beats outlet reports the interval on the named clock (#506)") {
    // #506's "second outlet reporting the interval in beats against a bound
    // domain clock". The clock is advanced instantly on this thread, so almost
    // no wall time passes while two whole beats go by — which is exactly what
    // makes the case falsify a beats figure derived from the milliseconds.
    auto& mgr = YSE::CLOCK::Manager();
    ClockRig rig("ti.beats 120", "ti.beats");
    rig.Tick();
    rig.Drive("start");

    rig.Start();
    rig.Ticks(4); // half a beat each: two beats
    rig.Report();

    const std::vector<float> beats = rig.out.on(1);
    const std::vector<float> ms = rig.out.on(0);
    REQUIRE(beats.size() == 1);
    REQUIRE(ms.size() == 1);
    CHECK(beats[0] == doctest::Approx(2.0).epsilon(0.001));
    CHECK(mgr.beatPosition("ti.beats") == doctest::Approx(2.0));
    // Two beats at 120 BPM is a second of *musical* time, and barely a
    // millisecond of real time went by. The outlets are two quantities, not two
    // spellings of one.
    CHECK(ms[0] < 500.f);

    DropClock("ti.beats");
  }

  TEST_CASE("timer: both outlets fire right to left, beats before milliseconds (#506)") {
    ClockRig rig("ti.order 120", "ti.order");
    rig.Tick();
    rig.Drive("start");

    rig.Start();
    rig.Ticks(2);
    rig.Report();

    REQUIRE(rig.out.events.size() == 2);
    // Max's universal outlet order, the one `.transport` and `.trigger` keep.
    CHECK(rig.out.events[0].first == 1);
    CHECK(rig.out.events[1].first == 0);

    DropClock("ti.order");
  }

  TEST_CASE("timer: a tempo change between the two events is accounted for (#506)") {
    // The claim that makes the beats outlet worth having. Half the interval runs
    // at 120 BPM and half at 240; the beats figure is the clock's own integral,
    // so it comes out at 1 + 2 = 3. Milliseconds divided by any single tempo
    // could not produce that number.
    auto& mgr = YSE::CLOCK::Manager();
    ClockRig rig("ti.ramp 120", "ti.ramp");
    rig.Tick();
    rig.Drive("start");

    rig.Start();
    rig.Ticks(2); // one beat at 120
    mgr.setTempo("ti.ramp", 240.f, 0.f);
    rig.Ticks(2); // two beats at 240
    rig.Report();

    const std::vector<float> beats = rig.out.on(1);
    REQUIRE(beats.size() == 1);
    CHECK(beats[0] == doctest::Approx(3.0).epsilon(0.001));

    DropClock("ti.ramp");
  }

  TEST_CASE("timer: a paused clock gives an interval of no beats, and time still passes (#506)") {
    // A domain clock has no run flag — tempo 0 is what "stopped" means — so a
    // measurement across a stopped clock is honestly zero beats long while the
    // milliseconds keep counting. That asymmetry is the whole reason for two
    // outlets.
    ClockRig rig("ti.paused 120", "ti.paused");
    rig.Tick();
    // Never started: `.transport` creates its clock stopped, per #513.

    rig.Start();
    rig.Ticks(6);
    std::this_thread::sleep_for(20ms);
    rig.Report();

    const std::vector<float> beats = rig.out.on(1);
    const std::vector<float> ms = rig.out.on(0);
    REQUIRE(beats.size() == 1);
    REQUIRE(ms.size() == 1);
    CHECK(beats[0] == doctest::Approx(0.0));
    CHECK(ms[0] >= 15.f);

    DropClock("ti.paused");
  }

  // ─── the ownership contract inherited from #513 ─────────────────────────────

  TEST_CASE("timer: it never creates a clock, and reports beats once one appears (#506)") {
    // `.transport` is the sole creator. A `.timer` binds a name and reads; a
    // patch that measured a name into existence would leave every object bound
    // to it sitting on a clock nobody started.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE_FALSE(mgr.clockExists("ti.never"));

    ClockRig rig("", "ti.never");
    CHECK_FALSE(mgr.clockExists("ti.never"));
    // Bound, though — the name is claimed in the patcher's bridge, unresolved.
    CHECK(rig.patcher.Clocks()->BoundCount() == 1);
    CHECK(std::string(rig.patcher.Clocks()->NameOf(1)) == "ti.never");
    CHECK_FALSE(rig.patcher.Clocks()->Resolved(1));

    // An interval measured entirely before the clock exists has milliseconds
    // and no beats: nothing was there to give it a baseline.
    rig.Start();
    rig.Ticks(4);
    rig.Report();
    CHECK(rig.out.count(0) == 1);
    CHECK(rig.out.count(1) == 0);
    CHECK_FALSE(mgr.clockExists("ti.never"));

    // The clock appears — stopped, which is how `.transport` makes one — and
    // Calculate's poll of the bridge picks the binding up. That poll is
    // rate-limited to once every RESOLVE_INTERVAL_BLOCKS blocks, so this many
    // ticks is what it takes for the retry to come round. The join after it is
    // only for determinism — the resolve job it waits on is the one Calculate
    // itself armed. Ticks alone cannot stand in for it: the lookup runs on the
    // background pool, and this loop is synchronous with no wall-clock wait in
    // it, so a fixed budget of blocks bounds nothing about when a pool thread
    // gets scheduled (issue #740).
    REQUIRE(mgr.createClock("ti.never", kTempo));
    for (std::uint64_t i = 0; i < clockBridge::RESOLVE_INTERVAL_BLOCKS + 2; i++)
      rig.Tick();
    rig.patcher.Clocks()->WaitIdle();
    REQUIRE(rig.patcher.Clocks()->Resolved(1));

    // A fresh start now has a beat to measure from, so the next interval has
    // both halves.
    rig.Start();
    rig.Ticks(4);
    rig.Report();
    const std::vector<float> beats = rig.out.on(1);
    REQUIRE(beats.size() == 1);
    CHECK(beats[0] == doctest::Approx(2.0).epsilon(0.001));

    DropClock("ti.never");
  }

  TEST_CASE("timer: a start on a bound-but-unresolved clock still gets its baseline (#506)") {
    // The reason the by-name route exists at all, in the one state that pins it
    // deterministically. The object binds `ti.late` before anything has created
    // it, so its resolve job runs and finds nothing; `clockBridge::Poll` then
    // only retries every RESOLVE_INTERVAL_BLOCKS (64) blocks, so a clock created
    // in the meantime leaves the binding unresolved for a good while. A start
    // taken in that window has no bridge to read — and must still take a
    // baseline, or the whole interval silently loses its beats half.
    //
    // Falsifiable: with the by-name route removed this case reports no beats at
    // all, while every other beats case here still passes.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE_FALSE(mgr.clockExists("ti.late"));

    ClockRig rig("", "ti.late");
    REQUIRE(rig.patcher.Clocks()->BoundCount() == 1);
    REQUIRE_FALSE(rig.patcher.Clocks()->Resolved(1));

    REQUIRE(mgr.createClock("ti.late", kTempo));
    // No block has rendered yet, so nothing has re-armed the resolve.
    REQUIRE_FALSE(rig.patcher.Clocks()->Resolved(1));

    rig.Start();
    rig.Ticks(4); // two beats, and far short of the 64 blocks Poll waits
    rig.Report();

    const std::vector<float> beats = rig.out.on(1);
    REQUIRE(beats.size() == 1);
    CHECK(beats[0] == doctest::Approx(2.0).epsilon(0.001));

    DropClock("ti.late");
  }

  // ─── the audio-callback route ───────────────────────────────────────────────

  TEST_CASE("timer: both events delivered from inside Calculate still measure (#506)") {
    // The property a direct call structurally cannot show. A patcher message
    // handler runs on whichever thread dispatched it, and the deferred drain at
    // the top of `Calculate` dispatches *from the audio callback* — so this
    // chain (`.delay` -> `.timer`) puts both events on the audio thread, where
    // the clock may only be read through the bridge's wait-free pair.
    ClockRig rig("ti.rt 120", "ti.rt");
    rig.Tick();
    rig.Drive("start");

    YSE::pHandle* startDelay = rig.patcher.CreateObject(YSE::OBJ::G_DELAY, "");
    YSE::pHandle* reportDelay = rig.patcher.CreateObject(YSE::OBJ::G_DELAY, "");
    REQUIRE(startDelay != nullptr);
    REQUIRE(reportDelay != nullptr);
    rig.patcher.Connect(startDelay, 0, rig.timer, 0);
    rig.patcher.Connect(reportDelay, 0, rig.timer, 1);

    startDelay->SetBang(0);
    rig.Tick(); // the bang comes back out of the scheduler on the audio thread
    REQUIRE(rig.timer != nullptr);
    rig.Ticks(4); // two beats

    reportDelay->SetBang(0);
    for (int i = 0; i < 8 && rig.out.events.empty(); i++)
      rig.Tick();

    const std::vector<float> beats = rig.out.on(1);
    REQUIRE(beats.size() == 1);
    // The start landed one block after its bang and the report one block after
    // its own, so the span is the four ticks between them give or take a block.
    CHECK(beats[0] >= 1.9f);
    CHECK(beats[0] <= 3.1f);
    CHECK(rig.out.count(0) == 1);

    DropClock("ti.rt");
  }

  // ─── real-time behaviour ────────────────────────────────────────────────────

  TEST_CASE("timer: its inlets allocate nothing (#506)") {
    // A patcher message handler runs on whichever thread dispatched it, and
    // there is no predicate an object can ask up front — so a whole measurement,
    // clock read and both sends included, has to be allocation-free. The probe
    // sees std::string allocations since issue #697, so this is not vacuous.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    ClockRig rig("ti.noalloc 120", "ti.noalloc");
    rig.Tick();
    rig.Drive("start");
    // Resolved before the probe: the by-name fallback is a control-thread route
    // by construction and is not what this measures.
    REQUIRE(rig.patcher.Clocks()->Resolved(1));

    {
      TestHelpers::ProbeScope probe;
      rig.Start();
      rig.Report();
      rig.Report();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    // And the probed messages really did something — an assertion that only
    // proves nothing happened proves nothing.
    CHECK(rig.out.count(0) == 2);
    CHECK(rig.out.count(1) == 2);

    DropClock("ti.noalloc");
  }

  // ─── persistence ────────────────────────────────────────────────────────────

  TEST_CASE("timer: parameters survive a DumpJSON / ParseJSON round trip (#506)") {
    // Checked by *driving* the restored object rather than by reading the JSON
    // back: a parameter that survived the file but not the rebuild would pass a
    // string comparison and fail here.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("ti.save", kTempo));

    YSE::patcher src;
    src.create(2);
    YSE::pHandle* obj = src.CreateObject(YSE::OBJ::G_TIMER, "ti.save");
    REQUIRE(obj != nullptr);

    const std::string json = src.DumpJSON();
    CHECK(json.find("ti.save") != std::string::npos);

    patcherImplementation restored(1, nullptr);
    restored.ParseJSON(json);
    REQUIRE(restored.Objects() == 1);
    restored.Clocks()->WaitIdle();
    // The name came back and the rebuilt object bound it — without creating it,
    // the clock this case made being the one it found.
    CHECK(restored.Clocks()->BoundCount() == 1);
    CHECK(std::string(restored.Clocks()->NameOf(1)) == "ti.save");

    YSE::pHandle* back = restored.GetHandleFromList(0);
    REQUIRE(back != nullptr);
    CHECK(back->GetParams() == "ti.save");

    Sink out;
    YSE::pHandle outHandle(&out);
    restored.Connect(back, 0, &outHandle, 0);
    restored.Connect(back, 1, &outHandle, 1);

    back->SetBang(0);
    for (int i = 0; i < 4; i++)
      Tick(restored);
    back->SetBang(1);

    // The restored clock name is the one that runs: two beats on ti.save.
    const std::vector<float> beats = out.on(1);
    REQUIRE(beats.size() == 1);
    CHECK(beats[0] == doctest::Approx(2.0).epsilon(0.001));
    CHECK(out.count(0) == 1);

    DropClock("ti.save");
  }

} // TEST_SUITE
