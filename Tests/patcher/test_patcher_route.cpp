// Tests for .route (issue #672) — route a message by what its first item
// matches, and take that item off.
//
// The object had two defects, and both are pinned here because both are the
// kind a plausible implementation reintroduces:
//
//   - **the matched selector was never stripped.** What .route implemented was
//     Max's `routepass`, while the engine's own .sel documentation described
//     Max's `route` ("a list is matched on its first element alone and the rest
//     is dropped — .route is the object that keeps the remainder"). Every
//     "which outlet fired" assertion passes either way, so what *arrives* at
//     the sink is asserted here, never merely where it arrived.
//   - **a float could not match.** The matcher compared std::to_string(value)
//     against the selector text, and std::to_string(5.f) is "5.000000", a
//     spelling no hand-written selector ever has — so `.route 5` could not
//     match the float 5.0 and `.route 5.0` could not either. The float cases
//     are asserted directly.
//
// The stripped remainder has a *kind*, which is the subtle half of the fix: the
// patcher carries a list as text, so `foo 1` has to come back out as the int 1
// rather than as a one-element list, `foo 1.5` as a float, and `foo` alone as a
// bang. Each of those shapes has its own case.
//
// No audio device required.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pEnums.h"
#include "patcher/inlet.h"
#include "patcher/genericObjects/gRoute.h"
#include "patcher/sinks.hpp"

namespace {
  using TestHelpers::FloatSink;
  using TestHelpers::MultiSink;
  using TestHelpers::OrderSink;
  using YSE::PATCHER::gRoute;

  // One order-logging sink per outlet, all sharing one log. Outlet i is tagged
  // 'a' + i, so a single message reads back as the outlet it left by — and,
  // because OrderSink records the kind and the value too, as exactly what left
  // it. A sink per outlet rather than one on the outlet under test is what
  // makes "exactly one outlet fired" assertable.
  struct Rig {
    gRoute op;
    std::vector<std::unique_ptr<OrderSink>> sinks;
    std::vector<char> order;

