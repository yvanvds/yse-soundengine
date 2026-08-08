// Tests for patcher generic + time objects: gGate, gRoute, gSwitch, gReceive,
// gSend, gMetro.  No audio device required.
//
// gSend is parent-coupled: its SetXValue handlers cast `parent` to
// `patcherImplementation*` and call PassData on it.  The realistic path is
// therefore exercised through a patcher containing both gSend and gReceive with
// matching dataNames.  Because control-thread PassData now defers delivery to
// the audio thread (issue #225), the gSend->gReceive case drives
// patcherImplementation directly so it can drain the value queue via Calculate.
//
// gMetro spawns a background TimerThread tick.  Most tests stop the metro
// before destruction (Toggle 0) so the dtor's id==0 branch runs; the #663 case
// deliberately does not, because destroying a *running* metro is the path that
// used to leave a live timer bound to freed memory.

#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/genericObjects/gGate.h"
#include "patcher/genericObjects/gRoute.h"
#include "patcher/genericObjects/gSwitch.h"
#include "patcher/genericObjects/gReceive.h"
#include "patcher/genericObjects/gSend.h"
#include "patcher/time/gMetro.h"
#include "patcher/sinks.hpp"

using TestHelpers::BangSink;
using TestHelpers::MultiSink;

using namespace std::chrono_literals;

namespace {

  // BangSink counts with a plain int, which is fine while the sends come from
  // the test thread.  A *running* metro bangs from the TimerThread worker, so
  // the live-period tests below need a counter that both threads may touch.
  struct AtomicBangSink : YSE::PATCHER::pObject {
    std::atomic<int> bangCount{0};
    AtomicBangSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang(
          [this](int, YSE::THREAD) { bangCount.fetch_add(1, std::memory_order_relaxed); });
    }
    const char* Type() const override {
      return "atomic_bang_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
    int count() const {
      return bangCount.load(std::memory_order_relaxed);
    }
  };

