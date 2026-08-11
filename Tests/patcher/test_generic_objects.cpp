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
//
// What the cases that let that timer deliver may *not* do is bet on how many
// ticks the machine produces inside a wall-clock budget, in either direction —
// neither "three bangs inside 500 ms", which a saturated box falsifies while
// the object behaves perfectly, nor "nothing more inside 200 ms", which the
// same box makes vacuously true.  Everything joinable is joined instead
// (`timerBridge::WaitIdle()`, and `timerThread::size()` after a control-thread
// toggle, which takes the bridge's blocking `ClearTimer` front), and delivery
// itself — which has no handshake — is paced against a reference timer armed on
// the same worker at the same period.  See `support/timer_pacing.hpp` and issue
// #752.

#include <doctest/doctest.h>
#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/genericObjects/gGate.h"
#include "patcher/genericObjects/gRoute.h"
#include "patcher/genericObjects/gSwitch.h"
#include "patcher/genericObjects/gReceive.h"
#include "patcher/genericObjects/gSend.h"
#include "patcher/time/TimerThread.h"
#include "patcher/time/gMetro.h"
#include "patcher/time/timerBridge.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"
#include "support/timer_pacing.hpp"

using TestHelpers::BangSink;
using TestHelpers::MultiSink;

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

  // A sink that switches the metro feeding it off from inside the very tick
  // that delivered the bang — an object in the patch stopping the metronome
  // (issue #721).  On the millisecond engine that handler runs on the timer
  // worker, inside `gMetro::Bang`, which is the case the object has to net out
  // and publish rather than perform.
  struct SelfStoppingSink : YSE::PATCHER::pObject {
    std::atomic<int> bangCount{0};
    YSE::pHandle* metro = nullptr;
    int stopAt = 0;

    SelfStoppingSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) {
        const int n = bangCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (metro != nullptr && n == stopAt) metro->SetIntData(0, 0);
      });
    }
    const char* Type() const override {
      return "self_stopping_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
    int count() const {
      return bangCount.load(std::memory_order_relaxed);
    }
  };

  // Pacing a case against the machine rather than against a clock — the
  // instrument every `.metro` case below that lets the real `timerThread`
  // deliver is written against, and the whole of issue #752. The reasoning
  // lives in `support/timer_pacing.hpp`; the short version is that a reference
  // timer armed on the same worker at the same period lets a case wait for a
  // number of the machine's own *ticks* instead of a number of milliseconds,
  // and that two of them armed either side of the object's own timer bound its
  // output exactly. It replaced a `waitFor(pred, budgetMs)` here, which asserted
  // that the OS millisecond timer delivered N ticks inside a wall-clock budget —
  // a statement about the scheduler and not about `.metro`.
  using TestHelpers::refTimer;
  using TestHelpers::waitPaced;

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

  // ─── gMetro: the rest of Max's left-inlet start/stop methods (issue #711) ────
  //
  // Max's reference page gives four, and until #711 this object had one.  These
  // cases pin the three that were missing on the millisecond engine; the beat
  // engine's answer — what a mid-run bang does to #705's grid — is in the clock
  // suite, where a domain clock can be stepped deterministically.

  TEST_CASE("gMetro: a bang starts the metronome (#711)") {
    // Max, left inlet: "starts the metro object", and its Output section: "bang
    // is sent immediately when metro is started."
    YSE::PATCHER::gMetro metro;
    BangSink sink;
    metro.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(metro.GetOutlet(0), 0);

    metro.GetInlet(1)->SetInt(1'000'000, YSE::T_GUI);
    metro.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(sink.bangCount == 1);

    metro.GetInlet(0)->SetInt(0, YSE::T_GUI); // stop before destruction
  }

  TEST_CASE("gMetro: a bang re-starts a metro that is already running (#711)") {
    // Max Basic Tutorial 4 (Metro and Toggle) is what settles this, the
    // reference page saying only "starts": "when a metro receives a bang, the
    // metro will 're-start' itself and begin scheduling subsequent bang
    // messages from the moment we triggered it."  So a bang into a running
    // metro is not swallowed — it bangs again, which is what lets one button
    // put several metros in sync.
    YSE::PATCHER::gMetro metro;
    BangSink sink;
    metro.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(metro.GetOutlet(0), 0);

    metro.GetInlet(1)->SetInt(1'000'000, YSE::T_GUI);
    metro.GetInlet(0)->SetInt(1, YSE::T_GUI); // start: +1
    metro.GetInlet(0)->SetBang(YSE::T_GUI); // re-start: +1
    metro.GetInlet(0)->SetBang(YSE::T_GUI); // and again: +1
    CHECK(sink.bangCount == 3);

    metro.GetInlet(0)->SetInt(0, YSE::T_GUI);
  }

  TEST_CASE("gMetro: 'stop' in the left inlet stops the metronome (#711)") {
    // Max: "in left inlet: stops metro" — int 0 under another name.  A running
    // metro bangs from the TimerThread worker, so the counter has to be the
    // atomic one.
    YSE::PATCHER::gMetro metro;
    AtomicBangSink sink;
    metro.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(metro.GetOutlet(0), 0);

    metro.GetInlet(1)->SetInt(5, YSE::T_GUI);
    const std::size_t before = YSE::PATCHER::TimerThread().size();
    metro.GetInlet(0)->SetInt(1, YSE::T_GUI);
    // A start from the control thread takes the *blocking* half of the bridge,
    // so the timer is armed inline: that it exists is a fact on return rather
    // than something to wait for.
    REQUIRE(YSE::PATCHER::TimerThread().size() == before + 1);

    // Really running before the word arrives, so what follows is the word's
    // doing and not a timer that never started. Paced against the worker's own
    // deliveries rather than against a budget (#752): this reference is younger
    // than the metro's timer, so every tick of it is a deadline that timer has
    // already passed.
    refTimer younger(5);
    REQUIRE(younger.WaitTicks(3));
    const int atLeast = younger.n();
    REQUIRE(sink.count() >= atLeast);

    metro.GetInlet(0)->SetList("stop", YSE::T_GUI);
    const int afterStop = sink.count();
    // The word reached the same blocking front, so the timer is gone from the
    // worker's queue on return rather than merely told to be quiet: only the
    // reference is left.
    CHECK(YSE::PATCHER::TimerThread().size() == before + 1);
    YSE::PATCHER::TimerBridge().WaitIdle();
    CHECK(sink.count() == afterStop);

    // And the worker kept firing throughout, so the bangs ending is this metro
    // having stopped rather than the machine having gone quiet — a 5 ms timer
    // still armed would have advanced ten more times over this window, and the
    // window stretches with the load instead of expiring under it.
    REQUIRE(younger.WaitTicks(atLeast + 10));
    CHECK(sink.count() == afterStop);
  }

  TEST_CASE("gMetro: 'stop' in the right inlet is not a command (#711)") {
    // Max documents stop as a left-inlet method.  On the right inlet the word
    // is only a token that is not a time value, and the object's list handler
    // already ignores those — so the metro must keep running.
    YSE::PATCHER::gMetro metro;
    AtomicBangSink sink;
    metro.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(metro.GetOutlet(0), 0);

    metro.GetInlet(1)->SetInt(5, YSE::T_GUI);
    const std::size_t before = YSE::PATCHER::TimerThread().size();
    metro.GetInlet(0)->SetInt(1, YSE::T_GUI);
    REQUIRE(YSE::PATCHER::TimerThread().size() == before + 1);

    refTimer younger(5); // younger than the metro's timer — see the note above
    REQUIRE(younger.WaitTicks(3));
    const int atLeast = younger.n();
    REQUIRE(sink.count() >= atLeast);

    metro.GetInlet(1)->SetList("stop", YSE::T_GUI);
    const int afterWord = sink.count();
    // Still armed — the word reached no handler that could retire it — and
    // still delivering. The second half is budgeted in the worker's own ticks
    // rather than in milliseconds (#752): twelve of them at the metro's period
    // is ample room for the three bangs asked for, on any box.
    CHECK(YSE::PATCHER::TimerThread().size() == before + 2);
    CHECK(waitPaced(younger, 12, [&] { return sink.count() > afterWord + 2; }));

    metro.GetInlet(0)->SetInt(0, YSE::T_GUI); // stop before destruction
  }

  TEST_CASE("gMetro: a float in the left inlet starts and stops, uncast (#711)") {
    // Max: "float — performs the same function as int", and int's rule is "any
    // number other than 0 starts".  0.5 is a number other than 0, so it starts;
    // a metro that cast its float to int would stop instead.
    YSE::PATCHER::gMetro metro;
    BangSink sink;
    metro.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(metro.GetOutlet(0), 0);

    metro.GetInlet(1)->SetInt(1'000'000, YSE::T_GUI);
    metro.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(sink.bangCount == 1);

    metro.GetInlet(0)->SetFloat(0.f, YSE::T_GUI); // and 0. stops it
    CHECK(sink.bangCount == 1);
    metro.GetInlet(0)->SetFloat(-2.5f, YSE::T_GUI); // "other than 0" includes negatives
    CHECK(sink.bangCount == 2);

    metro.GetInlet(0)->SetInt(0, YSE::T_GUI);
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

    {
      // Let part of the cycle elapse, measured in the worker's own 10 ms
      // deliveries: three of them is at least 30 ms of the 5 s cycle gone,
      // which is what puts the re-anchored expiry below in the past.
      refTimer elapsed(10);
      REQUIRE(elapsed.WaitTicks(3));

      metro.GetInlet(1)->SetInt(10, YSE::T_GUI);

      // 30ms of the cycle has already elapsed and the new interval is 10ms, so
      // the re-anchored expiry is in the past: the metro fires as soon as the
      // worker wakes instead of skipping the beat. `timerThread::SetPeriod`
      // clamps such an expiry to *now*, so it sorts ahead of this reference's
      // own pending deadline and the worker takes it first — six of the
      // reference's ticks is a generous budget for something due before its
      // next one (#752), and not a wall-clock window.
      CHECK(waitPaced(elapsed, 6, [&] { return sink.count() >= 2; }));
    }

    // ... and keeps the new rate. A reference of the metro's *new* period armed
    // after the count is read is an exact lower bound on how far that count
    // moves: the metro's next deadline is at most one period out, so its i-th
    // delivery from here is due no later than this reference's i-th and the one
    // worker fires them in deadline order. At the original 5 s the count could
    // not move at all. Twenty deliveries rather than "≥6 in a 200 ms window",
    // which is the bet #752 is about — that window held six on a quiet box and
    // regularly none on a saturated one.
    const int base = sink.count();
    refTimer younger(10);
    REQUIRE(younger.WaitTicks(20));
    const int paced = younger.n();
    CHECK(sink.count() - base >= paced);

    metro.GetInlet(0)->SetInt(0, YSE::T_GUI); // stop before destruction
  }

  TEST_CASE("gMetro: growing the period through the cold inlet defers the next bang (#625)") {
    YSE::PATCHER::gMetro metro;
    AtomicBangSink sink;
    metro.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(metro.GetOutlet(0), 0);

    metro.GetInlet(1)->SetInt(10, YSE::T_GUI);
    metro.GetInlet(0)->SetInt(1, YSE::T_GUI);

    // Really running at 10 ms before the period is grown, paced against a
    // reference of that period armed after the metro's own timer (#752).
    refTimer younger(10);
    REQUIRE(younger.WaitTicks(3));
    const int atLeast = younger.n();
    REQUIRE(sink.count() >= atLeast);

    metro.GetInlet(1)->SetFloat(5000.f, YSE::T_GUI); // float inlet takes the same path
    const int settled = sink.count();

    // The cold inlet reschedules eagerly on the caller's thread, so the 5 s
    // expiry is in force by the time that call returns; the one thing that may
    // still be outstanding is a callback already in flight, whose bang lands
    // after it. Fifteen ticks of the 10 ms reference is the join for that — the
    // worker is serial, so a callback in flight then has certainly returned —
    // and at the same time the window the metro would have banged fifteen more
    // times in had the growth not taken. Neither half is a sleep: the sleeps
    // this replaces bet that 30 ms was enough for the in-flight tick and that
    // nothing arrived in 150 ms, and a loaded box breaks the first and makes
    // the second vacuous (#752).
    REQUIRE(younger.WaitTicks(younger.n() + 15));
    CHECK(sink.count() <= settled + 1);

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
    // the metro is on 10ms.  Wait for that tick, then measure the rate — both
    // budgeted in a reference timer's own deliveries rather than in
    // milliseconds (#752).
    //
    // Twenty ticks of a 10 ms reference armed after the start is not a guess:
    // its 16th deadline is at least 160 ms past an arming that already came
    // after the metro's 150 ms one, so the worker takes the metro's first
    // before it, on any box.
    refTimer ref(10);
    REQUIRE(waitPaced(ref, 20, [&] { return sink.count() >= 2; }));

    // That tick has *asked* for the new interval, and asking is all it can do:
    // `Bang()` runs on the timer worker, so it publishes a `RequestPeriod` and
    // the reconcile that reaches `timerThread::SetPeriod` happens a background
    // pool hop later. Until then the metro is still a 150 ms metronome. The
    // request is issued before the bang that this thread just observed, so
    // `WaitIdle()` is a join on that reconcile and the 10 ms interval is a fact
    // on return.
    //
    // The 200 ms sleep this replaces was covering that hop by accident, which
    // is why it needed only six bangs where twenty were due: on #752's
    // saturated box the pool hop alone outlasted the window and the case failed
    // 9 runs in 10 against a reference-paced count.
    YSE::PATCHER::TimerBridge().WaitIdle();

    // From here the metro is a 10 ms metronome, so a 10 ms reference armed
    // after the count is read bounds how far that count moves from below —
    // at the old 150 ms it could move at most twice.
    const int base = sink.count();
    refTimer younger(10);
    REQUIRE(younger.WaitTicks(20));
    const int paced = younger.n();
    CHECK(sink.count() - base >= paced);

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
      REQUIRE(YSE::PATCHER::TimerThread().size() == timersBefore + 1);

      {
        // Real timer-thread ticks, not just the immediate bang, so the worker
        // is demonstrably armed and firing at the moment of destruction —
        // counted in the worker's own deliveries rather than inside a 1000 ms
        // budget (#752). Armed after the metro's timer, so every tick of it is
        // a deadline that timer has already passed; scoped so it is retired
        // before the size() checks that follow.
        refTimer younger(10);
        REQUIRE(younger.WaitTicks(3));
        const int atLeast = younger.n();
        REQUIRE(sink.count() >= atLeast);
      }
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
    // would have fired twenty more times inside this window. The window is
    // twenty of the worker's own 10 ms deliveries rather than 200 ms of
    // wall clock, so a saturated box stretches it instead of making it
    // vacuously quiet (#752).
    const int settled = sink.count();
    refTimer after(10);
    REQUIRE(after.WaitTicks(20));
    CHECK(sink.count() == settled);
  }

  // ─── gMetro: toggling from the audio callback (issue #718) ───────────────────
  //
  // A patcher message handler runs on whichever thread dispatched the message,
  // and a `.metro` fed by a `.delay` is dispatched from
  // `patcherImplementation::Calculate` — `messageScheduler::DeliverDue` is the
  // first thing a block does.  Everything `timerThread` offers is forbidden
  // there: `Add` takes a mutex and allocates two container nodes plus the
  // caller's std::function, `SetPeriod` takes the same mutex, and `ClearTimer`
  // blocks on a condition variable until an in-flight callback returns.  Before
  // #718 the toggle called all three directly.
  //
  // The allocation half is what the probe can pin, and it pins it hard: on the
  // unfixed object the unordered_map and multiset node allocations happen
  // unconditionally, so `Add` alone puts the count at two or more.  The lock and
  // the block are the same call and cannot be observed separately here; the
  // sanitizer legs are where a mutex on this path shows up.
  //
  // The probe counts only the arming thread (issue #701), so Calculate is driven
  // on the test thread — which is exactly right: the *background* reconcile does
  // allocate, legitimately, and a probe that saw it would be measuring the wrong
  // thread.

  namespace {
    // A `.delay` wired into a `.metro`'s left inlet inside a real patcher: the
    // shortest patch that puts a metro toggle on the audio callback.
    struct DeferredToggleRig {
      YSE::PATCHER::patcherImplementation patcher{1, nullptr};
      AtomicBangSink sink;
      YSE::pHandle sinkHandle{&sink};
      YSE::pHandle* delay = nullptr;
      YSE::pHandle* metro = nullptr;

      DeferredToggleRig(const char* metroArgs) {
        // `delay 0` still defers a whole block — messageScheduler's floor — so
        // the bang lands in the *next* Calculate, on the audio thread.
        delay = patcher.CreateObject(YSE::OBJ::G_DELAY, "0");
        metro = patcher.CreateObject(YSE::OBJ::G_METRO, metroArgs);
        REQUIRE(delay != nullptr);
        REQUIRE(metro != nullptr);
        patcher.Connect(delay, 0, metro, 0);
        patcher.Connect(metro, 0, &sinkHandle, 0);
        patcher.Calculate(YSE::T_DSP); // publish the graph before anything arms
      }

      // Arm the deferral from the control thread, then run the block that
      // delivers it. Returns with the metro toggled from inside Calculate.
      void ArmToggle() {
        delay->SetBang(0);
      }
      void DeliverBlock() {
        patcher.Calculate(YSE::T_DSP);
      }
    };
  } // namespace

  TEST_CASE("gMetro: a toggle delivered on the audio callback allocates nothing (#718)") {
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    DeferredToggleRig rig("1000");

    // Start, delivered inside Calculate.
    rig.ArmToggle();
    {
      TestHelpers::ProbeScope probe;
      rig.DeliverBlock();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    // An assertion that only proves nothing happened proves nothing: the metro
    // really was started, and Max's immediate bang really came out.
    REQUIRE(rig.sink.count() == 1);

    // A second delivery into a *running* metro re-starts it (#711), which is the
    // heavier half of the same path: the toggle drops the armed timer and arms a
    // new one, so it reaches `ClearTimer` and `Add` both.
    rig.ArmToggle();
    {
      TestHelpers::ProbeScope probe;
      rig.DeliverBlock();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(rig.sink.count() == 2);

    rig.metro->SetIntData(0, 0); // stop before teardown, from the control thread
  }

  TEST_CASE("gMetro: a cold-inlet period change delivered on the audio callback retimes (#718)") {
    // The cold inlet is the third way onto `timerThread` from a handler, and it
    // is reachable on the callback for the same reason the toggle is: here a
    // `.delay` bangs an `.i` whose stored number lands in the metro's right
    // inlet, all inside one Calculate.
    //
    // The allocation probe is deliberately *not* the assertion here, and the
    // honest reason is that it cannot discriminate: `timerThread::SetPeriod`
    // takes the mutex but allocates nothing, so the pre-#718 code passes an
    // allocation check on this path.  What this case pins is that routing it
    // through the bridge did not quietly turn the retime into a no-op — the
    // request has to reach `SetPeriod` on the pool.  The lock itself is the
    // toggle case's business (where `Add` does allocate) and the sanitizers'.
    YSE::PATCHER::patcherImplementation p(1, nullptr);
    YSE::pHandle* delay = p.CreateObject(YSE::OBJ::G_DELAY, "0");
    YSE::pHandle* number = p.CreateObject(YSE::OBJ::G_INT, "40");
    YSE::pHandle* metro = p.CreateObject(YSE::OBJ::G_METRO, "5000");
    REQUIRE(delay != nullptr);
    REQUIRE(number != nullptr);
    REQUIRE(metro != nullptr);
    AtomicBangSink sink;
    YSE::pHandle sinkHandle(&sink);
    p.Connect(delay, 0, number, 0);
    p.Connect(number, 0, metro, 1);
    p.Connect(metro, 0, &sinkHandle, 0);
    p.Calculate(YSE::T_DSP);

    metro->SetIntData(0, 1); // running at 5s, from the control thread
    REQUIRE(sink.count() == 1);

    delay->SetBang(0);
    p.Calculate(YSE::T_DSP); // the 40ms lands from inside the block

    // At 5s nothing more can bang inside this test, so every tick below is the
    // retime's doing.
    YSE::PATCHER::TimerBridge().WaitIdle();
    const int base = sink.count();

    // Budgeted in a 40 ms reference's own deliveries rather than inside a
    // 2000 ms budget (#752). Armed after the count was read, so the metro's
    // retimed timer — whose next deadline is at most one 40 ms period out —
    // has its i-th delivery from here due no later than this reference's i-th,
    // and the one worker fires them in deadline order.
    refTimer younger(40);
    REQUIRE(younger.WaitTicks(3));
    const int paced = younger.n();
    CHECK(sink.count() - base >= paced);

    metro->SetIntData(0, 0);
  }

  TEST_CASE("gMetro: a metro started on the audio callback really runs (#718)") {
    // The deferral must not turn the start into a no-op: the timer is armed on
    // the background pool a hop later, and from there the metro is an ordinary
    // millisecond metronome.
    DeferredToggleRig rig("10");

    rig.ArmToggle();
    rig.DeliverBlock();
    REQUIRE(rig.sink.count() == 1); // Max's immediate bang, never deferred

    // WaitIdle makes the pool half deterministic instead of sleeping for it,
    // so the timer is armed as a fact on return — and the reference armed
    // straight after it is therefore younger than the metro's own, which makes
    // its ticks a lower bound on the metro's bangs rather than a 1000 ms bet
    // (#752).
    YSE::PATCHER::TimerBridge().WaitIdle();
    refTimer younger(10);
    const std::size_t armed = YSE::PATCHER::TimerThread().size(); // metro + reference
    REQUIRE(younger.WaitTicks(4));
    const int atLeast = younger.n();
    CHECK(rig.sink.count() >= atLeast);

    rig.metro->SetIntData(0, 0);
    // The stop comes from the control thread, so it takes the blocking half of
    // the bridge and `ClearTimer`'s handshake has been performed on return: the
    // timer is gone from the worker's queue rather than merely told to be
    // quiet, and only the reference is left.
    CHECK(YSE::PATCHER::TimerThread().size() == armed - 1);
    const int afterStop = rig.sink.count();
    // And the worker kept firing throughout, so the bangs ending is this metro
    // having stopped rather than the machine having gone quiet.
    REQUIRE(younger.WaitTicks(atLeast + 10));
    CHECK(rig.sink.count() == afterStop);
  }

  // ─── gMetro: an outlet wired back into its own left inlet (issue #721) ───────
  //
  // Max's metro banging itself re-phases it every tick — Max Basic Tutorial 4's
  // "the metro will 're-start' itself", applied to its own bang — so the patch
  // is a fast metronome.  It used to be a permanent hang, and not only for this
  // object: `gMetro::Bang()` runs *inside* the timer's callback on the timer
  // worker, its `SendBang` came back round to this object's own `Toggle`, and
  // the stop that starts every toggle reached `timerThread::ClearTimer` for the
  // id whose callback was on that very stack.  `destroyImpl` then waited on a
  // condition variable for `timer.destroyed`, which only the worker sets after
  // the callback returns — the worker being the thread now parked in the wait.
  // One timer worker serves the whole process, so every other `.metro` stopped
  // with it.  The fault is as old as the object (180147e) and unchanged by #718,
  // which only moved *which* thread calls `timerThread`.
  //
  // A test for a deadlock cannot be written as a blocking join, so the check is
  // a bounded poll: on a regression the count simply never moves and the CHECK
  // fails with a printed message rather than parking this thread.  It cannot
  // make a regression free — a parked timer worker takes teardown down with it,
  // `~gMetro`'s `Release` waiting on a callback that will never return — but the
  // failure is reported first, and ctest's TIMEOUT (300 s on yse_tests_patcher,
  // 600 s on yse_unit_tests) is what ends the run instead of a hang without end.

  TEST_CASE("gMetro: a metro wired into its own left inlet keeps ticking (#721)") {
    YSE::PATCHER::patcherImplementation p(1, nullptr);
    YSE::pHandle* metro = p.CreateObject(YSE::OBJ::G_METRO, "10");
    REQUIRE(metro != nullptr);

    AtomicBangSink sink;
    YSE::pHandle hSink(&sink);
    p.Connect(metro, 0, metro, 0); // the cord this issue is about
    p.Connect(metro, 0, &hSink, 0);
    p.Calculate(YSE::T_DSP);

    // Starting from the control thread already runs the cycle once: the start
    // bang re-enters Toggle, which stops and starts again, until #236's
    // send-depth ceiling breaks it at 64 frames.  That half never deadlocked —
    // no callback is in flight on this thread — and it is only the baseline.
    metro->SetIntData(0, 1);
    const int afterStart = sink.count();
    REQUIRE(afterStart >= 1);

    // The first *tick* is the one that used to park the worker forever, and
    // what budgets the wait for it is a reference timer of the metro's own
    // period rather than 2000 ms of wall clock (#752). That is the better
    // instrument for this case and not merely a safer one: a *busy* box simply
    // makes the reference tick slower and the case wait longer, while a
    // *wedged* worker — the regression this exists for — stops the reference
    // dead, and `waitPaced`'s stall guard reports that in five seconds instead
    // of sitting out a budget that cannot tell the two apart.
    refTimer ref(10);
    CHECK(waitPaced(ref, 20, [&] { return sink.count() > afterStart; }));

    // ... and it keeps ticking, rather than surviving one tick and stopping.
    const int base = sink.count();
    CHECK(waitPaced(ref, 20, [&] { return sink.count() > base; }));

    metro->SetIntData(0, 0); // stop from the control thread; keeps the handshake
    YSE::PATCHER::TimerBridge().WaitIdle();
    const int afterStop = sink.count();
    // Twenty of the worker's own deliveries, not 100 ms of wall clock: a
    // surviving timer at this period would have banged over that window, and
    // the window grows with the load instead of expiring under it.
    REQUIRE(ref.WaitTicks(ref.n() + 20));
    CHECK(sink.count() == afterStop);
  }

  TEST_CASE("gMetro: the self-bang cycle also survives a .t in the loop (#721)") {
    // Max's own phrasing of the same patch — "directly, or through any object
    // that passes the bang along".  The route back is not the metro's own
    // `SendBang` frame any more, so this pins that the fix keys on *which
    // object's callback this thread is inside* rather than on the send that
    // started it.
    YSE::PATCHER::patcherImplementation p(1, nullptr);
    YSE::pHandle* metro = p.CreateObject(YSE::OBJ::G_METRO, "10");
    YSE::pHandle* trig = p.CreateObject(YSE::OBJ::G_TRIGGER, "b");
    REQUIRE(metro != nullptr);
    REQUIRE(trig != nullptr);

    AtomicBangSink sink;
    YSE::pHandle hSink(&sink);
    p.Connect(metro, 0, trig, 0);
    p.Connect(trig, 0, metro, 0);
    p.Connect(metro, 0, &hSink, 0);
    p.Calculate(YSE::T_DSP);

    metro->SetIntData(0, 1);
    const int afterStart = sink.count();
    REQUIRE(afterStart >= 1);
    // Paced against the worker's own deliveries, for the reason the previous
    // case gives: a wedged worker stops the reference and is reported, where a
    // wall-clock budget could not tell it from a busy one (#752).
    refTimer ref(10);
    CHECK(waitPaced(ref, 20, [&] { return sink.count() > afterStart; }));

    metro->SetIntData(0, 0);
    YSE::PATCHER::TimerBridge().WaitIdle();
  }

  TEST_CASE("gMetro: a stop sent from inside the metro's own tick is honoured (#721)") {
    // The other half of the same frame.  A tick that ends with the metro running
    // has nothing to publish — the timer worker's own reschedule *is* that
    // state, and publishing "running" would overwrite a stop landing from
    // another thread — but a tick that ends stopped has to reach the bridge, or
    // that same reschedule puts the metro straight back on.  So the cycle's
    // verdict is recorded and issued once, after the send unwinds, which is what
    // makes it safe from a thread that may not block.
    YSE::PATCHER::patcherImplementation p(1, nullptr);
    YSE::pHandle* metro = p.CreateObject(YSE::OBJ::G_METRO, "10");
    REQUIRE(metro != nullptr);

    SelfStoppingSink sink;
    sink.metro = metro;
    sink.stopAt = 4; // bang 1 is the immediate one; 4 is the third timer tick
    YSE::pHandle hSink(&sink);
    p.Connect(metro, 0, &hSink, 0);
    p.Calculate(YSE::T_DSP);

    const std::size_t timersBefore = YSE::PATCHER::TimerThread().size();
    metro->SetIntData(0, 1);
    REQUIRE(sink.count() == 1);

    // Armed after the metro's own timer, so every tick of it is a deadline that
    // timer has already passed, and paced in those ticks rather than inside a
    // 2000 ms budget (#752). `timersBefore` was taken before either timer
    // existed, so `timersBefore + 1` below is this reference alone.
    refTimer younger(10);
    REQUIRE(waitPaced(younger, 20, [&] { return sink.count() >= sink.stopAt; }));

    // The disarm this asked for is not something to poll for either. `Bang()`
    // issues the `RequestStop` *before* the callback returns, and the one
    // worker runs callbacks serially — so the reference's next completed tick
    // is proof that the callback that stopped the metro has returned and that
    // its request is on the pool. `WaitIdle()` then joins the pool, and the
    // count below is a fact on return rather than a wait.
    REQUIRE(younger.WaitTicks(younger.n() + 1));
    YSE::PATCHER::TimerBridge().WaitIdle();

    // The singleton really dropped the timer, and this is the check that talks
    // on a regression: the deadlocked worker parks *inside* `destroyImpl`'s
    // wait, which releases `sync`, so `size()` still answers — with the id that
    // will never be retired.  Without it the case would pass its counting
    // assertions and hang silently in teardown instead. (On that regression the
    // `WaitTicks` above fails first, the reference being stopped with it.)
    CHECK(YSE::PATCHER::TimerThread().size() == timersBefore + 1);

    // The stop was the `stopAt`-th bang's doing rather than something that had
    // already happened.
    const int settled = sink.count();
    CHECK(settled >= sink.stopAt);

    // What must not happen is the metro carrying on: twenty of the worker's own
    // deliveries is twenty of the metro's periods' worth of window, and it
    // stretches with the load instead of expiring under it.
    //
    // What is *not* asserted any more is `settled <= sink.stopAt + 1` — "the
    // disarm lands a pool hop after the tick that asked for it, so one more
    // tick may come out". The deferral is #718's documented cost and is pinned
    // above; how many 10 ms ticks fit inside a background-pool hop is a fact
    // about the machine and not about the object, and on #752's saturated box
    // it was regularly more than one.
    REQUIRE(younger.WaitTicks(younger.n() + 20));
    CHECK(sink.count() == settled);
  }

} // TEST_SUITE("patcher")
