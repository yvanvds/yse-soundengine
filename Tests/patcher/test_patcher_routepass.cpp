// Tests for .routepass (issue #483) — route a complete message by what its
// first item matches, without consuming that item.
//
// Six rules carry this file, and each is one a plausible implementation gets
// wrong:
//   - **the matched item stays on the message.** This is the whole object. Max:
//     routepass "does not strip off the matched portion of the message". An
//     implementation that stripped would still route every message to the right
//     outlet and pass every "which outlet fired" test, so what arrives at the
//     sink is asserted, not merely where it arrived.
//   - **a bare .routepass has one outlet, not two.** Max ties the outlet count
//     to the argument count "in addition to the rightmost outlet", so no
//     arguments means no match outlets. .sel invents a default selector of 0 for
//     its own no-argument case, which is documented for select and not for this,
//     and copying it would put a branch in a patch that did not ask for one.
//   - **the match is on the first item alone.** `.routepass 60` must not answer
//     `note 60`, and matching the whole message text would pass every
//     single-item test and fail this.
//   - **a number and a symbol never match each other**, and an int and a float
//     of the same value both match a numeric selector. The predecessor .route
//     compares std::to_string(value) against the selector text, which makes
//     `.route 5` unable to match the float 5.0 at all (issue #672); this object
//     uses .sel's matcher, and the float case is asserted directly so it cannot
//     regress into the same shape.
//   - **exactly one outlet fires**, and on a repeated selector it is the
//     leftmost. An object that fanned out would pass a "did outlet 0 get it"
//     test and fail this.
//   - **the reject outlet passes the message through untouched**, in its own
//     kind, so a chain of these objects each sees what the first one saw.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/pEnums.h"
#include "patcher/inlet.h"
#include "patcher/genericObjects/gRoutePass.h"
#include "patcher/sinks.hpp"

namespace {
  using TestHelpers::FloatSink;
  using TestHelpers::ListSink;
  using TestHelpers::OrderSink;
  using YSE::PATCHER::gRoutePass;

