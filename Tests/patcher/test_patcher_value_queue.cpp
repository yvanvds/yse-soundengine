// Regression tests for the SPSC value-command queue (issue #225).
//
// Before this change, patcherImplementation::PassBang / PassData ran the target
// gReceive's inlet handler *synchronously on the control/GUI thread*, racing the
// audio thread's Calculate (a genuine UAF through Parameters::Set). Now a value
// message is enqueued and delivered on the audio thread when Calculate drains
// the queue at the top of a block.
//
// These tests drive patcherImplementation directly (the audio-thread entry
// point is Calculate) and observe an external sink wired to the receiver's
// outlet, so they can assert the load-bearing property: nothing is delivered
// until Calculate runs, and then exactly the queued values arrive. A receiver
// removed between enqueue and drain must drop the message without touching the
// retired object. No audio device required.

#include <doctest/doctest.h>
#include <string>
#include <vector>
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "sinks.hpp"
#include "log.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::patcherImplementation;

namespace {
  // Build a patcher holding a gReceive named `name`, with `sink` wired to the
  // receiver's outlet. Returns the receiver's handle (owned by the patcher).
  YSE::pHandle* wireReceiver(patcherImplementation& p, const std::string& name, MultiSink& sink,
                             YSE::pHandle& sinkHandle) {
    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, name);
    REQUIRE(recv != nullptr);
    p.Connect(recv, 0, &sinkHandle, 0);
    return recv;
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("value queue: PassData defers delivery until the next Calculate") {
    patcherImplementation p(1, nullptr);
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    wireReceiver(p, "target", sink, sinkHandle);

    // Enqueued, not delivered: the old code poked the inlet here and the sink
    // would already show the value. It must stay empty until the audio thread
    // drains.
    CHECK(p.PassData(42, "target", YSE::T_GUI));
    CHECK_FALSE(sink.gotInt);

    // The drain happens at the top of Calculate.
    p.Calculate(YSE::T_DSP);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 42);
  }

  TEST_CASE("value queue: bang, float and list all deliver on drain") {
    patcherImplementation p(1, nullptr);
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    wireReceiver(p, "ch", sink, sinkHandle);

    CHECK(p.PassBang("ch", YSE::T_GUI));
    CHECK(p.PassData(0.25f, "ch", YSE::T_GUI));
    CHECK(p.PassData(std::string("hello world"), "ch", YSE::T_GUI));
    CHECK_FALSE(sink.gotBang);
    CHECK_FALSE(sink.gotFloat);
    CHECK_FALSE(sink.gotList);

    p.Calculate(YSE::T_DSP);
    CHECK(sink.gotBang);
    CHECK(sink.gotFloat);
    CHECK(sink.floatValue == doctest::Approx(0.25f));
    CHECK(sink.gotList);
    CHECK(sink.listValue == "hello world");
  }

  TEST_CASE("value queue: several messages queued between blocks all arrive in one drain") {
    patcherImplementation p(1, nullptr);
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    wireReceiver(p, "acc", sink, sinkHandle);

    for (int i = 1; i <= 5; ++i) {
      CHECK(p.PassData(i, "acc", YSE::T_GUI));
    }
    CHECK_FALSE(sink.gotInt);

    p.Calculate(YSE::T_DSP);
    // The last message wins (each overwrites intValue in the sink).
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 5);
  }

  TEST_CASE("value queue: PassData to an unknown target returns false and delivers nothing") {
    patcherImplementation p(1, nullptr);
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    wireReceiver(p, "target", sink, sinkHandle);

    // No receiver named "missing" and no OSC handler: the call reports failure.
    CHECK_FALSE(p.PassData(7, "missing", YSE::T_GUI));
    p.Calculate(YSE::T_DSP);
    CHECK_FALSE(sink.gotInt);
  }

  TEST_CASE("value queue: a receiver deleted before the drain drops the message safely") {
    patcherImplementation p(1, nullptr);
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::pHandle* recv = wireReceiver(p, "gone", sink, sinkHandle);

    // Target exists at enqueue time, so the value is accepted...
    CHECK(p.PassData(99, "gone", YSE::T_GUI));
    // ...but it is removed before the audio thread drains. The drain re-resolves
    // the target against the current snapshot, finds nothing, and must not touch
    // the retired object (an ASan build would trip if it did).
    p.DeleteObject(recv);
    p.Calculate(YSE::T_DSP);
    CHECK_FALSE(sink.gotInt);
  }

  TEST_CASE("value queue: overfilling the queue drops with no crash and keeps delivering") {
    patcherImplementation p(1, nullptr);
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    wireReceiver(p, "spam", sink, sinkHandle);

    // Push well past the fixed queue depth without draining. Excess messages are
    // dropped (logged), never block or crash the producer.
    for (int i = 0; i < 1000; ++i) {
      p.PassData(i, "spam", YSE::T_GUI);
    }
    p.Calculate(YSE::T_DSP);
    CHECK(sink.gotInt); // whatever survived the bound still delivers

    // The queue is healthy afterwards: a fresh message delivers on the next block.
    sink.reset();
    CHECK(p.PassData(123, "spam", YSE::T_GUI));
    p.Calculate(YSE::T_DSP);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 123);
  }

  // ── A send reached from a deferred delivery (issue #690) ───────────────────
  //
  // The drains at the top of Calculate — the value queue above, the #628
  // deferred-message scheduler, the #683 file scheduler — all dispatch with
  // T_GUI *from the audio callback*, because T_GUI means "set the state and let
  // the block's own traversal render what you caused", not "you are on the
  // control thread". PassBang / PassData used to read it as the latter, and
  // answered a `.s` reached that way by locking `mtx`, scanning the object map
  // and — for an unknown name — concatenating a log string out of every
  // receiver in the patcher. A lock and an allocation on the audio callback,
  // reachable from any `.delay` / `.bondo` / `.qlist` wired into a `.s`.
  //
  // Both halves of that branch are asserted, because each can be passed alone:
  // *when* the value lands (the control-thread queue is drained before the
  // deferred one, so a message enqueued from a deferred delivery costs a whole
  // extra block) and *what the miss costs* (only the not-found path logs).
  //
  // The rig is `.r trigger` → `.delay 0` → `.s <name>`, plus a `.r <name>` when
  // the send is meant to land. `.delay 0` still defers by the scheduler's
  // one-block floor, so its bang leaves the delay inside a T_GUI drain on the
  // audio thread — exactly the reported path — and the stimulus enters through
  // the public PassBang door rather than by poking an inlet.
  namespace {
    void wireDeferredSend(patcherImplementation& p, const std::string& sendTo) {
      YSE::pHandle* trigger = p.CreateObject(YSE::OBJ::G_RECEIVE, "trigger");
      YSE::pHandle* delay = p.CreateObject(YSE::OBJ::G_DELAY, "0");
      YSE::pHandle* send = p.CreateObject(YSE::OBJ::G_SEND, sendTo);
      REQUIRE(trigger != nullptr);
      REQUIRE(delay != nullptr);
      REQUIRE(send != nullptr);
      p.Connect(trigger, 0, delay, 0);
      p.Connect(delay, 0, send, 0);
    }

    // Records every log line, so "the audio thread emitted a message" is an
    // assertion rather than an inference. A counting sink rather than the
    // throwing one test_log_nothrow needs, so it is safe in the shared process;
    // cases match on a substring so an unrelated line cannot fail them.
    class RecordingHandler : public YSE::logHandler {
    public:
      void AddMessage(const std::string& message) override {
        messages.push_back(message);
      }
      bool sawSubstring(const std::string& needle) const {
        for (const std::string& m : messages) {
          if (m.find(needle) != std::string::npos) return true;
        }
        return false;
      }
      std::vector<std::string> messages;
    };

    // Installs a sink and a level that lets errors through for one case, and
    // restores both even if an assertion unwinds.
    class ScopedSink {
    public:
      explicit ScopedSink(YSE::logHandler* handler) : previousLevel(YSE::Log().getLevel()) {
        YSE::Log().setLevel(YSE::EL_DEBUG);
        YSE::Log().setHandler(handler);
      }
      ~ScopedSink() {
        YSE::Log().setHandler(nullptr);
        YSE::Log().setLevel(previousLevel);
      }
      ScopedSink(const ScopedSink&) = delete;
      ScopedSink& operator=(const ScopedSink&) = delete;

    private:
      YSE::ERROR_LEVEL previousLevel;
    };
  } // namespace

  TEST_CASE("deferred send: a .s reached from a delayed bang delivers in the same block") {
    patcherImplementation p(1, nullptr);
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    wireReceiver(p, "target", sink, sinkHandle);
    wireDeferredSend(p, "target");

    // Block 1 drains the value queue, which bangs `.r trigger` and arms the
    // delay one block out.
    CHECK(p.PassBang("trigger", YSE::T_GUI));
    p.Calculate(YSE::T_DSP);
    CHECK_FALSE(sink.gotBang);

    // Block 2 is the one the delay comes due in. The bang leaves `.delay`
    // tagged T_GUI on the audio thread and reaches `.s target`, which must
    // dispatch against the snapshot already pinned for this block. Taking the
    // control-thread branch instead — the bug, and the branch that locks `mtx`
    // — leaves the sink untouched here and only delivers on the block after.
    p.Calculate(YSE::T_DSP);
    CHECK(sink.gotBang);
  }

  TEST_CASE("deferred send: a .s to an unknown name logs nothing from the audio thread") {
    patcherImplementation p(1, nullptr);
    // No `.r nowhere` anywhere in the patcher, so the send misses. On the
    // control thread a miss is worth reporting; on the audio callback the
    // report is a std::string built from a locked scan of the object map, and
    // must not happen at all.
    wireDeferredSend(p, "nowhere");

    CHECK(p.PassBang("trigger", YSE::T_GUI));
    p.Calculate(YSE::T_DSP); // arms the delay

    RecordingHandler handler;
    ScopedSink sink(&handler);
    p.Calculate(YSE::T_DSP); // the block the deferred bang fires in
    CHECK_FALSE(handler.sawSubstring("nowhere"));
  }

  TEST_CASE("deferred send: a .s missing its target still reports the miss on the control thread") {
    // The counterpart of the case above: the fix must not silence the miss for
    // callers that really are on the control thread, where logging it is right.
    patcherImplementation p(1, nullptr);
    wireDeferredSend(p, "nowhere");

    RecordingHandler handler;
    ScopedSink sink(&handler);
    CHECK_FALSE(p.PassBang("nowhere", YSE::T_GUI));
    CHECK(handler.sawSubstring("nowhere"));
  }

  TEST_CASE("deferred send: a control-thread send still routes through the value queue") {
    // And the fix must be a branch on *which thread*, not a blanket switch to
    // synchronous dispatch: a PassBang from this (control) thread is still
    // queued rather than delivered inline, exactly as the cases at the top of
    // this file pin down. The `.s` in the chain re-enters PassBang from inside
    // the drain — genuinely on the audio thread — and lands in that same block.
    patcherImplementation p(1, nullptr);
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    wireReceiver(p, "target", sink, sinkHandle);

    YSE::pHandle* trigger = p.CreateObject(YSE::OBJ::G_RECEIVE, "trigger");
    YSE::pHandle* send = p.CreateObject(YSE::OBJ::G_SEND, "target");
    REQUIRE(trigger != nullptr);
    REQUIRE(send != nullptr);
    p.Connect(trigger, 0, send, 0);

    CHECK(p.PassBang("trigger", YSE::T_GUI));
    CHECK_FALSE(sink.gotBang);

    p.Calculate(YSE::T_DSP);
    CHECK(sink.gotBang);
  }

} // TEST_SUITE("patcher")
