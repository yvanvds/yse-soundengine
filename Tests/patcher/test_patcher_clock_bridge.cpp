// Tests for the patcher ↔ domain-clock bridge (issue #688) and its first
// consumer, `.qlist`'s `clock <name>`.
//
// Three layers, and they are not interchangeable:
//
//   - **bridge cases** pin the binding mechanism on a standalone clockBridge:
//     that binding by name is idempotent (so every object naming one clock
//     shares one slot), that a name which does not fit is refused rather than
//     truncated, that the table is bounded, and — the load-bearing one — that a
//     name with no live clock stays unresolved and answers *false* instead of
//     answering zero. A bridge that reported beat 0 for an unknown clock would
//     make every wait on it fire at once.
//
//   - **scheduler cases** pin the one line #688 actually changes inside
//     messageScheduler: the deadline test. Same slot, same lifecycle, same
//     cancellation — a beat comparison instead of a block comparison. Driven
//     with the block clock held *still*, so a beat deadline that quietly fell
//     back to blocks cannot pass.
//
//   - **`.seq` cases** (issue #704) are the bridge's second consumer and a
//     different shape of consumer: the clock does not change what a stored
//     number *means* — the tape stays milliseconds — it supplies the **ticks**
//     of Max's `start -1`. They need the same real patcher for the same reason,
//     plus one property `.qlist` has no equivalent of: the tick count is read
//     off the clock's beat position rather than counted from the wakeups that
//     deliver it, and only a rig where a wakeup covers many ticks can tell the
//     two apart.
//
//   - **`.delay` / `.metro` cases** (issue #705) are the bridge's third and
//     fourth consumers, and the first two where `clock` is a *port* rather than
//     an addition — Max's `setclock` names both objects explicitly. They bring
//     the tempo-relative half of Max's time syntax with them (`timeValue.h`),
//     which gets its own pure-parser cases against Max's published tick table.
//     `.metro`'s cases carry the load-bearing one: a metro that drifts is
//     broken, so the rig is built so that the grid and the block boundaries do
//     *not* line up, which is the only shape that can tell "re-arm one interval
//     from here" apart from "read the count off the clock".
//
//   - **`.qlist` cases** are the user-visible end: a real patcherImplementation,
//     a real cue list, real Calculate blocks, and a real domain clock advancing
//     underneath. They are what shows that the cue numbers become beats, that a
//     tempo change on the clock bends a wait already armed — the thing
//     milliseconds cannot do and the whole reason #500 asked for this — and that
//     an object never sent a `clock` message is still Max's object.
//
// Registered in the `clock` suite rather than in `patcher`: the cases drive
// CLOCK::Manager().update() directly on the test thread to advance beats
// deterministically, which would race a live audio thread from another suite in
// the shared monolithic process. yse_tests_clock runs them isolated. No audio
// device required.

#include <doctest/doctest.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "clock/clockManager.h"
#include "headers/constants.hpp"
#include "internal/global.h"
#include "internal/threadPool.h"
#include "patcher/genericObjects/gSeq.h"
#include "patcher/graphState.h"
#include "patcher/time/gDelay.h"
#include "patcher/time/gMetro.h"
#include "patcher/time/timeValue.h"
#include "patcher/inlet.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/time/clockBridge.h"
#include "patcher/time/messageScheduler.h"
#include "support/alloc_probe.hpp"

namespace {

  using YSE::PATCHER::clockBridge;
  using YSE::PATCHER::deferredMessage;
  using YSE::PATCHER::GraphState;
  using YSE::PATCHER::messageScheduler;
  using YSE::PATCHER::patcherImplementation;

  // 0.25 s per tick at 120 BPM is exactly half a beat, and every number in that
  // sentence is exact in binary — 0.25 f, 120 BPM, and the midpoint integral
  // domainClock::update does. So "four ticks is two beats" is an equality the
  // tests can assert on a boundary rather than around one.
  constexpr float kTickSeconds = 0.25f;
  constexpr float kTempo = 120.f;

  // One block of the whole world: advance every domain clock, then let the
  // patcher render. That order is the engine's own — deviceManager ticks the
  // clock manager before the sound graph — so the drain at the top of Calculate
  // reads this block's beat, not the previous one's.
  void Tick(patcherImplementation& p) {
    YSE::CLOCK::Manager().update(kTickSeconds);
    p.Calculate(YSE::T_DSP);
  }

  // Wait until the clock manager's slow-pool delete job has run to completion.
  // The background pool is a single worker over a FIFO ring, so a job pushed
  // after the reap was enqueued cannot finish before the reap does — joining
  // this one therefore means the reap is done. Returns false if the pool was
  // not running at all, in which case a caller relying on the reap should stop
  // rather than assert on a sequence that never happened.
  struct barrierJob : YSE::INTERNAL::threadPoolJob {
    std::atomic<bool> ran{false};
    void run() override {
      ran.store(true);
    }
  };

  bool DrainSlowPool() {
    barrierJob barrier;
    YSE::INTERNAL::Global().addSlowJob(&barrier);
    barrier.join();
    return barrier.ran.load();
  }

  // Run destroyClock's full retirement sequence to completion: one tick drops
  // the clock from the audio-thread working list, the next enqueues the
  // slow-pool delete job, and the barrier waits for that job to finish.
  //
  // Drain-then-tick, repeatedly, because the manager skips the enqueue while a
  // previous delete job is still queued — and the clock manager is a
  // process-global singleton, so an earlier case in this suite may well have
  // left one in flight. Draining first makes `isQueued()` false at the moment
  // the tick wants to enqueue ours.
  bool ReapClock(const std::string& name) {
    YSE::CLOCK::Manager().destroyClock(name);
    bool pooled = true;
    for (int i = 0; i < 3; i++) {
      pooled = DrainSlowPool() && pooled;
      YSE::CLOCK::Manager().update(kTickSeconds);
    }
    return DrainSlowPool() && pooled;
  }

  // Clocks created purely to reclaim the heap a reaped clock used to occupy,
  // so a dangling read lands on live data rather than on its own stale bytes.
  constexpr int kFillerClocks = 256;

  std::string FillerName(int i) {
    return "clk.filler" + std::to_string(i);
  }

  void DropFillerClocks() {
    for (int i = 0; i < kFillerClocks; i++)
      YSE::CLOCK::Manager().destroyClock(FillerName(i));
    YSE::CLOCK::Manager().update(kTickSeconds);
  }

  // Records every value it receives, in order and with its kind.
  struct Recorder : YSE::PATCHER::pObject {
    std::vector<std::string> seen;

