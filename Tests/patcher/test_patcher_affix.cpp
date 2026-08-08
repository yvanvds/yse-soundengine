// Tests for .prepend and .append (issue #487) — the two objects that build a
// message out of a stored part and an incoming one.
//
// They are mirror images sharing one body (gAffixBase), so almost every case
// below runs against both and asserts only on which side of the separator the
// stored message landed. What the pair has to get right:
//
//   - **the join produces a list, and the first token is a real token.** The
//     whole reason to have these objects is that the patcher's routing family
//     reads the first token of a list: `.route` matching on it is the end-to-end
//     case at the bottom of this file. A stray separator — from an untrimmed
//     stored message, or from joining onto a bang — would give the result an
//     empty leading token that matches nothing.
//   - **with nothing stored the object is the identity, type included.** Not
//     "joins with an empty message" (a stray separator) and not "drops" (a hole
//     in the patch). An int must come out an int, not the one-token list that
//     spells it, or inserting an unconfigured .prepend into a working patch
//     would break every numeric object downstream.
//   - **inlet 0 has no reserved words in it.** Max spells the replacement as
//     `set <message>` into its one inlet; here that would swallow the very
//     message `.prepend set` exists to build. The stored message therefore has
//     its own inlet, and `set 3` through inlet 0 comes out joined like anything
//     else.
//   - **setting the stored message emits nothing**, and does not rewrite the
//     creation argument: re-aiming the object is a message, and a message must
//     not change what a saved patch contains.
//   - **an over-long message is refused, not truncated.** Half a message is a
//     different message, and a downstream .route would not recognise it.
//
// No audio device and no engine of its own.

#include <doctest/doctest.h>
#include <string>

#include "patcher/genericObjects/gAffix.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::gAffixBase;
using YSE::PATCHER::gAppend;
using YSE::PATCHER::gPrepend;

namespace {

  // A standalone object with a sink on its outlet. Standalone on purpose: these
  // two need no patcher at all, and a test that needed one could not tell a
  // dropped message from a message the patcher never delivered.
  struct Rig {
    MultiSink sink;

