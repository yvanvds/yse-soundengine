// Tests for the patcher's deferred-message scheduler (issue #628) and its
// first user, `.bondo`'s delay argument.
//
// The scheduler is the infrastructure #628 asks for: a fixed-capacity,
// wait-free pending set owned by the patcher, armed from a message handler on
// any thread, delivered inside the patcher's own dispatch (the top of
// Calculate) wrapped in a real event frame so a deferred send carries a proper
// logical-event id. Two layers here, deliberately:
//
//   - **direct scheduler cases** drive a standalone messageScheduler against a
//     hand-built GraphState and a probe object, pinning the mechanism's own
//     contract: the one-block floor, arm-order delivery, cancellation and
//     handle staleness, the capacity/refusal bound, payload kinds, and the
//     target re-resolution that makes a deleted object's pending message a
//     no-op instead of a use-after-free.
//
//   - **patcher-level `.bondo` cases** exercise the user-visible behaviour end
//     to end through patcherImplementation — real objects created through the
//     registry, real Calculate blocks as the clock — including the load-bearing
//     event guarantee: a *deferred* release still reads as one logical event to
//     a real downstream `.next`, which is exactly what a timer-thread deferral
//     would have broken (the reason #628 exists).
//
// Deadlines are asserted through messageScheduler::BlocksForMillis at the live
// SAMPLERATE rather than hard-coded block counts, so the suite is valid at any
// negotiated rate.
//
// No audio device required.

#include <doctest/doctest.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include "headers/constants.hpp"
#include "patcher/graphState.h"
#include "patcher/inlet.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/time/messageScheduler.h"
#include "sinks.hpp"

namespace {

  using TestHelpers::OrderSink;
  using YSE::PATCHER::CurrentMessageEvent;
  using YSE::PATCHER::DEFERRED_KIND;
  using YSE::PATCHER::deferredMessage;
  using YSE::PATCHER::GraphState;
  using YSE::PATCHER::messageScheduler;
  using YSE::PATCHER::patcherImplementation;

  // Records every deferred delivery it receives, including the logical-event
  // id observed *during* the callback — the property the scheduler exists to
  // provide.
  struct DeferProbe : YSE::PATCHER::pObject {
    struct Hit {
      int tag;
      DEFERRED_KIND kind;
      int intValue;
      float floatValue;
      std::string text;
      std::uint64_t event;
    };
    std::vector<Hit> hits;

    DeferProbe() : pObject(false) {}
    const char* Type() const override {
      return "defer_probe";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
    void DeliverDeferred(const deferredMessage& msg, YSE::THREAD) override {
      hits.push_back(
          {msg.tag, msg.kind, msg.intValue, msg.floatValue, msg.text, CurrentMessageEvent()});
    }
  };

  // A scheduler with its own hand-cranked block clock and a one-object graph —
  // the minimal honest rig for the mechanism itself.
  struct SchedRig {
    std::atomic<std::uint64_t> clock{0};
    messageScheduler sched{clock};
    DeferProbe probe;
    GraphState graph;

    SchedRig() {
      graph.objects.push_back(&probe);
    }

