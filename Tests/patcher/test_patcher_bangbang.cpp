// Tests for .bangbang (issue #467) — fan one input out as a bang from many
// outlets, right to left.
//
// .bangbang is .trigger's degenerate case: the payload is thrown away, so the
// *only* thing the object does is fire N outlets in a defined order. That makes
// the ordering guarantee carry even more of this file than it does #466's, and
// it is asserted in three ways that a "did every outlet fire?" test could not
// tell apart from a broken one:
//
//   - **the sequence itself**, through sinks that all log into one shared
//     buffer, so the log reads back as the exact order the outlets were served.
//     Reverse the loop in EmitAll and these fail; leave the order unspecified
//     and they fail too.
//   - **each send completes before the next starts**, checked by hanging a
//     second .bangbang off an outlet and requiring its whole subtree in the log
//     before the outlet to its left appears. Right-to-left with breadth-first
//     sends would pass the first check and fail this one.
//   - **the idiom the guarantee exists for**, end to end in a real patcher:
//     the right outlet bangs a value out of one `.i` and into the cold inlet of
//     another, and only then does the left outlet bang that second `.i`. Under
//     any other order it reports the *previous* value, which is the bug this
//     family of objects is here to prevent.
//
// The rest is the argument, which is where .bangbang genuinely differs from
// `.trigger b b b` rather than merely spelling it differently — a count with
// Max's own 1-40 range, not a format list with .trigger's 256 — and that
// difference is pinned directly against .trigger at the end of the file so it
// cannot drift into an alias.
//
// The standalone rigs wire objects directly rather than through a patcher, as
// every sibling suite does. That exercises the same outlet::SendBang loop the
// pinned-GraphState path of #226 runs: the snapshot changes *which* adjacency
// vector is walked, not that it is walked front to back.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/genericObjects/gBangBang.h"
#include "patcher/genericObjects/gTrigger.h"
#include "patcher/sinks.hpp"

namespace {

  using TestHelpers::IntSink;
  using TestHelpers::OrderSink;
  using YSE::PATCHER::gBangBang;
  using YSE::PATCHER::gTrigger;

  // One order-logging sink per outlet, all sharing one log. "Which outlet
  // fired, and in what order" is then an assertion rather than an inference —
  // which is the whole subject of this object.
  struct Rig {
    std::unique_ptr<gBangBang> op;
    std::vector<std::unique_ptr<OrderSink>> sinks;
    std::vector<char> order;

    explicit Rig(const std::string& args = "") : op(new gBangBang()) {
      if (!args.empty()) op->SetParams(args);
      Wire();
    }

    // Rebuilt after a SetParams, since re-parsing replaces the outlets.
    void Wire() {
      sinks.clear();
      order.clear();
      for (int i = 0; i < op->NumOutputs(); i++) {
        sinks.push_back(std::unique_ptr<OrderSink>(new OrderSink()));
        // 'a' for outlet 0, 'b' for outlet 1, ... so the log reads left to
        // right in *outlet* order and a right-to-left firing shows up
        // reversed. Wrapped at 26, which only matters for the 40-outlet cap
        // test — that one asserts the count, not the sequence.
        sinks.back()->tag = (char)('a' + (i % 26));
        sinks.back()->log = &order;
        op->ConnectOutlet(sinks.back()->GetInlet(0), i);
        sinks.back()->ConnectInlet(op->GetOutlet(i), 0);
      }
    }

    void Bang() {
      op->GetInlet(0)->SetBang(YSE::T_GUI);
    }
    void SendInt(int v) {
      op->GetInlet(0)->SetInt(v, YSE::T_GUI);
    }
    void Send(float v) {
      op->GetInlet(0)->SetFloat(v, YSE::T_GUI);
    }
    void List(const std::string& text) {
      op->GetInlet(0)->SetList(text, YSE::T_GUI);
    }