    void Wire(gAffixBase& obj) {
      obj.ConnectOutlet(sink.GetInlet(0), 0);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("affix: both objects are registered, with two inlets and one outlet (#487)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* pre = p.CreateObject(YSE::OBJ::G_PREPEND);
    REQUIRE(pre != nullptr);
    CHECK(std::string(pre->Type()) == ".prepend");
    CHECK(pre->GetInputs() == 2);
    CHECK(pre->GetOutputs() == 1);

    YSE::pHandle* app = p.CreateObject(YSE::OBJ::G_APPEND);
    REQUIRE(app != nullptr);
    CHECK(std::string(app->Type()) == ".append");
    CHECK(app->GetInputs() == 2);
    CHECK(app->GetOutputs() == 1);
  }

  TEST_CASE("affix: both appear in the registry's name list (#487)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool foundPrepend = false;
    bool foundAppend = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_PREPEND)) foundPrepend = true;
      if (name == std::string(YSE::OBJ::G_APPEND)) foundAppend = true;
    }
    CHECK(foundPrepend);
    CHECK(foundAppend);
  }

  TEST_CASE("affix: inlet 0 takes everything, inlet 1 takes everything but bang (#487)") {
    // Inlet 1 declines a bang because there is nothing for it to do — the
    // .decode / .router discipline of not registering an empty handler, which
    // keeps GetAcceptedTypes() reporting the real contract.
    gPrepend obj;
    const unsigned int data = obj.GetInlet(0)->GetAcceptedTypes();
    CHECK((data & YSE::PATCHER::IT_BANG) != 0);
    CHECK((data & YSE::PATCHER::IT_INT) != 0);
    CHECK((data & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((data & YSE::PATCHER::IT_LIST) != 0);

    const unsigned int msg = obj.GetInlet(1)->GetAcceptedTypes();
    CHECK((msg & YSE::PATCHER::IT_BANG) == 0);
    CHECK((msg & YSE::PATCHER::IT_INT) != 0);
    CHECK((msg & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((msg & YSE::PATCHER::IT_LIST) != 0);
  }

  // ─── the join ───────────────────────────────────────────────────────────────

  TEST_CASE("prepend: the stored message goes in front of an int (#487)") {
    Rig rig;
    gPrepend obj;
    obj.SetParams("note");
    rig.Wire(obj);

    obj.GetInlet(0)->SetInt(60, YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "note 60");
    CHECK_FALSE(rig.sink.gotInt);
  }

  TEST_CASE("append: the stored message goes after an int (#487)") {
    Rig rig;
    gAppend obj;
    obj.SetParams("note");
    rig.Wire(obj);

    obj.GetInlet(0)->SetInt(60, YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "60 note");
  }

  TEST_CASE("affix: a float keeps its decimal point inside the joined list (#487)") {
    // ExprFormatValue always gives a float a point, so a float stays visibly a
    // float in the middle of the list it has just become.
    Rig prependRig;
    gPrepend prepend;
    prepend.SetParams("gain");
    prependRig.Wire(prepend);
    prepend.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(prependRig.sink.listValue == "gain 0.5");

    Rig appendRig;
    gAppend append;
    append.SetParams("gain");
    appendRig.Wire(append);
    append.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(appendRig.sink.listValue == "0.5 gain");
  }

  TEST_CASE("affix: an incoming list is joined whole, on the right side (#487)") {
    Rig prependRig;
    gPrepend prepend;
    prepend.SetParams("connect");
    prependRig.Wire(prepend);
    prepend.GetInlet(0)->SetList("0 1", YSE::T_GUI);
    CHECK(prependRig.sink.listValue == "connect 0 1");

    Rig appendRig;
    gAppend append;
    append.SetParams("connect");
    appendRig.Wire(append);
    append.GetInlet(0)->SetList("0 1", YSE::T_GUI);
    CHECK(appendRig.sink.listValue == "0 1 connect");
  }

  TEST_CASE("affix: the creation argument is the whole argument string (#487)") {
    // A LIST parameter, so it absorbs every remaining token rather than only
    // the next one: ".prepend note 60" stores two words.
    Rig rig;
    gPrepend obj;
    obj.SetParams("note 60");
    rig.Wire(obj);

    obj.GetInlet(0)->SetInt(100, YSE::T_GUI);
    CHECK(rig.sink.listValue == "note 60 100");
  }

  TEST_CASE("affix: a bang emits the stored message on its own, with no separator (#487)") {
    // Joining onto nothing would leave a trailing or leading space, and the
    // empty token that produces matches no .route.
    Rig prependRig;
    gPrepend prepend;
    prepend.SetParams("stop");
    prependRig.Wire(prepend);
    prepend.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(prependRig.sink.gotList);
    CHECK(prependRig.sink.listValue == "stop");
    CHECK_FALSE(prependRig.sink.gotBang);

    Rig appendRig;
    gAppend append;
    append.SetParams("stop");
    appendRig.Wire(append);
    append.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(appendRig.sink.listValue == "stop");
  }

  TEST_CASE("affix: no word in inlet 0 is reserved — 'set' is data (#487)") {
    // The whole reason the stored message has its own inlet: `.prepend set`
    // feeding a number box is the canonical idiom, so an implementation that
    // read `set` out of inlet 0 would swallow the one message these objects
    // exist to carry.
    Rig rig;
    gPrepend obj;
    obj.SetParams("note");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("set 3", YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "note set 3");
    CHECK(obj.Stored() == "note");
  }

  // ─── the identity case ──────────────────────────────────────────────────────

  TEST_CASE("affix: with nothing stored the value passes through as its own type (#487)") {
    // Not the one-token list that spells it: an unconfigured .prepend dropped
    // into a working patch must not break the numeric objects downstream.
    Rig rig;
    gPrepend obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetInt(7, YSE::T_GUI);
    CHECK(rig.sink.gotInt);
    CHECK(rig.sink.intValue == 7);
    CHECK_FALSE(rig.sink.gotList);

    rig.sink.reset();
    obj.GetInlet(0)->SetFloat(0.25f, YSE::T_GUI);
    CHECK(rig.sink.gotFloat);
    CHECK(rig.sink.floatValue == doctest::Approx(0.25f));

    rig.sink.reset();
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.sink.gotBang);
    CHECK_FALSE(rig.sink.gotList);

    rig.sink.reset();
    obj.GetInlet(0)->SetList("a b", YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "a b");
  }

  TEST_CASE("append: with nothing stored it is the identity too (#487)") {
    Rig rig;
    gAppend obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetInt(7, YSE::T_GUI);
    CHECK(rig.sink.gotInt);
    CHECK(rig.sink.intValue == 7);
    CHECK_FALSE(rig.sink.gotList);
  }

  // ─── inlet 1: the stored message ────────────────────────────────────────────

  TEST_CASE("affix: inlet 1 replaces the stored message and emits nothing (#487)") {
    Rig rig;
    gPrepend obj;
    obj.SetParams("note");
    rig.Wire(obj);

    obj.GetInlet(1)->SetList("chord", YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);
    CHECK_FALSE(rig.sink.gotBang);
    CHECK(obj.Stored() == "chord");

    obj.GetInlet(0)->SetInt(60, YSE::T_GUI);
    CHECK(rig.sink.listValue == "chord 60");
  }

  TEST_CASE("affix: inlet 1 takes a whole list, not just its first token (#487)") {
    // The difference from .forward's destination, whose first token is all that
    // could ever match a .r: a stored message is allowed to be several words.
    Rig rig;
    gAppend obj;
    rig.Wire(obj);

    obj.GetInlet(1)->SetList("out 3", YSE::T_GUI);
    CHECK(obj.Stored() == "out 3");

    obj.GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(rig.sink.listValue == "1 out 3");
  }

  TEST_CASE("affix: inlet 1 takes a number, spelled the way an argument spells one (#487)") {
    Rig rig;
    gPrepend obj;
    rig.Wire(obj);

    obj.GetInlet(1)->SetInt(3, YSE::T_GUI);
    CHECK(obj.Stored() == "3");
    obj.GetInlet(0)->SetInt(60, YSE::T_GUI);
    CHECK(rig.sink.listValue == "3 60");

    rig.sink.reset();
    obj.GetInlet(1)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(obj.Stored() == "0.5");
    obj.GetInlet(0)->SetInt(60, YSE::T_GUI);
    CHECK(rig.sink.listValue == "0.5 60");
  }

  TEST_CASE("affix: inlet 1 trims the message it is given (#487)") {
    // Untrimmed, the join would produce a doubled separator and so an empty
    // token, which no .route matches.
    Rig rig;
    gPrepend obj;
    rig.Wire(obj);

    obj.GetInlet(1)->SetList("  note  ", YSE::T_GUI);
    CHECK(obj.Stored() == "note");
    obj.GetInlet(0)->SetInt(60, YSE::T_GUI);
    CHECK(rig.sink.listValue == "note 60");
  }

  TEST_CASE("affix: an empty message on inlet 1 restores the pass-through (#487)") {
    Rig rig;
    gPrepend obj;
    obj.SetParams("note");
    rig.Wire(obj);

    obj.GetInlet(1)->SetList("   ", YSE::T_GUI);
    CHECK(obj.Stored().empty());

    obj.GetInlet(0)->SetInt(60, YSE::T_GUI);
    CHECK(rig.sink.gotInt);
    CHECK(rig.sink.intValue == 60);
    CHECK_FALSE(rig.sink.gotList);
  }

  TEST_CASE("affix: a message longer than the capacity is refused, not truncated (#487)") {
    Rig rig;
    gPrepend obj;
    obj.SetParams("note");
    rig.Wire(obj);

    const std::string atCapacity(gAffixBase::MESSAGE_CAPACITY, 'x');
    obj.GetInlet(1)->SetList(atCapacity, YSE::T_GUI);
    CHECK(obj.Stored() == atCapacity);

    const std::string tooLong(gAffixBase::MESSAGE_CAPACITY + 1, 'y');
    obj.GetInlet(1)->SetList(tooLong, YSE::T_GUI);
    // The previous message is kept whole rather than replaced by a prefix of
    // the new one.
    CHECK(obj.Stored() == atCapacity);
  }

  TEST_CASE("affix: an over-long creation argument leaves a pass-through (#487)") {
    // Control thread, so this one is logged as well as refused; what matters
    // here is that the object does not join on half a message.
    Rig rig;
    gAppend obj;
    obj.SetParams(std::string(gAffixBase::MESSAGE_CAPACITY + 1, 'z'));
    rig.Wire(obj);
    CHECK(obj.Stored().empty());

    obj.GetInlet(0)->SetInt(5, YSE::T_GUI);
    CHECK(rig.sink.gotInt);
    CHECK_FALSE(rig.sink.gotList);
  }

  // ─── parameters ─────────────────────────────────────────────────────────────

  TEST_CASE("affix: SetParams(\"\") is a real reset, not a no-op (#487)") {
    // Parameters::Set returns early on an empty argument string, so only the
    // clear callback makes this a reset.
    gPrepend obj;
    obj.SetParams("note");
    REQUIRE(obj.Stored() == "note");

    obj.SetParams("");
    CHECK(obj.Stored().empty());
  }

  TEST_CASE("affix: the stored message is run-time state, not a parameter (#487)") {
    // What a saved patch carries is what the object started with. Re-aiming it
    // is a message, exactly as .forward's destination and .router's connections
    // are, and a message must not rewrite the file.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_PREPEND, "note");
    REQUIRE(h != nullptr);

    h->SetListData(1, "chord");
    CHECK(h->GetParams() == std::string("note"));
  }

  TEST_CASE("affix: params survive a DumpJSON / ParseJSON round trip (#487)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_PREPEND, "note 60") != nullptr);
    REQUIRE(src.CreateObject(YSE::OBJ::G_APPEND, "out 3") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".prepend") != std::string::npos);
    CHECK(json.find(".append") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 2);

    for (int i = 0; i < 2; i++) {
      YSE::pHandle* copy = loaded.GetHandleFromList(i);
      REQUIRE(copy != nullptr);
      const std::string type = copy->Type();
      CAPTURE(type);
      CHECK(copy->GetInputs() == 2);
      CHECK(copy->GetOutputs() == 1);
      if (type == ".prepend") {
        CHECK(copy->GetParams() == std::string("note 60"));
      } else {
        CHECK(type == ".append");
        CHECK(copy->GetParams() == std::string("out 3"));
      }
    }
  }

  // ─── end to end, through a real patcher graph ───────────────────────────────

  TEST_CASE("prepend: builds a selector a .route downstream matches on (#487)") {
    // The use case the issue names, run through the real thing: a value that
    // only exists at run time reaches the matching outlet of a .route because a
    // .prepend put a word in front of it. Nothing short of the whole chain
    // proves that — a unit test asserting on the joined text cannot tell
    // "note 60" from " note 60", and only one of those routes.
    //
    // Sinks before the patcher: the patcher is torn down first, while the
    // inlets it is wired to still exist.
    MultiSink matched;
    MultiSink fallthrough;
    YSE::pHandle matchedHandle(&matched);
    YSE::pHandle fallthroughHandle(&fallthrough);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* pre = p.CreateObject(YSE::OBJ::G_PREPEND, "note");
    YSE::pHandle* route = p.CreateObject(YSE::OBJ::G_ROUTE, "note");
    REQUIRE(pre != nullptr);
    REQUIRE(route != nullptr);
    REQUIRE(route->GetOutputs() == 2);

    p.Connect(pre, 0, route, 0);
    p.Connect(route, 0, &matchedHandle, 0);
    p.Connect(route, 1, &fallthroughHandle, 0);

    pre->SetIntData(0, 60);
    CHECK(matched.gotList);
    CHECK(matched.listValue == "note 60");
    CHECK_FALSE(fallthrough.gotList);

    // And re-aiming it at run time moves the value to the other outlet, without
    // the graph changing shape at all.
    matched.reset();
    fallthrough.reset();
    pre->SetListData(1, "chord");
    pre->SetIntData(0, 60);
    CHECK_FALSE(matched.gotList);
    CHECK(fallthrough.gotList);
    CHECK(fallthrough.listValue == "chord 60");
  }

  TEST_CASE("affix: a .prepend and a .append compose into one message (#487)") {
    // Selector in front, trailing argument behind — the shape every
    // message-driven object in the patcher is addressed with.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* pre = p.CreateObject(YSE::OBJ::G_PREPEND, "connect");
    YSE::pHandle* app = p.CreateObject(YSE::OBJ::G_APPEND, "1");
    REQUIRE(pre != nullptr);
    REQUIRE(app != nullptr);

    p.Connect(pre, 0, app, 0);
    p.Connect(app, 0, &sinkHandle, 0);

    pre->SetIntData(0, 2);
    CHECK(sink.gotList);
    CHECK(sink.listValue == "connect 2 1");
  }
}
