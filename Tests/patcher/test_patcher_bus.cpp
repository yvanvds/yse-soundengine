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
#include <variant>

#include "yse.hpp"
#include "internal/namedBus.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
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
    auto& ref = p.name("voice.lead");
    CHECK(&ref == &p);
    CHECK(p.name() == "voice.lead");

    // Calling create() should preserve the user-provided name rather than
    // overwrite it with the default.
    p.create(2);
    CHECK(p.name() == "voice.lead");
  }

  TEST_CASE("bus routing: gSend in A reaches gReceive in B when patchers share a name") {
    REQUIRE(TestHelpers::engineInit());

    YSE::patcher a;
    a.name("synth").create(2);
    YSE::patcher b;
    b.name("synth").create(2);

    YSE::pHandle* send = a.CreateObject(YSE::OBJ::G_SEND, "cutoff");
    YSE::pHandle* recv = b.CreateObject(YSE::OBJ::G_RECEIVE, "cutoff");
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

    YSE::patcher a;
    a.name("kick").create(2);
    YSE::patcher b;
    b.name("snare").create(2);

    YSE::pHandle* send = a.CreateObject(YSE::OBJ::G_SEND, "trig");
    YSE::pHandle* recv = b.CreateObject(YSE::OBJ::G_RECEIVE, "trig");
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

    YSE::patcher a;
    a.name("dangle.test").create(2);

    MultiSink sink;
    {
      YSE::patcher b;
      b.name("dangle.test").create(2);
      YSE::pHandle* recv = b.CreateObject(YSE::OBJ::G_RECEIVE, "x");
      REQUIRE(recv != nullptr);

      YSE::pHandle sinkHandle(&sink);
      b.Connect(recv, 0, &sinkHandle, 0);

      // Sanity: while b is alive, sends from a still cross.
      YSE::pHandle* probe = a.CreateObject(YSE::OBJ::G_SEND, "x");
      REQUIRE(probe != nullptr);
      probe->SetIntData(0, 1);
      REQUIRE(sink.gotInt);
      a.DeleteObject(probe);
      sink.reset();
    }
    // b has been destroyed; send another value on a. If b's gReceive were
    // still subscribed the bus would dispatch into the freed outlet's memory.
    YSE::pHandle* send = a.CreateObject(YSE::OBJ::G_SEND, "x");
    REQUIRE(send != nullptr);
    send->SetIntData(0, 99);

    // Sink owned by us (still alive) — no callback should have fired into it.
    CHECK_FALSE(sink.gotInt);
  }

  TEST_CASE("bus routing: renaming the parent patcher re-subscribes existing gReceives") {
    REQUIRE(TestHelpers::engineInit());

    YSE::patcher a;
    a.name("rename.src").create(2);
    YSE::patcher b;
    b.name("rename.dst.old").create(2);

    YSE::pHandle* send = a.CreateObject(YSE::OBJ::G_SEND, "v");
    YSE::pHandle* recv = b.CreateObject(YSE::OBJ::G_RECEIVE, "v");
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

    YSE::patcher local;
    local.name("globalOnly.local").create(2);
    // gSend with globalOnly=1; the second arg in the params string is the
    // ADD_PARAM(globalOnly) slot.
    YSE::pHandle* send = local.CreateObject(YSE::OBJ::G_SEND, "ping 1");
    YSE::pHandle* recv = local.CreateObject(YSE::OBJ::G_RECEIVE, "ping");
    REQUIRE(send != nullptr);
    REQUIRE(recv != nullptr);

    MultiSink localSink;
    YSE::pHandle localSinkHandle(&localSink);
    local.Connect(recv, 0, &localSinkHandle, 0);

    // A second patcher with the same name confirms the bus path is still
    // active — globalOnly only suppresses the *in-patcher* PassData path.
    YSE::patcher peer;
    peer.name("globalOnly.local").create(2);
    YSE::pHandle* peerRecv = peer.CreateObject(YSE::OBJ::G_RECEIVE, "ping");
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

  // ─── .forward (issue #485) ──────────────────────────────────────────────────
  //
  // The object-level suite (test_patcher_forward.cpp) runs without an engine, so
  // it can only reach the in-patcher PassData half. These cases run the real
  // thing end to end: a live engine, the global bus, and receivers in a *second*
  // patcher — which is where the "<patcherName>.<destination>" address is
  // actually exercised, and the only place the cached prefix can be caught
  // going stale.

  TEST_CASE("bus routing: .forward re-aimed at run time reaches a different receiver (#485)") {
    REQUIRE(TestHelpers::engineInit());

    YSE::patcher a;
    a.name("fwd.live").create(2);
    YSE::patcher b;
    b.name("fwd.live").create(2);

    YSE::pHandle* fwd = a.CreateObject(YSE::OBJ::G_FORWARD, "cutoff");
    YSE::pHandle* cutoff = b.CreateObject(YSE::OBJ::G_RECEIVE, "cutoff");
    YSE::pHandle* res = b.CreateObject(YSE::OBJ::G_RECEIVE, "res");
    REQUIRE(fwd != nullptr);
    REQUIRE(cutoff != nullptr);
    REQUIRE(res != nullptr);

    MultiSink cutoffSink;
    YSE::pHandle cutoffHandle(&cutoffSink);
    b.Connect(cutoff, 0, &cutoffHandle, 0);
    MultiSink resSink;
    YSE::pHandle resHandle(&resSink);
    b.Connect(res, 0, &resHandle, 0);

    fwd->SetIntData(0, 42);
    CHECK(cutoffSink.gotInt);
    CHECK(cutoffSink.intValue == 42);
    CHECK_FALSE(resSink.gotInt);

    // The whole object: one message re-aims it, with no edit to the graph.
    cutoffSink.reset();
    fwd->SetListData(1, "res");
    fwd->SetIntData(0, 7);
    CHECK_FALSE(cutoffSink.gotInt);
    CHECK(resSink.gotInt);
    CHECK(resSink.intValue == 7);

    // And the float / list payloads travel the same address.
    resSink.reset();
    fwd->SetFloatData(0, 0.125f);
    CHECK(resSink.gotFloat);
    CHECK(resSink.floatValue == doctest::Approx(0.125f));

    resSink.reset();
    fwd->SetListData(0, "open sesame");
    CHECK(resSink.gotList);
    CHECK(resSink.listValue == "open sesame");
  }

  TEST_CASE("bus routing: renaming the parent patcher re-anchors a .forward (#485)") {
    // .forward caches the "<patcherName>." prefix its destination is appended
    // to, exactly as gSend caches the whole address — so patcherImplementation
    // ::SetName has to refresh it or the object keeps publishing under the old
    // patcher name.
    REQUIRE(TestHelpers::engineInit());

    YSE::patcher a;
    a.name("fwd.rename.src").create(2);
    YSE::patcher b;
    b.name("fwd.rename.dst").create(2);

    YSE::pHandle* fwd = a.CreateObject(YSE::OBJ::G_FORWARD, "v");
    YSE::pHandle* recv = b.CreateObject(YSE::OBJ::G_RECEIVE, "v");
    REQUIRE(fwd != nullptr);
    REQUIRE(recv != nullptr);

    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    b.Connect(recv, 0, &sinkHandle, 0);

    fwd->SetIntData(0, 1);
    CHECK_FALSE(sink.gotInt);

    a.name("fwd.rename.shared");
    b.name("fwd.rename.shared");
    sink.reset();
    fwd->SetIntData(0, 7);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 7);
  }

  TEST_CASE("bus routing: renaming the parent patcher re-anchors a .table (#699)") {
    // Same miss as #485, one object later: .table's `send` (#498) caches the
    // "<patcherName>." prefix through .forward's pre-reserved-string pattern but
    // was left out of patcherImplementation::SetName, so it kept publishing under
    // the old patcher name while every .r around it re-anchored under the new
    // one. The receiver lives in the *other* patcher deliberately: the in-patcher
    // PassData half matches on the bare name and never noticed the stale prefix,
    // which is what made the failure silent and partial.
    REQUIRE(TestHelpers::engineInit());

    YSE::patcher a;
    a.name("table.rename.src").create(2);
    YSE::patcher b;
    b.name("table.rename.dst").create(2);

    YSE::pHandle* t = a.CreateObject(YSE::OBJ::G_TABLE, "8");
    YSE::pHandle* recv = b.CreateObject(YSE::OBJ::G_RECEIVE, "curve");
    REQUIRE(t != nullptr);
    REQUIRE(recv != nullptr);

    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    b.Connect(recv, 0, &sinkHandle, 0);

    t->SetListData(0, "set 4 321");

    // Different names, so nothing crosses yet.
    t->SetListData(0, "send curve 4");
    CHECK_FALSE(sink.gotInt);

    // Now the two patchers share a name and the send has to follow.
    a.name("table.rename.shared");
    b.name("table.rename.shared");
    sink.reset();
    t->SetListData(0, "send curve 4");
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 321);
  }

  TEST_CASE("bus routing: a .forward with no destination publishes nothing (#485)") {
    // "<patcherName>." is a real, reachable bus address — the one an unnamed
    // gReceive subscribes to. An unconfigured .forward that published to it
    // would broadcast into every same-named patcher in the process.
    REQUIRE(TestHelpers::engineInit());

    YSE::patcher a;
    a.name("fwd.unset").create(2);
    YSE::patcher b;
    b.name("fwd.unset").create(2);

    YSE::pHandle* fwd = a.CreateObject(YSE::OBJ::G_FORWARD);
    YSE::pHandle* recv = b.CreateObject(YSE::OBJ::G_RECEIVE);
    REQUIRE(fwd != nullptr);
    REQUIRE(recv != nullptr);

    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    b.Connect(recv, 0, &sinkHandle, 0);

    fwd->SetIntData(0, 1);
    fwd->SetBang(0);
    fwd->SetListData(0, "hello");
    CHECK_FALSE(sink.gotInt);
    CHECK_FALSE(sink.gotBang);
    CHECK_FALSE(sink.gotList);

    // Once it is told where to point, the same object reaches that receiver.
    fwd->SetListData(1, "target");
    YSE::pHandle* target = b.CreateObject(YSE::OBJ::G_RECEIVE, "target");
    REQUIRE(target != nullptr);
    MultiSink targetSink;
    YSE::pHandle targetHandle(&targetSink);
    b.Connect(target, 0, &targetHandle, 0);

    fwd->SetIntData(0, 5);
    CHECK(targetSink.gotInt);
    CHECK(targetSink.intValue == 5);
  }

  TEST_CASE("bus routing: .forward globalOnly=1 skips in-patcher PassData (#485)") {
    // Parity with gSend's parameter of the same name: the local fan-out is
    // suppressed, the bus publish is not.
    REQUIRE(TestHelpers::engineInit());

    YSE::patcher local;
    local.name("fwd.globalOnly").create(2);
    YSE::pHandle* fwd = local.CreateObject(YSE::OBJ::G_FORWARD, "ping 1");
    YSE::pHandle* recv = local.CreateObject(YSE::OBJ::G_RECEIVE, "ping");
    REQUIRE(fwd != nullptr);
    REQUIRE(recv != nullptr);

    MultiSink localSink;
    YSE::pHandle localSinkHandle(&localSink);
    local.Connect(recv, 0, &localSinkHandle, 0);

    YSE::patcher peer;
    peer.name("fwd.globalOnly").create(2);
    YSE::pHandle* peerRecv = peer.CreateObject(YSE::OBJ::G_RECEIVE, "ping");
    REQUIRE(peerRecv != nullptr);
    MultiSink peerSink;
    YSE::pHandle peerSinkHandle(&peerSink);
    peer.Connect(peerRecv, 0, &peerSinkHandle, 0);

    fwd->SetIntData(0, 5);
    CHECK(localSink.gotInt);
    CHECK(localSink.intValue == 5);
    CHECK(peerSink.gotInt);
    CHECK(peerSink.intValue == 5);
  }

  // ── Publishing from a deferred delivery (issue #690) ──────────────────────
  //
  // A `.s` reached from the deferred-message drain runs on the audio callback
  // carrying a T_GUI tag. `NamedBus::publish` reads T_GUI off its control
  // thread as "park this for the next drainPending()", which takes
  // `pendingMutex_` and grows a vector of (std::string, BusValue) — a lock and
  // two allocations on the callback, on top of the `BusValue{value}` copy the
  // caller had already made. gSend / gForward / gTable therefore ask
  // `patcherImplementation::CallingThread` which thread they are really on and
  // publish with T_DSP when the answer is the audio thread.
  //
  // That puts a deferred publish under the bus's documented audio-thread
  // contract (namedBus.h): int and float ride the lock-free per-thread queue
  // and arrive on the next drain; bang and list are dropped, as they already
  // were for a `.s` fanning out mid-traversal. The two cases below pin both
  // halves, since the second is a deliberate behaviour change.
  //
  // These drive `patcherImplementation` directly because a deferral is measured
  // in audio blocks and `Calculate` is where a block happens; `YSE::patcher`
  // keeps its impl private for everything but `YSE::sound`.
  TEST_CASE("bus routing: an int from a deferred .s arrives on the next bus drain") {
    REQUIRE(TestHelpers::engineInit());

    YSE::PATCHER::patcherImplementation p(1, nullptr);
    p.SetName("deferred.bus");

    // `.r trigger` → `.bondo 1 5` → `.s out`: a bang releases the stored set,
    // and the 5 ms argument makes that release a deferred one, so the int
    // leaves `.bondo` inside the audio thread's drain.
    YSE::pHandle* trigger = p.CreateObject(YSE::OBJ::G_RECEIVE, "trigger");
    YSE::pHandle* bondo = p.CreateObject(YSE::OBJ::G_BONDO, "1 5");
    YSE::pHandle* send = p.CreateObject(YSE::OBJ::G_SEND, "out");
    REQUIRE(trigger != nullptr);
    REQUIRE(bondo != nullptr);
    REQUIRE(send != nullptr);
    p.Connect(trigger, 0, bondo, 0);
    p.Connect(bondo, 0, send, 0);

    int received = 0;
    int intValue = -1;
    const YSE::INTERNAL::SubHandle sub = YSE::INTERNAL::Bus().subscribe(
        "deferred.bus.out", [&received, &intValue](const YSE::INTERNAL::BusValue& value) {
          received++;
          if (const int* i = std::get_if<int>(&value)) intValue = *i;
        });

    CHECK(p.PassData(11, "trigger", YSE::T_GUI));
    // Blocks: the first drains the value queue and arms the release, and then
    // as many as the 5 ms deadline needs. Nothing is dispatched inline from any
    // of them — the audio path only ever enqueues.
    for (int block = 0; block < 32; ++block) {
      p.Calculate(YSE::T_DSP);
    }
    CHECK(received == 0);

    // drainPending() runs from update(), on the control thread.
    YSE::System().update();
    CHECK(received == 1);
    CHECK(intValue == 11);

    YSE::INTERNAL::Bus().unsubscribe(sub);
  }

  TEST_CASE("bus routing: a bang from a deferred .s follows the bus audio-thread contract") {
    REQUIRE(TestHelpers::engineInit());

    YSE::PATCHER::patcherImplementation p(1, nullptr);
    p.SetName("deferred.bang");

    // `.r trigger` → `.delay 0` → `.s out`, whose deferred release is a bang.
    YSE::pHandle* trigger = p.CreateObject(YSE::OBJ::G_RECEIVE, "trigger");
    YSE::pHandle* delay = p.CreateObject(YSE::OBJ::G_DELAY, "0");
    YSE::pHandle* send = p.CreateObject(YSE::OBJ::G_SEND, "out");
    REQUIRE(trigger != nullptr);
    REQUIRE(delay != nullptr);
    REQUIRE(send != nullptr);
    p.Connect(trigger, 0, delay, 0);
    p.Connect(delay, 0, send, 0);

    // A `.r out` in the same patcher proves the bang really was released: the
    // in-patcher half of the send is unaffected by any of this.
    MultiSink local;
    YSE::pHandle localHandle(&local);
    YSE::pHandle* localRecv = p.CreateObject(YSE::OBJ::G_RECEIVE, "out");
    REQUIRE(localRecv != nullptr);
    p.Connect(localRecv, 0, &localHandle, 0);

    int received = 0;
    const YSE::INTERNAL::SubHandle sub = YSE::INTERNAL::Bus().subscribe(
        "deferred.bang.out", [&received](const YSE::INTERNAL::BusValue&) { received++; });

    CHECK(p.PassBang("trigger", YSE::T_GUI));
    for (int block = 0; block < 8; ++block) {
      p.Calculate(YSE::T_DSP);
    }
    YSE::System().update();

    CHECK(local.gotBang); // the release happened
    // ...and the bus did not carry it: a monostate payload has no audio-thread
    // route, exactly as for a `.s` banged mid-traversal on T_DSP.
    CHECK(received == 0);

    YSE::INTERNAL::Bus().unsubscribe(sub);
  }

} // TEST_SUITE("patcher")
