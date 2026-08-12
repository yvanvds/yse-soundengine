// Tests for `.nslider` — Max's nslider, "display or output a pitch on a musical
// staff" (issue #555), and the two-cell case of the structured GUI value
// protocol from issue #551.
//
// Three claims, and the tests are organised around them:
//
//   - **it is not `.i` with a different name.** What a staff needs and a number
//     box does not is how to *spell* the pitch: MIDI 61 is C sharp or D flat,
//     the same key drawn on two different lines with two different signs. So
//     the state is a pitch *and* an accidental, and the accidental is what
//     these tests are mostly about.
//
//   - **the accidental is derived, and overridable.** 0 for every white key
//     whatever was asked for, the `spelling` parameter's choice for a black
//     one, and the per-note override when a message set one — which lasts until
//     the next bare pitch, so the key the music is in survives a single
//     re-spelled note.
//
//   - **it is a two-cell settable GUI object.** GetGuiValueCount() is 2, the
//     cells are the pitch and the accidental, and inlet 0 takes the whole state
//     straight back as well as `set <index> <value>` for one cell.
//
// End-to-end cases build a real patcher through the public API — real
// CreateObject, real Connect, driven through pHandle the way a host drives a
// control, read back through the downstream objects' own handles. Plus the JSON
// round trip, the doc metadata and an allocation probe over every message path.
//
// No audio device required.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <vector>

#include "patcher/guiObjects/gNSlider.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::MultiSink;
using TestHelpers::OrderSink;
using YSE::PATCHER::gNSlider;
using YSE::PATCHER::patcherImplementation;
using YSE::PATCHER::Register;

namespace {

