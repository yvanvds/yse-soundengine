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
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "sinks.hpp"

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

} // TEST_SUITE("patcher")
