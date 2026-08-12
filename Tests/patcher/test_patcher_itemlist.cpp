// Tests for the indexed selector family — `.umenu`, `.radiogroup` and `.tab`
// (issue #556), the patcher's first bounded *named* choice.
//
// Five claims, and the cases are organised around them:
//
//   - **one implementation, three names.** The value model is an index into a
//     named item list in all three, and only the widget a host draws differs.
//     That is asserted rather than assumed: the shared cases run over all three
//     type names and require identical answers, so a divergence cannot hide in
//     one of them.
//
//   - **the item names are creation arguments and nothing else changes them.**
//     Which is what lets every path read a name with no synchronisation:
//     registering the list parameter and the parse callbacks makes
//     `ParamsNeedRebuild()` true, so a live SetParams replaces the object
//     through the #234 graph swap instead of rewriting a bank of strings under
//     the audio thread.
//
//   - **the object outputs the index, the name and the whole state.** Outlet 1
//     is what makes this more than a bounded `.i`, and the three fire right to
//     left.
//
//   - **`multi` is the mode, and it changes what the GUI cells mean.** Single
//     select is the one-cell scalar case; multi select is one 0/1 cell per
//     item, which is the mask issue #556 asks `.radiogroup` for. Both halves of
//     issue #551's write contract hold in both modes.
//
//   - **nothing on a message path allocates.**
//
// Plus the JSON round trip, the doc metadata and a host driving a real patch
// through pHandle.
//
// No audio device required.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <vector>

#include "patcher/guiObjects/gItemList.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::MultiSink;
using TestHelpers::OrderSink;
using YSE::PATCHER::gItemListBase;
using YSE::PATCHER::gRadioGroup;
using YSE::PATCHER::gTab;
using YSE::PATCHER::gUMenu;
using YSE::PATCHER::Register;

namespace {

