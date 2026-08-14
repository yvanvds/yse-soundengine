// Tests for `.preset` — Max's preset, "store and recall the values of the
// control objects in a patcher" (issue #564).
//
// Three claims, and the tests are organised around them:
//
//   - **participation is the settable promise.** Exactly the objects that
//     answer true to GuiValueIsSettable() are captured (issue #551's round
//     trip *is* what a preset needs), checked before the value is read; the
//     pre-protocol scalars are left alone, `.preset` never captures itself,
//     and objects nested in subpatchers participate because a preset belongs
//     to the patch, not to one level of it.
//
//   - **recall is the ordinary message path.** Each captured object receives
//     the exact string its GetGuiValue() produced, as a list on inlet 0
//     through pHandle::SetListData — never a poke into its fields — and the
//     announcement on outlet 0 fires only after the patch is in the preset.
//     Entries are guarded against storage-ID reuse across a delete.
//
//   - **it is control-thread work, and it persists.** A message that
//     physically arrives on the audio callback (a deferred `.delay` drain) is
//     dropped whole before any work — the refusal costs no allocation — and
//     the slots ride DumpJSON / ParseJSON through DumpState / RestoreState,
//     spelled as live-object ranks so a patch whose IDs went sparse through
//     deletes still recalls correctly after a save and a load, which a raw ID
//     would not survive.
//
// End-to-end cases build a real patcher through the public API — real
// CreateObject, real Connect, driven through pHandle the way a host drives a
// patch, read back through the captured objects' own handles — because that
// composition is what a host actually runs.
//
// No audio device required.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <vector>

#include "patcher/guiObjects/gPreset.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"
#include "utils/json.hpp"

