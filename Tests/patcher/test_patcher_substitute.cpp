// Tests for .substitute (issue #488) — find-and-replace inside a message.
//
// What the object has to get right:
//
//   - **it edits the middle of a message.** That is the whole reason it exists:
//     .prepend / .append reach only the ends of a message and the routing
//     family reads only the word it starts with, so until this object a patch
//     could not rewrite an element in place without a round trip out to the
//     host application.
//   - **exactly one outlet fires, and which one is the answer.** Max: "if no
//     substitution occurred ... the original input message is passed out the
//     rightmost outlet". A left outlet that also fired for an unedited message
//     would make the right one a duplicate of it and cost the object its only
//     way of reporting that the edit happened. So every case below asserts on
//     *both* sinks, not just the one it expects.
//   - **with nothing to match it is a wire, type included.** An int must come
//     out an int, or dropping an unconfigured .substitute into a working patch
//     would break every numeric object downstream.
//   - **a matched scalar keeps its type.** `.substitute 5 7` turns the int 5
//     into the int 7, not into the one-token list that spells it.
//   - **inlet 0 has no reserved words in it.** The messages this object edits
//     are exactly the ones that begin with a word, so the match and the
//     replacement get inlets of their own — and setting either emits nothing
//     and does not rewrite the creation arguments.
//   - **an over-long token is refused, not truncated.** Half a symbol is a
//     different symbol.
//
// No audio device and no engine of its own.

#include <doctest/doctest.h>
#include <string>

#include "patcher/genericObjects/gSubstitute.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::gSubstitute;

namespace {

  // A standalone object with a sink on each of its two outlets. Standalone on
  // purpose: the object needs no patcher at all, and a test that needed one
  // could not tell a dropped message from a message the patcher never
  // delivered. Both sinks are asserted on in every case, since "exactly one
  // outlet fires" is half the contract.
  struct Rig {
    MultiSink out;
    MultiSink rest;

    void Wire(gSubstitute& obj) {
      TestHelpers::Wire(obj, 0, out);
      TestHelpers::Wire(obj, 1, rest);
    }