    int Outlets() const {
      return op->NumOutputs();
    }
    const OrderSink& At(int outlet) const {
      return *sinks[(std::size_t)outlet];
    }
    std::string Log() const {
      return std::string(order.begin(), order.end());
    }
    int Total() const {
      int n = 0;
      for (const auto& s : sinks)
        n += s->count;
      return n;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("bangbang: creatable through the registry (#467)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_BANGBANG, "4");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".bangbang"));
    // "The number of outlets is determined by an argument."
    CHECK(h->GetOutputs() == 4);
    CHECK(h->GetInputs() == 1);
    for (int i = 0; i < 4; i++) {
      CAPTURE(i);
      CHECK(h->OutputDataType(i) == YSE::OUT_TYPE::BANG);
    }
  }

  TEST_CASE("bangbang: listed by pRegistry::AllNames (#467)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".bangbang")) != names.end());
  }

  TEST_CASE("bangbang: with no argument there are two bang outlets (#467)") {
    Rig rig;
    REQUIRE(rig.Outlets() == 2);
    CHECK(rig.op->OutletCount() == gBangBang::DEFAULT_OUTLETS);
    CHECK(rig.op->GetOutputType(0) == YSE::OUT_TYPE::BANG);
    CHECK(rig.op->GetOutputType(1) == YSE::OUT_TYPE::BANG);

    rig.Bang();
    CHECK(rig.At(0).lastKind == OrderSink::BANG);
    CHECK(rig.At(1).lastKind == OrderSink::BANG);
  }

  // ─── the ordering guarantee ─────────────────────────────────────────────────
  // The reason the object exists. Every test in this block would pass an
  // implementation that fires every outlet, and fail one that fires them in any
  // order but right to left.

  TEST_CASE("bangbang: outlets fire right to left, not left to right (#467)") {
    Rig rig("4");
    rig.Bang();
    // Sinks are tagged 'a' (outlet 0) through 'd' (outlet 3). Right to left is
    // the reverse of the outlet order.
    CHECK(rig.Log() == "dcba");
    CHECK(rig.Total() == 4);
  }

  // Max documents bang, int, float and anything separately and gives all four
  // the identical description, so the order cannot be a property of one path.
  TEST_CASE("bangbang: the order is the same for every input type (#467)") {
    Rig rig("3");
    rig.Bang();
    CHECK(rig.Log() == "cba");
    rig.SendInt(1);
    CHECK(rig.Log() == "cbacba");
    rig.Send(1.5f);
    CHECK(rig.Log() == "cbacbacba");
    rig.List("some list");
    CHECK(rig.Log() == "cbacbacbacba");
  }

  // The two-outlet default is the shape most patches use, and a two-element
  // sequence is the one a broken implementation is most likely to get right by
  // accident — so it is asserted on its own rather than only via the wider rig.
  TEST_CASE("bangbang: the default pair also fires right to left (#467)") {
    Rig rig;
    REQUIRE(rig.Outlets() == 2);
    rig.Bang();
    CHECK(rig.Log() == "ba");
  }

  // Not a small-n special case: twenty outlets come out in the reverse of the
  // alphabet, so an implementation that only got the two- or three-outlet case
  // right has nowhere to hide.
  TEST_CASE("bangbang: the order holds for many outlets (#467)") {
    std::string expected;
    for (int i = 0; i < 20; i++)
      expected += (char)('a' + (19 - i));

    Rig rig("20");
    REQUIRE(rig.Outlets() == 20);
    rig.SendInt(1);
    CHECK(rig.Log() == expected);
  }

  TEST_CASE("bangbang: every outlet fires exactly once per input (#467)") {
    Rig rig("5");
    rig.SendInt(1);
    for (int i = 0; i < rig.Outlets(); i++) {
      CAPTURE(i);
      CHECK(rig.At(i).count == 1);
    }
    rig.Bang();
    for (int i = 0; i < rig.Outlets(); i++) {
      CAPTURE(i);
      CHECK(rig.At(i).count == 2);
    }
  }

  // The half of the guarantee a bare sequence check cannot see: a send does not
  // merely *start* before the one to its left, it finishes — the whole subgraph
  // behind it runs first. A breadth-first fan-out would still log "b" before
  // "a" at the top level while interleaving everything below.
  TEST_CASE("bangbang: each send completes before the outlet to its left fires (#467)") {
    std::vector<char> order;

    gBangBang outer;
    gBangBang inner;

    // outer outlet 1 (fired first) drives the inner bangbang.
    outer.ConnectOutlet(inner.GetInlet(0), 1);
    inner.ConnectInlet(outer.GetOutlet(1), 0);

    OrderSink innerLeft;
    OrderSink innerRight;
    OrderSink outerLeft;
    innerLeft.tag = 'x';
    innerRight.tag = 'y';
    outerLeft.tag = 'z';
    innerLeft.log = innerRight.log = outerLeft.log = &order;

    inner.ConnectOutlet(innerLeft.GetInlet(0), 0);
    innerLeft.ConnectInlet(inner.GetOutlet(0), 0);
    inner.ConnectOutlet(innerRight.GetInlet(0), 1);
    innerRight.ConnectInlet(inner.GetOutlet(1), 0);
    outer.ConnectOutlet(outerLeft.GetInlet(0), 0);
    outerLeft.ConnectInlet(outer.GetOutlet(0), 0);

    outer.GetInlet(0)->SetBang(YSE::T_GUI);

    // The inner object's own right-to-left pair runs to completion ('y' then
    // 'x') before the outer object's left outlet is served ('z').
    CHECK(std::string(order.begin(), order.end()) == "yxz");
  }

  // The idiom the guarantee is for, in a real patcher with real objects. The
  // right outlet bangs a stored value out of `source` and into the *cold* inlet
  // of `store`; the left outlet then bangs `store`, which reports what it was
  // just handed. Fire left to right and `store` bangs before it has been
  // written, so it reports the previous value — exactly the class of bug this
  // object exists to make impossible.
  TEST_CASE("bangbang: the right outlet's chain lands before the left one bangs, in a patcher "
            "(#467)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* fan = p.CreateObject(YSE::OBJ::G_BANGBANG, "2");
    YSE::pHandle* source = p.CreateObject(YSE::OBJ::G_INT);
    YSE::pHandle* store = p.CreateObject(YSE::OBJ::G_INT);
    REQUIRE(fan != nullptr);
    REQUIRE(source != nullptr);
    REQUIRE(store != nullptr);

    IntSink sink;
    YSE::pHandle sinkHandle(&sink);

    p.Connect(fan, 1, source, 0); // right outlet bangs the source...
    p.Connect(source, 0, store, 1); // ...whose value lands in the cold inlet
    p.Connect(fan, 0, store, 0); // left outlet bangs the store
    p.Connect(store, 0, &sinkHandle, 0);

    source->SetIntData(1, 42); // silently, on the cold inlet
    fan->SetBang(0);
    CHECK(sink.gotInt);
    CHECK(sink.received == 42);

    // Twice, with a different value: a left-to-right implementation would be
    // one message behind here (42 rather than 7) rather than wrong only on the
    // first bang.
    source->SetIntData(1, 7);
    fan->SetBang(0);
    CHECK(sink.received == 7);
  }

  // ─── the payload is discarded ───────────────────────────────────────────────

  // Max gives bang, int, float and anything the same one-line description:
  // "Causes a bang to be sent out all outlets in right-to-left order." The
  // payload is not merely unused, its being ignored is the documented
  // behaviour — which is the whole difference from .trigger.
  TEST_CASE("bangbang: every input type produces bangs and nothing else (#467)") {
    Rig rig("2");

    rig.SendInt(5);
    CHECK(rig.At(0).lastKind == OrderSink::BANG);
    CHECK(rig.At(1).lastKind == OrderSink::BANG);

    rig.Send(2.5f);
    CHECK(rig.At(0).lastKind == OrderSink::BANG);

    rig.List("60 100 note");
    CHECK(rig.At(0).lastKind == OrderSink::BANG);

    rig.Bang();
    CHECK(rig.At(0).lastKind == OrderSink::BANG);

    // Four inputs, four bangs per outlet, and no int/float/list ever recorded.
    CHECK(rig.At(0).count == 4);
    CHECK(rig.At(1).count == 4);
    CHECK(rig.At(0).lastInt == 0);
    CHECK(rig.At(0).lastList.empty());
  }

  TEST_CASE("bangbang: an empty list is still an input (#467)") {
    Rig rig("2");
    rig.List("");
    CHECK(rig.At(0).lastKind == OrderSink::BANG);
    CHECK(rig.Log() == "ba");
  }

  // ─── the argument: an outlet count ──────────────────────────────────────────

  TEST_CASE("bangbang: the argument is the outlet count (#467)") {
    Rig one("1");
    CHECK(one.Outlets() == 1);
    CHECK(one.op->OutletCount() == 1);
    one.Bang();
    CHECK(one.Log() == "a");

    Rig seven("7");
    CHECK(seven.Outlets() == 7);
    CHECK(seven.op->OutletCount() == 7);
  }

  // Max: "Floats are converted to ints." Towards zero, as C truncates.
  TEST_CASE("bangbang: a float argument is truncated to an int (#467)") {
    Rig low("3.2");
    CHECK(low.Outlets() == 3);

    Rig high("3.7");
    CHECK(high.Outlets() == 3);

    Rig exponent("1e1");
    CHECK(exponent.Outlets() == 10);
  }

  // Max: "The number of outlets can be any number between 1 and 40."
  TEST_CASE("bangbang: the outlet count is clamped into Max's 1-40 range (#467)") {
    Rig zero("0");
    CHECK(zero.Outlets() == gBangBang::MIN_OUTLETS);
    CHECK(zero.Outlets() == 1);

    Rig negative("-5");
    CHECK(negative.Outlets() == 1);

    Rig fractional("0.5");
    CHECK(fractional.Outlets() == 1);

    Rig atCeiling("40");
    CHECK(atCeiling.Outlets() == gBangBang::MAX_OUTLETS);
    CHECK(atCeiling.Outlets() == 40);

    Rig overCeiling("100");
    CHECK(overCeiling.Outlets() == 40);

    // Clamped as a float *before* the int conversion: 1e30 is out of int range,
    // so converting first would answer 0 and land this at the floor instead of
    // the ceiling.
    Rig huge("1e30");
    CHECK(huge.Outlets() == 40);
    Rig hugeNegative("-1e30");
    CHECK(hugeNegative.Outlets() == 1);

    // The cap is real, not just a stored number: all forty fire.
    atCeiling.Bang();
    CHECK(atCeiling.Total() == 40);
  }

  // The strict token reader shared with .sel and .trigger: a token only counts
  // as a number when the *whole* of it does, and a non-finite reading is not a
  // number at all. Without both, `.bangbang 3abc` would silently become three
  // outlets and `.bangbang 1e999` would clamp an overflowed infinity.
  TEST_CASE("bangbang: a non-numeric argument falls back to the default (#467)") {
    Rig word("abc");
    CHECK(word.Outlets() == gBangBang::DEFAULT_OUTLETS);

    Rig partial("3abc");
    CHECK(partial.Outlets() == 2);

    Rig infinite("inf");
    CHECK(infinite.Outlets() == 2);

    Rig notANumber("nan");
    CHECK(notANumber.Outlets() == 2);

    Rig overflowed("1e999");
    CHECK(overflowed.Outlets() == 2);

    // ...and the fallback is a working object, not a stub.
    word.Bang();
    CHECK(word.Log() == "ba");
  }

  TEST_CASE("bangbang: only the first token is read as the count (#467)") {
    // Max's bangbang takes one argument; anything after it is surplus rather
    // than a second count.
    Rig rig("3 9 nonsense");
    CHECK(rig.Outlets() == 3);
  }

  // ─── parameter edges ────────────────────────────────────────────────────────

  TEST_CASE("bangbang: re-parsing an empty parameter string returns to two outlets (#467)") {
    Rig rig("6");
    REQUIRE(rig.Outlets() == 6);

    rig.op->SetParams("");
    CHECK(rig.Outlets() == 2);
    CHECK(rig.op->OutletCount() == 2);

    rig.Wire();
    rig.Bang();
    CHECK(rig.Log() == "ba");
  }

  TEST_CASE("bangbang: re-parsing rebuilds the outlets (#467)") {
    Rig rig("1");
    REQUIRE(rig.Outlets() == 1);

    rig.op->SetParams("4");
    CHECK(rig.Outlets() == 4);
    for (int i = 0; i < 4; i++) {
      CAPTURE(i);
      CHECK(rig.op->GetOutputType(i) == YSE::OUT_TYPE::BANG);
    }

    rig.Wire();
    rig.Bang();
    CHECK(rig.Log() == "dcba");

    // ...and shrinking works too, which is the direction a resize-only
    // implementation gets wrong.
    rig.op->SetParams("2");
    CHECK(rig.Outlets() == 2);
    rig.Wire();
    rig.Bang();
    CHECK(rig.Log() == "ba");
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("bangbang: survives a DumpJSON / ParseJSON round trip (#467)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_BANGBANG);
    REQUIRE(h != nullptr);
    h->SetParams("5");
    const std::string json = src.DumpJSON();
    CHECK(json.find(".bangbang") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".bangbang"));
    CHECK(copy->GetParams() == std::string("5"));
    // The shape has to come back with it, or the saved patch cords land on the
    // wrong outlets.
    CHECK(copy->GetOutputs() == 5);
    CHECK(copy->OutputDataType(4) == YSE::OUT_TYPE::BANG);
  }

  // ─── the difference from .trigger ───────────────────────────────────────────
  // Pinned rather than assumed. What comes *out* of the two is identical; what
  // the same argument token *means* is not, and that is the whole reason this
  // is a separate object rather than an alias.

  TEST_CASE("bangbang: '.bangbang 3' emits exactly what '.trigger b b b' emits (#467)") {
    std::vector<char> bangbangLog;
    std::vector<char> triggerLog;

    gBangBang fan;
    fan.SetParams("3");
    gTrigger trig;
    trig.SetParams("b b b");
    REQUIRE(fan.NumOutputs() == 3);
    REQUIRE(trig.NumOutputs() == 3);

    std::vector<std::unique_ptr<OrderSink>> sinks;
    for (int i = 0; i < 3; i++) {
      sinks.push_back(std::unique_ptr<OrderSink>(new OrderSink()));
      sinks.back()->tag = (char)('a' + i);
      sinks.back()->log = &bangbangLog;
      fan.ConnectOutlet(sinks.back()->GetInlet(0), i);
      sinks.back()->ConnectInlet(fan.GetOutlet(i), 0);

      sinks.push_back(std::unique_ptr<OrderSink>(new OrderSink()));
      sinks.back()->tag = (char)('a' + i);
      sinks.back()->log = &triggerLog;
      trig.ConnectOutlet(sinks.back()->GetInlet(0), i);
      sinks.back()->ConnectInlet(trig.GetOutlet(i), 0);
    }

    fan.GetInlet(0)->SetInt(42, YSE::T_GUI);
    trig.GetInlet(0)->SetInt(42, YSE::T_GUI);

    CHECK(std::string(bangbangLog.begin(), bangbangLog.end()) == "cba");
    CHECK(std::string(bangbangLog.begin(), bangbangLog.end()) ==
          std::string(triggerLog.begin(), triggerLog.end()));
    // Same outlet types, too.
    CHECK(fan.GetOutputType(0) == trig.GetOutputType(0));
  }

  // The argument surface is where they part company: the same token '3' asks
  // .bangbang for three outlets and .trigger for one outlet carrying the int
  // constant 3.
  TEST_CASE("bangbang: the argument is a count where .trigger's is a format (#467)") {
    gBangBang fan;
    fan.SetParams("3");
    CHECK(fan.NumOutputs() == 3);
    CHECK(fan.GetOutputType(0) == YSE::OUT_TYPE::BANG);

    gTrigger trig;
    trig.SetParams("3");
    CHECK(trig.NumOutputs() == 1);
    CHECK(trig.SlotKind(0) == gTrigger::Kind::CONST_INT);
    CHECK(trig.GetOutputType(0) == YSE::OUT_TYPE::INT);
  }

  // And the ceilings differ, because Max states one for bangbang (1-40) and
  // none for trigger (this patcher caps that one at 256).
  TEST_CASE("bangbang: its ceiling is Max's 40, not .trigger's 256 (#467)") {
    CHECK(gBangBang::MAX_OUTLETS == 40);
    CHECK(gTrigger::MAX_OUTLETS == 256);

    gBangBang fan;
    fan.SetParams("256");
    CHECK(fan.NumOutputs() == 40);
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category, the port shape and the parameter name,
  // which is what a binding generator keys on. It also pins that the outlets
  // built by the parse callback are documented — the coverage test only ever
  // sees a default-constructed object.

  TEST_CASE("bangbang: documents itself as GENERIC with a labelled port set (#467)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_BANGBANG));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    CHECK_FALSE(obj->GetDescription().empty());

    REQUIRE(obj->NumInputs() == 1);
    CHECK(obj->GetInlet(0)->GetDocLabel() == "in");

    REQUIRE(obj->NumOutputs() == 2);
    CHECK(obj->GetOutlet(0)->GetDocLabel() == "out0");
    CHECK(obj->GetOutlet(1)->GetDocLabel() == "out1");

    REQUIRE(obj->GetParamDocs().size() == 1);
    CHECK(obj->GetParamDocs()[0].name == "outlets");
    CHECK(obj->GetParamDocs()[0].defaultValue == "2");
    CHECK(obj->GetParamDocs()[0].range == "1-40");
  }

  TEST_CASE("bangbang: outlets created by the parse callback are documented too (#467)") {
    gBangBang op;
    op.SetParams("12");
    REQUIRE(op.NumOutputs() == 12);
    for (int i = 0; i < op.NumOutputs(); i++) {
      CAPTURE(i);
      CHECK_FALSE(op.GetOutlet(i)->GetDocLabel().empty());
      CHECK_FALSE(op.GetOutlet(i)->GetDocDescription().empty());
      CHECK_FALSE(op.GetOutlet(i)->GetRange().empty());
    }
    // Two-digit labels come out of the shared WriteInt path, not std::to_string.
    CHECK(op.GetOutlet(11)->GetDocLabel() == "out11");
  }

  TEST_CASE("bangbang: the inlet accepts bang, int, float and list (#467)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_BANGBANG));
    REQUIRE(obj != nullptr);
    const unsigned int in = obj->GetInlet(0)->GetAcceptedTypes();
    CHECK((in & YSE::PATCHER::IT_BANG) != 0);
    CHECK((in & YSE::PATCHER::IT_INT) != 0);
    CHECK((in & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((in & YSE::PATCHER::IT_LIST) != 0);
  }

} // TEST_SUITE("patcher")