    Recorder() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { seen.emplace_back("!"); });
      inputs.back().RegisterInt(
          [this](int v, int, YSE::THREAD) { seen.push_back("i" + std::to_string(v)); });
      inputs.back().RegisterFloat([this](float, int, YSE::THREAD) { seen.emplace_back("f"); });
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { seen.push_back("s" + v); });
    }
    const char* Type() const override {
      return "clockbridge_recorder";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  std::string Joined(const std::vector<std::string>& seen) {
    std::string all;
    for (std::size_t i = 0; i < seen.size(); i++) {
      if (i > 0) all += ",";
      all += seen[i];
    }
    return all;
  }

  // Counts deferred deliveries. The scheduler cases only need "did it fire".
  struct DeferProbe : YSE::PATCHER::pObject {
    int hits = 0;
    DeferProbe() : pObject(false) {}
    const char* Type() const override {
      return "clockbridge_probe";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
    void DeliverDeferred(const deferredMessage&, YSE::THREAD) override {
      hits++;
    }
  };

  // A scheduler wired to a real bridge, with its block clock deliberately
  // frozen at 0: anything that fires here fired because a *beat* deadline
  // passed, since no block deadline ever can.
  struct BeatRig {
    std::atomic<std::uint64_t> block{0};
    clockBridge bridge;
    messageScheduler sched{block, &bridge};
    DeferProbe probe;
    GraphState graph;

    BeatRig() {
      graph.objects.push_back(&probe);
    }

    clockBridge::Handle BindResolved(const char* name) {
      const clockBridge::Handle h = bridge.Bind(name, std::string(name).size());
      bridge.WaitIdle();
      return h;
    }

    // One tick of the world for the bare scheduler: advance the clocks, then
    // drain. The block counter never moves.
    void Tick() {
      YSE::CLOCK::Manager().update(kTickSeconds);
      sched.DeliverDue(&graph, YSE::T_GUI);
    }
  };

  // A `.qlist` living in a real patcher, with a recorder on each outlet.
  struct QlistRig {
    patcherImplementation patcher{1, nullptr};
    Recorder data;
    Recorder end;
    YSE::pHandle dataHandle{&data};
    YSE::pHandle endHandle{&end};
    YSE::pHandle* qlist = nullptr;

    QlistRig() {
      qlist = patcher.CreateObject(YSE::OBJ::G_QLIST, "");
      REQUIRE(qlist != nullptr);
      patcher.Connect(qlist, 0, &dataHandle, 0);
      patcher.Connect(qlist, 1, &endHandle, 0);
    }

    void Send(const std::string& message) {
      qlist->SetListData(0, message);
    }
    // Bind a clock and wait for the background resolve, so a following `bang`
    // arms against a clock that is already there. WaitIdle only makes the
    // *attempt* deterministic — it cannot conjure a clock that does not exist.
    void UseClock(const std::string& name) {
      Send("clock " + name);
      patcher.Clocks()->WaitIdle();
    }
    void Tick() {
      ::Tick(patcher);
    }
  };

  // A `.seq` living in a real patcher, with a recorder on Max's byte outlet and
  // one on his end bang (issue #704).
  struct SeqRig {
    patcherImplementation patcher{1, nullptr};
    Recorder data;
    Recorder end;
    YSE::pHandle dataHandle{&data};
    YSE::pHandle endHandle{&end};
    YSE::pHandle* seq = nullptr;

    SeqRig() {
      seq = patcher.CreateObject(YSE::OBJ::G_SEQ, "");
      REQUIRE(seq != nullptr);
      patcher.Connect(seq, 0, &dataHandle, 0);
      patcher.Connect(seq, 1, &endHandle, 0);
    }

    void Send(const std::string& message) {
      seq->SetListData(0, message);
    }
    void UseClock(const std::string& name) {
      Send("clock " + name);
      patcher.Clocks()->WaitIdle();
    }

    // A one-byte tape whose single event sits `onsetMs` into the sequence. The
    // byte records at delta 0 — nothing renders between the `record` and it —
    // and Max's `delay` then writes that delta, which is "the onset time, in
    // milliseconds, of the first event in the recorded sequence".
    void RecordByte(int byte, int onsetMs) {
      Send("record");
      seq->SetIntData(0, byte);
      Send("stop");
      Send("delay " + std::to_string(onsetMs));
    }

    void Tick() {
      ::Tick(patcher);
    }
  };

  // A `.delay` living in a real patcher, with a recorder on its one outlet
  // (issue #705).
  struct DelayRig {
    patcherImplementation patcher{1, nullptr};
    Recorder out;
    YSE::pHandle outHandle{&out};
    YSE::pHandle* del = nullptr;

    DelayRig() {
      del = patcher.CreateObject(YSE::OBJ::G_DELAY, "");
      REQUIRE(del != nullptr);
      patcher.Connect(del, 0, &outHandle, 0);
    }

    void Send(const std::string& message) {
      del->SetListData(0, message);
    }
    void UseClock(const std::string& name) {
      Send("clock " + name);
      patcher.Clocks()->WaitIdle();
    }
    void Tick() {
      ::Tick(patcher);
    }
  };

  // A `.metro` living in a real patcher, with a recorder on its bang outlet
  // (issue #705). Every case here drives it on a *domain clock*, so no real
  // timerThread timer is ever started.
  struct MetroRig {
    patcherImplementation patcher{1, nullptr};
    Recorder out;
    YSE::pHandle outHandle{&out};
    YSE::pHandle* metro = nullptr;

    MetroRig() {
      metro = patcher.CreateObject(YSE::OBJ::G_METRO, "");
      REQUIRE(metro != nullptr);
      patcher.Connect(metro, 0, &outHandle, 0);
    }

    void UseClock(const std::string& name) {
      metro->SetListData(0, "clock " + name);
      patcher.Clocks()->WaitIdle();
    }
    // Max's time formats arrive on the right inlet, which is where Max
    // documents metro's list method.
    void SetInterval(const std::string& time) {
      metro->SetListData(1, time);
    }
    void Toggle(int on) {
      metro->SetIntData(0, on);
    }
    void Tick() {
      ::Tick(patcher);
    }
    std::size_t Bangs() const {
      return out.seen.size();
    }
  };

  // `8nd` is a dotted eighth: 360 of Max's 480 ticks per quarter note, so 0.75
  // of a beat — and 0.75 is exact in binary. Against the rig's half-beat tick
  // that grid deliberately does *not* line up with the block boundaries, which
  // is the whole point: a wakeup lands at the first block at or after its
  // deadline, so only a grid that overshoots can tell a metro that re-arms one
  // interval from *here* apart from one that reads its count off the clock.
  constexpr double kDottedEighthBeats = 0.75;
  // `16n` is a quarter of a beat — half the rig's tick — so two grid points
  // fall inside one block and a metro that emits one bang per wakeup caps.
  constexpr double kSixteenthBeats = 0.25;

  // 1000 ms of recorded sequence is 48 ticks (Max's rate at the original tempo)
  // and therefore two beats at 120 BPM, which is four of the rig's half-beat
  // ticks. Every number in that chain is exact in binary, so the case can assert
  // on the boundary rather than around it.
  constexpr int kOnsetMs = 1000;
  constexpr int kTicksForOnset = 48;
  constexpr int kRigTicksForOnset = 4;

} // namespace

TEST_SUITE("clock") {

  // ─── the bridge ─────────────────────────────────────────────────────────────

  TEST_CASE("clockBridge: binding by name is idempotent, and one slot per name (#688)") {
    // The property that makes CAPACITY a bound on clock *names* rather than on
    // objects: every .qlist, .seq and .del naming one clock shares a slot.
    clockBridge bridge;
    CHECK(bridge.BoundCount() == 0);

    const clockBridge::Handle first = bridge.Bind("bridge.a", 8);
    REQUIRE(first != 0);
    CHECK(bridge.Bind("bridge.a", 8) == first);
    CHECK(bridge.BoundCount() == 1);

    const clockBridge::Handle second = bridge.Bind("bridge.b", 8);
    REQUIRE(second != 0);
    CHECK(second != first);
    CHECK(bridge.BoundCount() == 2);

    CHECK(std::string(bridge.NameOf(first)) == "bridge.a");
    CHECK(std::string(bridge.NameOf(second)) == "bridge.b");
    // A prefix of a bound name is a different name, not the same one.
    CHECK(bridge.Bind("bridge.", 7) != first);
  }

  TEST_CASE("clockBridge: a name that does not fit is refused, and counted (#688)") {
    // Refused rather than truncated: half a clock name is a different clock.
    // Counted rather than logged, because the binding thread may be the audio
    // callback.
    clockBridge bridge;
    CHECK(bridge.Bind(nullptr, 4) == 0);
    CHECK(bridge.Bind("", 0) == 0);

    const std::string tooLong(clockBridge::NAME_CAPACITY, 'x');
    CHECK(bridge.Bind(tooLong.c_str(), tooLong.size()) == 0);

    CHECK(bridge.Dropped() == 3);
    CHECK(bridge.BoundCount() == 0);
    // Nothing valid was bound, so nothing reads back.
    double beat = -1.0;
    CHECK_FALSE(bridge.Beat(0, beat));
    CHECK(beat == doctest::Approx(-1.0));
    CHECK(std::string(bridge.NameOf(0)).empty());
  }

  TEST_CASE("clockBridge: the table is bounded and refuses past capacity (#688)") {
    clockBridge bridge;
    for (std::size_t i = 0; i < clockBridge::CAPACITY; i++) {
      const std::string name = "bridge.cap" + std::to_string(i);
      CHECK(bridge.Bind(name.c_str(), name.size()) != 0);
    }
    CHECK(bridge.BoundCount() == clockBridge::CAPACITY);
    CHECK(bridge.Bind("bridge.one.too.many", 19) == 0);
    CHECK(bridge.Dropped() == 1);
    // A name already bound still answers, full table or not.
    CHECK(bridge.Bind("bridge.cap0", 11) != 0);
  }

  TEST_CASE("clockBridge: an unknown clock stays unresolved and answers false (#688)") {
    // Not "beat 0". A bridge that answered 0 for a clock that does not exist
    // would make every wait armed on it come due immediately, which is the
    // opposite of what a clock that is not running means.
    clockBridge bridge;
    const clockBridge::Handle h = bridge.Bind("bridge.nosuchclock", 18);
    REQUIRE(h != 0);
    bridge.WaitIdle();

    CHECK_FALSE(bridge.Resolved(h));
    double beat = -1.0;
    CHECK_FALSE(bridge.Beat(h, beat));
    CHECK(beat == doctest::Approx(-1.0));
    CHECK(bridge.ResolveBeat(h) == doctest::Approx(0.0));
    // The name is still remembered, which is what lets Poll try again.
    CHECK(std::string(bridge.NameOf(h)) == "bridge.nosuchclock");
  }

  TEST_CASE("clockBridge: a live clock resolves and tracks its beat position (#688)") {
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("bridge.live", kTempo));

    clockBridge bridge;
    const clockBridge::Handle h = bridge.Bind("bridge.live", 11);
    REQUIRE(h != 0);
    bridge.WaitIdle();
    REQUIRE(bridge.Resolved(h));

    double beat = -1.0;
    REQUIRE(bridge.Beat(h, beat));
    CHECK(beat == doctest::Approx(mgr.beatPosition("bridge.live")));

    mgr.update(kTickSeconds);
    REQUIRE(bridge.Beat(h, beat));
    CHECK(beat == doctest::Approx(0.5));
    CHECK(beat == doctest::Approx(mgr.beatPosition("bridge.live")));

    mgr.destroyClock("bridge.live");
    mgr.update(0.01f);
  }

  TEST_CASE("clockBridge: a binding outlives destroyClock and freezes (#707)") {
    // The bug #707 reported. `destroyClock` used to retire the clock and let
    // the slow pool free it, while every resolved binding kept reading the
    // freed object through its raw pointer — on the audio thread, from the
    // deferred-message drain. A binding is never released by design, so
    // "unbind before you destroy" was a contract this side could not keep;
    // the fix is that a binding owns a share of the clock it resolved to.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("bridge.doomed", kTempo));

    clockBridge bridge;
    const clockBridge::Handle h = bridge.Bind("bridge.doomed", 13);
    REQUIRE(h != 0);
    bridge.WaitIdle();
    REQUIRE(bridge.Resolved(h));

    mgr.update(kTickSeconds);
    double beat = -1.0;
    REQUIRE(bridge.Beat(h, beat));
    CHECK(beat == doctest::Approx(0.5));

    REQUIRE(ReapClock("bridge.doomed"));
    CHECK_FALSE(mgr.clockExists("bridge.doomed"));

    // Take back the ground the old reap would have freed, before reading
    // through the binding. Every createClock allocates a node of exactly the
    // size a domainClock occupies, so a binding still pointing at freed memory
    // is overwhelmingly likely to be sitting on one of these by now — each with
    // its own tempo, and none of them anywhere near beat 0.5. Without this the
    // pre-fix read is undefined behaviour that happens to return the old value
    // on a quiet heap, and the case would pass on the very bug it exists for.
    // (The asan job catches it either way; this is what makes the case bite on
    // an ordinary build.)
    for (int i = 0; i < kFillerClocks; i++)
      REQUIRE(mgr.createClock(FillerName(i), kTempo * (float)(i + 2)));
    for (int i = 0; i < 4; i++)
      mgr.update(kTickSeconds);

    // The binding still answers, and answers the beat *its* clock held when it
    // was released — domainClock::update refuses to advance a released clock,
    // so a destroyed clock is a stopped clock rather than a dangling one.
    CHECK(bridge.Resolved(h));
    beat = -1.0;
    REQUIRE(bridge.Beat(h, beat));
    CHECK(beat == doctest::Approx(0.5));

    // A new clock of the same name is a different clock, and the binding must
    // not drift onto it either.
    REQUIRE(mgr.createClock("bridge.doomed", kTempo));
    for (int i = 0; i < 4; i++)
      mgr.update(kTickSeconds);
    CHECK(mgr.beatPosition("bridge.doomed") == doctest::Approx(2.0));
    REQUIRE(bridge.Beat(h, beat));
    CHECK(beat == doctest::Approx(0.5));

    REQUIRE(ReapClock("bridge.doomed"));
    DropFillerClocks();
  }

  TEST_CASE("clockBridge: a wait armed on a destroyed clock never comes due (#707)") {
    // The user-visible half: a clock that is destroyed under a live patch is
    // the same thing as a clock that is not running. The wait does not fire
    // early (which a bridge that reported beat 0 for a dead clock would do),
    // and it does not crash (which reading the freed clock would).
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("sched.doomed", kTempo));

    BeatRig rig;
    const clockBridge::Handle bound = rig.BindResolved("sched.doomed");
    REQUIRE(rig.bridge.Resolved(bound));

    const messageScheduler::Handle h = rig.sched.ScheduleBangOnClock(&rig.probe, 0, bound, 2.0);
    REQUIRE(h != 0);

    rig.Tick(); // half a beat in — nowhere near due
    CHECK(rig.probe.hits == 0);

    REQUIRE(ReapClock("sched.doomed"));

    // Reclaim the reaped clock's heap (see the case above) and put a same-named
    // replacement well past the deadline. Every drain from here reads the bound
    // clock: however many run, the wait stays armed on the clock it was armed
    // against rather than coming due on somebody else's beat.
    for (int i = 0; i < kFillerClocks; i++)
      REQUIRE(mgr.createClock(FillerName(i), kTempo * (float)(i + 2)));
    REQUIRE(mgr.createClock("sched.doomed", kTempo));
    for (int i = 0; i < 12; i++)
      rig.Tick();
    CHECK(rig.probe.hits == 0);
    CHECK(rig.sched.Pending(h));

    REQUIRE(ReapClock("sched.doomed"));
    DropFillerClocks();
  }

  TEST_CASE("clockBridge: Poll finds a clock created after the binding (#688)") {
    // The ordering a patch loaded before its host set up its domains produces.
    // Without the retry such a binding would be dead for the life of the
    // patcher; with it the sequence simply starts when the clock appears.
    clockBridge bridge;
    const clockBridge::Handle h = bridge.Bind("bridge.late", 11);
    REQUIRE(h != 0);
    bridge.WaitIdle();
    REQUIRE_FALSE(bridge.Resolved(h));

    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("bridge.late", kTempo));
    mgr.update(kTickSeconds); // the clock is at beat 0.5 when it is found

    bridge.Poll(0);
    bridge.WaitIdle();
    CHECK(bridge.Resolved(h));
    // The baseline a wait armed before resolution measures from: where the
    // clock stood the moment it appeared.
    CHECK(bridge.ResolveBeat(h) == doctest::Approx(0.5));

    mgr.destroyClock("bridge.late");
    mgr.update(0.01f);
  }

  // ─── the deadline test ──────────────────────────────────────────────────────

  TEST_CASE("scheduler: a beat deadline waits on the clock, not on the block counter (#688)") {
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("sched.beats", kTempo));

    BeatRig rig;
    const clockBridge::Handle bound = rig.BindResolved("sched.beats");
    REQUIRE(rig.bridge.Resolved(bound));

    const messageScheduler::Handle h = rig.sched.ScheduleBangOnClock(&rig.probe, 3, bound, 2.0);
    REQUIRE(h != 0);
    CHECK(rig.sched.Pending(h));

    // Three ticks is 1.5 beats. The block counter never moves at all, so a
    // deadline that had silently fallen back to blocks would have fired by now.
    for (int i = 0; i < 3; i++)
      rig.Tick();
    CHECK(rig.probe.hits == 0);
    CHECK(rig.sched.Pending(h));

    rig.Tick(); // 2.0 beats exactly
    CHECK(rig.probe.hits == 1);
    CHECK_FALSE(rig.sched.Pending(h));
    CHECK(rig.sched.PendingCount() == 0);

    // Once only.
    rig.Tick();
    CHECK(rig.probe.hits == 1);

    mgr.destroyClock("sched.beats");
    mgr.update(0.01f);
  }

  TEST_CASE("scheduler: a beat deadline is cancellable like any other (#688)") {
    // The point of routing this through the same slot: nothing else about the
    // pending set had to learn about clocks.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("sched.cancel", kTempo));

    BeatRig rig;
    const clockBridge::Handle bound = rig.BindResolved("sched.cancel");
    const messageScheduler::Handle h = rig.sched.ScheduleBangOnClock(&rig.probe, 0, bound, 1.0);
    REQUIRE(h != 0);
    CHECK(rig.sched.Cancel(h));
    CHECK_FALSE(rig.sched.Cancel(h)); // stale handle: nothing to do

    for (int i = 0; i < 6; i++)
      rig.Tick();
    CHECK(rig.probe.hits == 0);

    mgr.destroyClock("sched.cancel");
    mgr.update(0.01f);
  }

  TEST_CASE("scheduler: a wait on a clock that does not exist yet starts when it does (#688)") {
    // Never coming due is the honest reading of "two beats from now" on a clock
    // that is not running — the same answer a paused engine already gets. And
    // when the clock appears the wait is measured from *there*, not from a
    // baseline that was never taken.
    BeatRig rig;
    const clockBridge::Handle bound = rig.bridge.Bind("sched.later", 11);
    REQUIRE(bound != 0);
    rig.bridge.WaitIdle();
    REQUIRE_FALSE(rig.bridge.Resolved(bound));

    const messageScheduler::Handle h = rig.sched.ScheduleBangOnClock(&rig.probe, 0, bound, 1.0);
    REQUIRE(h != 0);

    // No clock, no time: drains do nothing however many run.
    for (int i = 0; i < 10; i++)
      rig.Tick();
    CHECK(rig.probe.hits == 0);
    CHECK(rig.sched.Pending(h));

    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("sched.later", kTempo));
    mgr.update(kTickSeconds); // resolve baseline will be 0.5
    rig.bridge.Poll(0);
    rig.bridge.WaitIdle();
    REQUIRE(rig.bridge.Resolved(bound));

    // One beat from the resolve beat: one more tick is only half of it.
    rig.Tick();
    CHECK(rig.probe.hits == 0);
    rig.Tick();
    CHECK(rig.probe.hits == 1);

    mgr.destroyClock("sched.later");
    mgr.update(0.01f);
  }

  TEST_CASE("scheduler: a clock arm with no bridge or no binding is refused (#688)") {
    // Refused rather than quietly demoted to the block clock, which would wait
    // out a beat count in milliseconds.
    std::atomic<std::uint64_t> block{0};
    messageScheduler bare{block}; // no bridge at all
    DeferProbe probe;
    CHECK(bare.ScheduleBangOnClock(&probe, 0, 1, 1.0) == 0);
    CHECK(bare.Dropped() == 1);

    BeatRig rig;
    CHECK(rig.sched.ScheduleBangOnClock(&rig.probe, 0, 0, 1.0) == 0);
    // A handle no one ever bound names nothing, which is a bug rather than a
    // pause.
    CHECK(rig.sched.ScheduleBangOnClock(&rig.probe, 0, (clockBridge::Handle)clockBridge::CAPACITY,
                                        1.0) == 0);
  }

  // ─── .qlist on a domain clock ───────────────────────────────────────────────

  TEST_CASE("qlist: 'clock <name>' reads a numeric cue as beats (#688)") {
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("qlist.beats", kTempo));

    QlistRig rig;
    rig.Send("set 2; 1");
    rig.UseClock("qlist.beats");
    rig.qlist->SetBang(0);

    // The first cue leaves in the arming dispatch, exactly as in millisecond
    // mode; what changed is only what its number means.
    CHECK(Joined(rig.data.seen) == "i2");
    CHECK(rig.patcher.Scheduler()->PendingCount() == 1);

    for (int i = 0; i < 3; i++)
      rig.Tick(); // 1.5 beats
    CHECK(Joined(rig.data.seen) == "i2");

    rig.Tick(); // 2.0 beats
    CHECK(Joined(rig.data.seen) == "i2,i1");
    CHECK(rig.end.seen.empty());

    for (int i = 0; i < 2; i++)
      rig.Tick(); // one more beat
    CHECK(Joined(rig.end.seen) == "!");
    CHECK(rig.patcher.Scheduler()->PendingCount() == 0);

    mgr.destroyClock("qlist.beats");
    mgr.update(0.01f);
  }

  TEST_CASE("qlist: a tempo change on the clock bends a wait already armed (#688)") {
    // The whole reason #500 asked for this, and the one thing a millisecond
    // wait cannot do: the deadline is a beat position, so speeding the domain up
    // brings it forward even though the wait was armed before the change.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("qlist.bend", kTempo));

    QlistRig rig;
    rig.Send("set 2; 1");
    rig.UseClock("qlist.bend");
    rig.qlist->SetBang(0);
    REQUIRE(Joined(rig.data.seen) == "i2");

    rig.Tick(); // 0.5 beat at 120 BPM
    CHECK(Joined(rig.data.seen) == "i2");

    // Double the domain's tempo: a tick is now a whole beat, so the 2-beat wait
    // lands on the third tick rather than on the fourth.
    mgr.setTempo("qlist.bend", kTempo * 2.f, 0.f);
    rig.Tick(); // 1.5 beats
    CHECK(Joined(rig.data.seen) == "i2");
    rig.Tick(); // 2.5 beats — past the deadline
    CHECK(Joined(rig.data.seen) == "i2,i1");

    mgr.destroyClock("qlist.bend");
    mgr.update(0.01f);
  }

  TEST_CASE("qlist: a clock at tempo zero holds the walk where it stands (#688)") {
    // domainClock's tempo is playable and unclamped, so a paused domain is a
    // paused cue list — with no code in .qlist knowing anything about it.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("qlist.paused", kTempo));

    QlistRig rig;
    rig.Send("set 1; 7");
    rig.UseClock("qlist.paused");
    rig.qlist->SetBang(0);
    REQUIRE(Joined(rig.data.seen) == "i1");

    mgr.setTempo("qlist.paused", 0.f, 0.f);
    for (int i = 0; i < 20; i++)
      rig.Tick();
    CHECK(Joined(rig.data.seen) == "i1");
    CHECK(rig.patcher.Scheduler()->PendingCount() == 1);

    // Start the domain again and the sequence carries on from where it stopped.
    mgr.setTempo("qlist.paused", kTempo, 0.f);
    for (int i = 0; i < 2; i++)
      rig.Tick();
    CHECK(Joined(rig.data.seen) == "i1,i7");

    mgr.destroyClock("qlist.paused");
    mgr.update(0.01f);
  }

  TEST_CASE("qlist: 'tempo' still divides in clock mode (#688)") {
    // Max's multiplier is a speed and keeps meaning that; the unit under it is
    // what `clock` changed. `tempo 2` therefore halves a beat wait too.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("qlist.mult", kTempo));

    QlistRig rig;
    rig.Send("set 2; 5");
    rig.Send("tempo 2");
    rig.UseClock("qlist.mult");
    rig.qlist->SetBang(0);
    REQUIRE(Joined(rig.data.seen) == "i2");

    rig.Tick(); // 0.5 beat
    CHECK(Joined(rig.data.seen) == "i2");
    rig.Tick(); // 1.0 beat — 2 beats at double speed
    CHECK(Joined(rig.data.seen) == "i2,i5");

    mgr.destroyClock("qlist.mult");
    mgr.update(0.01f);
  }

  TEST_CASE("qlist: a bare 'clock' goes back to Max's milliseconds (#688)") {
    // The opt-in has to be reversible, and an object never sent a `clock` is
    // Max's object: this is the case that keeps the port honest.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("qlist.off", kTempo));

    QlistRig rig;
    rig.Send("set 100; 3");
    rig.UseClock("qlist.off");
    rig.Send("clock");
    rig.qlist->SetBang(0);
    REQUIRE(Joined(rig.data.seen) == "i100");

    // Back on the block clock: 100 ms of Calculate blocks, and the domain clock
    // ticking underneath makes no difference at all.
    const std::uint64_t due = messageScheduler::BlocksForMillis(100);
    for (std::uint64_t block = 1; block < due; ++block)
      rig.Tick();
    CHECK(Joined(rig.data.seen) == "i100");
    rig.Tick();
    CHECK(Joined(rig.data.seen) == "i100,i3");

    mgr.destroyClock("qlist.off");
    mgr.update(0.01f);
  }

  TEST_CASE("qlist: a clock named before it exists holds, then plays (#688)") {
    // End to end through the patcher's own Poll: no test-only nudge, just
    // enough blocks for the retry interval to come round.
    QlistRig rig;
    rig.Send("set 1; 4");
    rig.UseClock("qlist.pending");
    rig.qlist->SetBang(0);
    REQUIRE(Joined(rig.data.seen) == "i1");

    for (int i = 0; i < 10; i++)
      rig.Tick();
    CHECK(Joined(rig.data.seen) == "i1");
    CHECK(rig.patcher.Scheduler()->PendingCount() == 1);

    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("qlist.pending", kTempo));
    // Calculate polls the bridge, rate-limited to once every
    // RESOLVE_INTERVAL_BLOCKS blocks, so this many ticks is what it takes for
    // the retry to come round. The join after it is only for determinism — the
    // resolve job it waits on is the one Calculate itself armed.
    for (std::uint64_t i = 0; i < clockBridge::RESOLVE_INTERVAL_BLOCKS + 2; i++)
      rig.Tick();
    rig.patcher.Clocks()->WaitIdle();
    REQUIRE(rig.patcher.Clocks()->Resolved(1));

    // Four beats is comfortably past the one the cue asked for, wherever the
    // baseline landed.
    for (int i = 0; i < 8; i++)
      rig.Tick();
    CHECK(Joined(rig.data.seen) == "i1,i4");

    mgr.destroyClock("qlist.pending");
    mgr.update(0.01f);
  }

  TEST_CASE("qlist: a 'clock' message allocates nothing (#688)") {
    // The handler may be the audio callback — there is no predicate an object
    // can ask to find out otherwise — so binding has to be a memcpy into
    // storage that already exists. The probe sees std::string allocations since
    // issue #697, so this assertion is not vacuous over a path that carries a
    // name.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("qlist.noalloc", kTempo));

    QlistRig rig;
    rig.Send("set 1; 2");
    // Built outside the probe: it is the *handler* that must not allocate, not
    // the test's own construction of the message.
    const std::string bind = "clock qlist.noalloc";
    const std::string unbind = "clock";
    {
      TestHelpers::ProbeScope probe;
      rig.qlist->SetListData(0, bind);
      rig.qlist->SetListData(0, unbind);
      rig.qlist->SetListData(0, bind);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    // And it really did bind — an assertion that only proves nothing happened
    // proves nothing.
    rig.patcher.Clocks()->WaitIdle();
    CHECK(rig.patcher.Clocks()->BoundCount() == 1);
    CHECK(std::string(rig.patcher.Clocks()->NameOf(1)) == "qlist.noalloc");

    mgr.destroyClock("qlist.noalloc");
    mgr.update(0.01f);
  }

  TEST_CASE("qlist: the clock binding is run-time state and is not saved (#688)") {
    // The same rule the cursor and the tempo follow. A reloaded patch that
    // re-bound itself to a clock the host may not have created yet would have
    // two answers to what the object is waiting on.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("qlist.save", kTempo));

    YSE::patcher src;
    src.create(2);
    YSE::pHandle* qlist = src.CreateObject(YSE::OBJ::G_QLIST);
    REQUIRE(qlist != nullptr);
    qlist->SetListData(0, "set 1; 2");
    qlist->SetListData(0, "clock qlist.save");

    const std::string json = src.DumpJSON();
    CHECK(json.find("qlist.save") == std::string::npos);

    patcherImplementation restored(1, nullptr);
    restored.ParseJSON(json);
    // The cue list came back...
    CHECK(restored.Objects() == 1);
    // ...and nothing bound a clock on the way.
    CHECK(restored.Clocks()->BoundCount() == 0);

    mgr.destroyClock("qlist.save");
    mgr.update(0.01f);
  }

  // ─── `.seq`: the domain clock supplies the ticks (issue #704) ───────────────

  TEST_CASE("seq: 'clock <name>' supplies the ticks of Max's start -1 (#704)") {
    // The acceptance criterion. Max's tick mode waits for a `tick` message per
    // 1/48 second of recorded time; bound to a domain clock it waits for 1/24
    // of a beat instead, which is the same MIDI clock at 120 BPM — so a
    // one-second onset falls due exactly two beats in.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("seq.beats", kTempo));

    SeqRig rig;
    rig.RecordByte(144, kOnsetMs);
    rig.UseClock("seq.beats");
    rig.Send("start -1");

    // Unlike Max's tick mode, which takes no scheduler slot at all, this one
    // holds exactly one: the wakeup that reads the clock.
    CHECK(rig.patcher.Scheduler()->PendingCount() == 1);
    CHECK(rig.data.seen.empty());

    for (int i = 0; i < kRigTicksForOnset - 1; i++)
      rig.Tick();
    CHECK(rig.data.seen.empty());

    // And this is the tick that would *not* arrive for another forty-odd blocks
    // if the tick count were counted from wakeups rather than read off the
    // clock: each wakeup here is worth twelve ticks.
    rig.Tick();
    CHECK(Joined(rig.data.seen) == "i144");
    CHECK(Joined(rig.end.seen) == "!");

    // The sequence ended, so the wakeup armed before the byte went out arrives
    // once more, finds nothing playing and re-arms nothing.
    rig.Tick();
    CHECK(rig.patcher.Scheduler()->PendingCount() == 0);

    mgr.destroyClock("seq.beats");
    mgr.update(0.01f);
  }

  TEST_CASE("seq: the domain's tempo is the sequence's tempo (#704)") {
    // What a patch-supplied tick source cannot give for free, and the whole
    // reason issue #502 asked for a domain clock: doubling the domain's tempo
    // doubles the tick rate, so the same recorded second arrives sooner —
    // without the sequence, the tape or the object knowing anything about it.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("seq.bend", kTempo));

    SeqRig rig;
    rig.RecordByte(144, kOnsetMs);
    rig.UseClock("seq.bend");
    rig.Send("start -1");

    rig.Tick(); // 0.5 beat at 120 BPM — 12 ticks
    CHECK(rig.data.seen.empty());

    // A tick of the rig is now a whole beat, so the two beats the onset needs
    // land on the third rig tick rather than on the fourth.
    mgr.setTempo("seq.bend", kTempo * 2.f, 0.f);
    rig.Tick(); // 1.5 beats
    CHECK(rig.data.seen.empty());
    rig.Tick(); // 2.5 beats — past the onset
    CHECK(Joined(rig.data.seen) == "i144");

    mgr.destroyClock("seq.bend");
    mgr.update(0.01f);
  }

  TEST_CASE("seq: a clock at tempo zero holds the sequence where it stands (#704)") {
    // domainClock's tempo is playable and unclamped, so a paused domain is a
    // paused sequencer — and it resumes from where it stopped rather than
    // catching up, because the tick count is a beat position and not an elapsed
    // wall time.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("seq.paused", kTempo));

    SeqRig rig;
    rig.RecordByte(144, kOnsetMs);
    rig.UseClock("seq.paused");
    rig.Send("start -1");

    mgr.setTempo("seq.paused", 0.f, 0.f);
    for (int i = 0; i < 20; i++)
      rig.Tick();
    CHECK(rig.data.seen.empty());
    CHECK(rig.patcher.Scheduler()->PendingCount() == 1);

    mgr.setTempo("seq.paused", kTempo, 0.f);
    for (int i = 0; i < kRigTicksForOnset - 1; i++)
      rig.Tick();
    CHECK(rig.data.seen.empty());
    rig.Tick();
    CHECK(Joined(rig.data.seen) == "i144");

    mgr.destroyClock("seq.paused");
    mgr.update(0.01f);
  }

  TEST_CASE("seq: a 'tick' is ignored while a clock drives, and a bare 'clock' hands it back "
            "(#704)") {
    // Two tick sources at once would be two answers to where the sequence
    // stands, and the clock's is the one that gets overwritten last. So the
    // patch's ticks are ignored — and the opt-in stays reversible, which is
    // what keeps an object never sent a `clock` message Max's object.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("seq.hand", kTempo));

    SeqRig rig;
    rig.RecordByte(144, kOnsetMs);
    rig.UseClock("seq.hand");
    rig.Send("start -1");

    // Enough ticks to run the whole onset out in Max's mode. Nothing renders
    // here, so the domain clock does not move either: whatever fires would have
    // fired because these messages advanced the sequence.
    for (int i = 0; i < kTicksForOnset; i++)
      rig.Send("tick");
    CHECK(rig.data.seen.empty());

    // Handed back: the wakeup is cancelled and the same messages now count.
    rig.Send("clock");
    CHECK(rig.patcher.Scheduler()->PendingCount() == 0);
    for (int i = 0; i < kTicksForOnset - 1; i++)
      rig.Send("tick");
    CHECK(rig.data.seen.empty());
    rig.Send("tick");
    CHECK(Joined(rig.data.seen) == "i144");

    mgr.destroyClock("seq.hand");
    mgr.update(0.01f);
  }

  TEST_CASE("seq: a bound clock leaves a millisecond 'start' alone (#704)") {
    // The departure from `.qlist`, and the reason this is a tick source rather
    // than a unit: `.seq`'s tape is milliseconds by construction, so a bound
    // clock changes nothing at all about an ordinary `start`. A domain clock
    // racing along underneath must not move it one block.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("seq.ms", kTempo));

    SeqRig rig;
    rig.RecordByte(144, 100);
    rig.UseClock("seq.ms");
    rig.Send("start");
    REQUIRE(rig.patcher.Scheduler()->PendingCount() == 1);

    const std::uint64_t due = messageScheduler::BlocksForMillis(100);
    for (std::uint64_t block = 1; block < due; ++block)
      rig.Tick();
    CHECK(rig.data.seen.empty());
    rig.Tick();
    CHECK(Joined(rig.data.seen) == "i144");

    mgr.destroyClock("seq.ms");
    mgr.update(0.01f);
  }

  TEST_CASE("seq: a clock named before it exists holds, then plays (#704)") {
    // End to end through the patcher's own Poll, with no test-only nudge. The
    // baseline is taken at the first wakeup that finds a clock, so the sequence
    // starts when the clock starts existing rather than jumping to wherever the
    // new clock's beat position happens to be.
    SeqRig rig;
    rig.RecordByte(144, kOnsetMs);
    rig.UseClock("seq.pending");
    rig.Send("start -1");
    REQUIRE(rig.patcher.Scheduler()->PendingCount() == 1);

    for (int i = 0; i < 10; i++)
      rig.Tick();
    CHECK(rig.data.seen.empty());

    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("seq.pending", kTempo));
    for (std::uint64_t i = 0; i < clockBridge::RESOLVE_INTERVAL_BLOCKS + 2; i++)
      rig.Tick();
    rig.patcher.Clocks()->WaitIdle();
    REQUIRE(rig.patcher.Clocks()->Resolved(1));

    // Two beats past wherever inside that loop the baseline landed, which this
    // many ticks covers whichever retry pass resolved it.
    for (int i = 0; i < 2 * kRigTicksForOnset; i++)
      rig.Tick();
    CHECK(Joined(rig.data.seen) == "i144");

    mgr.destroyClock("seq.pending");
    mgr.update(0.01f);
  }

  TEST_CASE("seq: a 'clock' message allocates nothing (#704)") {
    // The handler may be the audio callback — there is no predicate an object
    // can ask to find out otherwise — so binding has to be a memcpy into
    // storage that already exists. The probe sees std::string allocations since
    // issue #697, so this assertion is not vacuous over a path that carries a
    // name.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("seq.noalloc", kTempo));

    SeqRig rig;
    rig.RecordByte(144, kOnsetMs);
    // Built outside the probe: it is the *handler* that must not allocate, not
    // the test's own construction of the message. `start -1` is inside it
    // because arming the first wakeup is part of the same path.
    const std::string bind = "clock seq.noalloc";
    const std::string unbind = "clock";
    const std::string start = "start -1";
    {
      TestHelpers::ProbeScope probe;
      rig.seq->SetListData(0, bind);
      rig.seq->SetListData(0, unbind);
      rig.seq->SetListData(0, bind);
      rig.seq->SetListData(0, start);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    // And it really did bind — an assertion that only proves nothing happened
    // proves nothing.
    rig.patcher.Clocks()->WaitIdle();
    CHECK(rig.patcher.Clocks()->BoundCount() == 1);
    CHECK(std::string(rig.patcher.Clocks()->NameOf(1)) == "seq.noalloc");
    CHECK(rig.patcher.Scheduler()->PendingCount() == 1);

    mgr.destroyClock("seq.noalloc");
    mgr.update(0.01f);
  }

  TEST_CASE("seq: OnClock and ClockName report which tick source is in force (#704)") {
    // The object's own view of the binding, which is what a host asks and what
    // the cases above can only see the consequences of.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("seq.named", kTempo));

    patcherImplementation patcher(1, nullptr);
    // Declared after the patcher so it is destroyed before it. Not created
    // through the patcher, because nothing here needs to be in the graph: this
    // is the accessor pair and not a delivery.
    YSE::PATCHER::gSeq obj;
    obj.SetParent(&patcher);

    CHECK_FALSE(obj.OnClock());
    CHECK(std::string(obj.ClockName()).empty());

    obj.GetInlet(0)->SetList("clock seq.named", YSE::T_GUI);
    CHECK(obj.OnClock());
    CHECK(std::string(obj.ClockName()) == "seq.named");

    obj.GetInlet(0)->SetList("clock", YSE::T_GUI);
    CHECK_FALSE(obj.OnClock());
    CHECK(std::string(obj.ClockName()).empty());

    mgr.destroyClock("seq.named");
    mgr.update(0.01f);
  }

  // ─── Max's tempo-relative time syntax (issue #705) ──────────────────────────

  TEST_CASE("timeValue: Max's note-value table reads as the beats Max lists (#705)") {
    // Max publishes the table in ticks, and a tick is "1/480th of a quarter
    // note" — so every row is a division this can be checked against rather
    // than a number to be trusted. A quarter note is one beat on a domainClock,
    // which is the whole of the conversion.
    struct Row {
      const char* spelling;
      double ticks;
    };
    const Row table[] = {
        {"1nd", 2880}, {"1n", 1920}, {"1nt", 1280}, {"2nd", 1440}, {"2n", 960}, {"2nt", 640},
        {"4nd", 720},  {"4n", 480},  {"4nt", 320},  {"8nd", 360},  {"8n", 240}, {"8nt", 160},
        {"16nd", 180}, {"16n", 120}, {"16nt", 80},  {"32nd", 90},  {"32n", 60}, {"32nt", 40},
        {"64nd", 45},  {"64n", 30},  {"128n", 15},
    };

    for (const Row& row : table) {
      double beats = 0.0;
      const std::string text(row.spelling);
      CAPTURE(row.spelling);
      REQUIRE(YSE::PATCHER::ReadBeatTime(text.c_str(), text.size(), beats));
      CHECK(beats == doctest::Approx(row.ticks / 480.0));
    }

    // A quarter note is the beat, which is the one row worth stating exactly
    // rather than approximately.
    double beats = 0.0;
    REQUIRE(YSE::PATCHER::ReadNoteValue("4n", 2, beats));
    CHECK(beats == 1.0);
    REQUIRE(YSE::PATCHER::ReadNoteValue("8nd", 3, beats));
    CHECK(beats == 0.75);
  }

  TEST_CASE("timeValue: 'ticks' is the general beat unit (#705)") {
    // The note values cannot spell "three beats", and this is Max's own unit
    // that can — without a meter, which is why it is in and bars.beats.units is
    // out.
    double beats = 0.0;
    REQUIRE(YSE::PATCHER::ReadBeatTime("1440 ticks", 10, beats));
    CHECK(beats == 3.0);
    REQUIRE(YSE::PATCHER::ReadBeatTime("  240   ticks  ", 15, beats));
    CHECK(beats == 0.5);
    REQUIRE(YSE::PATCHER::ReadBeatTime("120 ticks", 9, beats));
    CHECK(beats == 0.25);
  }

  TEST_CASE("timeValue: a spelling Max does not have is refused, not half read (#705)") {
    // The refusals matter more than the acceptances: a bare number is
    // milliseconds on both objects, so a tick count read as a leading number
    // would silently become that many milliseconds.
    const char* refused[] = {
        "4", // a bare number is milliseconds, not a note value
        "3n", // Max's table is the powers of two, and only those
        "6nd", //
        "0n", //
        "256n", // past Max's 128n
        "4x", //
        "4nq", // neither dotted nor triplet
        "4n 5", // a time value is the whole of what it is read from
        "1440", // a tick count without its unit is milliseconds
        "1440 tick", "1440 ticks 2", "ticks", "n", "",
        "2.3.240", // bars.beats.units: needs a meter no domain clock has
    };
    for (const char* text : refused) {
      double beats = -1.0;
      const std::string token(text);
      CAPTURE(text);
      CHECK_FALSE(YSE::PATCHER::ReadBeatTime(token.c_str(), token.size(), beats));
      CHECK(beats == -1.0); // untouched on refusal
    }
  }

  // ─── .delay on a domain clock (issue #705) ──────────────────────────────────

  TEST_CASE("delay: a note value is a beat count on the bound clock (#705)") {
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("delay.beats", kTempo));

    DelayRig rig;
    rig.UseClock("delay.beats");
    // Max's left inlet "then automatically sends a bang message to itself to
    // start the delay", for a time format exactly as for a bare number.
    rig.Send("4n");
    CHECK(rig.patcher.Scheduler()->PendingCount() == 1);

    rig.Tick(); // 0.5 beat at 120 BPM
    CHECK(rig.out.seen.empty());
    rig.Tick(); // 1.0 beat — a quarter note has gone by
    CHECK(Joined(rig.out.seen) == "!");
    CHECK(rig.patcher.Scheduler()->PendingCount() == 0);

    // A dotted eighth is three quarters of that, and the object is Max's
    // one-bang-at-a-time clock, so this is a fresh wait.
    rig.Send("8nd");
    rig.Tick(); // 1.5
    CHECK(Joined(rig.out.seen) == "!");
    rig.Tick(); // 2.0 — 0.75 beat has passed since the arm at 1.0
    CHECK(Joined(rig.out.seen) == "!,!");

    mgr.destroyClock("delay.beats");
    mgr.update(0.01f);
  }

  TEST_CASE("delay: a tempo change on the clock bends a beat wait already armed (#705)") {
    // The whole reason a beat unit is worth having, and the one thing a
    // millisecond delay cannot do.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("delay.bend", kTempo));

    DelayRig rig;
    rig.UseClock("delay.bend");
    rig.Send("2n"); // two beats

    rig.Tick(); // 0.5 beat at 120 BPM
    CHECK(rig.out.seen.empty());
    // Double the domain's tempo: a tick is now a whole beat, so the two-beat
    // wait lands on the third tick rather than on the fourth.
    mgr.setTempo("delay.bend", kTempo * 2.f, 0.f);
    rig.Tick(); // 1.5 beats
    CHECK(rig.out.seen.empty());
    rig.Tick(); // 2.5 beats — past the deadline
    CHECK(Joined(rig.out.seen) == "!");

    mgr.destroyClock("delay.bend");
    mgr.update(0.01f);
  }

  TEST_CASE("delay: a beat time with no clock bound arms nothing at all (#705)") {
    // This patcher has no transport for a note value to fall back on, so there
    // is nothing to measure a beat against. The bang goes nowhere rather than
    // being re-read as milliseconds — which would turn a `4n` into a wait of
    // one millisecond, the failure this case exists to forbid.
    DelayRig rig;
    rig.Send("4n");
    CHECK(rig.patcher.Scheduler()->PendingCount() == 0);
    for (int i = 0; i < 20; i++)
      rig.Tick();
    CHECK(rig.out.seen.empty());

    // …and the two messages may arrive in either order: naming the clock
    // afterwards makes the next bang work.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("delay.late", kTempo));
    rig.UseClock("delay.late");
    rig.del->SetBang(0);
    CHECK(rig.patcher.Scheduler()->PendingCount() == 1);
    rig.Tick();
    rig.Tick(); // 1.0 beat
    CHECK(Joined(rig.out.seen) == "!");

    mgr.destroyClock("delay.late");
    mgr.update(0.01f);
  }

  TEST_CASE("delay: a bare 'clock' and a plain number both go back to milliseconds (#705)") {
    // Max's own two sentences: "the word clock by itself sets the delay object
    // back to using Max's regular millisecond clock", and "the number is stored
    // as the number of milliseconds". The unit travels with the value; `clock`
    // only decides which clock a beat time is counted on.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("delay.off", kTempo));

    DelayRig rig;
    rig.UseClock("delay.off");
    rig.Send("4n");
    REQUIRE(rig.patcher.Scheduler()->PendingCount() == 1);
    rig.Send("stop");

    // The clock goes away; the beat time stays, so there is nothing to arm on.
    rig.Send("clock");
    rig.del->SetBang(0);
    CHECK(rig.patcher.Scheduler()->PendingCount() == 0);

    // A plain number puts the unit back too, and then Max's millisecond clock
    // is running the object again — the domain clock ticking underneath makes
    // no difference at all.
    rig.del->SetIntData(0, 100);
    CHECK(rig.patcher.Scheduler()->PendingCount() == 1);
    const std::uint64_t due = messageScheduler::BlocksForMillis(100);
    for (std::uint64_t block = 1; block < due; ++block)
      rig.Tick();
    CHECK(rig.out.seen.empty());
    rig.Tick();
    CHECK(Joined(rig.out.seen) == "!");

    mgr.destroyClock("delay.off");
    mgr.update(0.01f);
  }

  TEST_CASE("delay: DelayBeats, OnClock and ClockName report the unit in force (#705)") {
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("delay.named", kTempo));

    patcherImplementation patcher(1, nullptr);
    // Declared after the patcher so it is destroyed before it; not in the graph,
    // because this is the accessor set and not a delivery.
    YSE::PATCHER::gDelay obj;
    obj.SetParent(&patcher);

    CHECK(obj.DelayBeats() == 0.0);
    CHECK(obj.DelayTime() == YSE::PATCHER::gDelay::DEFAULT_DELAY);
    CHECK_FALSE(obj.OnClock());
    CHECK(std::string(obj.ClockName()).empty());

    // The cold inlet sets without starting, for a time format as for a number.
    obj.GetInlet(1)->SetList("4nd", YSE::T_GUI);
    CHECK(obj.DelayBeats() == 1.5);
    CHECK_FALSE(obj.IsPending());

    obj.GetInlet(1)->SetList("1440 ticks", YSE::T_GUI);
    CHECK(obj.DelayBeats() == 3.0);

    // Any plain number is a statement about the unit as well as the value.
    obj.GetInlet(1)->SetInt(250, YSE::T_GUI);
    CHECK(obj.DelayBeats() == 0.0);
    CHECK(obj.DelayTime() == 250);

    obj.GetInlet(0)->SetList("clock delay.named", YSE::T_GUI);
    CHECK(obj.OnClock());
    CHECK(std::string(obj.ClockName()) == "delay.named");
    obj.GetInlet(0)->SetList("clock", YSE::T_GUI);
    CHECK_FALSE(obj.OnClock());
    CHECK(std::string(obj.ClockName()).empty());

    mgr.destroyClock("delay.named");
    mgr.update(0.01f);
  }

  TEST_CASE("delay: the beat time is the second creation argument (#705)") {
    // It is a time like the millisecond one, so it saves like one — and the
    // order matters, `delaytime` staying first so an existing one-argument
    // `.delay` still means what it always did. The binding is *not* saved, for
    // `.qlist`'s reason.
    YSE::PATCHER::gDelay obj;
    CHECK(obj.DelayTime() == YSE::PATCHER::gDelay::DEFAULT_DELAY);
    CHECK(obj.DelayBeats() == 0.0);

    obj.SetParams("40");
    CHECK(obj.DelayTime() == 40);
    CHECK(obj.DelayBeats() == 0.0);

    obj.SetParams("5 1.5");
    CHECK(obj.DelayTime() == 5);
    CHECK(obj.DelayBeats() == 1.5);
    CHECK_FALSE(obj.OnClock());
    CHECK(obj.GetParams() == "5 1.5");
  }

  TEST_CASE("delay: a 'clock' message allocates nothing (#705)") {
    // The handler may be the audio callback. The probe sees std::string
    // allocations since issue #697, so this is not vacuous over a path that
    // carries a name.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("delay.noalloc", kTempo));

    DelayRig rig;
    // Built outside the probe: it is the *handler* that must not allocate, not
    // the test's own construction of the message. The note value is inside it
    // because arming the beat wait is part of the same path.
    const std::string bind = "clock delay.noalloc";
    const std::string unbind = "clock";
    const std::string note = "8nt";
    const std::string ticks = "1440 ticks";
    {
      TestHelpers::ProbeScope probe;
      rig.del->SetListData(0, bind);
      rig.del->SetListData(0, unbind);
      rig.del->SetListData(0, bind);
      rig.del->SetListData(1, ticks);
      rig.del->SetListData(0, note);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    // And it really did bind and arm — an assertion that only proves nothing
    // happened proves nothing.
    rig.patcher.Clocks()->WaitIdle();
    CHECK(rig.patcher.Clocks()->BoundCount() == 1);
    CHECK(std::string(rig.patcher.Clocks()->NameOf(1)) == "delay.noalloc");
    CHECK(rig.patcher.Scheduler()->PendingCount() == 1);

    mgr.destroyClock("delay.noalloc");
    mgr.update(0.01f);
  }

  // ─── .metro on a domain clock (issue #705) ──────────────────────────────────

  TEST_CASE("metro: the bang count follows the clock's beat, not the wakeups (#705)") {
    // The load-bearing case of the whole issue. A dotted-eighth grid (0.75 beat)
    // against half-beat blocks never lines up, so every wakeup overshoots its
    // deadline by part of a block. A metro that re-armed "one interval from
    // here" per delivery would hand that overshoot back every time and run slow
    // — 11 bangs over this run instead of 14, one every two blocks. Reading the
    // count off the clock's beat position, and arming at the *absolute* next
    // grid point, is what makes the number below exact.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("metro.grid", kTempo));

    MetroRig rig;
    rig.UseClock("metro.grid");
    rig.SetInterval("8nd");
    rig.Toggle(1);
    // Max's metro bangs the moment it is started.
    CHECK(rig.Bangs() == 1);

    // Block by block over the first grid points, which is where the two halves
    // of the discipline show up separately. The *arm* must target the absolute
    // next grid point: 0.75 falls in block 2 and 1.5 in block 3, so a metro
    // re-arming "one interval from here" would sit out block 3 and bang every
    // second block instead. The *count* must come off the beat: it is what
    // makes the totals below right even when a wakeup arrives late.
    rig.Tick(); // beat 0.5 — nothing due yet
    CHECK(rig.Bangs() == 1);
    rig.Tick(); // beat 1.0 — past the first grid point at 0.75
    CHECK(rig.Bangs() == 2);
    rig.Tick(); // beat 1.5 — exactly the second grid point
    CHECK(rig.Bangs() == 3);

    for (int i = 0; i < 17; i++)
      rig.Tick();

    // 20 blocks is 10 beats at 120 BPM; 10 / 0.75 is 13 whole dotted eighths,
    // plus the bang at the start.
    const double beats = 20 * 0.5;
    const std::size_t expected = 1 + (std::size_t)(beats / kDottedEighthBeats);
    CHECK(expected == 14);
    CHECK(rig.Bangs() == expected);

    rig.Toggle(0);
    mgr.destroyClock("metro.grid");
    mgr.update(0.01f);
  }

  TEST_CASE("metro: an interval shorter than a block does not cap at one bang per block (#705)") {
    // The second half of the same failure, and the one that does not merely run
    // slow: a delivery is quantised to the block it lands in, so a metro that
    // emitted one bang per wakeup would top out at one bang per block however
    // fast the domain ran. A sixteenth is half a block here, so every block owes
    // two bangs.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("metro.dense", kTempo));

    MetroRig rig;
    rig.UseClock("metro.dense");
    rig.SetInterval("16n");
    rig.Toggle(1);
    REQUIRE(rig.Bangs() == 1);

    for (int i = 0; i < 8; i++)
      rig.Tick();

    // 8 blocks is 4 beats, which is 16 sixteenths, plus the start bang. A
    // one-bang-per-wakeup metro would report 9.
    const double beats = 8 * 0.5;
    const std::size_t expected = 1 + (std::size_t)(beats / kSixteenthBeats);
    CHECK(expected == 17);
    CHECK(rig.Bangs() == expected);

    rig.Toggle(0);
    mgr.destroyClock("metro.dense");
    mgr.update(0.01f);
  }

  TEST_CASE("metro: the domain's tempo is the metro's tempo (#705)") {
    // What a millisecond metro cannot do: the interval is a beat count, so
    // doubling the domain's tempo doubles the bang rate, with no code in the
    // object knowing anything about it.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("metro.tempo", kTempo));

    MetroRig rig;
    rig.UseClock("metro.tempo");
    rig.SetInterval("4n"); // one beat: one bang every two blocks
    rig.Toggle(1);
    REQUIRE(rig.Bangs() == 1);

    for (int i = 0; i < 8; i++)
      rig.Tick(); // 4 beats
    CHECK(rig.Bangs() == 5);

    mgr.setTempo("metro.tempo", kTempo * 2.f, 0.f);
    for (int i = 0; i < 8; i++)
      rig.Tick(); // 8 more beats at double tempo
    CHECK(rig.Bangs() == 13);

    rig.Toggle(0);
    mgr.destroyClock("metro.tempo");
    mgr.update(0.01f);
  }

  TEST_CASE("metro: a clock at tempo zero holds the metro where it stands (#705)") {
    // domainClock's tempo is playable and unclamped, so a paused domain is a
    // paused metronome — and it picks up where it left off rather than firing a
    // burst of everything it "missed", because the beat did not move.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("metro.paused", kTempo));

    MetroRig rig;
    rig.UseClock("metro.paused");
    rig.SetInterval("4n");
    rig.Toggle(1);
    for (int i = 0; i < 4; i++)
      rig.Tick(); // 2 beats
    REQUIRE(rig.Bangs() == 3);

    mgr.setTempo("metro.paused", 0.f, 0.f);
    for (int i = 0; i < 20; i++)
      rig.Tick();
    CHECK(rig.Bangs() == 3);

    mgr.setTempo("metro.paused", kTempo, 0.f);
    for (int i = 0; i < 2; i++)
      rig.Tick(); // one more beat
    CHECK(rig.Bangs() == 4);

    rig.Toggle(0);
    mgr.destroyClock("metro.paused");
    mgr.update(0.01f);
  }

  TEST_CASE("metro: a beat interval with no clock bound does not run (#705)") {
    // Falling back to the millisecond interval would be a metronome at a tempo
    // nobody asked for, so the object does not start at all — not even the bang
    // Max sends on start.
    MetroRig rig;
    rig.SetInterval("4n");
    rig.Toggle(1);
    CHECK(rig.Bangs() == 0);
    for (int i = 0; i < 10; i++)
      rig.Tick();
    CHECK(rig.Bangs() == 0);

    // Naming the clock and toggling again is all it takes.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("metro.late", kTempo));
    rig.UseClock("metro.late");
    rig.Toggle(1);
    CHECK(rig.Bangs() == 1);
    for (int i = 0; i < 4; i++)
      rig.Tick(); // 2 beats
    CHECK(rig.Bangs() == 3);

    rig.Toggle(0);
    mgr.destroyClock("metro.late");
    mgr.update(0.01f);
  }

  TEST_CASE("metro: a clock named before it exists holds, then runs (#705)") {
    // End to end through the bridge's own Poll: no test-only nudge, just enough
    // blocks for the retry interval to come round. The run begins when the clock
    // starts existing, not one interval after it.
    MetroRig rig;
    rig.UseClock("metro.notyet");
    rig.SetInterval("4n");
    rig.Toggle(1);
    // Max's start bang goes out regardless — nothing about it needs a clock.
    CHECK(rig.Bangs() == 1);

    for (int i = 0; i < 4; i++)
      rig.Tick();
    CHECK(rig.Bangs() == 1);

    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("metro.notyet", kTempo));
    // Poll retries every RESOLVE_INTERVAL_BLOCKS blocks; a comfortable margin
    // over it, then two more beats' worth.
    for (std::uint64_t i = 0; i < clockBridge::RESOLVE_INTERVAL_BLOCKS + 8; i++)
      rig.Tick();
    CHECK(rig.Bangs() > 1);

    rig.Toggle(0);
    mgr.destroyClock("metro.notyet");
    mgr.update(0.01f);
  }

  TEST_CASE("metro: a beat interval change rebases the grid instead of re-triggering (#705)") {
    // Issue #625's rule for the millisecond path — "rescheduling keeps the
    // phase, so a tempo tweak does not re-trigger whatever the bang drives" —
    // on the beat clock. Measuring the new grid from *here* is what that means.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("metro.retime", kTempo));

    MetroRig rig;
    rig.UseClock("metro.retime");
    rig.SetInterval("2n"); // two beats: one bang every four blocks
    rig.Toggle(1);
    REQUIRE(rig.Bangs() == 1);

    for (int i = 0; i < 4; i++)
      rig.Tick(); // 2 beats
    CHECK(rig.Bangs() == 2);

    // Halve the interval mid-run. The retime itself must not bang.
    rig.SetInterval("4n");
    CHECK(rig.Bangs() == 2);
    for (int i = 0; i < 4; i++)
      rig.Tick(); // 2 more beats, now one bang per beat
    CHECK(rig.Bangs() == 4);

    rig.Toggle(0);
    mgr.destroyClock("metro.retime");
    mgr.update(0.01f);
  }

  TEST_CASE("metro: PeriodBeats, OnClock and ClockName report the unit in force (#705)") {
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("metro.named", kTempo));

    patcherImplementation patcher(1, nullptr);
    // Declared after the patcher so it is destroyed before it.
    YSE::PATCHER::gMetro obj;
    obj.SetParent(&patcher);

    CHECK(obj.PeriodBeats() == 0.0);
    CHECK_FALSE(obj.OnClock());
    CHECK_FALSE(obj.RunningOnClock());
    CHECK(std::string(obj.ClockName()).empty());

    obj.GetInlet(1)->SetList("8nd", YSE::T_GUI);
    CHECK(obj.PeriodBeats() == 0.75);
    obj.GetInlet(1)->SetList("1440 ticks", YSE::T_GUI);
    CHECK(obj.PeriodBeats() == 3.0);

    // Max: "the number is the time interval, in milliseconds" — a statement
    // about the unit as much as about the value.
    obj.GetInlet(1)->SetInt(250, YSE::T_GUI);
    CHECK(obj.PeriodBeats() == 0.0);

    obj.GetInlet(0)->SetList("clock metro.named", YSE::T_GUI);
    CHECK(obj.OnClock());
    CHECK(std::string(obj.ClockName()) == "metro.named");
    // Still not *running* on it: the unit of a run is decided at the toggle,
    // and this object's interval is milliseconds again.
    CHECK_FALSE(obj.RunningOnClock());

    obj.GetInlet(0)->SetList("clock", YSE::T_GUI);
    CHECK_FALSE(obj.OnClock());
    CHECK(std::string(obj.ClockName()).empty());

    mgr.destroyClock("metro.named");
    mgr.update(0.01f);
  }

  TEST_CASE("metro: a 'clock' message and a beat wakeup allocate nothing (#705)") {
    // Binding may run on the audio callback, and the wakeup *is* the audio
    // callback. The probe sees std::string and array allocations since #697.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("metro.noalloc", kTempo));

    MetroRig rig;
    const std::string bind = "clock metro.noalloc";
    const std::string unbind = "clock";
    const std::string note = "16n";
    {
      TestHelpers::ProbeScope probe;
      rig.metro->SetListData(0, bind);
      rig.metro->SetListData(0, unbind);
      rig.metro->SetListData(0, bind);
      rig.metro->SetListData(1, note);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    rig.patcher.Clocks()->WaitIdle();
    REQUIRE(rig.patcher.Clocks()->BoundCount() == 1);

    rig.Toggle(1);
    REQUIRE(rig.Bangs() == 1);
    // The recorder's own vector reallocates as it grows, which is the *test*
    // allocating and not the object; give it room up front so the probe is
    // measuring the delivery path.
    rig.out.seen.reserve(64);
    // Two bangs per block on this grid, so the delivery path really is
    // exercised rather than skipped over. The clock manager's own tick is
    // outside the probe: what is being measured is the patcher's block, which
    // is where the wakeup is delivered.
    for (int i = 0; i < 4; i++) {
      YSE::CLOCK::Manager().update(kTickSeconds);
      TestHelpers::ProbeScope probe;
      rig.patcher.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(rig.Bangs() == 9);

    rig.Toggle(0);
    mgr.destroyClock("metro.noalloc");
    mgr.update(0.01f);
  }

} // TEST_SUITE("clock")
