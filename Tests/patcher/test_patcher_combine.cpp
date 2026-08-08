// Tests for .combine (issue #491) — join items held one per inlet into a single
// symbol.
//
// The object it is easiest to mistake this for is .tosymbol (#490), and the
// tests are written to pin the difference rather than to assume it:
//
//   - **.combine has a memory, one cell per inlet.** .tosymbol collapses one
//     message that has already arrived whole. .combine assembles a symbol out of
//     parts arriving at different times from different sources, which is the
//     whole reason it exists here; the cold-inlet cases and the end-to-end case
//     at the bottom are what prove it.
//   - **there is no separator, because every separator is an item.** The items
//     are concatenated exactly, so `.combine synth . lead` spells its own
//     punctuation, and each separator may differ.
//   - **which inlets emit is configurable** — Max's @triggers, spelled here as a
//     leading creation argument the way .sprintf spells Max's symout, so that
//     inlet 0 keeps no reserved words in it.
//   - **an over-long item is refused, not truncated**, and an unconfigured
//     object is inert rather than a source of empty messages.
//
// No audio device and no engine of its own, except where a real patcher graph
// is the point.

#include <doctest/doctest.h>
#include <string>

#include "patcher/genericObjects/gCombine.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::gCombine;

namespace {

  // A standalone object with a sink on its outlet. Standalone on purpose: this
  // object needs no patcher at all, and a test that needed one could not tell a
  // dropped message from a message the patcher never delivered.
  struct Rig {
    MultiSink sink;