    void DrainAt(std::uint64_t block) {
      clock.store(block, std::memory_order_release);
      sched.DeliverDue(&graph, YSE::T_GUI);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── the mechanism ──────────────────────────────────────────────────────────

  TEST_CASE("scheduler: a message waits for its deadline, then delivers exactly once (#628)") {
    SchedRig rig;
    const std::uint64_t due = messageScheduler::BlocksForMillis(10);
    REQUIRE(due >= 1);

    messageScheduler::Handle h = rig.sched.ScheduleInt(&rig.probe, 7, 10, 42);
    REQUIRE(h != 0);
    CHECK(rig.sched.Pending(h));
    CHECK(rig.sched.PendingCount() == 1);

    // Not due yet: nothing may deliver, however often the drain runs.
    rig.DrainAt(0);
    rig.DrainAt(due - 1);
    CHECK(rig.probe.hits.empty());
    CHECK(rig.sched.Pending(h));

    rig.DrainAt(due);
    REQUIRE(rig.probe.hits.size() == 1);
    CHECK(rig.probe.hits[0].tag == 7);
    CHECK(rig.probe.hits[0].kind == DEFERRED_KIND::INT);
    CHECK(rig.probe.hits[0].intValue == 42);
    CHECK_FALSE(rig.sched.Pending(h));
    CHECK(rig.sched.PendingCount() == 0);

    // Once only: later drains find nothing.
    rig.DrainAt(due + 5);
    CHECK(rig.probe.hits.size() == 1);
  }

  TEST_CASE("scheduler: the deadline floor is one block — never the arming dispatch (#628)") {
    // "Later, as its own event" is the semantic; a zero (or negative) delay
    // still defers to the next block rather than delivering synchronously.
    SchedRig rig;
    CHECK(messageScheduler::BlocksForMillis(0) == 1);
    CHECK(messageScheduler::BlocksForMillis(-5) == 1);

    messageScheduler::Handle h = rig.sched.ScheduleBang(&rig.probe, 0, 0);
    REQUIRE(h != 0);
    rig.DrainAt(0); // the block the arm happened in
    CHECK(rig.probe.hits.empty());
    rig.DrainAt(1);
    CHECK(rig.probe.hits.size() == 1);
  }

  TEST_CASE("scheduler: BlocksForMillis is ceil at the live SAMPLERATE (#628)") {
    // The conversion the deadlines rest on: ceil(ms * SAMPLERATE / 1000 /
    // STANDARD_BUFFERSIZE), floored at one block. Asserted against the live
    // rate so the test is valid at any negotiated SAMPLERATE.
    const std::uint64_t samples =
        (std::uint64_t)100 * (std::uint64_t)YSE::SAMPLERATE / (std::uint64_t)1000;
    const std::uint64_t expected =
        (samples + YSE::STANDARD_BUFFERSIZE - 1) / YSE::STANDARD_BUFFERSIZE;
    CHECK(messageScheduler::BlocksForMillis(100) == expected);
    if (YSE::SAMPLERATE == 48000) {
      // The default rate, spelled out: 100 ms = 4800 samples = ceil(37.5) = 38
      // blocks of 128.
      CHECK(messageScheduler::BlocksForMillis(100) == 38);
    }
  }

  TEST_CASE("scheduler: every delivery is its own fresh logical event (#628)") {
    // The dispatch-frame half of #628: a deferred delivery must carry a real
    // event id — nonzero, and distinct per delivered message, because two
    // deferred messages are two stimuli exactly as two scheduler ticks are in
    // Max. (That one *cascade* shares its id is pinned through .bondo + .next
    // below, where a real multi-send fan-out exists to observe it.)
    SchedRig rig;
    REQUIRE(rig.sched.ScheduleBang(&rig.probe, 1, 0) != 0);
    REQUIRE(rig.sched.ScheduleBang(&rig.probe, 2, 0) != 0);
    rig.DrainAt(1);
    REQUIRE(rig.probe.hits.size() == 2);
    CHECK(rig.probe.hits[0].event != 0);
    CHECK(rig.probe.hits[1].event != 0);
    CHECK(rig.probe.hits[0].event != rig.probe.hits[1].event);
    // And the frame closed properly: no event leaks out of the drain.
    CHECK(CurrentMessageEvent() == 0);
  }

  TEST_CASE("scheduler: messages due in the same block deliver in arm order (#628)") {
    SchedRig rig;
    REQUIRE(rig.sched.ScheduleInt(&rig.probe, 1, 5, 10) != 0);
    REQUIRE(rig.sched.ScheduleInt(&rig.probe, 2, 5, 20) != 0);
    REQUIRE(rig.sched.ScheduleInt(&rig.probe, 3, 5, 30) != 0);
    rig.DrainAt(messageScheduler::BlocksForMillis(5));
    REQUIRE(rig.probe.hits.size() == 3);
    CHECK(rig.probe.hits[0].tag == 1);
    CHECK(rig.probe.hits[1].tag == 2);
    CHECK(rig.probe.hits[2].tag == 3);
  }

  TEST_CASE("scheduler: all four payload kinds arrive intact (#628)") {
    SchedRig rig;
    const std::string text = "sym 1 2.5";
    REQUIRE(rig.sched.ScheduleBang(&rig.probe, 1, 0) != 0);
    REQUIRE(rig.sched.ScheduleInt(&rig.probe, 2, 0, -3) != 0);
    REQUIRE(rig.sched.ScheduleFloat(&rig.probe, 3, 0, 0.25f) != 0);
    REQUIRE(rig.sched.ScheduleList(&rig.probe, 4, 0, text.c_str(), text.size()) != 0);
    rig.DrainAt(1);
    REQUIRE(rig.probe.hits.size() == 4);
    CHECK(rig.probe.hits[0].kind == DEFERRED_KIND::BANG);
    CHECK(rig.probe.hits[1].intValue == -3);
    CHECK(rig.probe.hits[2].floatValue == doctest::Approx(0.25f));
    CHECK(rig.probe.hits[3].kind == DEFERRED_KIND::LIST);
    CHECK(rig.probe.hits[3].text == text);
  }

  TEST_CASE("scheduler: cancel unarm's a pending message; stale handles are no-ops (#628)") {
    SchedRig rig;
    messageScheduler::Handle h = rig.sched.ScheduleBang(&rig.probe, 1, 10);
    REQUIRE(h != 0);
    CHECK(rig.sched.Cancel(h));
    CHECK_FALSE(rig.sched.Pending(h));
    CHECK_FALSE(rig.sched.Cancel(h)); // already cancelled

    rig.DrainAt(messageScheduler::BlocksForMillis(10) + 1);
    CHECK(rig.probe.hits.empty());

    // A handle whose message was delivered is stale too...
    messageScheduler::Handle h2 = rig.sched.ScheduleBang(&rig.probe, 2, 0);
    REQUIRE(h2 != 0);
    rig.DrainAt(rig.clock.load() + 1);
    REQUIRE(rig.probe.hits.size() == 1);
    CHECK_FALSE(rig.sched.Cancel(h2));

    // ...and a reused slot's generation makes the old handle inert: cancelling
    // h/h2 again must not touch h3's pending message.
    messageScheduler::Handle h3 = rig.sched.ScheduleBang(&rig.probe, 3, 10);
    REQUIRE(h3 != 0);
    CHECK_FALSE(rig.sched.Cancel(h));
    CHECK_FALSE(rig.sched.Cancel(h2));
    CHECK(rig.sched.Pending(h3));
    CHECK(rig.sched.Cancel(h3));
    CHECK(rig.sched.Cancel(0) == false);
  }

  TEST_CASE("scheduler: the pending set is bounded — a full table refuses, then recovers (#628)") {
    SchedRig rig;
    std::vector<messageScheduler::Handle> handles;
    for (std::size_t i = 0; i < messageScheduler::CAPACITY; ++i) {
      messageScheduler::Handle h = rig.sched.ScheduleBang(&rig.probe, (int)i, 10);
      REQUIRE(h != 0);
      handles.push_back(h);
    }
    CHECK(rig.sched.PendingCount() == messageScheduler::CAPACITY);

    const std::uint64_t droppedBefore = rig.sched.Dropped();
    CHECK(rig.sched.ScheduleBang(&rig.probe, 999, 10) == 0);
    CHECK(rig.sched.Dropped() == droppedBefore + 1);

    // Cancelling one frees exactly one slot.
    CHECK(rig.sched.Cancel(handles[0]));
    CHECK(rig.sched.ScheduleBang(&rig.probe, 1000, 10) != 0);

    // Delivery drains the rest; the table is fully reusable afterwards.
    rig.DrainAt(messageScheduler::BlocksForMillis(10));
    CHECK(rig.sched.PendingCount() == 0);
    CHECK(rig.sched.ScheduleBang(&rig.probe, 1001, 10) != 0);
  }

  TEST_CASE("scheduler: over-long list text refuses the arm instead of truncating (#628)") {
    SchedRig rig;
    const std::string tooLong(messageScheduler::TEXT_CAPACITY, 'x');
    const std::uint64_t droppedBefore = rig.sched.Dropped();
    CHECK(rig.sched.ScheduleList(&rig.probe, 1, 0, tooLong.c_str(), tooLong.size()) == 0);
    CHECK(rig.sched.Dropped() == droppedBefore + 1);

    // One character shorter rides fine.
    const std::string fits(messageScheduler::TEXT_CAPACITY - 1, 'y');
    REQUIRE(rig.sched.ScheduleList(&rig.probe, 2, 0, fits.c_str(), fits.size()) != 0);
    rig.DrainAt(1);
    REQUIRE(rig.probe.hits.size() == 1);
    CHECK(rig.probe.hits[0].text == fits);
  }

  TEST_CASE("scheduler: a target absent from the snapshot is dropped, not touched (#628)") {
    // The lifetime rule: the armed pObject* is never trusted by itself. A
    // target missing from the block's graph — deleted, replaced, or no graph
    // at all — drops the message and frees the slot.
    SchedRig rig;
    DeferProbe outsider; // never added to rig.graph
    REQUIRE(rig.sched.ScheduleBang(&outsider, 1, 0) != 0);
    rig.DrainAt(1);
    CHECK(outsider.hits.empty());
    CHECK(rig.sched.PendingCount() == 0); // dropped, not left armed forever

    // Null graph: same outcome, no crash.
    REQUIRE(rig.sched.ScheduleBang(&rig.probe, 2, 0) != 0);
    rig.clock.store(rig.clock.load() + 1, std::memory_order_release);
    rig.sched.DeliverDue(nullptr, YSE::T_GUI);
    CHECK(rig.probe.hits.empty());
    CHECK(rig.sched.PendingCount() == 0);
  }

  // ─── the first user: .bondo's delay argument ────────────────────────────────

  TEST_CASE("bondo: inside a patcher the delay argument defers the release (#628)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* bondo = p.CreateObject(YSE::OBJ::G_BONDO, "2 100");
    REQUIRE(bondo != nullptr);

    std::vector<char> order;
    OrderSink left;
    left.tag = 'a';
    left.log = &order;
    OrderSink right;
    right.tag = 'b';
    right.log = &order;
    YSE::pHandle leftHandle(&left);
    YSE::pHandle rightHandle(&right);
    p.Connect(bondo, 0, &leftHandle, 0);
    p.Connect(bondo, 1, &rightHandle, 0);

    const std::uint64_t due = messageScheduler::BlocksForMillis(100);
    REQUIRE(due >= 2); // the retrigger/holdback assertions below need room

    // A stored int releases nothing now — where the pre-#628 stopgap released
    // immediately.
    bondo->SetIntData(0, 5);
    CHECK(order.empty());

    // Nothing through every block before the deadline...
    for (std::uint64_t block = 1; block < due; ++block) {
      p.Calculate(YSE::T_DSP);
      CHECK(order.empty());
    }

    // ...then the whole set, right to left, values as held: outlet 1 (never
    // written) releases int 0, outlet 0 the stored 5.
    p.Calculate(YSE::T_DSP);
    CHECK(std::string(order.begin(), order.end()) == "ba");
    CHECK(left.lastInt == 5);
    CHECK(left.count == 1);
    CHECK(right.lastInt == 0);
    CHECK(right.count == 1);
  }

  TEST_CASE(
      "bondo: a new message reschedules the pending release — one release, final set (#628)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* bondo = p.CreateObject(YSE::OBJ::G_BONDO, "2 100");
    REQUIRE(bondo != nullptr);

    std::vector<char> order;
    OrderSink left;
    left.tag = 'a';
    left.log = &order;
    OrderSink right;
    right.tag = 'b';
    right.log = &order;
    YSE::pHandle leftHandle(&left);
    YSE::pHandle rightHandle(&right);
    p.Connect(bondo, 0, &leftHandle, 0);
    p.Connect(bondo, 1, &rightHandle, 0);

    const std::uint64_t due = messageScheduler::BlocksForMillis(100);
    REQUIRE(due >= 2);

    bondo->SetIntData(0, 1);
    // Part-way through the wait, a second message: Max's single clock is
    // re-delayed, so the first deadline must pass silently.
    const std::uint64_t rearmAt = due / 2;
    for (std::uint64_t block = 1; block <= rearmAt; ++block)
      p.Calculate(YSE::T_DSP);
    bondo->SetIntData(0, 2);

    for (std::uint64_t block = rearmAt + 1; block <= due; ++block) {
      p.Calculate(YSE::T_DSP);
      CHECK(order.empty()); // the original deadline passes with no release
    }
    for (std::uint64_t block = due + 1; block <= rearmAt + due; ++block)
      p.Calculate(YSE::T_DSP);

    // Exactly one release, carrying the *final* set.
    CHECK(std::string(order.begin(), order.end()) == "ba");
    CHECK(left.count == 1);
    CHECK(left.lastInt == 2);
  }

  TEST_CASE("bondo: a bang releases immediately and leaves the pending release alone (#628)") {
    // Max: "output will be immediate if triggered by a bang" — the delay
    // argument only defers message-triggered releases. A pending deferred
    // release is not consumed by the bang; it fires at its own time.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* bondo = p.CreateObject(YSE::OBJ::G_BONDO, "2 100");
    REQUIRE(bondo != nullptr);

    std::vector<char> order;
    OrderSink left;
    left.tag = 'a';
    left.log = &order;
    OrderSink right;
    right.tag = 'b';
    right.log = &order;
    YSE::pHandle leftHandle(&left);
    YSE::pHandle rightHandle(&right);
    p.Connect(bondo, 0, &leftHandle, 0);
    p.Connect(bondo, 1, &rightHandle, 0);

    bondo->SetIntData(0, 9); // arms the deferred release
    CHECK(order.empty());
    bondo->SetBang(0); // immediate, in the same call frame
    CHECK(std::string(order.begin(), order.end()) == "ba");
    CHECK(left.lastInt == 9);
    CHECK(left.count == 1);

    // The deferred release still fires at its deadline — second full set.
    const std::uint64_t due = messageScheduler::BlocksForMillis(100);
    for (std::uint64_t block = 1; block <= due; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(left.count == 2);
    CHECK(right.count == 2);
  }

  TEST_CASE("bondo: the deferred release emits the set held at fire time (#628)") {
    // The deferral defers the *release*, not a snapshot: a quiet `set` store
    // between arm and fire is included in what goes out.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* bondo = p.CreateObject(YSE::OBJ::G_BONDO, "2 100");
    REQUIRE(bondo != nullptr);

    std::vector<char> order;
    OrderSink left;
    left.tag = 'a';
    left.log = &order;
    OrderSink right;
    right.tag = 'b';
    right.log = &order;
    YSE::pHandle leftHandle(&left);
    YSE::pHandle rightHandle(&right);
    p.Connect(bondo, 0, &leftHandle, 0);
    p.Connect(bondo, 1, &rightHandle, 0);

    bondo->SetIntData(0, 1); // arms
    bondo->SetListData(1, "set 42"); // stores quietly: no release, no reschedule
    CHECK(order.empty());

    const std::uint64_t due = messageScheduler::BlocksForMillis(100);
    for (std::uint64_t block = 1; block <= due; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(std::string(order.begin(), order.end()) == "ba");
    CHECK(right.lastInt == 42);
    CHECK(left.lastInt == 1);
  }

  TEST_CASE("bondo: a deferred release is still one logical event to .next (#628)") {
    // The reason #628 exists: a timer-thread deferral would hand each outlet's
    // send its own event, so a downstream .next would read one release as
    // several stimuli. Through the scheduler, the deferred release happens
    // inside one dispatch frame: one separated bang, the rest continued —
    // checked against the real object, as the immediate-release test does.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* bondo = p.CreateObject(YSE::OBJ::G_BONDO, "3 50");
    REQUIRE(bondo != nullptr);
    YSE::pHandle* next = p.CreateObject(YSE::OBJ::G_NEXT, "");
    REQUIRE(next != nullptr);
    for (int outlet = 0; outlet < 3; ++outlet)
      p.Connect(bondo, outlet, next, 0);

    std::vector<char> order;
    OrderSink separated;
    separated.tag = 'S';
    separated.log = &order;
    OrderSink continued;
    continued.tag = 'C';
    continued.log = &order;
    YSE::pHandle separatedHandle(&separated);
    YSE::pHandle continuedHandle(&continued);
    p.Connect(next, 0, &separatedHandle, 0);
    p.Connect(next, 1, &continuedHandle, 0);

    bondo->SetIntData(0, 7);
    const std::uint64_t due = messageScheduler::BlocksForMillis(50);
    for (std::uint64_t block = 1; block <= due; ++block)
      p.Calculate(YSE::T_DSP);

    CHECK(std::string(order.begin(), order.end()) == "SCC");
    CHECK(separated.count == 1);
    CHECK(continued.count == 2);
  }

  TEST_CASE("bondo: deleting the object drops its pending release safely (#628)") {
    // Armed long before it is due — far outside the reclaimer's two-block
    // grace — so delivery must re-resolve the target and find nothing. An ASan
    // build trips here if the retired object is touched.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* bondo = p.CreateObject(YSE::OBJ::G_BONDO, "2 50");
    REQUIRE(bondo != nullptr);

    OrderSink sink;
    YSE::pHandle sinkHandle(&sink);
    p.Connect(bondo, 0, &sinkHandle, 0);

    bondo->SetIntData(0, 5);
    CHECK(p.Scheduler()->PendingCount() == 1);
    p.DeleteObject(bondo);

    const std::uint64_t due = messageScheduler::BlocksForMillis(50);
    for (std::uint64_t block = 1; block <= due + 2; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(sink.count == 0);
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

  TEST_CASE("bondo: without a delay argument the release stays synchronous in a patcher (#628)") {
    // The delay path must not tax the common case: no argument, same-frame
    // release, nothing pending, exactly as before #628.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* bondo = p.CreateObject(YSE::OBJ::G_BONDO, "2");
    REQUIRE(bondo != nullptr);

    OrderSink sink;
    YSE::pHandle sinkHandle(&sink);
    p.Connect(bondo, 0, &sinkHandle, 0);

    bondo->SetIntData(0, 3);
    CHECK(sink.count == 1);
    CHECK(sink.lastInt == 3);
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

} // TEST_SUITE("patcher")