    explicit Rig(const std::string& args) {
      op.SetParams(args);
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

    // The outlets that fired, in order, as tags.
    std::string Log() const {
      return std::string(order.begin(), order.end());
    }
    OrderSink& Sink(int index) {
      return *sinks[(std::size_t)index];
    }
    OrderSink& RestSink() {
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

  // ─── the headline: the matched selector is consumed ──────────────────────────

  TEST_CASE("route: a matched list arrives without its selector (#672)") {
    // Max: "the rest of the message is sent out the outlet that corresponds to
    // that argument". The object used to forward "note 60 100" whole, which is
    // routepass's contract, not this one's.
    Rig rig("note ctl");
    rig.List("note 60 100");
    CHECK(rig.Log() == std::string("a"));
    CHECK(rig.Sink(0).lastKind == OrderSink::LIST);
    CHECK(rig.Sink(0).lastList == std::string("60 100"));
  }

  TEST_CASE("route: a remainder of one number arrives as that number (#672)") {
    // Not a one-element list: in Max `route foo` turns `foo 1` into the int 1,
    // and everything downstream that tells an int from a list (.i, .sel, .match)
    // has to see the same thing here.
    Rig rig("note");
    rig.List("note 60");
    CHECK(rig.Log() == std::string("a"));
    CHECK(rig.Sink(0).lastKind == OrderSink::INT);
    CHECK(rig.Sink(0).lastInt == 60);
  }

  TEST_CASE("route: a remainder spelled as a float arrives as a float (#672)") {
    // The int/float decision is the spelling, the test .trigger and .match
    // already share — so a patch that sent `note 60` does not get `60.` back.
    Rig rig("note");
    rig.List("note 60.5");
    CHECK(rig.Sink(0).lastKind == OrderSink::FLOAT);
    CHECK(rig.Sink(0).lastFloat == doctest::Approx(60.5f));

    rig.Reset();
    rig.List("note 60.");
    CHECK(rig.Sink(0).lastKind == OrderSink::FLOAT);
    CHECK(rig.Sink(0).lastFloat == doctest::Approx(60.f));

    rig.Reset();
    rig.List("note 1e3");
    CHECK(rig.Sink(0).lastKind == OrderSink::FLOAT);
    CHECK(rig.Sink(0).lastFloat == doctest::Approx(1000.f));
  }

  TEST_CASE("route: a whole number too large for an int arrives as a float (#672)") {
    // Casting a float outside the int range is undefined behaviour, so the
    // value is kept rather than the type.
    Rig rig("note");
    rig.List("note 5000000000");
    CHECK(rig.Sink(0).lastKind == OrderSink::FLOAT);
    CHECK(rig.Sink(0).lastFloat == doctest::Approx(5000000000.f));
  }

  TEST_CASE("route: a remainder of one symbol arrives as a list (#672)") {
    // A lone symbol has no type of its own in this patcher, so it stays text.
    Rig rig("set");
    rig.List("set freq");
    CHECK(rig.Sink(0).lastKind == OrderSink::LIST);
    CHECK(rig.Sink(0).lastList == std::string("freq"));
  }

  TEST_CASE("route: a message that was only the selector arrives as a bang (#672)") {
    // Max: "If the first item of a message matches one of the arguments, but
    // the message has no additional items, bang is sent out the specified
    // outlet."
    Rig rig("note ctl");
    rig.List("note");
    CHECK(rig.Log() == std::string("a"));
    CHECK(rig.Sink(0).lastKind == OrderSink::BANG);
  }

  TEST_CASE("route: a matched bare number arrives as a bang (#672)") {
    // The number was the whole message, so there is nothing left of it — the
    // clearest difference from .routepass, which sends the number itself.
    Rig rig("5");
    rig.Int(5);
    CHECK(rig.Log() == std::string("a"));
    CHECK(rig.Sink(0).lastKind == OrderSink::BANG);

    rig.Reset();
    rig.Float(5.f);
    CHECK(rig.Log() == std::string("a"));
    CHECK(rig.Sink(0).lastKind == OrderSink::BANG);
  }

  TEST_CASE("route: the remainder is trimmed of the separators around it (#672)") {
    // Leading whitespace must not hide the first item, and the separator
    // between the selector and the rest is not part of the rest.
    Rig rig("note");
    rig.List("   note   60 100");
    CHECK(rig.Log() == std::string("a"));
    CHECK(rig.Sink(0).lastKind == OrderSink::LIST);
    CHECK(rig.Sink(0).lastList == std::string("60 100"));

    rig.Reset();
    // And a trailing separator does not turn a lone number into a list.
    rig.List("note 60 ");
    CHECK(rig.Sink(0).lastKind == OrderSink::INT);
    CHECK(rig.Sink(0).lastInt == 60);
  }

  // ─── the second defect: matching a float ────────────────────────────────────

  TEST_CASE("route: a numeric selector answers both the int and the float (#672)") {
    // The regression this file exists for as much as the stripping: the object
    // compared std::to_string(value) — "5.000000" — against the selector text,
    // so neither spelling of the selector could ever match a float.
    Rig five("5");
    five.Float(5.f);
    CHECK(five.Log() == std::string("a"));
    five.Reset();
    five.Int(5);
    CHECK(five.Log() == std::string("a"));

    Rig spelled("5.0");
    spelled.Float(5.f);
    CHECK(spelled.Log() == std::string("a"));
    spelled.Reset();
    spelled.Int(5);
    CHECK(spelled.Log() == std::string("a"));
  }

  TEST_CASE("route: a numeric selector matches a numeric leading token by value (#672)") {
    // `.route 5` answers the list `5 6` — and answers `5.0 6` too, because the
    // token is read as a number rather than compared as text.
    Rig rig("5");
    rig.List("5 6");
    CHECK(rig.Log() == std::string("a"));
    CHECK(rig.Sink(0).lastKind == OrderSink::INT);
    CHECK(rig.Sink(0).lastInt == 6);

    rig.Reset();
    rig.List("5.0 6");
    CHECK(rig.Log() == std::string("a"));
    CHECK(rig.Sink(0).lastInt == 6);
  }

  TEST_CASE("route: a number and a symbol never match each other (#672)") {
    Rig symbolic("note");
    symbolic.Int(5);
    CHECK(symbolic.Log() == std::string("b"));
    CHECK(symbolic.RestSink().lastKind == OrderSink::INT);

    Rig numeric("5");
    numeric.List("note 60");
    CHECK(numeric.Log() == std::string("b"));
    numeric.Reset();
    // A token that merely contains the digits is still a symbol.
    numeric.List("5abc 1");
    CHECK(numeric.Log() == std::string("b"));
  }

  TEST_CASE("route: a NaN matches nothing (#672)") {
    // A NaN compares unequal to everything including itself, so it leaves the
    // fall-through outlet rather than the outlet a patch wired for a real 0.
    Rig rig("0");
    rig.Float(std::nanf(""));
    CHECK(rig.Log() == std::string("b"));
  }

  // ─── which outlet, and how many ─────────────────────────────────────────────

  TEST_CASE("route: a list is matched on its first element alone (#672)") {
    // `.route 60` must not answer `note 60`: scanning the message for the
    // selector passes every single-item test and fails here.
    Rig rig("60");
    rig.List("note 60");
    CHECK(rig.Log() == std::string("b"));
    CHECK(rig.RestSink().lastList == std::string("note 60"));
  }

  TEST_CASE("route: each selector gets its own outlet, in order (#672)") {
    Rig rig("note ctl bend");
    rig.List("ctl 7 64");
    CHECK(rig.Log() == std::string("b"));
    CHECK(rig.Sink(1).lastList == std::string("7 64"));

    rig.Reset();
    rig.List("bend 8192");
    CHECK(rig.Log() == std::string("c"));
    CHECK(rig.Sink(2).lastKind == OrderSink::INT);
    CHECK(rig.Sink(2).lastInt == 8192);
  }

  TEST_CASE("route: exactly one outlet fires per input (#672)") {
    Rig rig("note ctl");
    rig.List("note 60");
    CHECK(rig.Total() == 1);
    rig.Reset();
    rig.List("nothing at all");
    CHECK(rig.Total() == 1);
  }

  TEST_CASE("route: a repeated selector uses the leftmost of its outlets (#672)") {
    Rig rig("note note");
    rig.List("note 60");
    CHECK(rig.Log() == std::string("a"));
    CHECK(rig.Total() == 1);
  }

  TEST_CASE("route: a bang matches a selector spelled 'bang' (#672)") {
    // There is nothing in a bang to strip, so a bang leaves as a bang whichever
    // outlet it takes.
    Rig rig("note bang");
    rig.Bang();
    CHECK(rig.Log() == std::string("b"));
    CHECK(rig.Sink(1).lastKind == OrderSink::BANG);

    Rig none("note ctl");
    none.Bang();
    CHECK(none.Log() == std::string("c"));
    CHECK(none.RestSink().lastKind == OrderSink::BANG);
  }

  TEST_CASE("route: an empty message matches nothing (#672)") {
    Rig rig("note");
    rig.List("");
    CHECK(rig.Log() == std::string("b"));
    rig.Reset();
    rig.List("   ");
    CHECK(rig.Log() == std::string("b"));
  }

  TEST_CASE("route: the fall-through outlet passes each kind through whole (#672)") {
    // Max: "If the first item does not match any of the arguments, the entire
    // message is passed out the rightmost outlet." Nothing is stripped there —
    // which is what lets a chain of .route objects be strung together.
    Rig rig("nothingmatchesthis");

    rig.Int(42);
    CHECK(rig.RestSink().lastKind == OrderSink::INT);
    CHECK(rig.RestSink().lastInt == 42);

    rig.Float(1.25f);
    CHECK(rig.RestSink().lastKind == OrderSink::FLOAT);
    CHECK(rig.RestSink().lastFloat == doctest::Approx(1.25f));

    rig.List("ctl 7 64");
    CHECK(rig.RestSink().lastKind == OrderSink::LIST);
    CHECK(rig.RestSink().lastList == std::string("ctl 7 64"));

    rig.Bang();
    CHECK(rig.RestSink().lastKind == OrderSink::BANG);

    CHECK(rig.Log() == std::string("bbbb"));
  }

  // ─── shape and lifecycle ────────────────────────────────────────────────────

  TEST_CASE("route: an object whose list was never set drops messages (#672)") {
    // The outlets are built by the parameter callbacks, so a bare `.route` has
    // none at all and there is nowhere for a message to go. It must not reach
    // for an outlet that is not there.
    gRoute op;
    REQUIRE(op.NumOutputs() == 0);
    op.GetInlet(0)->SetList("note 60", YSE::T_GUI);
    op.GetInlet(0)->SetInt(5, YSE::T_GUI);
    op.GetInlet(0)->SetFloat(5.f, YSE::T_GUI);
    op.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(op.NumOutputs() == 0);
  }

  TEST_CASE("route: Calculate() emits nothing (#672)") {
    Rig rig("note ctl");
    rig.List("note 60");
    rig.Reset();
    for (int i = 0; i < 8; i++)
      rig.op.Calculate(YSE::T_DSP);
    CHECK(rig.Total() == 0);
  }

  TEST_CASE("route: survives a DumpJSON / ParseJSON round trip (#672)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_ROUTE, "note ctl 5");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".route") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".route"));
    CHECK(copy->GetParams() == std::string("note ctl 5"));
    CHECK(copy->GetInputs() == 1);
    CHECK(copy->GetOutputs() == 4);

    // And the reloaded object still routes, and still strips.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    loaded.Connect(copy, 1, &sinkHandle, 0);
    copy->SetListData(0, "ctl 7 64");
    CHECK(sink.gotList);
    CHECK(sink.listValue == std::string("7 64"));
  }

  // ─── end to end, through a real patcher graph ───────────────────────────────

  TEST_CASE("route: dispatches a tagged stream and hands each branch bare values (#672)") {
    // The headline use, end to end through real objects: a tagged stream is
    // split by its tag and each branch receives only what follows the tag, so
    // the branch works in bare values. Anything unmatched leaves the
    // fall-through whole, which is what lets the next .route test it.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* first = p.CreateObject(YSE::OBJ::G_ROUTE, "note ctl");
    YSE::pHandle* second = p.CreateObject(YSE::OBJ::G_ROUTE, "bend");
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(first->GetOutputs() == 3);

    MultiSink notes;
    MultiSink controls;
    MultiSink bends;
    MultiSink rest;
    YSE::pHandle notesHandle(&notes);
    YSE::pHandle controlsHandle(&controls);
    YSE::pHandle bendsHandle(&bends);
    YSE::pHandle restHandle(&rest);

    p.Connect(first, 0, &notesHandle, 0);
    p.Connect(first, 1, &controlsHandle, 0);
    p.Connect(first, 2, second, 0); // the fall-through chains into the next test
    p.Connect(second, 0, &bendsHandle, 0);
    p.Connect(second, 1, &restHandle, 0);

    first->SetListData(0, "note 60 100");
    CHECK(notes.gotList);
    CHECK(notes.listValue == std::string("60 100"));
    CHECK_FALSE(controls.gotList);
    CHECK_FALSE(bends.gotList);

    first->SetListData(0, "ctl 7 64");
    CHECK(controls.gotList);
    CHECK(controls.listValue == std::string("7 64"));

    // Unmatched by the first object, matched by the second — only possible
    // because the fall-through outlet passes the message on whole.
    first->SetListData(0, "bend 8192");
    CHECK(bends.gotInt);
    CHECK(bends.intValue == 8192);
    CHECK_FALSE(rest.gotList);

    first->SetListData(0, "aftertouch 5");
    CHECK(rest.gotList);
    CHECK(rest.listValue == std::string("aftertouch 5"));
  }

  TEST_CASE("route: the stripped remainder reaches real arithmetic downstream (#672)") {
    // The consequence a patch actually feels: the note branch feeds a `.+ 100`
    // directly, with no object in between to take the word off. Before #672 the
    // list "note 60" arrived at the adder instead of the number 60.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* route = p.CreateObject(YSE::OBJ::G_ROUTE, "note");
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::G_ADD, "100");
    REQUIRE(route != nullptr);
    REQUIRE(add != nullptr);

    FloatSink sum;
    YSE::pHandle sumHandle(&sum);
    p.Connect(route, 0, add, 0);
    p.Connect(add, 0, &sumHandle, 0);

    route->SetListData(0, "note 60");
    CHECK(sum.gotFloat);
    CHECK(sum.received == doctest::Approx(160.f));

    // And a message for another branch never reaches the adder at all.
    sum.gotFloat = false;
    route->SetListData(0, "ctl 7");
    CHECK_FALSE(sum.gotFloat);
  }

} // TEST_SUITE("patcher")
