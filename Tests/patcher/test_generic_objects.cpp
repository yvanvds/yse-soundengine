// Tests for patcher generic + time objects: gGate, gRoute, gSwitch, gReceive,
// gSend, gMetro.  No audio device required.
//
// gSend is parent-coupled: its SetXValue handlers cast `parent` to
// `patcherImplementation*` and call PassData on it.  The realistic path is
// therefore exercised through a real `YSE::patcher` containing both gSend and
// gReceive with matching dataNames.
//
// gMetro spawns a background TimerThread tick.  Tests stop the metro before
// destruction (Toggle 0) so the dtor's id==0 branch runs and no temporary
// timerThread is constructed.

#include <doctest/doctest.h>
#include <string>
#include "patcher/patcher.hpp"
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
    CHECK(s10.gotInt);
    CHECK(s10.intValue == 10);
    CHECK_FALSE(s20.gotInt);
    CHECK_FALSE(sDefault.gotInt);

    s10.reset();
    s20.reset();
    sDefault.reset();
    route.GetInlet(0)->SetInt(20, YSE::T_GUI);
    CHECK(s20.gotInt);
    CHECK(s20.intValue == 20);

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

  TEST_CASE("gRoute: list with matching first token routes by token") {
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
    CHECK(sFoo.listValue == "foo 1 2 3");

    sFoo.reset();
    sBar.reset();
    sDefault.reset();
    route.GetInlet(0)->SetList("bar hello", YSE::T_GUI);
    CHECK(sBar.gotList);

    sFoo.reset();
    sBar.reset();
    sDefault.reset();
    route.GetInlet(0)->SetList("zzz nope", YSE::T_GUI);
    CHECK(sDefault.gotList);
    CHECK(sDefault.listValue == "zzz nope");
  }

  TEST_CASE("gRoute: float compared as its to_string representation") {
    YSE::PATCHER::gRoute route;
    // std::to_string(1.5f) yields "1.500000" on every platform we ship to.
    route.SetParams(std::to_string(1.5f));
    REQUIRE(route.NumOutputs() == 2);

    MultiSink sMatch, sDefault;
    route.ConnectOutlet(sMatch.GetInlet(0), 0);
    sMatch.ConnectInlet(route.GetOutlet(0), 0);
    route.ConnectOutlet(sDefault.GetInlet(0), 1);
    sDefault.ConnectInlet(route.GetOutlet(1), 0);

    route.GetInlet(0)->SetFloat(1.5f, YSE::T_GUI);
    CHECK(sMatch.gotFloat);
    CHECK_FALSE(sDefault.gotFloat);

    sMatch.reset();
    sDefault.reset();
    route.GetInlet(0)->SetFloat(2.0f, YSE::T_GUI);
    CHECK(sDefault.gotFloat);
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
    YSE::patcher p;
    p.create(2);
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

} // TEST_SUITE("patcher")