    void Wire(gCombine& obj) {
      obj.ConnectOutlet(sink.GetInlet(0), 0);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("combine: registered, and the inlets come from the arguments (#491)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* bare = p.CreateObject(YSE::OBJ::G_COMBINE);
    REQUIRE(bare != nullptr);
    CHECK(std::string(bare->Type()) == ".combine");
    // Always at least one inlet: an object with no arguments still has
    // somewhere for a patch to send.
    CHECK(bare->GetInputs() == 1);
    CHECK(bare->GetOutputs() == 1);

    YSE::pHandle* three = p.CreateObject(YSE::OBJ::G_COMBINE, "synth . lead");
    REQUIRE(three != nullptr);
    CHECK(three->GetInputs() == 3);
    CHECK(three->GetOutputs() == 1);
  }

  TEST_CASE("combine: appears in the registry's name list (#491)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_COMBINE)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("combine: inlet 0 takes a bang, the other inlets do not (#491)") {
    // A cold inlet has nothing to do with a bang, so it registers no handler —
    // the .prepend / .substitute discipline, which also keeps
    // GetAcceptedTypes() reporting the real contract.
    gCombine obj;
    obj.SetParams("a b");

    const unsigned int left = obj.GetInlet(0)->GetAcceptedTypes();
    CHECK((left & YSE::PATCHER::IT_BANG) != 0);
    CHECK((left & YSE::PATCHER::IT_INT) != 0);
    CHECK((left & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((left & YSE::PATCHER::IT_LIST) != 0);

    const unsigned int right = obj.GetInlet(1)->GetAcceptedTypes();
    CHECK((right & YSE::PATCHER::IT_BANG) == 0);
    CHECK((right & YSE::PATCHER::IT_INT) != 0);
    CHECK((right & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((right & YSE::PATCHER::IT_LIST) != 0);
  }

  TEST_CASE("combine: creation arguments are the starting items (#491)") {
    gCombine obj;
    obj.SetParams("synth . lead");
    REQUIRE(obj.ItemCount() == 3);
    CHECK(obj.Item(0) == "synth");
    CHECK(obj.Item(1) == ".");
    CHECK(obj.Item(2) == "lead");
    // Out of range reads back empty rather than trapping.
    CHECK(obj.Item(3).empty());
    CHECK(obj.Item(-1).empty());
  }

  TEST_CASE("combine: by default only inlet 0 is hot, Max's own default (#491)") {
    gCombine obj;
    obj.SetParams("a b c");
    CHECK(obj.Triggers() == 1u);
    CHECK(obj.IsHot(0));
    CHECK_FALSE(obj.IsHot(1));
    CHECK_FALSE(obj.IsHot(2));
  }

  // ─── the join ───────────────────────────────────────────────────────────────

  TEST_CASE("combine: the items are concatenated with nothing between them (#491)") {
    // The difference from .tosymbol that shows up first: there is no separator
    // at all, so the punctuation is spelled as items and each one may differ.
    Rig rig;
    gCombine obj;
    obj.SetParams("synth . lead / note");
    rig.Wire(obj);

    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "synth.lead/note");
    // One token, which is what "a symbol" can mean in this patcher.
    CHECK(rig.sink.listValue.find(' ') == std::string::npos);
  }

  TEST_CASE("combine: a hot inlet stores and sends, a cold one only stores (#491)") {
    // The memory is the object: a value into a cold inlet has to survive until
    // something asks for the result, which is what .tosymbol cannot do at all.
    Rig rig;
    gCombine obj;
    obj.SetParams("a . b");
    rig.Wire(obj);

    obj.GetInlet(2)->SetList("tail", YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);
    CHECK(obj.Item(2) == "tail");

    obj.GetInlet(0)->SetList("head", YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "head.tail");
  }

  TEST_CASE("combine: an int keeps its spelling and a float keeps its point (#491)") {
    Rig rig;
    gCombine obj;
    obj.SetParams("voice / x");
    rig.Wire(obj);

    obj.GetInlet(2)->SetInt(3, YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.sink.listValue == "voice/3");

    obj.GetInlet(2)->SetFloat(2.5f, YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.sink.listValue == "voice/2.5");
  }

  TEST_CASE("combine: a list spreads one token per item, rightwards (#491)") {
    // Max's "each item in the list is treated as if it had been received in a
    // separate inlet, up to the number of inlets"; .sprintf reads a list the
    // same way.
    Rig rig;
    gCombine obj;
    obj.SetParams("a b c");
    rig.Wire(obj);

    obj.GetInlet(1)->SetList("Y Z", YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);
    CHECK(obj.Item(0) == "a");
    CHECK(obj.Item(1) == "Y");
    CHECK(obj.Item(2) == "Z");

    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.sink.listValue == "aYZ");
  }

  TEST_CASE("combine: tokens past the last item are dropped (#491)") {
    gCombine obj;
    obj.SetParams("a b");
    obj.GetInlet(0)->SetList("1 2 3 4", YSE::T_GUI);
    CHECK(obj.ItemCount() == 2);
    CHECK(obj.Item(0) == "1");
    CHECK(obj.Item(1) == "2");
  }

  TEST_CASE("combine: an empty message clears the item it arrives on (#491)") {
    // The .prepend reading of an empty message — a real request rather than a
    // malformed one, and the only way a patch can take a component back out of
    // the symbol it is building.
    Rig rig;
    gCombine obj;
    obj.SetParams("synth . lead");
    rig.Wire(obj);

    obj.GetInlet(1)->SetList("", YSE::T_GUI);
    CHECK(obj.Item(1).empty());

    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.sink.listValue == "synthlead");
  }

  TEST_CASE("combine: an unset item renders as nothing (#491)") {
    // Max: "if no value has been received for a %s argument, that argument will
    // be left blank" — the only rendering that keeps the result one token.
    Rig rig;
    gCombine obj;
    obj.SetParams("a b c");
    rig.Wire(obj);

    // Only the middle item is replaced; the two the arguments set stay.
    obj.GetInlet(1)->SetList("", YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.sink.listValue == "ac");
  }

  TEST_CASE("combine: with nothing held at all it sends nothing (#491)") {
    // The .prepend / .sprintf rule that makes an unconfigured object safe to
    // drop into a working patch: inert, rather than a source of empty messages.
    Rig rig;
    gCombine obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);
    CHECK_FALSE(rig.sink.gotBang);
  }

  TEST_CASE("combine: one inlet and no arguments is the identity (#491)") {
    Rig rig;
    gCombine obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("hello", YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "hello");
  }

  TEST_CASE("combine: Calculate() sends nothing (#491)") {
    // The rule the whole message-construction family keeps: the object is
    // driven by its inlets, and one that emitted from Calculate() would re-send
    // on every DSP tick after an inlet fired.
    Rig rig;
    gCombine obj;
    obj.SetParams("a b");
    rig.Wire(obj);

    obj.Calculate(YSE::T_DSP);
    CHECK_FALSE(rig.sink.gotList);
    CHECK_FALSE(rig.sink.gotBang);
  }

  // ─── triggers ───────────────────────────────────────────────────────────────

  TEST_CASE("combine: triggers -1 makes every inlet hot (#491)") {
    Rig rig;
    gCombine obj;
    obj.SetParams("triggers -1 a . b");
    rig.Wire(obj);

    // The flag and its number are consumed, not read as items.
    REQUIRE(obj.ItemCount() == 3);
    CHECK(obj.Item(0) == "a");
    CHECK(obj.Triggers() == 0x7u);

    obj.GetInlet(2)->SetList("Z", YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "a.Z");
  }

  TEST_CASE("combine: triggers <n> makes that inlet the only hot one (#491)") {
    Rig rig;
    gCombine obj;
    obj.SetParams("triggers 2 a . b");
    rig.Wire(obj);

    CHECK(obj.Triggers() == 0x4u);
    CHECK_FALSE(obj.IsHot(0));
    CHECK(obj.IsHot(2));

    obj.GetInlet(0)->SetList("X", YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);

    obj.GetInlet(2)->SetList("Z", YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "X.Z");
  }

  TEST_CASE("combine: a bang on inlet 0 sends whatever triggers says (#491)") {
    // Max's "bang: sends stored items as combined symbol" — the explicit
    // request, so a patch is never left without a way to ask for the result
    // even when inlet 0 is cold.
    Rig rig;
    gCombine obj;
    obj.SetParams("triggers 1 a b");
    rig.Wire(obj);

    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "ab");
  }

  TEST_CASE("combine: a triggers index with no such inlet falls back to inlet 0 (#491)") {
    // Never leave the object unable to send at all — that is never what was
    // meant, and it would be invisible until the patch stayed silent.
    gCombine obj;
    obj.SetParams("triggers 7 a b");
    CHECK(obj.ItemCount() == 2);
    CHECK(obj.Triggers() == 1u);
  }

  TEST_CASE("combine: triggers without a number is read as items (#491)") {
    // A typo must not silently eat an argument and move every item after it, so
    // the flag is consumed only when it is actually followed by a whole
    // integer.
    gCombine obj;
    obj.SetParams("triggers x y");
    REQUIRE(obj.ItemCount() == 3);
    CHECK(obj.Item(0) == "triggers");
    CHECK(obj.Item(1) == "x");
    CHECK(obj.Item(2) == "y");
    CHECK(obj.Triggers() == 1u);
  }

  TEST_CASE("combine: an item that happens to spell a number is still an item (#491)") {
    // Only a *leading* `triggers` is a flag, so a patch may combine numbers.
    Rig rig;
    gCombine obj;
    obj.SetParams("1 2 3");
    rig.Wire(obj);

    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.sink.listValue == "123");
  }

