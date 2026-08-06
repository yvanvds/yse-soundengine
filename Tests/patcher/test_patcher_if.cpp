// Tests for the conditional-dispatch object (issue #451): .if.
//
// .if deliberately owns no parser of its own — the condition and every item of
// a then/else message are compiled by the ExprProgram that .expr (#449) and
// .vexpr (#450) already share, and test_patcher_expr.cpp covers that language
// at length. What is new here, and therefore what this file covers, is the
// layer .if adds on top:
//
//   - the statement grammar: where `then` splits, where `else` splits, and
//     every way a statement can be malformed. "Fails loudly at parse time" is
//     an acceptance criterion of the issue, so each rejection has a named test.
//   - the dispatch: which branch runs, what a branch sends (bang / int / float
//     / list) and which outlet it leaves by, including the second outlet that
//     `out2` grows.
//   - the object surface every patcher object owes: inlets grown from the
//     placeholders, the hot/cold split, the params round trip and the doc
//     metadata.
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
#include "patcher/genericObjects/gIf.h"
#include "patcher/sinks.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::gIf;
using YSE::PATCHER::kIfMaxMessageItems;

namespace {

  // Holds a .if and a sink per outlet. The statement is set *before* the
  // outlets are wired, mirroring the patcher's own order
  // (ReplaceObjectUnlocked parses params on an object that is not yet
  // published) — and it has to be, because the second outlet only exists once
  // a message has asked for it with out2.
  struct IfRig {
    gIf op;
    MultiSink left;
    MultiSink right;

    explicit IfRig(const std::string& statement) {
      op.SetParams(statement);
      op.ConnectOutlet(left.GetInlet(0), 0);
      left.ConnectInlet(op.GetOutlet(0), 0);
      if (op.NumOutputs() > 1) {
        op.ConnectOutlet(right.GetInlet(0), 1);
        right.ConnectInlet(op.GetOutlet(1), 0);
      }
    }

    void Reset() {
      left.reset();
      right.reset();
    }

    // Cold inlets only store; use before Feed().
    void Set(int inlet, float value) {
      op.GetInlet(inlet)->SetFloat(value, YSE::T_GUI);
    }

    // Fires the hot inlet after clearing the sinks, so the flags describe this
    // evaluation alone.
    void Feed(float value) {
      Reset();
      op.GetInlet(0)->SetFloat(value, YSE::T_GUI);
    }

    void FeedInt(int value) {
      Reset();
      op.GetInlet(0)->SetInt(value, YSE::T_GUI);
    }

    void Bang() {
      Reset();
      op.GetInlet(0)->SetBang(YSE::T_GUI);
    }

    void FeedList(const std::string& list) {
      Reset();
      op.GetInlet(0)->SetList(list, YSE::T_GUI);
    }

    // True when the left outlet sent nothing at all this evaluation.
    bool LeftSilent() const {
      return !left.gotBang && !left.gotInt && !left.gotFloat && !left.gotList;
    }
    bool RightSilent() const {
      return !right.gotBang && !right.gotInt && !right.gotFloat && !right.gotList;
    }

    // Whatever left the given outlet, as a float.
    float Out() const {
      return left.gotInt ? (float)left.intValue : left.floatValue;
    }
    float Out2() const {
      return right.gotInt ? (float)right.intValue : right.floatValue;
    }
  };

  // The statement is expected to be rejected; returns the error so a test can
  // assert on what it says as well as that it happened. A rejected statement
  // must also leave the object inert rather than half-built.
  std::string CompileError(const std::string& statement) {
    IfRig rig(statement);
    CHECK_FALSE(rig.op.Valid());
    CHECK_FALSE(rig.op.CompileError().empty());
    CHECK(rig.op.NumInputs() == 1);
    CHECK(rig.op.NumOutputs() == 1);

    // ...and it sends nothing at all — deliberately not "the else branch".
    rig.Feed(1.f);
    CHECK(rig.LeftSilent());
    rig.Feed(0.f);
    CHECK(rig.LeftSilent());
    return rig.op.CompileError();
  }

} // namespace

