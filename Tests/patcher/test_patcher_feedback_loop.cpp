// Regression tests for issue #236 — a feedback loop in the message send path
// must terminate instead of recursing until the stack overflows.
//
// Before the fix, a single value entering a message cycle recursed through
// outlet::Send* without bound (observed STATUS_STACK_OVERFLOW 0xC00000FD).
// The wait-free depth guard in outlet.cpp caps the send depth so any cycle
// unwinds instead of crashing. These tests build the two cycle shapes called
// out in the issue and prove the process survives delivering into them.
//
// Two shapes:
//   1. Purely local wired cycle among generic objects — no engine, no bus.
//      Two gReceive nodes wired output->input in a ring; a value driven into
//      one recurses through outlet::SendInt on the live wiring.
//   2. The canonical bus-routed gSend/gReceive feedback: a send and a receive
//      with the same dataName in one patcher, the receive's output wired back
//      into the send's inlet. Publishing a value makes the T_GUI bus dispatch
//      re-enter the send synchronously. This needs a live engine because the
//      bus only routes between init() and close().

#include <doctest/doctest.h>
#include <string>

#include "yse.hpp"
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/genericObjects/gReceive.h"
#include "patcher/sinks.hpp"
#include "support/null_device.hpp"

using TestHelpers::MultiSink;

TEST_SUITE("patcher") {

  TEST_CASE("feedback loop: local wired gReceive ring terminates instead of overflowing") {
    // gReceive forwards whatever hits its inlet straight to its outlet, so two
    // of them wired output->input form a message ring with no bus involved.
    YSE::PATCHER::gReceive a;
    YSE::PATCHER::gReceive b;

    // a.out -> b.in  and  b.out -> a.in : a closed cycle.
    a.ConnectOutlet(b.GetInlet(0), 0);
    b.ConnectInlet(a.GetOutlet(0), 0);
    b.ConnectOutlet(a.GetInlet(0), 0);
    a.ConnectInlet(b.GetOutlet(0), 0);

    // Deliver one value into the ring. Without the depth guard this recurses
    // until the stack overflows; with it, the fan-out is dropped once the
    // ceiling is hit and control returns here.
    a.GetInlet(0)->SetInt(1, YSE::T_GUI);

    // Reaching this line at all is the assertion: the cycle terminated.
    CHECK(true);
  }

  TEST_CASE("feedback loop: bus-routed gSend/gReceive cycle terminates instead of overflowing") {
    REQUIRE(TestHelpers::engineInit());

    YSE::patcher p;
    p.name("loop.feedback").create(2);

    // send "a" and receive "a" in the same patcher: the send publishes on the
    // bus, the receive (same dataName) is subscribed to it.
    YSE::pHandle* send = p.CreateObject(YSE::OBJ::G_SEND, "a");
    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "a");
    REQUIRE(send != nullptr);
    REQUIRE(recv != nullptr);

    // Wire the receive's output back into the send's inlet, closing the loop,
    // and also into a sink so we can confirm the value actually circulated.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    p.Connect(recv, 0, send, 0);
    p.Connect(recv, 0, &sinkHandle, 0);

    // One value into the send. On T_GUI the bus dispatches synchronously, so
    // recv -> send -> publish -> recv ... would recurse without bound. The
    // guard breaks it; control must return here.
    send->SetIntData(0, 7);

    CHECK(sink.gotInt);
    CHECK(sink.intValue == 7);
  }

} // TEST_SUITE("patcher")