    void reset() {
      out.reset();
      rest.reset();
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("substitute: registered, with three inlets and two outlets (#488)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_SUBSTITUTE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".substitute");
    CHECK(h->GetInputs() == 3);
    CHECK(h->GetOutputs() == 2);
  }

  TEST_CASE("substitute: appears in the registry's name list (#488)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_SUBSTITUTE)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("substitute: inlet 0 takes everything, the setting inlets decline a bang (#488)") {
    // Nothing for a bang to do on either setting inlet, so it stays out of the
    // reported contract — the .decode / .router / .prepend discipline.
    gSubstitute obj;
    const unsigned int data = obj.GetInlet(0)->GetAcceptedTypes();
    CHECK((data & YSE::PATCHER::IT_BANG) != 0);
    CHECK((data & YSE::PATCHER::IT_INT) != 0);
    CHECK((data & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((data & YSE::PATCHER::IT_LIST) != 0);

    for (int pin = 1; pin <= 2; pin++) {
      CAPTURE(pin);
      const unsigned int set = obj.GetInlet(pin)->GetAcceptedTypes();
      CHECK((set & YSE::PATCHER::IT_BANG) == 0);
      CHECK((set & YSE::PATCHER::IT_INT) != 0);
      CHECK((set & YSE::PATCHER::IT_FLOAT) != 0);
      CHECK((set & YSE::PATCHER::IT_LIST) != 0);
    }
  }

  // ─── the substitution ───────────────────────────────────────────────────────

  TEST_CASE("substitute: a symbol in the middle of a list is replaced (#488)") {
    // The edit no other object can make: not the first element, not an end.
    Rig rig;
    gSubstitute obj;
    obj.SetParams("note ctl");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("play note 60", YSE::T_GUI);
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "play ctl 60");
    CHECK_FALSE(rig.rest.gotList);
  }

  TEST_CASE("substitute: every matching element is replaced by default (#488)") {
    Rig rig;
    gSubstitute obj;
    obj.SetParams("a x");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("a b a c a", YSE::T_GUI);
    CHECK(rig.out.listValue == "x b x c x");
    CHECK_FALSE(rig.rest.gotList);
  }

  TEST_CASE("substitute: a third argument replaces the first match only (#488)") {
    // Max: "Any third number or symbol sets the 'replace first message only'
    // mode ... Only the first instance of the specified match will be replaced."
    Rig rig;
    gSubstitute obj;
    obj.SetParams("a x first");
    rig.Wire(obj);
    CHECK(obj.FirstOnly());

    obj.GetInlet(0)->SetList("a b a", YSE::T_GUI);
    CHECK(rig.out.listValue == "x b a");
    CHECK_FALSE(rig.rest.gotList);
  }

  TEST_CASE("substitute: a numeric match answers an int and a float alike (#488)") {
    // One numeric type in this patcher, the rule .sel and .routepass already
    // apply — `.substitute 5 x` cannot mean the int 5 only.
    Rig rig;
    gSubstitute obj;
    obj.SetParams("60 61");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("note 60", YSE::T_GUI);
    CHECK(rig.out.listValue == "note 61");

    rig.reset();
    obj.GetInlet(0)->SetList("note 60.0", YSE::T_GUI);
    CHECK(rig.out.listValue == "note 61");
  }

  TEST_CASE("substitute: a number and a symbol never match each other (#488)") {
    Rig rig;
    gSubstitute symbolMatch;
    symbolMatch.SetParams("60x y");
    rig.Wire(symbolMatch);

    // "60x" is not a number, so the numeric element 60 is not it.
    symbolMatch.GetInlet(0)->SetList("60", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);
    CHECK(rig.rest.gotList);
    CHECK(rig.rest.listValue == "60");

    rig.reset();
    symbolMatch.GetInlet(0)->SetList("60x", YSE::T_GUI);
    CHECK(rig.out.listValue == "y");
  }

  TEST_CASE("substitute: a matched scalar keeps a type, chosen by the replacement (#488)") {
    // The reason the outlet is an ANY outlet: an int that was rewritten must
    // stay an int, or the numeric objects downstream break.
    Rig intRig;
    gSubstitute toInt;
    toInt.SetParams("5 7");
    intRig.Wire(toInt);
    toInt.GetInlet(0)->SetInt(5, YSE::T_GUI);
    CHECK(intRig.out.gotInt);
    CHECK(intRig.out.intValue == 7);
    CHECK_FALSE(intRig.out.gotList);
    CHECK_FALSE(intRig.rest.gotInt);

    Rig floatRig;
    gSubstitute toFloat;
    toFloat.SetParams("5 7.5");
    floatRig.Wire(toFloat);
    toFloat.GetInlet(0)->SetFloat(5.f, YSE::T_GUI);
    CHECK(floatRig.out.gotFloat);
    CHECK(floatRig.out.floatValue == doctest::Approx(7.5f));

    // A symbolic replacement has no number in it, so the message becomes one.
    Rig symbolRig;
    gSubstitute toSymbol;
    toSymbol.SetParams("5 note");
    symbolRig.Wire(toSymbol);
    toSymbol.GetInlet(0)->SetInt(5, YSE::T_GUI);
    CHECK(symbolRig.out.gotList);
    CHECK(symbolRig.out.listValue == "note");
  }

  TEST_CASE("substitute: a bang matches a match spelled 'bang' (#488)") {
    // Max: "The bang message matches a 'bang' symbol in the arguments" —
    // .sel's and .routepass's rule, so all three agree on what a bang equals.
    Rig rig;
    gSubstitute obj;
    obj.SetParams("bang go");
    rig.Wire(obj);

    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "go");
    CHECK_FALSE(rig.rest.gotBang);
  }

  TEST_CASE("substitute: a match with no replacement takes Max's 0 (#488)") {
    // Not an empty replacement: that would leave a doubled separator, and so an
    // empty token in the message that no .route matches.
    Rig rig;
    gSubstitute obj;
    obj.SetParams("foo");
    rig.Wire(obj);
    CHECK(obj.Replacement() == "0");

    obj.GetInlet(0)->SetList("foo bar", YSE::T_GUI);
    CHECK(rig.out.listValue == "0 bar");
  }

  // ─── the rightmost outlet ───────────────────────────────────────────────────

  TEST_CASE("substitute: an unmatched message leaves the rightmost outlet in its own type (#488)") {
    Rig rig;
    gSubstitute obj;
    obj.SetParams("note ctl");
    rig.Wire(obj);

    obj.GetInlet(0)->SetInt(7, YSE::T_GUI);
    CHECK(rig.rest.gotInt);
    CHECK(rig.rest.intValue == 7);
    CHECK_FALSE(rig.rest.gotList);
    CHECK_FALSE(rig.out.gotInt);

    rig.reset();
    obj.GetInlet(0)->SetFloat(0.25f, YSE::T_GUI);
    CHECK(rig.rest.gotFloat);
    CHECK(rig.rest.floatValue == doctest::Approx(0.25f));
    CHECK_FALSE(rig.out.gotFloat);

    rig.reset();
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.rest.gotBang);
    CHECK_FALSE(rig.out.gotBang);

    rig.reset();
    obj.GetInlet(0)->SetList("ctl 60", YSE::T_GUI);
    CHECK(rig.rest.gotList);
    CHECK(rig.rest.listValue == "ctl 60");
    CHECK_FALSE(rig.out.gotList);
  }

  TEST_CASE("substitute: an unmatched list keeps its own spelling (#488)") {
    // Forwarded by reference rather than rebuilt, so the run of spaces survives
    // — the rewrite path is the only one that normalises separators.
    Rig rig;
    gSubstitute obj;
    obj.SetParams("note ctl");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("a  b", YSE::T_GUI);
    CHECK(rig.rest.listValue == "a  b");
  }

  TEST_CASE("substitute: with nothing to match the object is a wire (#488)") {
    // Max defaults both arguments to 0, which would route zeroes out the other
    // outlet than everything else. An unconfigured object is a wire instead —
    // the .prepend rule.
    Rig rig;
    gSubstitute obj;
    rig.Wire(obj);
    CHECK(obj.Match().empty());

    obj.GetInlet(0)->SetInt(0, YSE::T_GUI);
    CHECK(rig.rest.gotInt);
    CHECK(rig.rest.intValue == 0);
    CHECK_FALSE(rig.out.gotInt);
    CHECK_FALSE(rig.out.gotList);

    rig.reset();
    obj.GetInlet(0)->SetList("a b", YSE::T_GUI);
    CHECK(rig.rest.listValue == "a b");
    CHECK_FALSE(rig.out.gotList);

    rig.reset();
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.rest.gotBang);
    CHECK_FALSE(rig.out.gotBang);
  }