  // The state as the object spells it: "<pitch> <accidental>".
  std::string Note(int pitch, int accidental) {
    return std::to_string(pitch) + " " + std::to_string(accidental);
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("nslider: type name, port counts and outlet types (#555)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_NSLIDER);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".nslider");
    // One inlet, Max's.
    CHECK(h->GetInputs() == 1);
    // Two outlets: the pitch, and how to write it.
    CHECK(h->GetOutputs() == 2);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::INT);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::INT);
  }

  TEST_CASE("nslider: registry name and validity (#555)") {
    CHECK(YSE::patcher::IsValidObject(".nslider"));
    auto names = Register().AllNames();
    bool found = false;
    for (const auto& n : names) {
      if (n == ".nslider") found = true;
    }
    CHECK(found);
  }

  TEST_CASE("nslider: a bare object sits on the bottom of its range (#555)") {
    gNSlider staff;
    CHECK(staff.Pitch() == 0);
    CHECK(staff.Accidental() == 0);
    CHECK(staff.GetGuiValueCount() == 2u);
    CHECK(staff.GetGuiValue() == Note(0, 0));
  }

  // ─── the note ───────────────────────────────────────────────────────────────

  TEST_CASE("nslider: a pitch is stored and sent with its accidental (#555)") {
    MultiSink pitchSink, accidentalSink;
    gNSlider staff;
    TestHelpers::Wire(staff, 0, pitchSink);
    TestHelpers::Wire(staff, 1, accidentalSink);

    staff.GetInlet(0)->SetInt(60, YSE::T_GUI);
    CHECK(pitchSink.intValue == 60);
    // Middle C is a white key: a natural, and no sign to draw.
    CHECK(accidentalSink.intValue == 0);
    CHECK(staff.GetGuiValue() == Note(60, 0));
  }

  TEST_CASE("nslider: a bang re-sends without moving the note (#555)") {
    MultiSink pitchSink;
    gNSlider staff;
    TestHelpers::Wire(staff, 0, pitchSink);

    staff.GetInlet(0)->SetInt(64, YSE::T_GUI);
    pitchSink.reset();
    staff.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(pitchSink.intValue == 64);
    CHECK(staff.Pitch() == 64);
  }

  TEST_CASE("nslider: a float pitch is truncated (#555)") {
    gNSlider staff;
    staff.GetInlet(0)->SetFloat(60.9f, YSE::T_GUI);
    CHECK(staff.Pitch() == 60);
  }

  TEST_CASE("nslider: the accidental lands before the pitch (#555)") {
    // `.trigger`'s right-to-left ordering, and `.kslider`'s reason for a pair:
    // anything downstream taking the two on a hot and a cold inlet must have
    // the spelling in hand before the pitch arrives.
    std::vector<char> log;
    OrderSink pitchSink, accidentalSink;
    pitchSink.log = &log;
    pitchSink.tag = 'p';
    accidentalSink.log = &log;
    accidentalSink.tag = 'a';

    gNSlider staff;
    TestHelpers::Wire(staff, 0, pitchSink);
    TestHelpers::Wire(staff, 1, accidentalSink);

    staff.GetInlet(0)->SetInt(61, YSE::T_GUI);
    REQUIRE(log.size() == 2);
    CHECK(log[0] == 'a');
    CHECK(log[1] == 'p');
  }

  // ─── the accidental ─────────────────────────────────────────────────────────

  TEST_CASE("nslider: white keys are naturals and black keys take the spelling (#555)") {
    // Pitch classes 1, 3, 6, 8 and 10 are the black keys — arithmetic on the
    // pitch class, not a table, since there is nothing in music/ that answers
    // how a pitch should be spelled (scale answers membership, and M_PITCH
    // aliases CSM1 and DFM1 to the same value).
    gNSlider staff;
    const int black[5] = {61, 63, 66, 68, 70};
    const int white[7] = {60, 62, 64, 65, 67, 69, 71};

    for (int pitch : white) {
      staff.GetInlet(0)->SetInt(pitch, YSE::T_GUI);
      CHECK(staff.Accidental() == 0);
    }
    for (int pitch : black) {
      staff.GetInlet(0)->SetInt(pitch, YSE::T_GUI);
      CHECK(staff.Accidental() == 1);
    }

    // And an octave up, so the answer is the pitch *class* rather than a table
    // of the middle octave.
    staff.GetInlet(0)->SetInt(73, YSE::T_GUI);
    CHECK(staff.Accidental() == 1);
    staff.GetInlet(0)->SetInt(72, YSE::T_GUI);
    CHECK(staff.Accidental() == 0);
  }

  TEST_CASE("nslider: the spelling parameter picks sharps or flats (#555)") {
    gNSlider staff;
    staff.SetParams("0 127 -1");
    staff.GetInlet(0)->SetInt(61, YSE::T_GUI);
    CHECK(staff.Accidental() == -1);
    CHECK(staff.GetGuiValue() == Note(61, -1));

    // A white key is unaffected — its accidental is 0 whatever the key is.
    staff.GetInlet(0)->SetInt(60, YSE::T_GUI);
    CHECK(staff.Accidental() == 0);
  }

  TEST_CASE("nslider: a list re-spells one note without changing the key (#555)") {
    // The override lasts until the next bare pitch, so a patch in C sharp minor
    // that writes one D flat does not become a patch in D flat.
    MultiSink accidentalSink;
    gNSlider staff;
    TestHelpers::Wire(staff, 1, accidentalSink);

    staff.GetInlet(0)->SetList("61 -1", YSE::T_GUI);
    CHECK(accidentalSink.intValue == -1);
    CHECK(staff.GetGuiValue() == Note(61, -1));

    // The next bare pitch goes back to the parameter's choice.
    staff.GetInlet(0)->SetInt(66, YSE::T_GUI);
    CHECK(staff.Accidental() == 1);
  }

  TEST_CASE("nslider: an accidental is read as a sign, not a count (#555)") {
    // The object never reports an accidental a host cannot draw.
    gNSlider staff;
    staff.GetInlet(0)->SetList("61 5", YSE::T_GUI);
    CHECK(staff.Accidental() == 1);
    staff.GetInlet(0)->SetList("61 -7", YSE::T_GUI);
    CHECK(staff.Accidental() == -1);
    staff.GetInlet(0)->SetList("61 0", YSE::T_GUI);
    CHECK(staff.Accidental() == 1); // 0 means "use the key", not "natural"
  }

  TEST_CASE("nslider: an accidental on a white key is ignored (#555)") {
    // There is no such thing as a sharpened E drawn as an E.
    gNSlider staff;
    staff.GetInlet(0)->SetList("60 -1", YSE::T_GUI);
    CHECK(staff.Accidental() == 0);
    CHECK(staff.GetGuiValue() == Note(60, 0));
  }

  // ─── the bounds ─────────────────────────────────────────────────────────────

  TEST_CASE("nslider: the pitch is clamped into the bounds (#555)") {
    gNSlider staff;
    staff.SetParams("36 96");
    staff.GetInlet(0)->SetInt(20, YSE::T_GUI);
    CHECK(staff.Pitch() == 36);
    staff.GetInlet(0)->SetInt(120, YSE::T_GUI);
    CHECK(staff.Pitch() == 96);
  }

  TEST_CASE("nslider: reversed bounds still bound against the same numbers (#555)") {
    gNSlider staff;
    staff.SetParams("96 36");
    staff.GetInlet(0)->SetInt(20, YSE::T_GUI);
    CHECK(staff.Pitch() == 36);
    staff.GetInlet(0)->SetInt(120, YSE::T_GUI);
    CHECK(staff.Pitch() == 96);
  }

  TEST_CASE("nslider: the bounds are themselves confined to the MIDI range (#555)") {
    // A stave that claimed to cover pitch 500 would report a note nothing can
    // play.
    gNSlider staff;
    staff.SetParams("-50 500");
    staff.GetInlet(0)->SetInt(9999, YSE::T_GUI);
    CHECK(staff.Pitch() == 127);
    staff.GetInlet(0)->SetInt(-9999, YSE::T_GUI);
    CHECK(staff.Pitch() == 0);
  }

  TEST_CASE("nslider: a narrower range pulls the note in immediately (#555)") {
    // `.incdec`'s rule: clamping on the way out as well as in, so a live
    // re-range shows without waiting for the next message.
    gNSlider staff;
    staff.GetInlet(0)->SetInt(100, YSE::T_GUI);
    CHECK(staff.Pitch() == 100);
    staff.SetParams("36 96");
    CHECK(staff.Pitch() == 96);
  }

  // ─── the GUI value protocol (#551) ──────────────────────────────────────────

  TEST_CASE("nslider: it is a two-cell settable GUI object (#555)") {
    gNSlider staff;
    CHECK(staff.GuiValueIsSettable());
    CHECK(staff.GetGuiValueCount() == 2u);

    staff.GetInlet(0)->SetInt(61, YSE::T_GUI);
    CHECK(staff.GetGuiValue() == Note(61, 1));
    CHECK(staff.GetGuiValueAt(0) == "61");
    CHECK(staff.GetGuiValueAt(1) == "1");
    CHECK(staff.GetGuiValueAt(0) != staff.GetGuiValue());
  }

  TEST_CASE("nslider: cell reads past the end answer \"\" rather than indexing (#555)") {
    gNSlider staff;
    CHECK(staff.GetGuiValueAt(2).empty());
    CHECK(staff.GetGuiValueAt(50).empty());
    CHECK(staff.GetGuiValueAt(0xFFFFFFFFu).empty());
  }

  TEST_CASE("nslider: \"set <index> <value>\" writes one cell (#555)") {
    gNSlider staff;
    staff.SetParams("0 127 1");
    staff.GetInlet(0)->SetList("61 -1", YSE::T_GUI);
    REQUIRE(staff.GetGuiValue() == Note(61, -1));

    // Cell 0 moves the note and leaves the spelling, unlike a bare int: a cell
    // write touches one cell by definition.
    staff.GetInlet(0)->SetList("set 0 66", YSE::T_GUI);
    CHECK(staff.GetGuiValue() == Note(66, -1));

    // Cell 1 re-spells without moving.
    staff.GetInlet(0)->SetList("set 1 1", YSE::T_GUI);
    CHECK(staff.GetGuiValue() == Note(66, 1));

    // Out of range is dropped, never folded onto a real cell.
    staff.GetInlet(0)->SetList("set 2 99", YSE::T_GUI);
    staff.GetInlet(0)->SetList("set -1 99", YSE::T_GUI);
    CHECK(staff.GetGuiValue() == Note(66, 1));

    // A word that merely starts with "set" is not the keyword — it is a list,
    // so its numbers set the note.
    staff.GetInlet(0)->SetList("settle 63 -1", YSE::T_GUI);
    CHECK(staff.GetGuiValue() == Note(63, -1));
  }

  TEST_CASE("nslider: its own GetGuiValue round-trips through inlet 0 (#555)") {
    // The promise GuiValueIsSettable() makes, and the whole of what `.preset`
    // needs — including the *spelling*, which a bare pitch would have reset.
    gNSlider staff;
    staff.GetInlet(0)->SetList("61 -1", YSE::T_GUI);
    const std::string stored = staff.GetGuiValue();

    staff.GetInlet(0)->SetInt(60, YSE::T_GUI);
    REQUIRE(staff.GetGuiValue() != stored);

    staff.GetInlet(0)->SetList(stored, YSE::T_GUI);
    CHECK(staff.GetGuiValue() == stored);
  }

  TEST_CASE("nslider: a one-number list is a bare pitch (#555)") {
    gNSlider staff;
    staff.GetInlet(0)->SetList("61 -1", YSE::T_GUI);
    staff.GetInlet(0)->SetList("63", YSE::T_GUI);
    // No accidental given, so the key the music is in decides again.
    CHECK(staff.GetGuiValue() == Note(63, 1));
  }

  TEST_CASE("nslider: an empty list changes nothing (#555)") {
    MultiSink pitchSink;
    gNSlider staff;
    TestHelpers::Wire(staff, 0, pitchSink);
    staff.GetInlet(0)->SetInt(64, YSE::T_GUI);

    staff.GetInlet(0)->SetList("", YSE::T_GUI);
    CHECK(staff.Pitch() == 64);
    staff.GetInlet(0)->SetList("   ", YSE::T_GUI);
    CHECK(staff.Pitch() == 64);
    // It still re-sends, which is what a hot inlet does.
    CHECK(pitchSink.intValue == 64);
  }

  // ─── end to end, through the public patcher API ─────────────────────────────

  TEST_CASE("nslider: a host drives a real patch through pHandle (#555)") {
    // The issue's use case as a host builds it: a stave in F minor wired to a
    // pitch readout and an accidental readout, driven through the public handle
    // API and read back through the readouts' own handles. No internal pokes.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* staff = p.CreateObject(YSE::OBJ::G_NSLIDER, "36 96 -1");
    YSE::pHandle* pitchOut = p.CreateObject(YSE::OBJ::G_INT);
    YSE::pHandle* accidentalOut = p.CreateObject(YSE::OBJ::G_INT);
    REQUIRE(staff != nullptr);
    REQUIRE(pitchOut != nullptr);
    REQUIRE(accidentalOut != nullptr);
    p.Connect(staff, 0, pitchOut, 0);
    p.Connect(staff, 1, accidentalOut, 0);

    // A black key in a flat key signature.
    staff->SetIntData(0, 61);
    CHECK(pitchOut->GetGuiValue() == "61");
    CHECK(accidentalOut->GetGuiValue() == "-1");

    // One note re-spelled as a sharp, through the list form.
    staff->SetListData(0, "61 1");
    CHECK(accidentalOut->GetGuiValue() == "1");
    // And the key is unchanged: the next bare pitch is a flat again.
    staff->SetIntData(0, 63);
    CHECK(accidentalOut->GetGuiValue() == "-1");

    // Out of the stave's range, clamped.
    staff->SetIntData(0, 10);
    CHECK(pitchOut->GetGuiValue() == "36");

    // What a host polls to draw it.
    CHECK(staff->GuiValueIsSettable());
    CHECK(staff->GetGuiValueCount() == 2u);
    CHECK(staff->GetGuiValue() == Note(36, 0));
    CHECK(staff->GetGuiValueAt(0) == "36");
    CHECK(staff->GetGuiValueAt(1) == "0");
    CHECK(staff->GetGuiValueAt(2).empty());
  }

  TEST_CASE("nslider: a stored GUI value restores the note on a fresh patch (#555)") {
    // What `.preset` will do: read the string out of one patcher and push it
    // into the equivalent object in another — the spelling included, which is
    // the half a scalar cell could not have carried.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* from = src.CreateObject(YSE::OBJ::G_NSLIDER);
    REQUIRE(from != nullptr);
    from->SetListData(0, "61 -1");
    const std::string stored = from->GetGuiValue();
    REQUIRE(stored == Note(61, -1));

    YSE::patcher dst;
    dst.create(2);
    YSE::pHandle* to = dst.CreateObject(YSE::OBJ::G_NSLIDER);
    REQUIRE(to != nullptr);
    REQUIRE(to->GetGuiValue() != stored);

    to->SetListData(0, stored);
    CHECK(to->GetGuiValue() == stored);
  }

  TEST_CASE("nslider: params survive a DumpJSON / ParseJSON round trip (#555)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_NSLIDER, "36 96 -1") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".nslider") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".nslider"));
    CHECK(h->GetParams() == std::string("36 96 -1"));

    // The bounds and the spelling have to still *work*, not merely still be a
    // string: the note starts clamped up to the minimum, and a black key is a
    // flat.
    CHECK(h->GetGuiValue() == Note(36, 0));
    h->SetIntData(0, 61);
    CHECK(h->GetGuiValue() == Note(61, -1));
  }

  TEST_CASE("nslider: a live re-range rides the scalar plan, not a rebuild (#555)") {
    // All three params are scalars and the object registers no clear/parse
    // callbacks, so SetParams on a running patcher must defer to the audio
    // thread rather than replace the object (issue #234).
    patcherImplementation p(1, nullptr);
    YSE::pHandle* staff = p.CreateObject(YSE::OBJ::G_NSLIDER, "0 127 1");
    REQUIRE(staff != nullptr);

    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    p.Connect(staff, 1, &sinkHandle, 0);

    staff->SetIntData(0, 61);
    CHECK(sink.intValue == 1);

    const std::size_t retiredBefore = p.PendingRetired();
    const unsigned int idBefore = staff->GetID();
    staff->SetParams("0 127 -1");
    CHECK(p.PendingRetired() == retiredBefore);
    CHECK(staff->GetID() == idBefore);

    // Deferred: not visible until the audio thread has drained the plan.
    CHECK(staff->GetGuiValue() == Note(61, 1));
    p.Calculate(YSE::T_DSP);
    CHECK(staff->GetGuiValue() == Note(61, -1));
    CHECK(staff->GetID() == idBefore);
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the parameter order, which is what a
  // binding generator and a saved patch both key on.

  TEST_CASE("nslider: documents itself as GUI with three ordered params (#555)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::G_NSLIDER));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GUI);
    CHECK(obj->NumInputs() == 1);
    CHECK(obj->NumOutputs() == 2);

    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 3);
    CHECK(docs[0].name == "minimum");
    CHECK(docs[1].name == "maximum");
    CHECK(docs[2].name == "spelling");
  }

  // ─── real time ──────────────────────────────────────────────────────────────

  TEST_CASE("nslider: no message path allocates (#555)") {
    // The state is two atomic ints and the outlets carry ints, so no send
    // builds a string at all; the list handler compares its keyword in place
    // and parses through ExprParseFloatList rather than a stream. The counter
    // is read inside the scope and asserted outside it, since doctest's own
    // machinery allocates on first use.
    //
    // **The messages are built as strings before the scope opens, never passed
    // as literals inside it**, and that is not tidiness. `inlet::SetList` takes
    // a `const std::string&`, so a literal at the call site materialises a
    // temporary — a heap allocation whenever the text outgrows the
    // implementation's small-string buffer, and that buffer is *not* the same
    // width everywhere: 15 characters on libstdc++, 22 on libc++. A
    // 17-character list literal therefore costs nothing on the Windows/libc++
    // build and one allocation on the Linux/libstdc++ one, which is a probe
    // that passes locally and fails in CI while the object under test is
    // innocent. (Issue #554 learned this the hard way.) One of them is
    // deliberately longer than *both* buffers.
    const std::string warmList = "61 -1";
    const std::string warmSet = "set 0 63";
    const std::string warmLong = "66 1 ignored ignored"; // 20 chars: past libstdc++'s buffer
    const std::string wholeState = "68 -1";
    const std::string cellPitch = "set 0 70";
    const std::string cellSpelling = "set 1 1";
    const std::string single = "72";
    const std::string longList = "73 -1 ignored ignored"; // 21 chars

    MultiSink pitchSink, accidentalSink;
    gNSlider staff;
    TestHelpers::Wire(staff, 0, pitchSink);
    TestHelpers::Wire(staff, 1, accidentalSink);

    // Warm every path.
    staff.GetInlet(0)->SetList(warmList, YSE::T_GUI);
    staff.GetInlet(0)->SetList(warmSet, YSE::T_GUI);
    staff.GetInlet(0)->SetList(warmLong, YSE::T_GUI);
    staff.GetInlet(0)->SetInt(60, YSE::T_GUI);
    staff.GetInlet(0)->SetFloat(61.5f, YSE::T_GUI);
    staff.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(pitchSink.gotInt);
    REQUIRE(accidentalSink.gotInt);

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      staff.GetInlet(0)->SetList(wholeState, YSE::T_GUI);
      staff.GetInlet(0)->SetList(cellPitch, YSE::T_GUI);
      staff.GetInlet(0)->SetList(cellSpelling, YSE::T_GUI);
      staff.GetInlet(0)->SetList(single, YSE::T_GUI);
      staff.GetInlet(0)->SetList(longList, YSE::T_GUI);
      staff.GetInlet(0)->SetInt(64, YSE::T_GUI);
      staff.GetInlet(0)->SetFloat(65.25f, YSE::T_GUI);
      staff.GetInlet(0)->SetBang(YSE::T_GUI);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);
    // The messages really did land, so the zero above is not a vacuous pass.
    CHECK(staff.Pitch() == 65);
    CHECK(pitchSink.intValue == 65);
  }

} // TEST_SUITE("patcher")
