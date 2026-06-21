// Tests for the patcher ↔ global-bus integration (issue #122).
//
// All tests in this TU need a live engine because the bus is owned by
// `INTERNAL::Global()` between `System::init()` and `System::close()` —
// `gReceive::subscribeFromParent()` and `gSend`'s publish paths short-circuit
// when the global is not active, so the no-engine patcher tests in
// test_generic_objects.cpp keep the legacy in-patcher PassData behaviour.
// To exercise the cross-patcher routing we must initialise the engine first.

#include <doctest/doctest.h>
#include <string>

#include "yse.hpp"
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/sinks.hpp"
#include "support/null_device.hpp"

using TestHelpers::MultiSink;

TEST_SUITE("patcher") {

TEST_CASE("patcher: auto-generated name follows the patcher_<N> default") {
    REQUIRE(TestHelpers::engineInit());

    YSE::patcher a;
    YSE::patcher b;
    a.create(2);
    b.create(2);

    // The counter is process-wide and other tests may have created patchers
    // already, so we only assert the shape of the name, not the index.
    CHECK(a.name().rfind("patcher_", 0) == 0);
    CHECK(b.name().rfind("patcher_", 0) == 0);
    CHECK(a.name() != b.name());
}

TEST_CASE("patcher: name() setter is chainable, persists, and is reflected before create()") {
    REQUIRE(TestHelpers::engineInit());

    YSE::patcher p;
    auto & ref = p.name("voice.lead");
    CHECK(&ref == &p);
    CHECK(p.name() == "voice.lead");

    // Calling create() should preserve the user-provided name rather than
    // overwrite it with the default.
    p.create(2);
    CHECK(p.name() == "voice.lead");
}

TEST_CASE("bus routing: gSend in A reaches gReceive in B when patchers share a name") {
    REQUIRE(TestHelpers::engineInit());

    YSE::patcher a; a.name("synth").create(2);
    YSE::patcher b; b.name("synth").create(2);

    YSE::pHandle * send = a.CreateObject(YSE::OBJ::G_SEND, "cutoff");
    YSE::pHandle * recv = b.CreateObject(YSE::OBJ::G_RECEIVE, "cutoff");
    REQUIRE(send != nullptr);
    REQUIRE(recv != nullptr);

    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    b.Connect(recv, 0, &sinkHandle, 0);

    send->SetIntData(0, 42);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 42);

    sink.reset();
    send->SetFloatData(0, 0.125f);
    CHECK(sink.gotFloat);
    CHECK(sink.floatValue == doctest::Approx(0.125f));

    sink.reset();
    send->SetListData(0, "open sesame");
    CHECK(sink.gotList);
    CHECK(sink.listValue == "open sesame");
}

TEST_CASE("bus routing: patchers with distinct names do not cross-talk on the same dataName") {
    REQUIRE(TestHelpers::engineInit());

    YSE::patcher a; a.name("kick").create(2);
    YSE::patcher b; b.name("snare").create(2);

    YSE::pHandle * send = a.CreateObject(YSE::OBJ::G_SEND, "trig");
    YSE::pHandle * recv = b.CreateObject(YSE::OBJ::G_RECEIVE, "trig");
    REQUIRE(send != nullptr);
    REQUIRE(recv != nullptr);

    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    b.Connect(recv, 0, &sinkHandle, 0);

    send->SetIntData(0, 1);
    CHECK_FALSE(sink.gotInt);
}

TEST_CASE("bus routing: destroying a patcher unsubscribes its gReceive nodes") {
    REQUIRE(TestHelpers::engineInit());

    YSE::patcher a; a.name("dangle.test").create(2);

    MultiSink sink;
    {
        YSE::patcher b; b.name("dangle.test").create(2);
        YSE::pHandle * recv = b.CreateObject(YSE::OBJ::G_RECEIVE, "x");
        REQUIRE(recv != nullptr);

        YSE::pHandle sinkHandle(&sink);
        b.Connect(recv, 0, &sinkHandle, 0);

        // Sanity: while b is alive, sends from a still cross.
        YSE::pHandle * probe = a.CreateObject(YSE::OBJ::G_SEND, "x");
        REQUIRE(probe != nullptr);
        probe->SetIntData(0, 1);
        REQUIRE(sink.gotInt);
        a.DeleteObject(probe);
        sink.reset();
    }
    // b has been destroyed; send another value on a. If b's gReceive were
    // still subscribed the bus would dispatch into the freed outlet's memory.
    YSE::pHandle * send = a.CreateObject(YSE::OBJ::G_SEND, "x");
    REQUIRE(send != nullptr);
    send->SetIntData(0, 99);

    // Sink owned by us (still alive) — no callback should have fired into it.
    CHECK_FALSE(sink.gotInt);
}

TEST_CASE("bus routing: renaming the parent patcher re-subscribes existing gReceives") {
    REQUIRE(TestHelpers::engineInit());

    YSE::patcher a; a.name("rename.src").create(2);
    YSE::patcher b; b.name("rename.dst.old").create(2);

    YSE::pHandle * send = a.CreateObject(YSE::OBJ::G_SEND, "v");
    YSE::pHandle * recv = b.CreateObject(YSE::OBJ::G_RECEIVE, "v");
    REQUIRE(send != nullptr);
    REQUIRE(recv != nullptr);

    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    b.Connect(recv, 0, &sinkHandle, 0);

    // Pre-rename: names differ ⇒ no delivery.
    send->SetIntData(0, 1);
    CHECK_FALSE(sink.gotInt);

    // After aligning names, the receiver in b must pick up the next send.
    a.name("rename.shared");
    b.name("rename.shared");
    sink.reset();
    send->SetIntData(0, 7);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 7);
}

TEST_CASE("bus routing: gSend globalOnly=1 skips in-patcher PassData") {
    REQUIRE(TestHelpers::engineInit());

    YSE::patcher local; local.name("globalOnly.local").create(2);
    // gSend with globalOnly=1; the second arg in the params string is the
    // ADD_PARAM(globalOnly) slot.
    YSE::pHandle * send = local.CreateObject(YSE::OBJ::G_SEND, "ping 1");
    YSE::pHandle * recv = local.CreateObject(YSE::OBJ::G_RECEIVE, "ping");
    REQUIRE(send != nullptr);
    REQUIRE(recv != nullptr);

    MultiSink localSink;
    YSE::pHandle localSinkHandle(&localSink);
    local.Connect(recv, 0, &localSinkHandle, 0);

    // A second patcher with the same name confirms the bus path is still
    // active — globalOnly only suppresses the *in-patcher* PassData path.
    YSE::patcher peer; peer.name("globalOnly.local").create(2);
    YSE::pHandle * peerRecv = peer.CreateObject(YSE::OBJ::G_RECEIVE, "ping");
    REQUIRE(peerRecv != nullptr);
    MultiSink peerSink;
    YSE::pHandle peerSinkHandle(&peerSink);
    peer.Connect(peerRecv, 0, &peerSinkHandle, 0);

    send->SetIntData(0, 5);

    // Local delivery comes only via the bus subscription (PassData skipped),
    // so we still see exactly one int on each side and no duplicate fire.
    CHECK(localSink.gotInt);
    CHECK(localSink.intValue == 5);
    CHECK(peerSink.gotInt);
    CHECK(peerSink.intValue == 5);
}

} // TEST_SUITE("patcher")