  // ─── limits ─────────────────────────────────────────────────────────────────

  TEST_CASE("combine: an over-long token on an inlet is refused, not truncated (#491)") {
    // Half an item is a different item, and the object exists to produce a
    // token the boxes downstream still recognise. Silent, because this may be
    // the audio thread.
    gCombine obj;
    obj.SetParams("a b");

    const std::string tooLong(gCombine::ITEM_CAPACITY + 1, 'x');
    obj.GetInlet(1)->SetList(tooLong, YSE::T_GUI);
    CHECK(obj.Item(1) == "b");

    const std::string justFits(gCombine::ITEM_CAPACITY, 'y');
    obj.GetInlet(1)->SetList(justFits, YSE::T_GUI);
    CHECK(obj.Item(1) == justFits);
  }

  TEST_CASE("combine: an over-long creation argument is left empty (#491)") {
    gCombine obj;
    obj.SetParams("a " + std::string(gCombine::ITEM_CAPACITY + 1, 'x') + " b");
    REQUIRE(obj.ItemCount() == 3);
    CHECK(obj.Item(0) == "a");
    CHECK(obj.Item(1).empty());
    CHECK(obj.Item(2) == "b");
  }

  TEST_CASE("combine: at most 32 items, and the rest are dropped (#491)") {
    std::string args;
    for (std::size_t i = 0; i < gCombine::MAX_ITEMS + 5; i++) {
      if (i > 0) args += ' ';
      args += 'a';
    }
    gCombine obj;
    obj.SetParams(args);
    CHECK(obj.ItemCount() == (int)gCombine::MAX_ITEMS);
  }