  // Poll `pred` every millisecond up to `budgetMs`, so a test finishes as soon
  // as the metro delivers rather than sleeping a fixed worst case.
  template <typename P> bool waitFor(P pred, int budgetMs = 1000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
    while (std::chrono::steady_clock::now() < deadline) {
      if (pred()) return true;
      std::this_thread::sleep_for(1ms);
    }
    return pred();
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── gGate ────────────────────────────────────────────────────────────────────

  TEST_CASE("gGate: type name, default input/output count, and output types") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_GATE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".gate");
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 2);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::ANY);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::ANY);
  }

  TEST_CASE("gGate: activeOutlet=0 swallows messages on the value inlet") {
    YSE::PATCHER::gGate gate;
    MultiSink sink0, sink1;
    gate.ConnectOutlet(sink0.GetInlet(0), 0);
    sink0.ConnectInlet(gate.GetOutlet(0), 0);
    gate.ConnectOutlet(sink1.GetInlet(0), 1);
    sink1.ConnectInlet(gate.GetOutlet(1), 0);

    // activeOutlet is 0 by default; messages should not propagate.
    gate.GetInlet(1)->SetBang(YSE::T_GUI);
    gate.GetInlet(1)->SetInt(42, YSE::T_GUI);
    gate.GetInlet(1)->SetFloat(3.14f, YSE::T_GUI);
    gate.GetInlet(1)->SetList("hello", YSE::T_GUI);

    CHECK_FALSE(sink0.gotBang);
    CHECK_FALSE(sink0.gotInt);
    CHECK_FALSE(sink1.gotBang);
    CHECK_FALSE(sink1.gotInt);
  }

  TEST_CASE("gGate: activeOutlet selects which outlet receives messages") {
    YSE::PATCHER::gGate gate;
    MultiSink sink0, sink1;
    gate.ConnectOutlet(sink0.GetInlet(0), 0);
    sink0.ConnectInlet(gate.GetOutlet(0), 0);
    gate.ConnectOutlet(sink1.GetInlet(0), 1);
    sink1.ConnectInlet(gate.GetOutlet(1), 0);

    gate.GetInlet(0)->SetInt(1, YSE::T_GUI); // route to outlet 0
    gate.GetInlet(1)->SetBang(YSE::T_GUI);
    gate.GetInlet(1)->SetInt(7, YSE::T_GUI);
    gate.GetInlet(1)->SetFloat(2.5f, YSE::T_GUI);
    gate.GetInlet(1)->SetList("x", YSE::T_GUI);

    CHECK(sink0.gotBang);
    CHECK(sink0.gotInt);
    CHECK(sink0.intValue == 7);
    CHECK(sink0.gotFloat);
    CHECK(sink0.floatValue == doctest::Approx(2.5f));
    CHECK(sink0.gotList);
    CHECK(sink0.listValue == "x");
    CHECK_FALSE(sink1.gotBang);

    sink0.reset();
    gate.GetInlet(0)->SetInt(2, YSE::T_GUI); // route to outlet 1
    gate.GetInlet(1)->SetInt(8, YSE::T_GUI);
    CHECK_FALSE(sink0.gotInt);
    CHECK(sink1.gotInt);
    CHECK(sink1.intValue == 8);
  }

  TEST_CASE("gGate: SetParams grows the number of outlets") {
    YSE::PATCHER::gGate gate;
    REQUIRE(gate.NumOutputs() == 2);
    gate.SetParams("4");
    CHECK(gate.NumOutputs() == 4);

    MultiSink sink3;
    gate.ConnectOutlet(sink3.GetInlet(0), 3);
    sink3.ConnectInlet(gate.GetOutlet(3), 0);

    gate.GetInlet(0)->SetInt(4, YSE::T_GUI);
    gate.GetInlet(1)->SetInt(99, YSE::T_GUI);
    CHECK(sink3.gotInt);
    CHECK(sink3.intValue == 99);
  }

  TEST_CASE("gGate: out-of-range activeOutlet does nothing") {
    YSE::PATCHER::gGate gate;
    MultiSink sink0, sink1;
    gate.ConnectOutlet(sink0.GetInlet(0), 0);
    sink0.ConnectInlet(gate.GetOutlet(0), 0);
    gate.ConnectOutlet(sink1.GetInlet(0), 1);
    sink1.ConnectInlet(gate.GetOutlet(1), 0);

    gate.GetInlet(0)->SetInt(99, YSE::T_GUI); // > NumOutputs
    gate.GetInlet(1)->SetBang(YSE::T_GUI);
    CHECK_FALSE(sink0.gotBang);
    CHECK_FALSE(sink1.gotBang);
  }

  // ─── gRoute ───────────────────────────────────────────────────────────────────

  TEST_CASE("gRoute: type name and that outlets are created from SetParams") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ROUTE, "10 20 30");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".route");
    CHECK(h->GetInputs() == 1);
    // 3 keys + 1 default outlet
    CHECK(h->GetOutputs() == 4);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::ANY);
  }

  TEST_CASE("gRoute: integer keys route to the matching outlet") {
    // A matched bare number is Max's "the message has no additional items"
    // case: the selector *was* the message, so what leaves the matching outlet
    // is a bang and not the number (#672).  Only the unmatched value still
    // arrives as an int, out the fall-through.
    YSE::PATCHER::gRoute route;
    route.SetParams("10 20");
    REQUIRE(route.NumOutputs() == 3);

    MultiSink s10, s20, sDefault;
    route.ConnectOutlet(s10.GetInlet(0), 0);
    s10.ConnectInlet(route.GetOutlet(0), 0);
    route.ConnectOutlet(s20.GetInlet(0), 1);
    s20.ConnectInlet(route.GetOutlet(1), 0);
    route.ConnectOutlet(sDefault.GetInlet(0), 2);
    sDefault.ConnectInlet(route.GetOutlet(2), 0);

    route.GetInlet(0)->SetInt(10, YSE::T_GUI);
    CHECK(s10.gotBang);
    CHECK_FALSE(s10.gotInt);
    CHECK_FALSE(s20.gotBang);
    CHECK_FALSE(sDefault.gotBang);

    s10.reset();
    s20.reset();
    sDefault.reset();
    route.GetInlet(0)->SetInt(20, YSE::T_GUI);
    CHECK(s20.gotBang);
    CHECK_FALSE(s20.gotInt);

    s10.reset();
    s20.reset();
    sDefault.reset();
    route.GetInlet(0)->SetInt(99, YSE::T_GUI);
    CHECK(sDefault.gotInt);
    CHECK(sDefault.intValue == 99);
  }

  TEST_CASE("gRoute: 'bang' keyword in the list routes bangs") {
    YSE::PATCHER::gRoute route;
    route.SetParams("bang xyz");
    REQUIRE(route.NumOutputs() == 3);

    MultiSink sBang, sXyz, sDefault;
    route.ConnectOutlet(sBang.GetInlet(0), 0);
    sBang.ConnectInlet(route.GetOutlet(0), 0);
    route.ConnectOutlet(sXyz.GetInlet(0), 1);
    sXyz.ConnectInlet(route.GetOutlet(1), 0);
    route.ConnectOutlet(sDefault.GetInlet(0), 2);
    sDefault.ConnectInlet(route.GetOutlet(2), 0);

    route.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(sBang.gotBang);
    CHECK_FALSE(sDefault.gotBang);
  }

  TEST_CASE("gRoute: bang without a 'bang' key falls to the default outlet") {
    YSE::PATCHER::gRoute route;
    route.SetParams("10 20");
    MultiSink s10, s20, sDefault;
    route.ConnectOutlet(s10.GetInlet(0), 0);
    s10.ConnectInlet(route.GetOutlet(0), 0);
    route.ConnectOutlet(s20.GetInlet(0), 1);
    s20.ConnectInlet(route.GetOutlet(1), 0);
    route.ConnectOutlet(sDefault.GetInlet(0), 2);
    sDefault.ConnectInlet(route.GetOutlet(2), 0);

    route.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(s10.gotBang);
    CHECK_FALSE(s20.gotBang);
    CHECK(sDefault.gotBang);
  }

  TEST_CASE("gRoute: list with matching first token routes by token, minus the token") {
    // The matched token is consumed (#672): Max's "the rest of the message is
    // sent out the outlet that corresponds to that argument".  An unmatched
    // message is passed on whole, which is what lets .route objects chain.
    YSE::PATCHER::gRoute route;
    route.SetParams("foo bar");
    MultiSink sFoo, sBar, sDefault;
    route.ConnectOutlet(sFoo.GetInlet(0), 0);
    sFoo.ConnectInlet(route.GetOutlet(0), 0);
    route.ConnectOutlet(sBar.GetInlet(0), 1);
    sBar.ConnectInlet(route.GetOutlet(1), 0);
    route.ConnectOutlet(sDefault.GetInlet(0), 2);
    sDefault.ConnectInlet(route.GetOutlet(2), 0);

    route.GetInlet(0)->SetList("foo 1 2 3", YSE::T_GUI);
    CHECK(sFoo.gotList);
    CHECK(sFoo.listValue == "1 2 3");

    sFoo.reset();
    sBar.reset();
    sDefault.reset();
    route.GetInlet(0)->SetList("bar hello", YSE::T_GUI);
    CHECK(sBar.gotList);
    CHECK(sBar.listValue == "hello");

    sFoo.reset();
    sBar.reset();
    sDefault.reset();
    route.GetInlet(0)->SetList("zzz nope", YSE::T_GUI);
    CHECK(sDefault.gotList);
    CHECK(sDefault.listValue == "zzz nope");
  }

  TEST_CASE("gRoute: a float selector is matched by value, not by its spelling") {
    // This case used to assert the defect rather than the behaviour: the object
    // compared std::to_string(value) against the selector text, so the only
    // selector that could ever match the float 1.5 was the one spelled
    // "1.500000".  Since #672 the selector is read as a number, so the spelling
    // a patch author would actually type matches — and the old six-decimal
    // spelling still matches too, because both read as the same value.
    YSE::PATCHER::gRoute route;
    route.SetParams("1.5");
    REQUIRE(route.NumOutputs() == 2);

    MultiSink sMatch, sDefault;
    route.ConnectOutlet(sMatch.GetInlet(0), 0);
    sMatch.ConnectInlet(route.GetOutlet(0), 0);
    route.ConnectOutlet(sDefault.GetInlet(0), 1);
    sDefault.ConnectInlet(route.GetOutlet(1), 0);

    // The number was the whole message, so a match leaves as a bang.
    route.GetInlet(0)->SetFloat(1.5f, YSE::T_GUI);
    CHECK(sMatch.gotBang);
    CHECK_FALSE(sDefault.gotBang);
    CHECK_FALSE(sDefault.gotFloat);

    sMatch.reset();
    sDefault.reset();
    route.GetInlet(0)->SetFloat(2.0f, YSE::T_GUI);
    CHECK(sDefault.gotFloat);
    CHECK(sDefault.floatValue == doctest::Approx(2.0f));

    // std::to_string(1.5f) yields "1.500000" on every platform we ship to, and
    // it reads back as the same number.
    YSE::PATCHER::gRoute spelled;
    spelled.SetParams(std::to_string(1.5f));
    MultiSink sSpelled, sSpelledDefault;
    spelled.ConnectOutlet(sSpelled.GetInlet(0), 0);
    sSpelled.ConnectInlet(spelled.GetOutlet(0), 0);
    spelled.ConnectOutlet(sSpelledDefault.GetInlet(0), 1);
    sSpelledDefault.ConnectInlet(spelled.GetOutlet(1), 0);

    spelled.GetInlet(0)->SetFloat(1.5f, YSE::T_GUI);
    CHECK(sSpelled.gotBang);
    CHECK_FALSE(sSpelledDefault.gotBang);
  }

  // ─── gSwitch ──────────────────────────────────────────────────────────────────

  TEST_CASE("gSwitch: type name and default input/output count") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_SWITCH);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".switch");
    // selector + 2 message inlets
    CHECK(h->GetInputs() == 3);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::ANY);
  }

  TEST_CASE("gSwitch: activeInlet=0 swallows messages from all inlets") {
    YSE::PATCHER::gSwitch sw;
    MultiSink sink;
    sw.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(sw.GetOutlet(0), 0);

    sw.GetInlet(1)->SetBang(YSE::T_GUI);
    sw.GetInlet(2)->SetInt(7, YSE::T_GUI);

    CHECK_FALSE(sink.gotBang);
    CHECK_FALSE(sink.gotInt);
  }

  TEST_CASE("gSwitch: only the active inlet forwards messages") {
    YSE::PATCHER::gSwitch sw;
    MultiSink sink;
    sw.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(sw.GetOutlet(0), 0);

    sw.GetInlet(0)->SetInt(1, YSE::T_GUI); // active inlet 1
    sw.GetInlet(2)->SetBang(YSE::T_GUI);
    sw.GetInlet(2)->SetInt(99, YSE::T_GUI);
    CHECK_FALSE(sink.gotBang);
    CHECK_FALSE(sink.gotInt);

    sw.GetInlet(1)->SetBang(YSE::T_GUI);
    sw.GetInlet(1)->SetInt(5, YSE::T_GUI);
    sw.GetInlet(1)->SetFloat(0.25f, YSE::T_GUI);
    sw.GetInlet(1)->SetList("hi", YSE::T_GUI);
    CHECK(sink.gotBang);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 5);
    CHECK(sink.gotFloat);
    CHECK(sink.floatValue == doctest::Approx(0.25f));
    CHECK(sink.gotList);
    CHECK(sink.listValue == "hi");
  }

  TEST_CASE("gSwitch: SetParams grows the number of message inlets") {
    YSE::PATCHER::gSwitch sw;
    REQUIRE(sw.NumInputs() == 3);
    sw.SetParams("4");
    // selector + 4 message inlets
    CHECK(sw.NumInputs() == 5);

    MultiSink sink;
    sw.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(sw.GetOutlet(0), 0);

    sw.GetInlet(0)->SetInt(4, YSE::T_GUI);
    sw.GetInlet(4)->SetInt(42, YSE::T_GUI);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 42);
  }

  // ─── gReceive ─────────────────────────────────────────────────────────────────

  TEST_CASE("gReceive: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_RECEIVE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".r");
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::ANY);
  }

  TEST_CASE("gReceive: passes bang/int/float/list through to its outlet") {
    YSE::PATCHER::gReceive recv;
    MultiSink sink;
    recv.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(recv.GetOutlet(0), 0);

    recv.GetInlet(0)->SetBang(YSE::T_GUI);
    recv.GetInlet(0)->SetInt(3, YSE::T_GUI);
    recv.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    recv.GetInlet(0)->SetList("alpha", YSE::T_GUI);

    CHECK(sink.gotBang);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 3);
    CHECK(sink.gotFloat);
    CHECK(sink.floatValue == doctest::Approx(0.5f));
    CHECK(sink.gotList);
    CHECK(sink.listValue == "alpha");
  }

  // ─── gSend + gReceive (parent-coupled) ────────────────────────────────────────

  TEST_CASE("gSend: type name, single inlet, no outlets") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_SEND);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".s");
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 0);
  }

  TEST_CASE("gSend: without a parent patcher, sending data does not crash") {
    YSE::PATCHER::gSend send;
    // parent is nullptr; all four paths must short-circuit safely.
    send.GetInlet(0)->SetBang(YSE::T_GUI);
    send.GetInlet(0)->SetInt(1, YSE::T_GUI);
    send.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    send.GetInlet(0)->SetList("x", YSE::T_GUI);
  }

  TEST_CASE("gSend -> gReceive: matching dataName carries bang/int/float/list") {
    // Driven from the control thread, so gSend's PassData fan-out is deferred to
    // the audio thread (issue #225). Use patcherImplementation directly so the
    // value queue can be drained with an explicit Calculate.
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    YSE::pHandle* send = p.CreateObject(YSE::OBJ::G_SEND, "channelA");
    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "channelA");
    REQUIRE(send != nullptr);
    REQUIRE(recv != nullptr);

    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    p.Connect(recv, 0, &sinkHandle, 0);

    send->SetBang(0);
    send->SetIntData(0, 11);
    send->SetFloatData(0, 0.125f);
    send->SetListData(0, "msg body");

    // Drain the in-patcher value queue. (When the global bus is active gSend
    // also delivers via the bus, but this Calculate covers the local path too;
    // deferral itself is covered by test_patcher_value_queue.)
    p.Calculate(YSE::T_DSP);

    CHECK(sink.gotBang);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 11);
    CHECK(sink.gotFloat);
    CHECK(sink.floatValue == doctest::Approx(0.125f));
    CHECK(sink.gotList);
    CHECK(sink.listValue == "msg body");
  }

  TEST_CASE("gSend -> gReceive: mismatched dataName drops messages silently") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* send = p.CreateObject(YSE::OBJ::G_SEND, "left");
    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "right");
    REQUIRE(send != nullptr);
    REQUIRE(recv != nullptr);

    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    p.Connect(recv, 0, &sinkHandle, 0);

    send->SetIntData(0, 7);
    CHECK_FALSE(sink.gotInt);
  }

  // ─── gMetro ───────────────────────────────────────────────────────────────────

  TEST_CASE("gMetro: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_METRO);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".metro");
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BANG);
  }

  TEST_CASE("gMetro: period is set from int constructor parameter") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_METRO, "500");
    REQUIRE(h != nullptr);
    CHECK(h->GetParams() == "500");
  }

  TEST_CASE("gMetro: toggle=1 fires immediate bang, toggle=0 stops") {
    YSE::PATCHER::gMetro metro;
    BangSink sink;
    metro.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(metro.GetOutlet(0), 0);

    // Long period — we only assert the immediate bang at start.
    metro.GetInlet(1)->SetInt(1'000'000, YSE::T_GUI);
    metro.GetInlet(0)->SetInt(1, YSE::T_GUI); // start; immediate bang
    CHECK(sink.bangCount >= 1);
    int countAfterStart = sink.bangCount;

    metro.GetInlet(0)->SetInt(0, YSE::T_GUI); // stop
    // bang count must not advance from the immediate bang any further.
    CHECK(sink.bangCount == countAfterStart);
  }

  TEST_CASE("gMetro: SetFloatPeriod and SetIntPeriod do not start a timer") {
    YSE::PATCHER::gMetro metro;
    BangSink sink;
    metro.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(metro.GetOutlet(0), 0);

    metro.GetInlet(1)->SetFloat(750.5f, YSE::T_GUI);
    metro.GetInlet(1)->SetInt(250, YSE::T_GUI);
    // No public period accessor — verify only that period inlets never emit a
    // bang on their own.  Start/stop is exercised in the previous test.
    CHECK(sink.bangCount == 0);
  }

  TEST_CASE("gMetro: double-start clears the previous timer before starting again") {
    YSE::PATCHER::gMetro metro;
    BangSink sink;
    metro.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(metro.GetOutlet(0), 0);

    metro.GetInlet(1)->SetInt(1'000'000, YSE::T_GUI);
    metro.GetInlet(0)->SetInt(1, YSE::T_GUI); // first start: +1 bang
    metro.GetInlet(0)->SetInt(1, YSE::T_GUI); // re-start while running: +1 bang
    CHECK(sink.bangCount >= 2);

    metro.GetInlet(0)->SetInt(0, YSE::T_GUI); // stop before destruction
  }

  // ─── gMetro: live period changes (issue #625) ────────────────────────────────
  //
  // The timer used to keep the interval it was scheduled with, so every
  // documented way of setting the period was inert while the metro ran and only
  // a Toggle 0 → 1 cycle re-applied it.  The two routes into `period` behave
  // differently on purpose and both are pinned here:
  //
  //   * the cold inlet runs on the caller's thread and reschedules eagerly, so
  //     the new interval is in force before the call returns;
  //   * a live SetParams stores into `period` from the audio thread and can
  //     notify nobody, so Bang() picks it up — the running cycle finishes at the
  //     old interval and every cycle after it uses the new one.
  //
  // Neither restarts the metro: the phase is kept, so a tempo tweak does not
  // re-trigger whatever the bang drives.

  TEST_CASE("gMetro: shrinking the period through the cold inlet retimes a running metro (#625)") {
    YSE::PATCHER::gMetro metro;
    AtomicBangSink sink;
    metro.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(metro.GetOutlet(0), 0);

    // 5s cycle: with the old copy-at-start behaviour nothing can bang again
    // inside this test, so every tick observed below comes from the reschedule.
    metro.GetInlet(1)->SetInt(5000, YSE::T_GUI);
    metro.GetInlet(0)->SetInt(1, YSE::T_GUI); // start; immediate bang
    REQUIRE(sink.count() == 1);

    std::this_thread::sleep_for(30ms);
    metro.GetInlet(1)->SetInt(10, YSE::T_GUI);

    // 30ms of the cycle has already elapsed and the new interval is 10ms, so
    // the re-anchored expiry is in the past: the metro fires as soon as the
    // worker wakes instead of skipping the beat.
    CHECK(waitFor([&] { return sink.count() >= 2; }, 500));

    // ... and keeps the new rate: ~20 bangs fit in a 200ms window at 10ms,
    // against at most one at the original 5s.
    const int base = sink.count();
    std::this_thread::sleep_for(200ms);
    CHECK(sink.count() - base >= 6);

    metro.GetInlet(0)->SetInt(0, YSE::T_GUI); // stop before destruction
  }

  TEST_CASE("gMetro: growing the period through the cold inlet defers the next bang (#625)") {
    YSE::PATCHER::gMetro metro;
    AtomicBangSink sink;
    metro.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(metro.GetOutlet(0), 0);

    metro.GetInlet(1)->SetInt(10, YSE::T_GUI);
    metro.GetInlet(0)->SetInt(1, YSE::T_GUI);
    REQUIRE(waitFor([&] { return sink.count() >= 3; }, 1000));

    metro.GetInlet(1)->SetFloat(5000.f, YSE::T_GUI); // float inlet takes the same path
    std::this_thread::sleep_for(30ms); // let a tick already in flight land
    const int settled = sink.count();
    std::this_thread::sleep_for(150ms);
    CHECK(sink.count() == settled);

    metro.GetInlet(0)->SetInt(0, YSE::T_GUI);
  }

  TEST_CASE("gMetro: a live SetParams re-parse retimes a running metro (#625)") {
    // The user-visible flow behind yvanvds/phi#438: a metro wired up inside a
    // real patcher, started, and then re-argued from the object box while it
    // runs.  `period` is a scalar param, so SetParams takes the RT-safe queued
    // route and lands on the audio thread — no inlet handler is involved at all.
    YSE::PATCHER::patcherImplementation p(1, nullptr);
    YSE::pHandle* metro = p.CreateObject(YSE::OBJ::G_METRO, "150");
    REQUIRE(metro != nullptr);

    AtomicBangSink sink;
    YSE::pHandle hSink(&sink);
    p.Connect(metro, 0, &hSink, 0);
    p.Calculate(YSE::T_DSP);

    metro->SetIntData(0, 1); // start; immediate bang
    REQUIRE(sink.count() == 1);

    metro->SetParams("10");
    CHECK(metro->GetParams() == "10");
    p.Calculate(YSE::T_DSP); // audio thread applies the queued scalar store

    // The cycle in flight still runs out at 150ms; from the tick that ends it
    // the metro is on 10ms.  Wait for that tick, then measure the rate.
    REQUIRE(waitFor([&] { return sink.count() >= 2; }, 2000));
    const int base = sink.count();
    std::this_thread::sleep_for(200ms);
    CHECK(sink.count() - base >= 6); // ~20 at 10ms; at most 2 at the old 150ms

    metro->SetIntData(0, 0);
  }

  // ─── gMetro: destroying a running metro (issue #663) ─────────────────────────
  //
  // ~gMetro used to call ClearTimer on `timerThread()` — the lowercase *class*,
  // value-constructed as a throwaway on the stack — instead of the
  // `TimerThread()` singleton that actually owns the timer.  The throwaway had
  // never issued the id, so it dropped nothing, and the real timer outlived the
  // object: the worker kept invoking Bang(), and with it
  // `outputs[0].SendBang()`, on a freed pObject.
  //
  // The patcher's own teardown routes (DeleteObject, Clear, and the patcher
  // destructor through Clear) special-case G_METRO and push Toggle 0 first, so
  // they enter the dtor with id==0 and never reach the branch.  A standalone
  // metro — the object exactly as embedding code holds it — has no such guard,
  // and that is the user-visible flow pinned here.
  //
  // The name carries the `concurrency:` prefix on purpose: that is the selector
  // the sanitizer CI legs run (`yse_tests --test-case=concurrency:*`, see
  // Tests/CMakeLists.txt and the tests-asan / tests-tsan presets), and this is a
  // teardown-versus-worker-thread lifetime bug — exactly what that gate is for.
  // Without the prefix the case would only ever run in a plain build, where the
  // orphaned callback reads freed memory silently.  It still runs under
  // `yse_tests_patcher` too, since it is in the `patcher` suite.

  TEST_CASE("concurrency: gMetro destroyed while running stops its timer (#663)") {
    // Declared before the metro so it outlives it: nothing the timer might
    // still deliver can land on a dead sink and confuse the diagnosis.
    AtomicBangSink sink;
    const std::size_t timersBefore = YSE::PATCHER::TimerThread().size();

    {
      // Heap-allocated so ASan sees a real free rather than a stack frame that
      // happens to be reused.
      auto metro = std::make_unique<YSE::PATCHER::gMetro>();
      metro->ConnectOutlet(sink.GetInlet(0), 0);
      sink.ConnectInlet(metro->GetOutlet(0), 0);

      metro->GetInlet(1)->SetInt(10, YSE::T_GUI);
      metro->GetInlet(0)->SetInt(1, YSE::T_GUI); // start; immediate bang
      // Wait for real timer-thread ticks, not just the immediate bang, so the
      // worker is demonstrably armed and firing at the moment of destruction.
      REQUIRE(waitFor([&] { return sink.count() >= 3; }, 1000));
      REQUIRE(YSE::PATCHER::TimerThread().size() == timersBefore + 1);

      metro.reset(); // destroy while running — no Toggle 0
    }

    // The singleton dropped the timer.  This is the assertion that fails
    // deterministically on the unfixed code (1 == 0): the id stayed in the
    // singleton's active map because a throwaway instance was asked to remove
    // it.  Under ASan the orphaned callback additionally reports
    // heap-use-after-free on the freed metro.
    CHECK(YSE::PATCHER::TimerThread().size() == timersBefore);

    // ClearTimer also blocks out any Bang() still in flight, so by here no
    // callback can be touching the object at all — at 10ms a surviving timer
    // would have fired roughly twenty more times inside this window.
    const int settled = sink.count();
    std::this_thread::sleep_for(200ms);
    CHECK(sink.count() == settled);
  }

} // TEST_SUITE("patcher")