using TestHelpers::OrderSink;
using YSE::PATCHER::gPreset;
using YSE::PATCHER::patcherImplementation;
using YSE::PATCHER::Register;

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("preset: type name, port counts and outlet types (#564)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_PRESET);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".preset");
    // One hot inlet; two int outlets — Max's recalled/stored pair, in Max's
    // order. His leftmost pattrstorage outlet is the excluded pattr system.
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 2);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::INT);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::INT);
  }

  TEST_CASE("preset: registry name and validity (#564)") {
    CHECK(YSE::patcher::IsValidObject(".preset"));
    auto names = Register().AllNames();
    bool found = false;
    for (const auto& n : names) {
      if (n == ".preset") found = true;
    }
    CHECK(found);
  }

  // ─── the capacity parameter ─────────────────────────────────────────────────

  TEST_CASE("preset: the slot capacity is a clamped creation parameter (#564)") {
    gPreset preset;
    CHECK(preset.Capacity() == gPreset::DEFAULT_SLOTS);

    preset.SetParams("8");
    CHECK(preset.Capacity() == 8);

    // Clamped, not refused: the object still exists at the nearest honest
    // size, and the clamp is logged at construction time.
    preset.SetParams("0");
    CHECK(preset.Capacity() == gPreset::MIN_SLOTS);
    preset.SetParams("2000");
    CHECK(preset.Capacity() == gPreset::MAX_SLOTS);

    // SetParams("") returns the object to the no-argument shape.
    preset.SetParams("");
    CHECK(preset.Capacity() == gPreset::DEFAULT_SLOTS);
  }

  // ─── store and recall, through the public patcher API ───────────────────────

  TEST_CASE("preset: stores a patch into a slot and recalls it (#564)") {
    // The issue's use case as a host builds it: two settable controls, a
    // preset bank, everything driven through pHandle and read back through
    // the controls' own handles. No internal pokes.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* preset = p.CreateObject(YSE::OBJ::G_PRESET, "8");
    YSE::pHandle* pad = p.CreateObject(YSE::OBJ::G_XYSLIDER, "0 127 0 127");
    YSE::pHandle* text = p.CreateObject(YSE::OBJ::G_TEXTEDIT);
    REQUIRE(preset != nullptr);
    REQUIRE(pad != nullptr);
    REQUIRE(text != nullptr);

    pad->SetListData(0, "12 34");
    text->SetListData(0, "hello world");
    const std::string padState = pad->GetGuiValue();
    const std::string textState = text->GetGuiValue();
    REQUIRE(textState == "hello world");

    preset->SetListData(0, "store 2");

    // Move everything: the slot must hold the captured state, not track the
    // live one.
    pad->SetListData(0, "99 100");
    text->SetListData(0, "changed");
    REQUIRE(pad->GetGuiValue() != padState);

    // An int recalls — Max's "recalls the preset whose number is received".
    preset->SetIntData(0, 2);
    CHECK(pad->GetGuiValue() == padState);
    CHECK(text->GetGuiValue() == textState);
  }

  TEST_CASE("preset: slots are independent and floats truncate (#564)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* preset = p.CreateObject(YSE::OBJ::G_PRESET, "4");
    YSE::pHandle* pad = p.CreateObject(YSE::OBJ::G_XYSLIDER, "0 127 0 127");
    REQUIRE(preset != nullptr);
    REQUIRE(pad != nullptr);

    pad->SetListData(0, "10 20");
    const std::string first = pad->GetGuiValue();
    preset->SetListData(0, "store 0");

    pad->SetListData(0, "30 40");
    const std::string second = pad->GetGuiValue();
    preset->SetListData(0, "store 1");

    preset->SetIntData(0, 0);
    CHECK(pad->GetGuiValue() == first);

    // A float is truncated the way every numeric control truncates.
    preset->SetFloatData(0, 1.9f);
    CHECK(pad->GetGuiValue() == second);

    // "recall <n>" is the int, spelled out; a bare number as a list is the
    // same recall a `.m 0` sends.
    preset->SetListData(0, "recall 0");
    CHECK(pad->GetGuiValue() == first);
    preset->SetListData(0, "1");
    CHECK(pad->GetGuiValue() == second);
  }

  TEST_CASE("preset: a bang recalls the active slot again (#564)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* preset = p.CreateObject(YSE::OBJ::G_PRESET);
    YSE::pHandle* pad = p.CreateObject(YSE::OBJ::G_XYSLIDER, "0 127 0 127");
    REQUIRE(preset != nullptr);
    REQUIRE(pad != nullptr);

    // A bang with no active slot does nothing.
    preset->SetBang(0);
    CHECK(preset->GetGuiValue() == "-1");

    pad->SetListData(0, "50 60");
    const std::string stored = pad->GetGuiValue();
    preset->SetListData(0, "store 3");

    // The resync: edit by hand, bang back into the preset.
    pad->SetListData(0, "1 2");
    preset->SetBang(0);
    CHECK(pad->GetGuiValue() == stored);
  }

  TEST_CASE("preset: an empty or out-of-range slot recalls nothing (#564)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* preset = p.CreateObject(YSE::OBJ::G_PRESET, "4");
    YSE::pHandle* pad = p.CreateObject(YSE::OBJ::G_XYSLIDER, "0 127 0 127");
    REQUIRE(preset != nullptr);
    REQUIRE(pad != nullptr);

    pad->SetListData(0, "10 20");
    preset->SetListData(0, "store 1");
    pad->SetListData(0, "30 40");
    const std::string moved = pad->GetGuiValue();

    // Recalling an empty slot does nothing — not even the active marker
    // moves, so it keeps naming a slot that holds something.
    preset->SetIntData(0, 2);
    CHECK(pad->GetGuiValue() == moved);
    CHECK(preset->GetGuiValue() == "1");

    // Out of range, in both directions.
    preset->SetIntData(0, 99);
    preset->SetIntData(0, -1);
    CHECK(pad->GetGuiValue() == moved);
    CHECK(preset->GetGuiValue() == "1");

    // And a store past the bank is refused whole: the active marker stays.
    preset->SetListData(0, "store 99");
    CHECK(preset->GetGuiValue() == "1");
  }

  // ─── participation ──────────────────────────────────────────────────────────

  TEST_CASE("preset: only settable objects participate (#564)") {
    // The rule issue #564 asks for, made observable: `.slider` predates the
    // settable protocol (its inlet 0 takes a number, not the display string),
    // so a preset must leave it alone in both directions.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* preset = p.CreateObject(YSE::OBJ::G_PRESET);
    YSE::pHandle* pad = p.CreateObject(YSE::OBJ::G_XYSLIDER, "0 127 0 127");
    YSE::pHandle* slider = p.CreateObject(YSE::OBJ::G_SLIDER);
    REQUIRE(preset != nullptr);
    REQUIRE(pad != nullptr);
    REQUIRE(slider != nullptr);
    REQUIRE_FALSE(slider->GuiValueIsSettable());

    pad->SetListData(0, "10 20");
    slider->SetFloatData(0, 0.25f);
    const std::string padState = pad->GetGuiValue();
    const std::string sliderState = slider->GetGuiValue();

    preset->SetListData(0, "store 0");

    pad->SetListData(0, "90 90");
    slider->SetFloatData(0, 0.75f);
    const std::string sliderMoved = slider->GetGuiValue();
    REQUIRE(sliderMoved != sliderState);

    preset->SetIntData(0, 0);
    // The settable control is restored; the pre-protocol one is untouched.
    CHECK(pad->GetGuiValue() == padState);
    CHECK(slider->GetGuiValue() == sliderMoved);
  }

  TEST_CASE("preset: an object with an empty value at store time is left out (#564)") {
    // An empty string is not a message an inlet can take back, so the object
    // is not captured and a recall leaves whatever it holds by then alone.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* preset = p.CreateObject(YSE::OBJ::G_PRESET);
    YSE::pHandle* text = p.CreateObject(YSE::OBJ::G_TEXTEDIT);
    REQUIRE(preset != nullptr);
    REQUIRE(text != nullptr);
    REQUIRE(text->GetGuiValue().empty());

    preset->SetListData(0, "store 0");
    text->SetListData(0, "typed later");
    preset->SetIntData(0, 0);
    CHECK(text->GetGuiValue() == "typed later");
  }

  TEST_CASE("preset: objects inside a subpatcher participate (#564)") {
    // Nesting is addressing, not storage (issue #545): a preset belongs to
    // the patch, not to one level of it.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* preset = p.CreateObject(YSE::OBJ::G_PRESET);
    YSE::pHandle* sub = p.CreateObject(YSE::OBJ::PATCHER);
    YSE::pHandle* pad = p.CreateObject(YSE::OBJ::G_XYSLIDER, "0 127 0 127");
    REQUIRE(preset != nullptr);
    REQUIRE(sub != nullptr);
    REQUIRE(pad != nullptr);
    p.SetContainer(pad, sub);

    pad->SetListData(0, "44 55");
    const std::string nested = pad->GetGuiValue();
    preset->SetListData(0, "store 0");
    pad->SetListData(0, "1 1");
    preset->SetIntData(0, 0);
    CHECK(pad->GetGuiValue() == nested);
  }

  TEST_CASE("preset: a captured function round-trips, including an empty one (#564)") {
    // `.function` spells an empty store as the word "clear" — the one string
    // that both round-trips and empties. A preset must carry it like any
    // other state, which is exactly what makes "store, then recall" honest
    // for a control the user has since drawn into.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* preset = p.CreateObject(YSE::OBJ::G_PRESET);
    YSE::pHandle* fn = p.CreateObject(YSE::OBJ::G_FUNCTION);
    REQUIRE(preset != nullptr);
    REQUIRE(fn != nullptr);

    // Slot 0: an empty function (its bulk read is "clear").
    REQUIRE(fn->GetGuiValue() == "clear");
    preset->SetListData(0, "store 0");

    // Slot 1: a drawn envelope.
    fn->SetListData(0, "0 0");
    fn->SetListData(0, "100 1");
    const std::string drawn = fn->GetGuiValue();
    REQUIRE(drawn != "clear");
    preset->SetListData(0, "store 1");

    preset->SetIntData(0, 0);
    CHECK(fn->GetGuiValue() == "clear");
    preset->SetIntData(0, 1);
    CHECK(fn->GetGuiValue() == drawn);
  }

  // ─── the ID-reuse guard ─────────────────────────────────────────────────────

  TEST_CASE("preset: a recalled entry is skipped when its ID was reused (#564)") {
    // Storage IDs are reused after a delete (issue #733): the smallest free
    // number goes to the next object created. Without the type check a
    // recall would write the dead control's state into whatever inherited
    // its number.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* preset = p.CreateObject(YSE::OBJ::G_PRESET);
    YSE::pHandle* pad = p.CreateObject(YSE::OBJ::G_XYSLIDER, "0 127 0 127");
    REQUIRE(preset != nullptr);
    REQUIRE(pad != nullptr);
    const unsigned int padID = pad->GetID();

    pad->SetListData(0, "12 34");
    preset->SetListData(0, "store 0");

    p.DeleteObject(pad);
    YSE::pHandle* text = p.CreateObject(YSE::OBJ::G_TEXTEDIT);
    REQUIRE(text != nullptr);
    // The whole point of the case: the newcomer inherited the number.
    REQUIRE(text->GetID() == padID);

    text->SetListData(0, "innocent bystander");
    preset->SetIntData(0, 0);
    CHECK(text->GetGuiValue() == "innocent bystander");
  }

  // ─── clearing ───────────────────────────────────────────────────────────────

  TEST_CASE("preset: clear empties one slot, clearall every slot (#564)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* preset = p.CreateObject(YSE::OBJ::G_PRESET, "4");
    YSE::pHandle* pad = p.CreateObject(YSE::OBJ::G_XYSLIDER, "0 127 0 127");
    REQUIRE(preset != nullptr);
    REQUIRE(pad != nullptr);

    pad->SetListData(0, "10 20");
    preset->SetListData(0, "store 0");
    pad->SetListData(0, "30 40");
    preset->SetListData(0, "store 1");
    const std::string moved = pad->GetGuiValue();

    // Clearing the active slot clears the active marker with it — the number
    // no longer names stored contents.
    CHECK(preset->GetGuiValue() == "1");
    preset->SetListData(0, "clear 1");
    CHECK(preset->GetGuiValue() == "-1");
    preset->SetIntData(0, 1);
    CHECK(pad->GetGuiValue() == moved);

    // Slot 0 still holds its capture.
    preset->SetIntData(0, 0);
    CHECK(pad->GetGuiValue() != moved);

    // Bare `clear` empties the active slot — the parallel of the bang.
    preset->SetListData(0, "clear");
    CHECK(preset->GetGuiValue() == "-1");
    pad->SetListData(0, "30 40");
    preset->SetIntData(0, 0);
    CHECK(pad->GetGuiValue() == moved);

    // And clearall leaves nothing at all.
    pad->SetListData(0, "10 20");
    preset->SetListData(0, "store 2");
    preset->SetListData(0, "store 3");
    preset->SetListData(0, "clearall");
    CHECK(preset->GetGuiValue() == "-1");
    pad->SetListData(0, "30 40");
    preset->SetIntData(0, 2);
    preset->SetIntData(0, 3);
    CHECK(pad->GetGuiValue() == moved);
  }

  // ─── the announcements ──────────────────────────────────────────────────────

  TEST_CASE("preset: outlet 1 announces a store, outlet 0 a recall (#564)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* preset = p.CreateObject(YSE::OBJ::G_PRESET, "8");
    YSE::pHandle* pad = p.CreateObject(YSE::OBJ::G_XYSLIDER, "0 127 0 127");
    YSE::pHandle* recalled = p.CreateObject(YSE::OBJ::G_INT);
    YSE::pHandle* stored = p.CreateObject(YSE::OBJ::G_INT);
    REQUIRE(preset != nullptr);
    REQUIRE(pad != nullptr);
    REQUIRE(recalled != nullptr);
    REQUIRE(stored != nullptr);
    p.Connect(preset, 0, recalled, 0);
    p.Connect(preset, 1, stored, 0);

    pad->SetListData(0, "10 20");
    preset->SetListData(0, "store 3");
    CHECK(stored->GetGuiValue() == "3");
    CHECK(recalled->GetGuiValue() == "0"); // untouched default

    preset->SetIntData(0, 3);
    CHECK(recalled->GetGuiValue() == "3");

    // The GUI value is the same number, for the host's row of dots.
    CHECK(preset->GetGuiValue() == "3");
  }

  TEST_CASE("preset: the recall announcement fires after the values land (#564)") {
    // "Announced after the pushes, so whatever it triggers sees the patch
    // already in the preset." Observed through the order log: the recalled
    // pad emits its restored position before the preset announces the slot.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* preset = p.CreateObject(YSE::OBJ::G_PRESET, "");
    YSE::pHandle* pad = p.CreateObject(YSE::OBJ::G_XYSLIDER, "0 127 0 127");
    REQUIRE(preset != nullptr);
    REQUIRE(pad != nullptr);

    std::vector<char> log;
    OrderSink padSink, announceSink;
    padSink.log = &log;
    padSink.tag = 'v';
    announceSink.log = &log;
    announceSink.tag = 'r';
    YSE::pHandle padSinkHandle(&padSink);
    YSE::pHandle announceHandle(&announceSink);
    p.Connect(pad, 0, &padSinkHandle, 0);
    p.Connect(preset, 0, &announceHandle, 0);

    pad->SetListData(0, "10 20");
    preset->SetListData(0, "store 0");
    pad->SetListData(0, "30 40");

    log.clear();
    preset->SetIntData(0, 0);
    REQUIRE(log.size() >= 2);
    CHECK(log.front() == 'v');
    CHECK(log.back() == 'r');
  }

  TEST_CASE("preset: an announcement wired back into the inlet cannot recurse (#564)") {
    // Outlet 1 announces the stored slot as an int, and an int on inlet 0 is
    // a recall: wired around, the re-entrant recall is dropped by the
    // loser-drops guard rather than run over the staging buffer mid-store.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* preset = p.CreateObject(YSE::OBJ::G_PRESET);
    YSE::pHandle* pad = p.CreateObject(YSE::OBJ::G_XYSLIDER, "0 127 0 127");
    REQUIRE(preset != nullptr);
    REQUIRE(pad != nullptr);
    p.Connect(preset, 1, preset, 0);

    pad->SetListData(0, "10 20");
    const std::string state = pad->GetGuiValue();
    preset->SetListData(0, "store 0");

    // Still sane afterwards: the slot holds the capture and recalls it.
    pad->SetListData(0, "90 90");
    preset->SetIntData(0, 0);
    CHECK(pad->GetGuiValue() == state);
  }

  // ─── the GUI value (#551) ───────────────────────────────────────────────────

  TEST_CASE("preset: the GUI value is the active slot, read-only (#564)") {
    gPreset preset;
    CHECK(preset.GetGuiValue() == "-1");
    CHECK(preset.GetGuiValueCount() == 1u);
    CHECK(preset.GetGuiValueAt(0) == "-1");
    CHECK(preset.GetGuiValueAt(1).empty());
    // Read-only on purpose: recalling is inlet 0's job, and staying
    // unsettable is also what keeps `.preset` out of its own snapshots.
    CHECK_FALSE(preset.GuiValueIsSettable());
  }

  // ─── persistence ────────────────────────────────────────────────────────────

  TEST_CASE("preset: params and slots survive a DumpJSON / ParseJSON round trip (#564)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* preset = src.CreateObject(YSE::OBJ::G_PRESET, "8");
    YSE::pHandle* pad = src.CreateObject(YSE::OBJ::G_XYSLIDER, "0 127 0 127");
    REQUIRE(preset != nullptr);
    REQUIRE(pad != nullptr);

    pad->SetListData(0, "12 34");
    const std::string padState = pad->GetGuiValue();
    preset->SetListData(0, "store 5");

    const std::string json = src.DumpJSON();
    CHECK(json.find(".preset") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 2);

    YSE::pHandle* loadedPreset = nullptr;
    YSE::pHandle* loadedPad = nullptr;
    for (unsigned int i = 0; i < loaded.Objects(); i++) {
      YSE::pHandle* h = loaded.GetHandleFromList(i);
      REQUIRE(h != nullptr);
      if (std::string(h->Type()) == ".preset") loadedPreset = h;
      if (std::string(h->Type()) == ".xyslider") loadedPad = h;
    }
    // Spelled as a guard rather than a bare REQUIRE so the static analyzer
    // can see the null paths end here too.
    if (loadedPreset == nullptr || loadedPad == nullptr) {
      FAIL("loaded patch is missing an object");
      return;
    }

    // The parameter is a parameter.
    CHECK(loadedPreset->GetParams() == "8");
    // The active marker rides along, but loading does *not* recall: the pad
    // comes back at its defaults until something asks. A patch that wants to
    // come up in a slot wires `.loadbang` into the inlet.
    CHECK(loadedPreset->GetGuiValue() == "5");
    REQUIRE(loadedPad->GetGuiValue() != padState);

    loadedPreset->SetIntData(0, 5);
    CHECK(loadedPad->GetGuiValue() == padState);
  }

  TEST_CASE("preset: slots survive the round trip through sparse IDs (#564)") {
    // The regression the rank spelling exists for. A delete leaves the live
    // IDs sparse ({0, 1, 3} here); ParseJSON renumbers loaded objects densely
    // in stored-ID order, so a raw ID in the state would point one object
    // over after the load. An entry names its object by rank instead — the
    // dump's own record order — which is exactly the fresh numbering.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* preset = src.CreateObject(YSE::OBJ::G_PRESET);
    YSE::pHandle* padA = src.CreateObject(YSE::OBJ::G_XYSLIDER, "0 127 0 127");
    YSE::pHandle* padB = src.CreateObject(YSE::OBJ::G_XYSLIDER, "0 127 0 127");
    YSE::pHandle* padC = src.CreateObject(YSE::OBJ::G_XYSLIDER, "0 127 0 127");
    REQUIRE(preset != nullptr);
    REQUIRE(padA != nullptr);
    REQUIRE(padB != nullptr);
    REQUIRE(padC != nullptr);

    padA->SetListData(0, "1 2");
    padC->SetListData(0, "5 6");
    const std::string stateA = padA->GetGuiValue();
    const std::string stateC = padC->GetGuiValue();

    // Delete the middle pad *before* the store, leaving IDs {0, 1, 3}.
    src.DeleteObject(padB);
    preset->SetListData(0, "store 2");

    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 3);

    YSE::pHandle* loadedPreset = nullptr;
    std::vector<YSE::pHandle*> loadedPads;
    for (unsigned int i = 0; i < loaded.Objects(); i++) {
      YSE::pHandle* h = loaded.GetHandleFromList(i);
      REQUIRE(h != nullptr);
      if (std::string(h->Type()) == ".preset") loadedPreset = h;
      if (std::string(h->Type()) == ".xyslider") loadedPads.push_back(h);
    }
    REQUIRE(loadedPads.size() == 2);
    // A guard rather than a bare REQUIRE, for the analyzer — see above.
    if (loadedPreset == nullptr) {
      FAIL("loaded patch is missing the preset");
      return;
    }

    loadedPreset->SetIntData(0, 2);
    // Both pads restored to their own state — neither swapped nor skipped.
    // Identify them by the restored values: A was (1,2), C was (5,6).
    bool foundA = false;
    bool foundC = false;
    for (YSE::pHandle* h : loadedPads) {
      if (h->GetGuiValue() == stateA) foundA = true;
      if (h->GetGuiValue() == stateC) foundC = true;
    }
    CHECK(foundA);
    CHECK(foundC);
  }

  TEST_CASE("preset: an untouched object serialises without a state key (#564)") {
    // The DumpState hook is unconditional, but an untouched object writes
    // nothing — its serialised form is byte for byte what it would be
    // without the hook.
    YSE::patcher p;
    p.create(2);
    REQUIRE(p.CreateObject(YSE::OBJ::G_PRESET) != nullptr);
    const std::string json = p.DumpJSON();
    CHECK(json.find("\"state\"") == std::string::npos);
  }

  TEST_CASE("preset: RestoreState clamps what a hand-edited file may hold (#564)") {
    // Driven directly, the way ParseJSON drives it on a freshly built object.
    gPreset preset;
    preset.SetParams("4");

    nlohmann::json state;
    state["active"] = 99; // outside the bank: names nothing
    nlohmann::json slotJson;
    slotJson["slot"] = 2;
    nlohmann::json entry;
    entry["object"] = 0;
    entry["type"] = ".xyslider";
    entry["value"] = "1 2";
    slotJson["objects"].push_back(entry);
    state["slots"].push_back(slotJson);

    nlohmann::json outOfRange;
    outOfRange["slot"] = 7; // outside the bank: dropped per slot
    outOfRange["objects"].push_back(entry);
    state["slots"].push_back(outOfRange);

    preset.RestoreState(state);
    CHECK(preset.ActiveSlot() == -1);
    CHECK(preset.SlotEntryCount(2) == 1);
    CHECK(preset.SlotEntryCount(7) == 0);
  }

  // ─── the thread contract ────────────────────────────────────────────────────

  TEST_CASE("preset: a message on the audio callback is dropped whole (#564)") {
    // `.delay`'s deferred bang drains at the top of Calculate — T_GUI tag,
    // physically the audio callback (issue #690). The preset must refuse it:
    // store walks the patch under its mutex and recall runs whole subgraphs,
    // none of which may run there. The same bang from the control thread
    // works, which is the pair that proves the guard asks the thread, not
    // the message.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* preset = p.CreateObject(YSE::OBJ::G_PRESET, "");
    YSE::pHandle* pad = p.CreateObject(YSE::OBJ::G_XYSLIDER, "0 127 0 127");
    YSE::pHandle* delay = p.CreateObject(YSE::OBJ::G_DELAY, "0");
    REQUIRE(preset != nullptr);
    REQUIRE(pad != nullptr);
    REQUIRE(delay != nullptr);
    p.Connect(delay, 0, preset, 0);

    pad->SetListData(0, "5 5");
    const std::string stored = pad->GetGuiValue();
    preset->SetListData(0, "store 0");
    pad->SetListData(0, "9 9");
    const std::string moved = pad->GetGuiValue();

    // The deferred route: armed on the control thread, delivered inside
    // Calculate. The bang reaches the preset and is dropped whole.
    delay->SetBang(0);
    p.Calculate(YSE::T_DSP);
    p.Calculate(YSE::T_DSP);
    CHECK(pad->GetGuiValue() == moved);

    // The control-thread route: the same bang restores the slot.
    preset->SetBang(0);
    CHECK(pad->GetGuiValue() == stored);
  }

  TEST_CASE("preset: a standalone object trusts the tag and drops T_DSP (#564)") {
    // A standalone object has no patcher to ask, so the tag is the only
    // answer — and the conservative one, since this object's work must never
    // run on the callback. T_GUI deliveries are harmless no-ops standalone
    // (there is no patch to capture), so the observable here is simply that
    // nothing detonates and nothing becomes active.
    gPreset preset;
    std::string storeMsg = "store 0";
    preset.GetInlet(0)->SetList(storeMsg, YSE::T_DSP);
    preset.GetInlet(0)->SetInt(0, YSE::T_DSP);
    preset.GetInlet(0)->SetFloat(0.f, YSE::T_DSP);
    preset.GetInlet(0)->SetBang(YSE::T_DSP);
    CHECK(preset.ActiveSlot() == -1);
  }

  TEST_CASE("preset: the refusal path allocates nothing (#564)") {
    // The drop rule is what makes "entries are allocated at store time"
    // RT-safe: a message that arrives on the audio callback must be refused
    // before any work, at the cost of one thread-local load. Probed on the
    // standalone tag route, where the whole dispatch runs on this thread.
    gPreset preset;
    std::string storeMsg = "store 0";
    std::string clearMsg = "clearall";
    // Warm the dispatch machinery through the control-tag route first.
    preset.GetInlet(0)->SetBang(YSE::T_GUI);

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      preset.GetInlet(0)->SetList(storeMsg, YSE::T_DSP);
      preset.GetInlet(0)->SetList(clearMsg, YSE::T_DSP);
      preset.GetInlet(0)->SetInt(3, YSE::T_DSP);
      preset.GetInlet(0)->SetFloat(3.f, YSE::T_DSP);
      preset.GetInlet(0)->SetBang(YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the parameter, which is what a binding
  // generator and a saved patch both key on.

  TEST_CASE("preset: documents itself as GUI with one slots param (#564)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::G_PRESET));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GUI);
    CHECK(obj->NumInputs() == 1);
    CHECK(obj->NumOutputs() == 2);

    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 1);
    CHECK(docs[0].name == "slots");
    CHECK(docs[0].defaultValue == "32");
  }

} // TEST_SUITE("patcher")