  TEST_CASE("combine: triggers -1 on a full 32-item object sets every bit (#491)") {
    // The shift that would be undefined behaviour if the mask were built
    // without the width guard.
    std::string args = "triggers -1";
    for (std::size_t i = 0; i < gCombine::MAX_ITEMS; i++)
      args += " a";

    gCombine obj;
    obj.SetParams(args);
    CHECK(obj.ItemCount() == (int)gCombine::MAX_ITEMS);
    CHECK(obj.Triggers() == 0xFFFFFFFFu);
    CHECK(obj.IsHot(31));
  }

  // ─── re-configuration ───────────────────────────────────────────────────────

  TEST_CASE("combine: SetParams(\"\") returns the object to its no-argument shape (#491)") {
    // Parameters::Set returns without calling the parse callback for an empty
    // argument, so the clear callback is the whole of the reset — without it
    // the object would keep the previous arguments' inlets.
    gCombine obj;
    obj.SetParams("triggers -1 a b c");
    REQUIRE(obj.NumInputs() == 3);

    obj.SetParams("");
    CHECK(obj.NumInputs() == 1);
    CHECK(obj.ItemCount() == 1);
    CHECK(obj.Item(0).empty());
    CHECK(obj.Triggers() == 1u);
  }

  // ─── persistence ────────────────────────────────────────────────────────────

  TEST_CASE("combine: params survive a DumpJSON / ParseJSON round trip (#491)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_COMBINE, "triggers -1 synth . lead") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".combine") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".combine");
    CHECK(copy->GetParams() == std::string("triggers -1 synth . lead"));
    // The shape has to come back with it, or the saved patch cords land on the
    // wrong inlets.
    CHECK(copy->GetInputs() == 3);
    CHECK(copy->GetOutputs() == 1);
  }

  // ─── end to end, through a real patcher graph ───────────────────────────────

