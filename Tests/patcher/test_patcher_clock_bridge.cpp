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
#include "patcher/graphState.h"
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

} // TEST_SUITE("clock")