  // ─── the setting inlets ─────────────────────────────────────────────────────

  TEST_CASE("substitute: inlet 1 replaces the match and emits nothing (#488)") {
    Rig rig;
    gSubstitute obj;
    obj.SetParams("note ctl");
    rig.Wire(obj);

    obj.GetInlet(1)->SetList("chord", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);
    CHECK_FALSE(rig.rest.gotList);
    CHECK(obj.Match() == "chord");

    obj.GetInlet(0)->SetList("chord 60", YSE::T_GUI);
    CHECK(rig.out.listValue == "ctl 60");
  }

  TEST_CASE("substitute: inlet 2 replaces the replacement, on its own (#488)") {
    // The reason the two halves have an inlet each rather than sharing one: a
    // patch can re-aim what a match becomes without restating the match.
    Rig rig;
    gSubstitute obj;
    obj.SetParams("note ctl");
    rig.Wire(obj);

    obj.GetInlet(2)->SetList("bend", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);
    CHECK(obj.Match() == "note");
    CHECK(obj.Replacement() == "bend");

    obj.GetInlet(0)->SetList("note 60", YSE::T_GUI);
    CHECK(rig.out.listValue == "bend 60");
  }

  TEST_CASE(
      "substitute: the setting inlets take a number, spelled as an argument spells it (#488)") {
    gSubstitute obj;

    obj.GetInlet(1)->SetInt(60, YSE::T_GUI);
    CHECK(obj.Match() == "60");
    obj.GetInlet(2)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(obj.Replacement() == "0.5");
  }