  TEST_CASE("combine: builds an address from parts that arrive separately (#491)") {
    // The use case the issue names, run through the real thing: the parts of a
    // bus address arrive at different times down different cords, .combine
    // remembers each of them, and the result is one token a .route reads as a
    // single element. Nothing short of the whole chain proves that — a unit
    // test asserting on the joined text cannot tell "synth.lead" from
    // "synth lead" as far as any reader downstream is concerned, and the
    // *remembering* only shows up when the parts do not arrive together.
    //
    // Sinks before the patcher: the patcher is torn down first, while the
    // inlets it is wired to still exist.
    MultiSink matched;
    MultiSink fallthrough;
    YSE::pHandle matchedHandle(&matched);
    YSE::pHandle fallthroughHandle(&fallthrough);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* combine = p.CreateObject(YSE::OBJ::G_COMBINE, "synth . lead");
    YSE::pHandle* route = p.CreateObject(YSE::OBJ::G_ROUTE, "synth.bass");
    REQUIRE(combine != nullptr);
    REQUIRE(route != nullptr);
    REQUIRE(combine->GetInputs() == 3);
    REQUIRE(route->GetOutputs() == 2);

    p.Connect(combine, 0, route, 0);
    p.Connect(route, 0, &matchedHandle, 0);
    p.Connect(route, 1, &fallthroughHandle, 0);

    // The tail arrives first, down a cord into a cold inlet: nothing is sent,
    // and the value has to survive until the head arrives.
    combine->SetListData(2, "bass");
    CHECK_FALSE(matched.gotList);
    CHECK_FALSE(fallthrough.gotList);

    // The head arrives second and completes the address. The .route matching on
    // the whole of it is the evidence that the result really is one element
    // rather than merely text that looks like one — and because the address was
    // the *whole* message, .route consumes it and bangs (#672), which is Max's
    // "the message has no additional items" case. That bang is a stronger
    // reading of the claim than the old pass-through was: it can only happen if
    // the whole of `synth.bass` was the first item, with nothing beside it.
    combine->SetListData(0, "synth");
    CHECK(matched.gotBang);
    CHECK_FALSE(matched.gotList);
    CHECK_FALSE(fallthrough.gotList);
    CHECK_FALSE(fallthrough.gotBang);

    // A different tail re-addresses the same patch, and the head is remembered
    // this time.
    matched.reset();
    fallthrough.reset();
    combine->SetListData(2, "pad");
    CHECK_FALSE(matched.gotList);
    CHECK_FALSE(fallthrough.gotList);

    combine->SetIntData(0, 7);
    CHECK_FALSE(matched.gotList);
    CHECK_FALSE(matched.gotBang);
    CHECK(fallthrough.gotList);
    CHECK(fallthrough.listValue == "7.pad");
  }

  TEST_CASE("combine: triggers -1 re-addresses on every part, through the graph (#491)") {
    // The hot/cold split driven the way a patch drives it. With every inlet hot
    // the object behaves like Max's pak: any part changing rebuilds the whole
    // address immediately, which is what a live-coding patch wants when the
    // parts come from independent sources.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* combine = p.CreateObject(YSE::OBJ::G_COMBINE, "triggers -1 voice / 0");
    REQUIRE(combine != nullptr);
    REQUIRE(combine->GetInputs() == 3);

    p.Connect(combine, 0, &sinkHandle, 0);

    combine->SetIntData(2, 3);
    CHECK(sink.gotList);
    CHECK(sink.listValue == "voice/3");

    sink.reset();
    combine->SetListData(0, "chan");
    CHECK(sink.gotList);
    CHECK(sink.listValue == "chan/3");
  }

  TEST_CASE("combine: a .combine feeding a .fromsymbol takes the address apart again (#491)") {
    // The two objects the issue asks to keep distinct, working together rather
    // than standing in for one another: .combine assembles a name out of parts
    // that arrived separately, and .fromsymbol — the .tosymbol half that splits
    // — takes it back apart on the other side, with the number restored as a
    // number.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* combine = p.CreateObject(YSE::OBJ::G_COMBINE, "voice / 0");
    YSE::pHandle* from = p.CreateObject(YSE::OBJ::G_FROMSYMBOL, "/");
    REQUIRE(combine != nullptr);
    REQUIRE(from != nullptr);

    p.Connect(combine, 0, from, 0);
    p.Connect(from, 0, &sinkHandle, 0);

    combine->SetIntData(2, 60);
    CHECK_FALSE(sink.gotList);

    combine->SetListData(0, "voice");
    CHECK(sink.gotList);
    CHECK(sink.listValue == "voice 60");
  }
}