  // The three names, so the shared cases can be run over all of them and a
  // divergence between the renderings cannot hide in one.
  const char* const kNames[] = {YSE::OBJ::G_UMENU, YSE::OBJ::G_RADIOGROUP, YSE::OBJ::G_TAB};

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("itemlist: all three renderings are the same object (#556)") {
    YSE::patcher p;
    p.create(2);

    for (const char* name : kNames) {
      CAPTURE(name);
      YSE::pHandle* h = p.CreateObject(name);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == std::string(name));

      // One hot inlet, three outlets: the index, the name, the whole state.
      CHECK(h->GetInputs() == 1);
      CHECK(h->GetOutputs() == 3);
      CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::INT);
      CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::LIST);
      CHECK(h->OutputDataType(2) == YSE::OUT_TYPE::LIST);

      // Same value model, driven the same way, answering the same thing.
      h->SetIntData(0, 1);
      CHECK(h->GetGuiValueCount() == 1u);
      CHECK(h->GuiValueIsSettable());
      CHECK(h->GetGuiValue() == "1");

      CHECK(YSE::patcher::IsValidObject(name));
    }

    auto names = Register().AllNames();
    for (const char* name : kNames) {
      CAPTURE(name);
      bool found = false;
      for (const auto& n : names) {
        if (n == name) found = true;
      }
      CHECK(found);
    }
  }

  TEST_CASE("itemlist: a bare object is two items, named by index (#556)") {
    gUMenu menu;
    gRadioGroup group;
    gTab tabs;
    gItemListBase* controls[] = {&menu, &group, &tabs};

    for (gItemListBase* control : controls) {
      CAPTURE(control->Type());
      CHECK(control->ItemCount() == gItemListBase::DEFAULT_ITEMS);
      CHECK_FALSE(control->MultiSelect());
      CHECK(control->Label(0) == "0");
      CHECK(control->Label(1) == "1");
      CHECK(control->Index() == 0);
      // The item list is a LIST parameter with parse callbacks behind it, so a
      // live re-parse takes the #234 structural route rather than rewriting the
      // names underneath a reader.
      CHECK(control->ParamsNeedRebuild());
    }
  }

  // ─── the item list ──────────────────────────────────────────────────────────

  TEST_CASE("itemlist: several arguments are the item names, in order (#556)") {
    gUMenu menu;
    menu.SetParams("sine square saw");
    CHECK(menu.ItemCount() == 3);
    CHECK(menu.Label(0) == "sine");
    CHECK(menu.Label(1) == "square");
    CHECK(menu.Label(2) == "saw");
    // An item the list does not have has no name, rather than the nearest one's.
    CHECK(menu.Label(3).empty());
    CHECK(menu.Label(-1).empty());
  }

  TEST_CASE("itemlist: a single whole-number argument is a count, not a name (#556)") {
    // A one-item control is not a choice, so the single-token case is free to
    // mean "that many items, named by index" — which is what saves an unnamed
    // eight-button group from being written out as eight names.
    gRadioGroup group;
    group.SetParams("5");
    CHECK(group.ItemCount() == 5);
    CHECK(group.Label(0) == "0");
    CHECK(group.Label(4) == "4");

    // A single token that is not a whole positive number is a name, and one
    // item is what was asked for.
    gRadioGroup named;
    named.SetParams("solo");
    CHECK(named.ItemCount() == 1);
    CHECK(named.Label(0) == "solo");

    gRadioGroup zero;
    zero.SetParams("0");
    CHECK(zero.ItemCount() == 1);
    CHECK(zero.Label(0) == "0");

    // Two numeric tokens are two names, not a count and a name.
    gRadioGroup pair;
    pair.SetParams("4 8");
    CHECK(pair.ItemCount() == 2);
    CHECK(pair.Label(0) == "4");
    CHECK(pair.Label(1) == "8");
  }

  TEST_CASE("itemlist: the item count is clamped to the ceiling (#556)") {
    gTab tabs;
    tabs.SetParams("100000");
    CHECK(tabs.ItemCount() == gItemListBase::MAX_ITEMS);

    // And a longer list of names stops at the same ceiling rather than growing.
    std::string many;
    for (int i = 0; i < gItemListBase::MAX_ITEMS + 20; i++) {
      if (i > 0) many.push_back(' ');
      many += "i";
      many += std::to_string(i);
    }
    gTab wide;
    wide.SetParams(many);
    CHECK(wide.ItemCount() == gItemListBase::MAX_ITEMS);
    CHECK(wide.Label(gItemListBase::MAX_ITEMS - 1) == "i255");
  }

  TEST_CASE("itemlist: SetParams(\"\") returns the object to the no-argument shape (#556)") {
    gUMenu menu;
    menu.SetParams("multi a b c d");
    REQUIRE(menu.ItemCount() == 4);
    REQUIRE(menu.MultiSelect());

    menu.SetParams("");
    CHECK(menu.ItemCount() == gItemListBase::DEFAULT_ITEMS);
    CHECK_FALSE(menu.MultiSelect());
    CHECK(menu.Label(0) == "0");
  }

  TEST_CASE("itemlist: \"multi\" is a leading keyword and not an item (#556)") {
    gRadioGroup group;
    group.SetParams("multi red green blue");
    CHECK(group.MultiSelect());
    CHECK(group.ItemCount() == 3);
    CHECK(group.Label(0) == "red");

    // The keyword is only the keyword in first position: an item genuinely
    // named "multi" is one position away from working.
    gRadioGroup second;
    second.SetParams("red multi blue");
    CHECK_FALSE(second.MultiSelect());
    CHECK(second.ItemCount() == 3);
    CHECK(second.Label(1) == "multi");
  }

  // ─── single select ──────────────────────────────────────────────────────────

  TEST_CASE("itemlist: an int selects an item and emits index, name and state (#556)") {
    MultiSink indexSink, nameSink, stateSink;
    gUMenu menu;
    menu.SetParams("sine square saw");
    TestHelpers::Wire(menu, 0, indexSink);
    TestHelpers::Wire(menu, 1, nameSink);
    TestHelpers::Wire(menu, 2, stateSink);

    menu.GetInlet(0)->SetInt(2, YSE::T_GUI);
    CHECK(indexSink.intValue == 2);
    CHECK(nameSink.listValue == "saw");
    CHECK(stateSink.listValue == "2");
    CHECK(menu.Index() == 2);
    CHECK(menu.IsSelected(2));
    CHECK_FALSE(menu.IsSelected(0));

    // A float item number truncates towards zero, as every other index in the
    // patcher does.
    menu.GetInlet(0)->SetFloat(1.8f, YSE::T_GUI);
    CHECK(indexSink.intValue == 1);
    CHECK(nameSink.listValue == "square");
  }

  TEST_CASE("itemlist: an item the list does not have is dropped, and still re-sends (#556)") {
    // The hot-inlet rule: a message that addresses nothing changes nothing and
    // emits anyway, which is a re-send and exactly what a bang does.
    MultiSink indexSink;
    gUMenu menu;
    menu.SetParams("sine square saw");
    TestHelpers::Wire(menu, 0, indexSink);

    menu.GetInlet(0)->SetInt(1, YSE::T_GUI);
    REQUIRE(indexSink.intValue == 1);

    indexSink.reset();
    menu.GetInlet(0)->SetInt(9, YSE::T_GUI);
    CHECK(indexSink.gotInt);
    CHECK(indexSink.intValue == 1);
    CHECK(menu.Index() == 1);

    indexSink.reset();
    menu.GetInlet(0)->SetInt(-3, YSE::T_GUI);
    CHECK(indexSink.intValue == 1);
  }

  TEST_CASE("itemlist: a name selects the item that has it (#556)") {
    // Max's `symbol` message, and half the point of naming the items.
    MultiSink indexSink, nameSink;
    gTab tabs;
    tabs.SetParams("mixer synth fx");
    TestHelpers::Wire(tabs, 0, indexSink);
    TestHelpers::Wire(tabs, 1, nameSink);

    tabs.GetInlet(0)->SetList("fx", YSE::T_GUI);
    CHECK(indexSink.intValue == 2);
    CHECK(nameSink.listValue == "fx");

    // A name no item has is dropped rather than folded onto item 0.
    indexSink.reset();
    tabs.GetInlet(0)->SetList("reverb", YSE::T_GUI);
    CHECK(indexSink.intValue == 2);
    CHECK(tabs.Index() == 2);
  }

  TEST_CASE("itemlist: a bang re-sends without moving the selection (#556)") {
    MultiSink indexSink, nameSink;
    gUMenu menu;
    menu.SetParams("sine square saw");
    TestHelpers::Wire(menu, 0, indexSink);
    TestHelpers::Wire(menu, 1, nameSink);

    menu.GetInlet(0)->SetInt(1, YSE::T_GUI);
    indexSink.reset();
    nameSink.reset();

    menu.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(indexSink.gotInt);
    CHECK(indexSink.intValue == 1);
    CHECK(nameSink.listValue == "square");
  }

  TEST_CASE("itemlist: the three outlets fire right to left (#556)") {
    // `.trigger`'s ordering guarantee, and it is load-bearing: the state and the
    // name have to be in hand by the time the index lands on a hot inlet
    // downstream.
    std::vector<char> log;
    OrderSink indexSink, nameSink, stateSink;
    indexSink.log = &log;
    indexSink.tag = 'i';
    nameSink.log = &log;
    nameSink.tag = 'n';
    stateSink.log = &log;
    stateSink.tag = 's';

    {
      gUMenu menu;
      menu.SetParams("a b c");
      TestHelpers::Wire(menu, 0, indexSink);
      TestHelpers::Wire(menu, 1, nameSink);
      TestHelpers::Wire(menu, 2, stateSink);

      menu.GetInlet(0)->SetInt(1, YSE::T_GUI);
    }

    REQUIRE(log.size() == 3);
    CHECK(log[0] == 's');
    CHECK(log[1] == 'n');
    CHECK(log[2] == 'i');
  }

  // ─── multi select ───────────────────────────────────────────────────────────

  TEST_CASE("itemlist: with \"multi\" an int toggles rather than selects (#556)") {
    MultiSink indexSink, stateSink;
    gRadioGroup group;
    group.SetParams("multi a b c d");
    TestHelpers::Wire(group, 0, indexSink);
    TestHelpers::Wire(group, 2, stateSink);

    group.GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(stateSink.listValue == "0 1 0 0");
    CHECK(indexSink.intValue == 1);

    group.GetInlet(0)->SetInt(3, YSE::T_GUI);
    CHECK(stateSink.listValue == "0 1 0 1");
    CHECK(indexSink.intValue == 3);
    CHECK(group.IsSelected(1));
    CHECK(group.IsSelected(3));

    // The same item again turns it back off — the only reading of "the user
    // picked this one again" when several can be on at once.
    group.GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(stateSink.listValue == "0 0 0 1");
    CHECK(indexSink.intValue == 1);
    CHECK_FALSE(group.IsSelected(1));
  }

  TEST_CASE("itemlist: a name toggles in multi select too (#556)") {
    gRadioGroup group;
    group.SetParams("multi red green blue");
    group.GetInlet(0)->SetList("blue", YSE::T_GUI);
    CHECK(group.IsSelected(2));
    CHECK(group.Index() == 2);
    group.GetInlet(0)->SetList("blue", YSE::T_GUI);
    CHECK_FALSE(group.IsSelected(2));
  }

  TEST_CASE("itemlist: a multi-select whole-state write points at the lowest selected (#556)") {
    MultiSink indexSink, nameSink, stateSink;
    gRadioGroup group;
    group.SetParams("multi a b c d");
    TestHelpers::Wire(group, 0, indexSink);
    TestHelpers::Wire(group, 1, nameSink);
    TestHelpers::Wire(group, 2, stateSink);

    group.GetInlet(0)->SetList("0 0 1 1", YSE::T_GUI);
    CHECK(stateSink.listValue == "0 0 1 1");
    CHECK(indexSink.intValue == 2);
    CHECK(nameSink.listValue == "c");

    // A write that selected nothing leaves the pointer where it was — there is
    // no item such a message was about.
    group.GetInlet(0)->SetList("0 0 0 0", YSE::T_GUI);
    CHECK(stateSink.listValue == "0 0 0 0");
    CHECK(indexSink.intValue == 2);

    // A short list leaves the items it did not reach alone.
    group.GetInlet(0)->SetList("1", YSE::T_GUI);
    CHECK(stateSink.listValue == "1 0 0 0");
  }

  // ─── the GUI value protocol (issue #551) ────────────────────────────────────

  TEST_CASE("itemlist: single select is the one-cell scalar case (#556)") {
    gUMenu menu;
    menu.SetParams("sine square saw");
    CHECK(menu.GuiValueIsSettable());
    CHECK(menu.GetGuiValueCount() == 1u);

    menu.GetInlet(0)->SetInt(2, YSE::T_GUI);
    CHECK(menu.GetGuiValue() == "2");
    CHECK(menu.GetGuiValueAt(0) == "2");
    // Past the end is "", never the whole state again.
    CHECK(menu.GetGuiValueAt(1).empty());
    CHECK(menu.GetGuiValueAt(0xFFFFFFFFu).empty());
  }

  TEST_CASE("itemlist: multi select is one cell per item (#556)") {
    gRadioGroup group;
    group.SetParams("multi a b c d");
    CHECK(group.GuiValueIsSettable());
    CHECK(group.GetGuiValueCount() == 4u);

    group.GetInlet(0)->SetInt(1, YSE::T_GUI);
    group.GetInlet(0)->SetInt(3, YSE::T_GUI);
    CHECK(group.GetGuiValue() == "0 1 0 1");
    CHECK(group.GetGuiValueAt(0) == "0");
    CHECK(group.GetGuiValueAt(1) == "1");
    CHECK(group.GetGuiValueAt(3) == "1");
    CHECK(group.GetGuiValueAt(4).empty());
    CHECK(group.GetGuiValueAt(0xFFFFFFFFu).empty());
  }

  TEST_CASE("itemlist: \"set <index> <value>\" is the cell write in both modes (#556)") {
    // Single select has one cell and it holds the index, so `set 0 <item>`
    // selects outright — a cell write is absolute by definition, where a bare
    // int toggles in the other mode.
    gUMenu menu;
    menu.SetParams("sine square saw");
    menu.GetInlet(0)->SetList("set 0 2", YSE::T_GUI);
    CHECK(menu.Index() == 2);
    // Any other cell does not exist on a scalar control.
    menu.GetInlet(0)->SetList("set 1 0", YSE::T_GUI);
    CHECK(menu.Index() == 2);
    // An item outside the list is dropped, not folded onto a real one.
    menu.GetInlet(0)->SetList("set 0 99", YSE::T_GUI);
    CHECK(menu.Index() == 2);

    // Multi select writes one flag, absolutely: repeating it does not toggle.
    gRadioGroup group;
    group.SetParams("multi a b c d");
    group.GetInlet(0)->SetList("set 2 1", YSE::T_GUI);
    CHECK(group.GetGuiValue() == "0 0 1 0");
    group.GetInlet(0)->SetList("set 2 1", YSE::T_GUI);
    CHECK(group.GetGuiValue() == "0 0 1 0");
    group.GetInlet(0)->SetList("set 2 0", YSE::T_GUI);
    CHECK(group.GetGuiValue() == "0 0 0 0");
    group.GetInlet(0)->SetList("set 9 1", YSE::T_GUI);
    CHECK(group.GetGuiValue() == "0 0 0 0");
  }

  TEST_CASE("itemlist: the \"set\" keyword disambiguates the two write forms (#556)") {
    // Why the cell form is not a bare "<index> <value>": for a single-select
    // control the whole state is one number, so "0 2" would be unreadable
    // without the keyword.
    gUMenu menu;
    menu.SetParams("a b c");
    menu.GetInlet(0)->SetList("2", YSE::T_GUI); // whole state
    CHECK(menu.Index() == 2);
    menu.GetInlet(0)->SetList("set 0 1", YSE::T_GUI); // one cell
    CHECK(menu.Index() == 1);
  }

  TEST_CASE("itemlist: its own GetGuiValue round-trips through inlet 0 (#556)") {
    // The unconditional promise GuiValueIsSettable() makes, in both modes.
    gUMenu menu;
    menu.SetParams("a b c d");
    menu.GetInlet(0)->SetInt(3, YSE::T_GUI);
    const std::string menuState = menu.GetGuiValue();
    menu.GetInlet(0)->SetInt(0, YSE::T_GUI);
    REQUIRE(menu.GetGuiValue() != menuState);
    menu.GetInlet(0)->SetList(menuState, YSE::T_GUI);
    CHECK(menu.GetGuiValue() == menuState);

    gRadioGroup group;
    group.SetParams("multi a b c d");
    group.GetInlet(0)->SetInt(1, YSE::T_GUI);
    group.GetInlet(0)->SetInt(2, YSE::T_GUI);
    const std::string groupState = group.GetGuiValue();
    REQUIRE(groupState == "0 1 1 0");
    group.GetInlet(0)->SetList("0 0 0 0", YSE::T_GUI);
    REQUIRE(group.GetGuiValue() != groupState);
    group.GetInlet(0)->SetList(groupState, YSE::T_GUI);
    CHECK(group.GetGuiValue() == groupState);
  }

  // ─── a host, through the public API ─────────────────────────────────────────

  TEST_CASE("itemlist: a host drives a real patch through pHandle (#556)") {
    // The issue's use case as a host builds it: a menu of waveform names wired
    // to an index readout and a name readout, driven through the public handle
    // API and read back through the readouts' own handles. No internal pokes.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* menu = p.CreateObject(YSE::OBJ::G_UMENU, "sine square saw");
    YSE::pHandle* indexOut = p.CreateObject(YSE::OBJ::G_INT);
    YSE::pHandle* nameOut = p.CreateObject(YSE::OBJ::G_LIST);
    YSE::pHandle* stateOut = p.CreateObject(YSE::OBJ::G_LIST);
    REQUIRE(menu != nullptr);
    REQUIRE(indexOut != nullptr);
    REQUIRE(nameOut != nullptr);
    REQUIRE(stateOut != nullptr);
    p.Connect(menu, 0, indexOut, 0);
    p.Connect(menu, 1, nameOut, 0);
    p.Connect(menu, 2, stateOut, 0);

    // A click on item 2.
    menu->SetIntData(0, 2);
    CHECK(indexOut->GetGuiValue() == "2");
    CHECK(nameOut->GetGuiValue() == "saw");
    CHECK(stateOut->GetGuiValue() == "2");

    // The same choice by name — the reason the items are named at all.
    menu->SetListData(0, "square");
    CHECK(indexOut->GetGuiValue() == "1");
    CHECK(nameOut->GetGuiValue() == "square");

    // What a host polls to draw it, and what `.preset` stores.
    CHECK(menu->GuiValueIsSettable());
    CHECK(menu->GetGuiValueCount() == 1u);
    CHECK(menu->GetGuiValue() == "1");

    // And a check-box group in the same patch, driven the same way.
    YSE::pHandle* group = p.CreateObject(YSE::OBJ::G_RADIOGROUP, "multi mute solo arm");
    YSE::pHandle* maskOut = p.CreateObject(YSE::OBJ::G_LIST);
    REQUIRE(group != nullptr);
    REQUIRE(maskOut != nullptr);
    p.Connect(group, 2, maskOut, 0);

    group->SetIntData(0, 0);
    group->SetIntData(0, 2);
    CHECK(maskOut->GetGuiValue() == "1 0 1");
    CHECK(group->GetGuiValueCount() == 3u);
    CHECK(group->GetGuiValue() == "1 0 1");
  }

  TEST_CASE("itemlist: a stored GUI value restores the choice on a fresh patch (#556)") {
    // What `.preset` will do: read the string out of one patcher and push it
    // into the equivalent object in another. Nothing but the public API.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* from = src.CreateObject(YSE::OBJ::G_TAB, "mixer synth fx");
    REQUIRE(from != nullptr);
    from->SetListData(0, "fx");
    const std::string stored = from->GetGuiValue();

    YSE::patcher dst;
    dst.create(2);
    YSE::pHandle* to = dst.CreateObject(YSE::OBJ::G_TAB, "mixer synth fx");
    REQUIRE(to != nullptr);
    REQUIRE(to->GetGuiValue() != stored);

    to->SetListData(0, stored);
    CHECK(to->GetGuiValue() == stored);
  }

  TEST_CASE("itemlist: params survive a DumpJSON / ParseJSON round trip (#556)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_UMENU, "sine square saw") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".umenu") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".umenu"));
    CHECK(h->GetParams() == std::string("sine square saw"));

    // The item list has to still *work*, not merely still be a string. The live
    // selection is run-time state and is deliberately not saved — a reload
    // brings back the item list the patch was written with and item 0 chosen.
    CHECK(h->GetGuiValue() == "0");
    h->SetListData(0, "saw");
    CHECK(h->GetGuiValue() == "2");
  }

  TEST_CASE("itemlist: multi select survives a DumpJSON / ParseJSON round trip (#556)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_RADIOGROUP, "multi a b c") != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(h->GetParams() == std::string("multi a b c"));
    CHECK(h->GetGuiValueCount() == 3u);
    CHECK(h->GetGuiValue() == "0 0 0");
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the parameter, which is what a binding
  // generator and a saved patch both key on.

  TEST_CASE("itemlist: all three document themselves as GUI with one param (#556)") {
    std::string firstDescription;
    for (const char* name : kNames) {
      CAPTURE(name);
      std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(name));
      REQUIRE(obj != nullptr);
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GUI);

      const auto& docs = obj->GetParamDocs();
      REQUIRE(docs.size() == 1);
      CHECK(docs[0].name == "items");

      // The three share an implementation but not a description: each says what
      // a host is expected to draw, which is the only thing that differs.
      if (firstDescription.empty()) {
        firstDescription = obj->GetDescription();
      } else {
        CHECK(obj->GetDescription() != firstDescription);
      }
    }
  }

  // ─── real time ──────────────────────────────────────────────────────────────

  TEST_CASE("itemlist: no message path allocates (#556)") {
    // Including the whole-state send, which is the one that could: it fills a
    // string reserved when the item list was built, and the list handler
    // compares its keyword and its names in place and walks its numbers token
    // by token rather than into an MAX_ITEMS-wide stack buffer. Outlet 1 sends
    // the stored label itself rather than a copy, which is only sound because
    // the item list cannot change under a send. The counter is read inside the
    // scope and asserted outside it, since doctest's own machinery allocates on
    // first use.
    //
    // **The messages are built as strings before the scope opens, never passed
    // as literals inside it**, and that is not tidiness. `inlet::SetList` takes
    // a `const std::string&`, so a literal at the call site materialises a
    // temporary — a heap allocation whenever the text is longer than the
    // implementation's small-string buffer, and that buffer is *not* the same
    // width everywhere: 15 characters on libstdc++, 22 on libc++. A
    // 17-character list literal therefore costs nothing on the Windows/libc++
    // build and one allocation on the Linux/libstdc++ one, which is a probe
    // that passes locally and fails in CI while the object under test is
    // innocent. Hoisting the strings removes the test rig from the measurement.
    //
    // One of them is deliberately longer than *both* buffers, so the object is
    // driven from a genuinely heap-backed input on every platform.
    const std::string warmName = "square";
    const std::string warmSet = "set 0 2";
    const std::string warmState = "1";
    const std::string pickName = "saw";
    const std::string cellWrite = "set 0 1";
    const std::string wholeState = "2";
    const std::string longMiss = "a-name-no-item-in-this-list-has"; // past both buffers

    MultiSink indexSink, nameSink, stateSink;
    gUMenu menu;
    menu.SetParams("sine square saw");
    TestHelpers::Wire(menu, 0, indexSink);
    TestHelpers::Wire(menu, 1, nameSink);
    TestHelpers::Wire(menu, 2, stateSink);

    // Warm every path, so the sinks' own buffers and any first-call machinery
    // are not what the probe catches.
    menu.GetInlet(0)->SetInt(1, YSE::T_GUI);
    menu.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
    menu.GetInlet(0)->SetBang(YSE::T_GUI);
    menu.GetInlet(0)->SetList(warmName, YSE::T_GUI);
    menu.GetInlet(0)->SetList(warmSet, YSE::T_GUI);
    menu.GetInlet(0)->SetList(warmState, YSE::T_GUI);
    REQUIRE(nameSink.gotList);
    REQUIRE(stateSink.gotList);

    // Size the miss-path sink buffers independently of the object, so a long
    // input string is not what grows them inside the probe.
    const std::string sinkWarm(64, 'x');
    nameSink.GetInlet(0)->SetList(sinkWarm, YSE::T_GUI);
    stateSink.GetInlet(0)->SetList(sinkWarm, YSE::T_GUI);

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      menu.GetInlet(0)->SetInt(0, YSE::T_GUI);
      menu.GetInlet(0)->SetFloat(2.4f, YSE::T_GUI);
      menu.GetInlet(0)->SetBang(YSE::T_GUI);
      menu.GetInlet(0)->SetList(pickName, YSE::T_GUI);
      menu.GetInlet(0)->SetList(cellWrite, YSE::T_GUI);
      menu.GetInlet(0)->SetList(wholeState, YSE::T_GUI);
      menu.GetInlet(0)->SetList(longMiss, YSE::T_GUI);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);
    // The sends really did happen, so the zero above is not a vacuous pass.
    CHECK(menu.Index() == 2);
    CHECK(nameSink.listValue == "saw");
    CHECK(stateSink.listValue == "2");
  }

  TEST_CASE("itemlist: a multi-select mask send allocates nothing either (#556)") {
    // The widest send the family has: one flag per item plus a separator, into a
    // buffer reserved when the item list was built.
    const std::string warmToggle = "0 1 0 1 0 1 0 1";
    std::string wideState;
    for (int i = 0; i < 64; i++) {
      if (i > 0) wideState.push_back(' ');
      wideState.push_back((i % 2) == 0 ? '1' : '0');
    }

    MultiSink stateSink;
    gRadioGroup group;
    group.SetParams("multi 64");
    REQUIRE(group.ItemCount() == 64);
    TestHelpers::Wire(group, 2, stateSink);

    group.GetInlet(0)->SetList(warmToggle, YSE::T_GUI);
    group.GetInlet(0)->SetInt(7, YSE::T_GUI);
    REQUIRE(stateSink.gotList);
    REQUIRE(stateSink.listValue.size() == 127u);

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      group.GetInlet(0)->SetList(wideState, YSE::T_GUI);
      group.GetInlet(0)->SetInt(63, YSE::T_GUI);
      group.GetInlet(0)->SetBang(YSE::T_GUI);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);
    CHECK(group.Index() == 63);
    CHECK(stateSink.listValue.size() == 127u);
    // The wide write left item 63 clear (odd indices are 0 in `wideState`) and
    // the int toggled it on, so the mask really did move under the probe.
    CHECK(group.IsSelected(63));
  }

} // TEST_SUITE("patcher")