  // One order-logging sink per outlet, all sharing one log. Outlet i is tagged
  // 'a' + i, so a single message reads back as the outlet it left by — and,
  // because OrderSink records the kind and the value too, as exactly what left
  // it. A sink per outlet rather than one on the outlet under test is what makes
  // "exactly one outlet fired" assertable.
  struct Rig {
    gRoutePass op;
    std::vector<std::unique_ptr<OrderSink>> sinks;
    std::vector<char> order;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) op.SetParams(args);
      for (int i = 0; i < op.NumOutputs(); i++) {
        sinks.push_back(std::unique_ptr<OrderSink>(new OrderSink()));
        sinks.back()->tag = (char)('a' + (i % 26));
        sinks.back()->log = &order;
        op.ConnectOutlet(sinks.back()->GetInlet(0), i);
        sinks.back()->ConnectInlet(op.GetOutlet(i), 0);
      }
    }

    void Bang() {
      op.GetInlet(0)->SetBang(YSE::T_GUI);
    }
    void Int(int value) {
      op.GetInlet(0)->SetInt(value, YSE::T_GUI);
    }
    void Float(float value) {
      op.GetInlet(0)->SetFloat(value, YSE::T_GUI);
    }
    void List(const std::string& text) {
      op.GetInlet(0)->SetList(text, YSE::T_GUI);
    }

    // The outlets that fired, in order, as tags. One character for a
    // well-behaved input; the empty string when nothing fired.
    std::string Log() const {
      return std::string(order.begin(), order.end());
    }
    // The rightmost outlet's tag — the reject outlet, wherever it landed.
    char Reject() const {
      return (char)('a' + ((op.NumOutputs() - 1) % 26));
    }
    OrderSink& Sink(int index) {
      return *sinks[(std::size_t)index];
    }
    OrderSink& RejectSink() {
      return *sinks.back();
    }
    int Total() const {
      int t = 0;
      for (const auto& s : sinks)
        t += s->count;
      return t;
    }
    void Reset() {
      order.clear();
      for (auto& s : sinks)
        s->count = 0;
    }
  };
} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("routepass: creatable through the registry (#483)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ROUTEPASS, "note ctl");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".routepass"));
    CHECK(h->GetInputs() == 1);
    // Max: "The number of arguments determines the number of outlets, in
    // addition to the rightmost outlet."
    CHECK(h->GetOutputs() == 3);
  }

  TEST_CASE("routepass: listed by pRegistry::AllNames (#483)") {
    auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(YSE::OBJ::G_ROUTEPASS)) != names.end());
  }

  TEST_CASE("routepass: a bare object has no selectors and one outlet (#483)") {
    // Max documents a no-argument default for `select` and none here, so there
    // is no invented `0` selector: no arguments means no match outlets, and the
    // rightmost outlet is the only one there is.
    gRoutePass op;
    CHECK(op.SelectorCount() == 0);
    CHECK(op.NumInputs() == 1);
    CHECK(op.NumOutputs() == 1);
  }

  TEST_CASE("routepass: one outlet per argument plus the rightmost (#483)") {
    gRoutePass op;
    op.SetParams("note ctl bend");
    CHECK(op.SelectorCount() == 3);
    CHECK(op.NumOutputs() == 4);
  }

  TEST_CASE("routepass: a selector is a number or a symbol, decided once (#483)") {
    gRoutePass op;
    op.SetParams("5 note -2.5 1e3 5abc");
    REQUIRE(op.SelectorCount() == 5);

    CHECK(op.SelectorIsNumber(0));
    CHECK(op.SelectorValue(0) == doctest::Approx(5.f));
    CHECK(op.SelectorText(0).empty());

    CHECK_FALSE(op.SelectorIsNumber(1));
    CHECK(op.SelectorText(1) == std::string("note"));

    CHECK(op.SelectorIsNumber(2));
    CHECK(op.SelectorValue(2) == doctest::Approx(-2.5f));

    CHECK(op.SelectorIsNumber(3));
    CHECK(op.SelectorValue(3) == doctest::Approx(1000.f));

    // Partly a number is not a number — ReadNumericToken's contract, and the
    // reason `5abc` stays the symbol it was typed as.
    CHECK_FALSE(op.SelectorIsNumber(4));
    CHECK(op.SelectorText(4) == std::string("5abc"));

    // Out of range on either end answers rather than reads.
    CHECK_FALSE(op.SelectorIsNumber(5));
    CHECK(op.SelectorValue(5) == doctest::Approx(0.f));
    CHECK(op.SelectorText(-1).empty());
  }

  TEST_CASE("routepass: at most MAX_SELECTORS arguments are held (#483)") {
    std::string args;
    for (int i = 0; i < gRoutePass::MAX_SELECTORS + 20; i++) {
      if (i > 0) args += " ";
      args += "s" + std::to_string(i);
    }
    gRoutePass op;
    op.SetParams(args);
    CHECK(op.SelectorCount() == gRoutePass::MAX_SELECTORS);
    CHECK(op.NumOutputs() == gRoutePass::MAX_SELECTORS + 1);
  }

  TEST_CASE("routepass: SetParams(\"\") returns the object to the bare shape (#483)") {
    gRoutePass op;
    op.SetParams("note ctl");
    REQUIRE(op.SelectorCount() == 2);
    op.SetParams("");
    CHECK(op.SelectorCount() == 0);
    CHECK(op.NumOutputs() == 1);
  }

  TEST_CASE("routepass: every outlet is ANY (#483)") {
    // The object forwards whichever kind arrived rather than producing one of
    // its own, and that is true of the reject outlet as much as the match ones.
    gRoutePass op;
    op.SetParams("note 5");
    for (int i = 0; i < op.NumOutputs(); i++)
      CHECK(op.GetOutputType((unsigned int)i) == YSE::OUT_TYPE::ANY);
  }

  // ─── the headline: nothing is consumed ──────────────────────────────────────

  TEST_CASE("routepass: a matched list keeps its first item (#483)") {
    // Max: routepass "does not strip off the matched portion of the message".
    // The one assertion the object exists for — a stripping implementation
    // would route this to the same outlet and send "60 100".
    Rig rig("note ctl");
    rig.List("note 60 100");
    CHECK(rig.Log() == std::string("a"));
    CHECK(rig.Sink(0).lastKind == OrderSink::LIST);
    CHECK(rig.Sink(0).lastList == std::string("note 60 100"));
  }

  TEST_CASE("routepass: a matched message with nothing after the selector is not a bang (#483)") {
    // Max's `route note` emits a bang here, having consumed the only item there
    // was. This one has consumed nothing, so what leaves is the symbol itself —
    // the clearest case of the difference, and the one a stripping
    // implementation cannot fake.
    Rig rig("note");
    rig.List("note");
    CHECK(rig.Log() == std::string("a"));
    CHECK(rig.Sink(0).lastKind == OrderSink::LIST);
    CHECK(rig.Sink(0).lastList == std::string("note"));
  }

  TEST_CASE("routepass: a matched number leaves as that number, not a bang (#483)") {
    // The same rule for the numeric selectors. `route 5` would bang; this sends
    // the 5.
    Rig rig("5");
    rig.Int(5);
    CHECK(rig.Log() == std::string("a"));
    CHECK(rig.Sink(0).lastKind == OrderSink::INT);
    CHECK(rig.Sink(0).lastInt == 5);

    rig.Reset();
    rig.Float(5.f);
    CHECK(rig.Log() == std::string("a"));
    CHECK(rig.Sink(0).lastKind == OrderSink::FLOAT);
    CHECK(rig.Sink(0).lastFloat == doctest::Approx(5.f));
  }

  TEST_CASE("routepass: a matched list of two keeps both items (#483)") {
    // The shape where a stripping implementation is most tempting to write as
    // "send the remainder": `route note` turns `note 60` into the bare value 60.
    Rig rig("note");
    rig.List("note 60");
    CHECK(rig.Sink(0).lastKind == OrderSink::LIST);
    CHECK(rig.Sink(0).lastList == std::string("note 60"));
  }

  // ─── what matches what ──────────────────────────────────────────────────────

  TEST_CASE("routepass: each selector gets its own outlet, in order (#483)") {
    Rig rig("note ctl bend");
    rig.List("ctl 7 64");
    CHECK(rig.Log() == std::string("b"));
    CHECK(rig.Sink(1).lastList == std::string("ctl 7 64"));

    rig.Reset();
    rig.List("bend 8192");
    CHECK(rig.Log() == std::string("c"));
    CHECK(rig.Sink(2).lastList == std::string("bend 8192"));
  }

  TEST_CASE("routepass: exactly one outlet fires per input (#483)") {
    Rig rig("note ctl");
    rig.List("note 60");
    CHECK(rig.Total() == 1);
    rig.Reset();
    rig.List("nothing at all");
    CHECK(rig.Total() == 1);
  }

  TEST_CASE("routepass: a repeated selector uses the leftmost of its outlets (#483)") {
    // Max's rule for a repeated argument. An object that fanned out to both
    // would pass "outlet 0 got it" and fail this.
    Rig rig("note note");
    rig.List("note 60");
    CHECK(rig.Log() == std::string("a"));
    CHECK(rig.Total() == 1);
  }

  TEST_CASE("routepass: a numeric selector answers both the int and the float (#483)") {
    // This patcher has one numeric type, so `.routepass 5` has to answer both.
    // The predecessor `.route` cannot answer the float at all, because it
    // compares std::to_string(5.f) — "5.000000" — against the selector text
    // (issue #672).
    Rig rig("5");
    rig.Int(5);
    CHECK(rig.Log() == std::string("a"));
    rig.Reset();
    rig.Float(5.f);
    CHECK(rig.Log() == std::string("a"));
    rig.Reset();
    // And a selector spelled as a float answers the int, for the same reason.
    Rig spelled("5.0");
    spelled.Int(5);
    CHECK(spelled.Log() == std::string("a"));
    spelled.Reset();
    spelled.Float(5.f);
    CHECK(spelled.Log() == std::string("a"));
  }

  TEST_CASE("routepass: a number and a symbol never match each other (#483)") {
    Rig symbolic("note");
    symbolic.Int(5);
    CHECK(symbolic.Log() == std::string("b"));
    symbolic.Reset();
    symbolic.Float(5.f);
    CHECK(symbolic.Log() == std::string("b"));

    Rig numeric("5");
    numeric.List("note 60");
    CHECK(numeric.Log() == std::string("b"));
    numeric.Reset();
    // A symbol that merely contains the digits is still a symbol.
    numeric.List("5abc 1");
    CHECK(numeric.Log() == std::string("b"));
  }

  TEST_CASE("routepass: a list is matched on its first element alone (#483)") {
    // `.routepass 60` must not answer `note 60`: matching the whole message
    // text, or scanning it for the selector, passes every single-item test and
    // fails here.
    Rig rig("60");
    rig.List("note 60");
    CHECK(rig.Log() == std::string("b"));
    CHECK(rig.Sink(1).lastList == std::string("note 60"));

    rig.Reset();
    // But it does answer the list that leads with it, and keeps the rest.
    rig.List("60 100");
    CHECK(rig.Log() == std::string("a"));
    CHECK(rig.Sink(0).lastList == std::string("60 100"));
  }

  TEST_CASE("routepass: leading whitespace does not hide the first item (#483)") {
    Rig rig("note");
    rig.List("   note 60");
    CHECK(rig.Log() == std::string("a"));
    // Forwarded exactly as it arrived — the object routes, it does not tidy.
    CHECK(rig.Sink(0).lastList == std::string("   note 60"));
  }

  TEST_CASE("routepass: a bang matches a selector spelled 'bang' (#483)") {
    Rig rig("note bang");
    rig.Bang();
    CHECK(rig.Log() == std::string("b"));
    CHECK(rig.Sink(1).lastKind == OrderSink::BANG);
  }

  TEST_CASE("routepass: a bang with no 'bang' selector leaves the reject outlet as a bang (#483)") {
    Rig rig("note ctl");
    rig.Bang();
    CHECK(rig.Log() == std::string("c"));
    CHECK(rig.RejectSink().lastKind == OrderSink::BANG);
  }

  TEST_CASE("routepass: a NaN matches nothing (#483)") {
    // A NaN compares unequal to everything including itself, so it leaves the
    // reject outlet. The patcher's usual "read a non-finite as 0" substitution
    // is deliberately not applied — it would route a stray NaN to the outlet a
    // patch wired for a real 0.
    Rig rig("0");
    rig.Float(std::nanf(""));
    CHECK(rig.Log() == std::string("b"));
  }

  TEST_CASE("routepass: an empty message matches nothing (#483)") {
    Rig rig("note");
    rig.List("");
    CHECK(rig.Log() == std::string("b"));
    rig.Reset();
    rig.List("   ");
    CHECK(rig.Log() == std::string("b"));
  }

  // ─── the reject outlet ──────────────────────────────────────────────────────

  TEST_CASE("routepass: the reject outlet passes each kind through unchanged (#483)") {
    Rig rig("nothingmatchesthis");

    rig.Int(42);
    CHECK(rig.RejectSink().lastKind == OrderSink::INT);
    CHECK(rig.RejectSink().lastInt == 42);

    rig.Float(1.25f);
    CHECK(rig.RejectSink().lastKind == OrderSink::FLOAT);
    CHECK(rig.RejectSink().lastFloat == doctest::Approx(1.25f));

    rig.List("ctl 7 64");
    CHECK(rig.RejectSink().lastKind == OrderSink::LIST);
    CHECK(rig.RejectSink().lastList == std::string("ctl 7 64"));

    rig.Bang();
    CHECK(rig.RejectSink().lastKind == OrderSink::BANG);

    CHECK(rig.Log() == std::string("bbbb"));
  }

  TEST_CASE("routepass: a bare object passes everything through its one outlet (#483)") {
    Rig rig;
    REQUIRE(rig.op.NumOutputs() == 1);
    rig.List("note 60 100");
    rig.Int(7);
    CHECK(rig.Log() == std::string("aa"));
    CHECK(rig.Sink(0).lastKind == OrderSink::INT);
  }

  // ─── chaining ───────────────────────────────────────────────────────────────

  TEST_CASE("routepass: a chain of two, and the second sees what the first saw (#483)") {
    // The reject outlet of one feeding the inlet of the next is how a dispatch
    // tree is built. Because nothing was consumed, the second object matches on
    // the same first item the first one tested — which is exactly what a chain
    // of stripping `route` objects cannot do.
    gRoutePass first;
    gRoutePass second;
    first.SetParams("note");
    second.SetParams("ctl");

    ListSink matched;
    ListSink rejected;

    // first.reject -> second.in
    first.ConnectOutlet(second.GetInlet(0), 1);
    second.ConnectInlet(first.GetOutlet(1), 0);
    // second.match -> matched, second.reject -> rejected
    second.ConnectOutlet(matched.GetInlet(0), 0);
    matched.ConnectInlet(second.GetOutlet(0), 0);
    second.ConnectOutlet(rejected.GetInlet(0), 1);
    rejected.ConnectInlet(second.GetOutlet(1), 0);

    first.GetInlet(0)->SetList("ctl 7 64", YSE::T_GUI);
    CHECK(matched.gotList);
    CHECK(matched.received == std::string("ctl 7 64"));
    CHECK_FALSE(rejected.gotList);

    first.GetInlet(0)->SetList("bend 8192", YSE::T_GUI);
    CHECK(rejected.gotList);
    CHECK(rejected.received == std::string("bend 8192"));
  }

  // ─── lifecycle ──────────────────────────────────────────────────────────────

  TEST_CASE("routepass: Calculate() emits nothing (#483)") {
    // The object is driven by its inlet. An emitting Calculate() would route
    // again on every DSP tick after the inlet fired.
    Rig rig("note ctl");
    rig.List("note 60");
    rig.Reset();
    for (int i = 0; i < 8; i++)
      rig.op.Calculate(YSE::T_DSP);
    CHECK(rig.Total() == 0);
  }

  TEST_CASE("routepass: survives a DumpJSON / ParseJSON round trip (#483)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_ROUTEPASS);
    REQUIRE(h != nullptr);
    h->SetParams("note ctl 5");
    const std::string json = src.DumpJSON();
    CHECK(json.find(".routepass") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".routepass"));
    CHECK(copy->GetParams() == std::string("note ctl 5"));
    // The shape has to come back with it, or the saved patch cords land on the
    // wrong ports.
    CHECK(copy->GetInputs() == 1);
    CHECK(copy->GetOutputs() == 4);

    // And the reloaded object still routes, and still keeps the tag.
    ListSink sink;
    YSE::pHandle sinkHandle(&sink);
    loaded.Connect(copy, 1, &sinkHandle, 0);
    copy->SetListData(0, "ctl 7 64");
    CHECK(sink.gotList);
    CHECK(sink.received == std::string("ctl 7 64"));
  }

  // ─── end to end ─────────────────────────────────────────────────────────────

  TEST_CASE("routepass: dispatches by tag and keeps it, in a real patcher (#483)") {
    // The headline use, end to end through real objects. A tagged stream is
    // split by its tag, and each branch still carries the tag — so the branch
    // can be tested again further down without the patch having to re-attach a
    // word it would then know in two places.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* split = p.CreateObject(YSE::OBJ::G_ROUTEPASS, "note ctl");
    YSE::pHandle* again = p.CreateObject(YSE::OBJ::G_ROUTEPASS, "note");
    REQUIRE(split != nullptr);
    REQUIRE(again != nullptr);

    ListSink notes;
    ListSink controls;
    ListSink rest;
    YSE::pHandle notesHandle(&notes);
    YSE::pHandle controlsHandle(&controls);
    YSE::pHandle restHandle(&rest);

    // The note branch is re-tested downstream — only possible because the tag
    // survived the first object.
    p.Connect(split, 0, again, 0);
    p.Connect(again, 0, &notesHandle, 0);
    p.Connect(split, 1, &controlsHandle, 0);
    p.Connect(split, 2, &restHandle, 0);

    split->SetListData(0, "note 60 100");
    CHECK(notes.gotList);
    CHECK(notes.received == std::string("note 60 100"));
    CHECK_FALSE(controls.gotList);
    CHECK_FALSE(rest.gotList);

    split->SetListData(0, "ctl 7 64");
    CHECK(controls.gotList);
    CHECK(controls.received == std::string("ctl 7 64"));

    split->SetListData(0, "bend 8192");
    CHECK(rest.gotList);
    CHECK(rest.received == std::string("bend 8192"));
  }

  TEST_CASE("routepass: a matched number reaches real arithmetic downstream (#483)") {
    // The numeric half of the same point, and the one with a visible
    // consequence: Max's `route 5` consumes the 5 and bangs, so a `.+ 100`
    // downstream would emit its stored left value. Here the 5 arrives, and the
    // sum says so.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* route = p.CreateObject(YSE::OBJ::G_ROUTEPASS, "5");
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::G_ADD, "100");
    REQUIRE(route != nullptr);
    REQUIRE(add != nullptr);

    FloatSink sum;
    YSE::pHandle sumHandle(&sum);
    p.Connect(route, 0, add, 0);
    p.Connect(add, 0, &sumHandle, 0);

    route->SetIntData(0, 5);
    CHECK(sum.gotFloat);
    CHECK(sum.received == doctest::Approx(105.f));

    // And an unmatched number never reaches that branch at all.
    sum.gotFloat = false;
    route->SetIntData(0, 6);
    CHECK_FALSE(sum.gotFloat);
  }

} // TEST_SUITE("patcher")