TEST_SUITE("patcher") {

  // ═══ registration ═════════════════════════════════════════════════════════

  TEST_CASE("if: the object is creatable through the registry (#451)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_IF);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".if"));
    // With no statement there is still a left inlet and a left outlet.
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::ANY);
  }

  TEST_CASE("if: the object is listed by pRegistry::AllNames (#451)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".if")) != names.end());
  }

  // ═══ the statement grammar ════════════════════════════════════════════════

  TEST_CASE("if: the statement splits at then and else (#451)") {
    IfRig both("$i1 > 64 then 1 else 0");
    CHECK(both.op.Valid());
    CHECK(both.op.CompileError().empty());
    CHECK(both.op.HasElse());

    IfRig thenOnly("$i1 > 64 then 1");
    CHECK(thenOnly.op.Valid());
    CHECK_FALSE(thenOnly.op.HasElse());
  }

  TEST_CASE("if: the condition may be any .expr expression (#451)") {
    // No new syntax: whatever .expr compiles, .if accepts as a condition.
    for (const char* condition : {"$i1 > 64", "$f1 <= 0.5", "$i1 && $i2", "!$i1", "($i1 % 12) == 0",
                                  "min($i1, $i2) > 3", "$i1"}) {
      CAPTURE(condition);
      IfRig rig(std::string(condition) + " then bang");
      CHECK(rig.op.Valid());
    }
  }

  TEST_CASE("if: the inlet count covers the condition and both messages (#451)") {
    CHECK(IfRig("$i1 > 0 then bang").op.NumInputs() == 1);
    CHECK(IfRig("$i1 > $i2 then bang").op.NumInputs() == 2);
    // A placeholder that only appears in a message still grows the inlets.
    CHECK(IfRig("$i1 > 0 then $i3").op.NumInputs() == 3);
    CHECK(IfRig("$i1 > 0 then 1 else $f4").op.NumInputs() == 4);
    CHECK(IfRig("$i1 > 0 then $i9").op.NumInputs() == 9);
    // Never fewer than one, so a constant condition can still be banged.
    CHECK(IfRig("1 then bang").op.NumInputs() == 1);
  }

  TEST_CASE("if: a gap inlet exists but is marked unused (#451)") {
    IfRig rig("$i1 > 0 then $i3");
    REQUIRE(rig.op.NumInputs() == 3);
    CHECK(rig.op.UsesInlet(0));
    CHECK_FALSE(rig.op.UsesInlet(1));
    CHECK(rig.op.UsesInlet(2));
    CHECK_FALSE(rig.op.UsesInlet(3));
  }

  TEST_CASE("if: out2 grows a second outlet and nothing else does (#451)") {
    CHECK(IfRig("$i1 > 0 then 1 else 0").op.NumOutputs() == 1);
    CHECK(IfRig("$i1 > 0 then out2 1").op.NumOutputs() == 2);
    CHECK(IfRig("$i1 > 0 then 1 else out2 0").op.NumOutputs() == 2);
    CHECK(IfRig("$i1 > 0 then out2 1 else out2 0").op.NumOutputs() == 2);
    CHECK(IfRig("$i1 > 0 then out2 bang").op.NumOutputs() == 2);
  }

  // ═══ dispatch ═════════════════════════════════════════════════════════════

  TEST_CASE("if: a true condition sends the then message and a false one the else (#451)") {
    IfRig rig("$i1 > 64 then 1 else 0");
    rig.Feed(100.f);
    CHECK(rig.Out() == 1.f);
    rig.Feed(10.f);
    CHECK(rig.Out() == 0.f);
    // The boundary is the condition's, not the object's: 64 is not > 64.
    rig.Feed(64.f);
    CHECK(rig.Out() == 0.f);
    rig.Feed(65.f);
    CHECK(rig.Out() == 1.f);
  }

  TEST_CASE("if: with no else a false condition sends nothing at all (#451)") {
    // The routing half of the object: this is how a stream gets filtered.
    IfRig rig("$i1 > 64 then $i1");
    rig.Feed(100.f);
    CHECK(rig.left.gotInt);
    CHECK(rig.Out() == 100.f);

    rig.Feed(10.f);
    CHECK(rig.LeftSilent());
  }

  TEST_CASE("if: a non-zero condition of any kind is true (#451)") {
    // The condition is "non-zero", not "== 1" — a bare $i1 or a float works.
    IfRig ints("$i1 then 1 else 0");
    ints.Feed(-3.f);
    CHECK(ints.Out() == 1.f);
    ints.Feed(0.f);
    CHECK(ints.Out() == 0.f);

    IfRig floats("$f1 then 1 else 0");
    floats.Feed(0.25f);
    CHECK(floats.Out() == 1.f);
    floats.Feed(0.f);
    CHECK(floats.Out() == 0.f);
  }

  TEST_CASE("if: bang is the one message that is a word (#451)") {
    IfRig rig("$i1 > 0 then bang else 0");
    rig.Feed(1.f);
    CHECK(rig.left.gotBang);
    CHECK_FALSE(rig.left.gotInt);

    rig.Feed(-1.f);
    CHECK_FALSE(rig.left.gotBang);
    CHECK(rig.left.gotInt);
    CHECK(rig.left.intValue == 0);
  }

  TEST_CASE("if: a one-item message carries the item's C type (#451)") {
    IfRig ints("1 then $i1");
    ints.Feed(3.7f);
    CHECK(ints.left.gotInt);
    CHECK_FALSE(ints.left.gotFloat);
    CHECK(ints.left.intValue == 3);

    IfRig floats("1 then $f1");
    floats.Feed(3.5f);
    CHECK(floats.left.gotFloat);
    CHECK_FALSE(floats.left.gotInt);
    CHECK(floats.left.floatValue == doctest::Approx(3.5f));

    // A literal keeps its own type, exactly as in .expr.
    IfRig literalInt("1 then 42");
    literalInt.Bang();
    CHECK(literalInt.left.gotInt);
    IfRig literalFloat("1 then 0.5");
    literalFloat.Bang();
    CHECK(literalFloat.left.gotFloat);
  }

  TEST_CASE("if: a message item may be a space-free expression (#451)") {
    // Every item goes through the same evaluator the condition uses, so this
    // costs nothing extra and saves a .* downstream.
    IfRig rig("$i1 > 0 then $i1*2 else 0");
    rig.Feed(21.f);
    CHECK(rig.Out() == 42.f);
  }

  TEST_CASE("if: a message of two or more items is sent as a list (#451)") {
    IfRig rig("$i1 > 0 then $i1 $i1 else 0 0 0");
    rig.Feed(7.f);
    CHECK(rig.left.gotList);
    CHECK_FALSE(rig.left.gotInt);
    CHECK(rig.left.listValue == "7 7");

    rig.Feed(-1.f);
    CHECK(rig.left.gotList);
    CHECK(rig.left.listValue == "0 0 0");
  }

  TEST_CASE("if: a list message keeps each item's type (#451)") {
    // The shared, locale-free formatter: a float stays visibly a float.
    IfRig rig("1 then $i1 $f1");
    rig.Feed(2.5f);
    REQUIRE(rig.left.gotList);
    CHECK(rig.left.listValue == "2 2.5");
  }

  TEST_CASE("if: out2 routes a message to the right outlet (#451)") {
    // The issue's own example.
    IfRig rig("$i1 > 64 then bang else out2 $i1");
    REQUIRE(rig.op.NumOutputs() == 2);

    rig.Feed(100.f);
    CHECK(rig.left.gotBang);
    CHECK(rig.RightSilent());

    rig.Feed(10.f);
    CHECK(rig.LeftSilent());
    CHECK(rig.right.gotInt);
    CHECK(rig.Out2() == 10.f);
  }

  TEST_CASE("if: both messages may go to the right outlet (#451)") {
    IfRig rig("$i1 > 0 then out2 1 else out2 0");
    rig.Feed(5.f);
    CHECK(rig.LeftSilent());
    CHECK(rig.Out2() == 1.f);
    rig.Feed(-5.f);
    CHECK(rig.LeftSilent());
    CHECK(rig.Out2() == 0.f);
  }

  TEST_CASE("if: a list and a bang can be routed to the right outlet too (#451)") {
    IfRig list("1 then out2 1 2 3");
    list.Bang();
    CHECK(list.right.gotList);
    CHECK(list.right.listValue == "1 2 3");

    IfRig bang("1 then out2 bang");
    bang.Bang();
    CHECK(bang.right.gotBang);
    CHECK(bang.LeftSilent());
  }

  // ═══ the inlets ═══════════════════════════════════════════════════════════

  TEST_CASE("if: inlet 0 evaluates and the others only store (#451)") {
    IfRig rig("$i1 > $i2 then 1 else 0");
    REQUIRE(rig.op.NumInputs() == 2);

    rig.Reset();
    rig.Set(1, 10.f);
    CHECK(rig.LeftSilent());

    rig.Feed(20.f);
    CHECK(rig.Out() == 1.f);
    rig.Feed(5.f);
    CHECK(rig.Out() == 0.f);
  }

  TEST_CASE("if: stored values survive between evaluations (#451)") {
    IfRig rig("$i1 > $i2 then 1 else 0");
    rig.Set(1, 10.f);
    rig.Feed(20.f);
    CHECK(rig.Out() == 1.f);

    rig.Set(1, 100.f);
    rig.Feed(20.f);
    CHECK(rig.Out() == 0.f);
  }

  TEST_CASE("if: a bang on inlet 0 re-evaluates with the stored values (#451)") {
    IfRig rig("$i1 > $i2 then 1 else 0");
    rig.Set(1, 10.f);
    rig.Feed(20.f);
    CHECK(rig.Out() == 1.f);

    rig.Bang();
    CHECK(rig.Out() == 1.f);

    // A bang before anything arrives evaluates the zero-initialised inlets.
    IfRig fresh("$i1 > $i2 then 1 else 0");
    fresh.Bang();
    CHECK(fresh.Out() == 0.f); // 0 > 0 is false
  }

  TEST_CASE("if: an int on inlet 0 works as well as a float (#451)") {
    IfRig rig("$i1 > 64 then 1 else 0");
    rig.FeedInt(100);
    CHECK(rig.Out() == 1.f);
    rig.FeedInt(1);
    CHECK(rig.Out() == 0.f);
  }

  TEST_CASE("if: a list on inlet 0 fills the inlets and evaluates (#451)") {
    IfRig rig("$i1 > $i2 then $i3 else 0");
    REQUIRE(rig.op.NumInputs() == 3);

    rig.FeedList("5 1 99");
    CHECK(rig.Out() == 99.f);

    // Fewer items than inlets: the remaining inlets keep what they had.
    rig.FeedList("0");
    CHECK(rig.Out() == 0.f); // 0 > 1 is false -> the else message
  }

  // ═══ malformed statements ═════════════════════════════════════════════════
  // "A malformed statement fails loudly at parse time" is an acceptance
  // criterion of the issue, so every rejection is named.

  TEST_CASE("if: a statement without then is rejected (#451)") {
    CHECK(CompileError("$i1 > 64").find("'then'") != std::string::npos);
    CHECK(CompileError("bang").find("'then'") != std::string::npos);
  }

  TEST_CASE("if: a missing condition is rejected (#451)") {
    CHECK(CompileError("then bang").find("condition") != std::string::npos);
  }

  TEST_CASE("if: a condition that does not compile is rejected by the evaluator (#451)") {
    const std::string e = CompileError("$i1 > then bang");
    CHECK(e.find("condition") != std::string::npos);
    CHECK_FALSE(e.empty());

    CHECK_FALSE(CompileError("$i1 @ 2 then bang").empty());
    CHECK_FALSE(CompileError("$f10 > 1 then bang").empty());
  }

  // Max's $s placeholders need a `table` object the YSE patcher does not have;
  // the shared evaluator says so, and .if inherits the message.
  TEST_CASE("if: $s symbol placeholders are rejected with an explanation (#451)") {
    const std::string e = CompileError("$s1 > 1 then bang");
    CHECK(e.find("table access") != std::string::npos);
    CHECK(e.find("not supported") != std::string::npos);
  }

  TEST_CASE("if: a missing then message is rejected (#451)") {
    CHECK(CompileError("$i1 > 0 then").find("'then' message is missing") != std::string::npos);
    CHECK(CompileError("$i1 > 0 then else 1").find("'then' message is missing") !=
          std::string::npos);
  }

  TEST_CASE("if: a missing else message is rejected (#451)") {
    CHECK(CompileError("$i1 > 0 then 1 else").find("'else' message is missing") !=
          std::string::npos);
  }

  TEST_CASE("if: a message item that does not compile is rejected by name (#451)") {
    const std::string e = CompileError("$i1 > 0 then open");
    CHECK(e.find("\"open\"") != std::string::npos);
    CHECK(e.find("does not compile") != std::string::npos);
  }

  // Max's `send` would mean a receive-name lookup on the audio thread; the
  // patcher already has a .s object for that, so say so.
  TEST_CASE("if: the send keyword is rejected with a pointer to .s (#451)") {
    const std::string e = CompileError("$i1 > 0 then send foo 1");
    CHECK(e.find("'send'") != std::string::npos);
    CHECK(e.find(".s") != std::string::npos);
  }

  TEST_CASE("if: an out keyword other than out2 is rejected (#451)") {
    const std::string e = CompileError("$i1 > 0 then out3 1");
    CHECK(e.find("out3") != std::string::npos);
    CHECK(e.find("out2") != std::string::npos);
    CHECK_FALSE(CompileError("$i1 > 0 then out1 1").empty());
  }

  TEST_CASE("if: out2 must come first in a message (#451)") {
    CHECK(CompileError("$i1 > 0 then 1 out2 2").find("first word") != std::string::npos);
    CHECK(CompileError("$i1 > 0 then out2").find("empty after 'out2'") != std::string::npos);
  }

  TEST_CASE("if: bang may not be one item of a longer message (#451)") {
    const std::string e = CompileError("$i1 > 0 then bang 1");
    CHECK(e.find("'bang'") != std::string::npos);
    CHECK(e.find("on its own") != std::string::npos);
  }

  TEST_CASE("if: a second then or else inside a message is rejected (#451)") {
    CHECK(CompileError("$i1 > 0 then 1 then 2").find("cannot appear inside") != std::string::npos);
    CHECK(CompileError("$i1 > 0 then 1 else 2 else 3").find("cannot appear inside") !=
          std::string::npos);
  }

  TEST_CASE("if: an over-long message is rejected rather than truncated (#451)") {
    std::string statement = "1 then";
    for (int i = 0; i <= kIfMaxMessageItems; i++) {
      statement += " 1";
    }
    const std::string e = CompileError(statement);
    CHECK(e.find("more than") != std::string::npos);

    // ...and exactly the maximum is still accepted, and still sends.
    std::string ok = "1 then";
    for (int i = 0; i < kIfMaxMessageItems; i++) {
      ok += " 1";
    }
    IfRig rig(ok);
    REQUIRE(rig.op.Valid());
    rig.Bang();
    REQUIRE(rig.left.gotList);
    CHECK((int)std::count(rig.left.listValue.begin(), rig.left.listValue.end(), '1') ==
          kIfMaxMessageItems);
  }

  // ═══ params / persistence ═════════════════════════════════════════════════

  TEST_CASE("if: re-setting the statement recompiles, resizes and clears state (#451)") {
    gIf op;
    CHECK(op.NumInputs() == 1);
    CHECK(op.NumOutputs() == 1);
    CHECK_FALSE(op.Valid());

    op.SetParams("$i1 > $i3 then out2 1");
    CHECK(op.NumInputs() == 3);
    CHECK(op.NumOutputs() == 2);
    CHECK(op.Valid());

    // Shrinking works as well as growing — both the inlets and the outlet the
    // out2 keyword had grown.
    op.SetParams("$i1 > 0 then bang");
    CHECK(op.NumInputs() == 1);
    CHECK(op.NumOutputs() == 1);
    CHECK(op.Valid());

    // An empty argument returns the object to its blank state.
    op.SetParams("");
    CHECK(op.NumInputs() == 1);
    CHECK(op.NumOutputs() == 1);
    CHECK_FALSE(op.Valid());
    CHECK(op.CompileError().empty()); // no statement is not an error
  }

  TEST_CASE("if: re-setting the statement clears the stored inlet values (#451)") {
    IfRig rig("$i1 > $i2 then 1 else 0");
    rig.Set(1, 100.f);
    rig.Feed(5.f);
    CHECK(rig.Out() == 0.f);

    // The new statement re-uses inlet 1 for something else; the old value must
    // not leak into it. (In a live patcher SetParams builds a fresh object, so
    // this only matters for standalone use — which is exactly what a binding
    // driving pObject directly does.)
    rig.op.SetParams("$i1 > $i2 then 1 else 0");
    rig.Feed(5.f);
    CHECK(rig.Out() == 1.f); // 5 > 0
  }

  TEST_CASE("if: params survive a DumpJSON / ParseJSON round trip (#451)") {
    const std::string statement = "$i1 > 64 then bang else out2 $i1";

    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_IF, statement) != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".if") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".if"));
    CHECK(h->GetParams() == statement);
    // ...and the reloaded object really did recompile it.
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 2);
  }

  TEST_CASE("if: an operator-dense statement survives the params round trip (#451)") {
    // The param string is split on spaces and glued back together, so a
    // statement whose spacing is unusual is the interesting case.
    const std::string statement = "$i1%12==0 then $i1*2 else 0";

    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_IF, statement) != nullptr);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(src.DumpJSON());
    REQUIRE(loaded.Objects() == 1);
    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(h->GetParams() == statement);
    CHECK(h->GetInputs() == 1);
  }

  TEST_CASE("if: names its single parameter (#451)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_IF));
    REQUIRE(obj != nullptr);
    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 1);
    CHECK(docs[0].name == "statement");
  }

  // ═══ documentation ════════════════════════════════════════════════════════

  TEST_CASE("if: documents itself as GENERIC with a labelled port pair (#451)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_IF));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    CHECK_FALSE(obj->GetDescription().empty());

    REQUIRE(obj->NumInputs() == 1);
    CHECK(obj->GetInlet(0)->GetDocLabel() == std::string("$1"));
    CHECK_FALSE(obj->GetInlet(0)->GetDocDescription().empty());

    REQUIRE(obj->NumOutputs() == 1);
    CHECK(obj->GetOutputType(0) == YSE::OUT_TYPE::ANY);
    CHECK(obj->GetOutlet(0)->GetDocLabel() == std::string("out"));
    CHECK_FALSE(obj->GetOutlet(0)->GetDocDescription().empty());
  }

  // The doc-coverage test only sees the object the registry builds, which has
  // one inlet and one outlet; the ports ParseParams adds must carry labels too.
  TEST_CASE("if: the ports created from the statement are documented (#451)") {
    gIf op;
    op.SetParams("$i1 > $i3 then out2 bang");
    REQUIRE(op.NumInputs() == 3);
    REQUIRE(op.NumOutputs() == 2);

    const std::vector<std::string> labels = {"$1", "$2", "$3"};
    for (int i = 0; i < 3; i++) {
      CAPTURE(i);
      CHECK(op.GetInlet(i)->GetDocLabel() == labels[(size_t)i]);
      CHECK_FALSE(op.GetInlet(i)->GetDocDescription().empty());
    }
    // The gap inlet says it is a gap rather than pretending to be useful.
    CHECK(op.GetInlet(1)->GetDocDescription().find("Unused") != std::string::npos);

    CHECK(op.GetOutlet(1)->GetDocLabel() == std::string("out2"));
    CHECK_FALSE(op.GetOutlet(1)->GetDocDescription().empty());
    CHECK(op.GetOutputType(1) == YSE::OUT_TYPE::ANY);
  }

  // ═══ the RT contract ══════════════════════════════════════════════════════

  TEST_CASE("if: dispatching does not change the compiled statement (#451)") {
    // The whole RT argument in one assertion: everything Calculate() touches
    // was built by ParseParams. If a branch ever recompiled, resolved a name or
    // grew a buffer, these would move.
    IfRig rig("$i1 % 12 == 0 then $i1 $i1 $i1 else out2 $f1");
    REQUIRE(rig.op.Valid());
    const std::size_t size = rig.op.Condition().Size();
    const int depth = rig.op.Condition().StackDepth();
    const int inlets = rig.op.NumInputs();
    const int outlets = rig.op.NumOutputs();

    for (int i = 0; i < 1000; i++) {
      rig.Feed((float)i);
      CHECK((rig.left.gotList || rig.right.gotFloat));
    }

    CHECK(rig.op.Condition().Size() == size);
    CHECK(rig.op.Condition().StackDepth() == depth);
    CHECK(rig.op.NumInputs() == inlets);
    CHECK(rig.op.NumOutputs() == outlets);
    CHECK(rig.op.Valid());
  }

  // ═══ the use case from the issue ══════════════════════════════════════════

  TEST_CASE("if: replaces a comparison plus a gate plus a select (#451)") {
    // "if $i1 > 64 then bang else out2 $i1 in one object."
    IfRig rig("$i1 > 64 then bang else out2 $i1");
    REQUIRE(rig.op.NumInputs() == 1);
    REQUIRE(rig.op.NumOutputs() == 2);

    for (int velocity = 60; velocity <= 70; velocity++) {
      CAPTURE(velocity);
      rig.FeedInt(velocity);
      if (velocity > 64) {
        CHECK(rig.left.gotBang);
        CHECK(rig.RightSilent());
      } else {
        CHECK(rig.LeftSilent());
        CHECK(rig.right.gotInt);
        CHECK(rig.right.intValue == velocity);
      }
    }
  }

} // TEST_SUITE("patcher")