  TEST_CASE("substitute: the setting inlets take the first word only (#488)") {
    // A match and a replacement are single elements — several words would be
    // several elements, which is a different object.
    gSubstitute obj;

    obj.GetInlet(1)->SetList("  note  60 ", YSE::T_GUI);
    CHECK(obj.Match() == "note");
    obj.GetInlet(2)->SetList(" ctl 61", YSE::T_GUI);
    CHECK(obj.Replacement() == "ctl");
  }

  TEST_CASE("substitute: an empty match clears it, an empty replacement is ignored (#488)") {
    Rig rig;
    gSubstitute obj;
    obj.SetParams("note ctl");
    rig.Wire(obj);

    // No such thing as an empty replacement, so this is nothing to do rather
    // than a request.
    obj.GetInlet(2)->SetList("   ", YSE::T_GUI);
    CHECK(obj.Replacement() == "ctl");

    // An empty match is a real request: it returns the object to being a wire.
    obj.GetInlet(1)->SetList("   ", YSE::T_GUI);
    CHECK(obj.Match().empty());

    obj.GetInlet(0)->SetList("note 60", YSE::T_GUI);
    CHECK(rig.rest.listValue == "note 60");
    CHECK_FALSE(rig.out.gotList);
  }

  TEST_CASE("substitute: a token longer than the capacity is refused, not truncated (#488)") {
    gSubstitute obj;
    obj.SetParams("note ctl");

    const std::string atCapacity(gSubstitute::TOKEN_CAPACITY, 'x');
    obj.GetInlet(1)->SetList(atCapacity, YSE::T_GUI);
    CHECK(obj.Match() == atCapacity);

    const std::string tooLong(gSubstitute::TOKEN_CAPACITY + 1, 'y');
    obj.GetInlet(1)->SetList(tooLong, YSE::T_GUI);
    // The previous match is kept whole rather than replaced by a prefix of the
    // new one.
    CHECK(obj.Match() == atCapacity);

    obj.GetInlet(2)->SetList(tooLong, YSE::T_GUI);
    CHECK(obj.Replacement() == "ctl");
  }

  TEST_CASE("substitute: an over-long creation argument leaves a wire (#488)") {
    // Control thread, so this one is logged as well as refused; what matters
    // here is that the object does not match half a symbol.
    Rig rig;
    gSubstitute obj;
    obj.SetParams(std::string(gSubstitute::TOKEN_CAPACITY + 1, 'z') + " x");
    rig.Wire(obj);
    CHECK(obj.Match().empty());

    obj.GetInlet(0)->SetInt(5, YSE::T_GUI);
    CHECK(rig.rest.gotInt);
    CHECK_FALSE(rig.out.gotInt);
  }

  // ─── parameters ─────────────────────────────────────────────────────────────

  TEST_CASE("substitute: SetParams(\"\") is a real reset, not a no-op (#488)") {
    // Parameters::Set returns early on an empty argument string, so only the
    // clear callback makes this a reset.
    gSubstitute obj;
    obj.SetParams("a x first");
    REQUIRE(obj.Match() == "a");
    REQUIRE(obj.FirstOnly());

    obj.SetParams("");
    CHECK(obj.Match().empty());
    CHECK(obj.Replacement() == "0");
    CHECK_FALSE(obj.FirstOnly());
  }

  TEST_CASE("substitute: the match and replacement are run-time state, not parameters (#488)") {
    // What a saved patch carries is what the object started with. Re-aiming it
    // is a message, exactly as .prepend's stored message and .forward's
    // destination are, and a message must not rewrite the file.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_SUBSTITUTE, "note ctl");
    REQUIRE(h != nullptr);

    h->SetListData(1, "chord");
    h->SetListData(2, "bend");
    CHECK(h->GetParams() == std::string("note ctl"));
  }

  TEST_CASE("substitute: params survive a DumpJSON / ParseJSON round trip (#488)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_SUBSTITUTE, "note ctl") != nullptr);
    REQUIRE(src.CreateObject(YSE::OBJ::G_SUBSTITUTE, "60 61 first") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".substitute") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 2);

    bool sawPair = false;
    bool sawFirstOnly = false;
    for (int i = 0; i < 2; i++) {
      YSE::pHandle* copy = loaded.GetHandleFromList(i);
      REQUIRE(copy != nullptr);
      CHECK(std::string(copy->Type()) == ".substitute");
      CHECK(copy->GetInputs() == 3);
      CHECK(copy->GetOutputs() == 2);
      const std::string params = copy->GetParams();
      CAPTURE(params);
      if (params == "note ctl") sawPair = true;
      if (params == "60 61 first") sawFirstOnly = true;
    }
    CHECK(sawPair);
    CHECK(sawFirstOnly);
  }

  // ─── end to end, through a real patcher graph ───────────────────────────────

  TEST_CASE("substitute: re-tags a message a .routepass downstream then routes (#488)") {
    // The use case the issue names — "rewriting selectors ... as they flow
    // through the graph, without dropping to the host application" — run
    // through the real thing. Nothing short of the whole chain proves it: a
    // unit test asserting on the rewritten text cannot tell "ctl 60" from
    // "ctl  60" or from " ctl 60", and only one of those routes.
    //
    // Sinks before the patcher: the patcher is torn down first, while the
    // inlets it is wired to still exist.
    MultiSink matched;
    MultiSink unmatched;
    YSE::pHandle matchedHandle(&matched);
    YSE::pHandle unmatchedHandle(&unmatched);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* sub = p.CreateObject(YSE::OBJ::G_SUBSTITUTE, "note ctl");
    YSE::pHandle* route = p.CreateObject(YSE::OBJ::G_ROUTEPASS, "ctl");
    REQUIRE(sub != nullptr);
    REQUIRE(route != nullptr);
    REQUIRE(route->GetOutputs() == 2);

    p.Connect(sub, 0, route, 0);
    p.Connect(route, 0, &matchedHandle, 0);
    p.Connect(route, 1, &unmatchedHandle, 0);

    sub->SetListData(0, "note 60");
    CHECK(matched.gotList);
    CHECK(matched.listValue == "ctl 60");
    CHECK_FALSE(unmatched.gotList);

    // And re-aiming it at run time moves the message to the other branch,
    // without the graph changing shape at all.
    matched.reset();
    unmatched.reset();
    sub->SetListData(2, "bend");
    sub->SetListData(0, "note 60");
    CHECK_FALSE(matched.gotList);
    CHECK(unmatched.gotList);
    CHECK(unmatched.listValue == "bend 60");
  }

  TEST_CASE("substitute: the rightmost outlet chains into the next .substitute (#488)") {
    // What the two-outlet split buys: each object tries its own pair and hands
    // on what it could not edit, the way a chain of .routepass objects strings
    // together through their reject outlets.
    MultiSink first;
    MultiSink second;
    MultiSink rest;
    YSE::pHandle firstHandle(&first);
    YSE::pHandle secondHandle(&second);
    YSE::pHandle restHandle(&rest);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* a = p.CreateObject(YSE::OBJ::G_SUBSTITUTE, "a x");
    YSE::pHandle* b = p.CreateObject(YSE::OBJ::G_SUBSTITUTE, "b y");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);

    p.Connect(a, 0, &firstHandle, 0);
    p.Connect(a, 1, b, 0);
    p.Connect(b, 0, &secondHandle, 0);
    p.Connect(b, 1, &restHandle, 0);

    // Handled by the first object, so the second never sees it.
    a->SetListData(0, "a 1");
    CHECK(first.listValue == "x 1");
    CHECK_FALSE(second.gotList);
    CHECK_FALSE(rest.gotList);

    // Passed on and handled by the second.
    first.reset();
    a->SetListData(0, "b 1");
    CHECK_FALSE(first.gotList);
    CHECK(second.gotList);
    CHECK(second.listValue == "y 1");
    CHECK_FALSE(rest.gotList);

    // Handled by neither, and still intact at the end of the chain.
    second.reset();
    a->SetListData(0, "c 1");
    CHECK_FALSE(first.gotList);
    CHECK_FALSE(second.gotList);
    CHECK(rest.gotList);
    CHECK(rest.listValue == "c 1");
  }
}
