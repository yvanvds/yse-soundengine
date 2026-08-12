// Tests for `.rslider` — Max's rslider, "display or change a range of numbers"
// (issue #553), and the first object built on the structured GUI value
// protocol from issue #551.
//
// Two claims, and the tests are organised around them:
//
//   - **it holds a span, not a point.** Two ends, clamped into the object's
//     bounds and reported in order however they were written, out of two
//     separate outlets *and* as one two-element list — both forms the issue
//     asks for. Max's inlet split comes with it: the left inlet emits, the
//     right one stores silently.
//
//   - **it is a two-cell GUI object.** GetGuiValueCount() is 2, the cells are
//     the two ends, the whole state is "low high", and inlet 0 takes that
//     string straight back — plus "set <index> <value>" for one end, which is
//     the case the keyword was introduced for: for a two-cell control "0 1" is
//     otherwise both a whole state and a cell write. `set 0 1` writing one end
//     rather than the pair is therefore not a detail, it is the reason #551
//     spells the keyword out, and it is pinned here on the real object.
//
// End-to-end cases build a real patcher through the public API — real
// CreateObject with real bounds, real Connect, driven through pHandle the way
// a host drives a control, read back through the downstream objects' own
// handles — because that composition is what a host actually runs. Plus the
// JSON round trip, a live re-range, the doc metadata and an allocation probe
// over every message path.
//
// No audio device required.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <vector>

#include "patcher/guiObjects/gRSlider.h"
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
using YSE::PATCHER::gRSlider;
using YSE::PATCHER::patcherImplementation;
using YSE::PATCHER::Register;

namespace {

  // The text the object spells a float with — ExprFormatValue's shortest
  // round-tripping form, which is what both the list outlet and the GUI value
  // carry. Written out here rather than hard-coded per case so a case reads as
  // "the low end is 20" instead of as a string literal.
  std::string Text(float value) {
    char buffer[YSE::PATCHER::kExprValueTextMax];
    const int written = YSE::PATCHER::ExprFormatValue(YSE::PATCHER::ExprValue::Float(value), buffer,
                                                      YSE::PATCHER::kExprValueTextMax);
    return std::string(buffer, written > 0 ? (std::size_t)written : 0);
  }

  std::string Range(float low, float high) {
    return Text(low) + " " + Text(high);
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("rslider: type name, port counts and outlet types (#553)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_RSLIDER);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".rslider");
    // Two inlets, Max's: one end each, the left one hot and the right one cold.
    CHECK(h->GetInputs() == 2);
    // Three outlets: Max's two ends, plus the list its `listmode` attribute
    // would have switched between — offered together instead of behind a mode.
    CHECK(h->GetOutputs() == 3);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::FLOAT);
    CHECK(h->OutputDataType(2) == YSE::OUT_TYPE::LIST);
  }

  TEST_CASE("rslider: registry name and validity (#553)") {
    CHECK(YSE::patcher::IsValidObject(".rslider"));
    auto names = Register().AllNames();
    bool found = false;
    for (const auto& n : names) {
      if (n == ".rslider") found = true;
    }
    CHECK(found);
  }

  // ─── the range itself ───────────────────────────────────────────────────────

  TEST_CASE("rslider: a list sets both ends and sends the pair (#553)") {
    MultiSink lowSink, highSink, listSink;
    gRSlider slider;
    TestHelpers::Wire(slider, 0, lowSink);
    TestHelpers::Wire(slider, 1, highSink);
    TestHelpers::Wire(slider, 2, listSink);

    slider.GetInlet(0)->SetList("20 80", YSE::T_GUI);

    CHECK(lowSink.floatValue == doctest::Approx(20.f));
    CHECK(highSink.floatValue == doctest::Approx(80.f));
    CHECK(listSink.gotList);
    CHECK(listSink.listValue == Range(20.f, 80.f));
  }

  TEST_CASE("rslider: the left inlet emits, the right one stores silently (#553)") {
    // Max's split exactly: each inlet sets one end of the range, the left one
    // outputs and the right one only stores.
    MultiSink lowSink, highSink;
    gRSlider slider;
    TestHelpers::Wire(slider, 0, lowSink);
    TestHelpers::Wire(slider, 1, highSink);

    slider.GetInlet(1)->SetFloat(90.f, YSE::T_GUI);
    CHECK_FALSE(lowSink.gotFloat);
    CHECK_FALSE(highSink.gotFloat);
    CHECK(slider.GetGuiValue() == Range(0.f, 90.f));

    slider.GetInlet(0)->SetFloat(10.f, YSE::T_GUI);
    CHECK(lowSink.floatValue == doctest::Approx(10.f));
    CHECK(highSink.floatValue == doctest::Approx(90.f));
  }

  TEST_CASE("rslider: a bang re-sends the range without moving it (#553)") {
    MultiSink lowSink, highSink, listSink;
    gRSlider slider;
    TestHelpers::Wire(slider, 0, lowSink);
    TestHelpers::Wire(slider, 1, highSink);
    TestHelpers::Wire(slider, 2, listSink);

    slider.GetInlet(0)->SetList("30 60", YSE::T_GUI);
    lowSink.reset();
    highSink.reset();
    listSink.reset();

    slider.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(lowSink.floatValue == doctest::Approx(30.f));
    CHECK(highSink.floatValue == doctest::Approx(60.f));
    CHECK(listSink.listValue == Range(30.f, 60.f));
    CHECK(slider.GetGuiValue() == Range(30.f, 60.f));
  }

  TEST_CASE("rslider: an int is a float on either inlet (#553)") {
    MultiSink lowSink;
    gRSlider slider;
    TestHelpers::Wire(slider, 0, lowSink);

    slider.GetInlet(1)->SetInt(70, YSE::T_GUI);
    slider.GetInlet(0)->SetInt(40, YSE::T_GUI);
    CHECK(lowSink.floatValue == doctest::Approx(40.f));
    CHECK(slider.GetGuiValue() == Range(40.f, 70.f));
  }

  TEST_CASE("rslider: a single-number list moves that inlet's end alone (#553)") {
    // Max: "the first two numbers in the list are used". One number is
    // therefore the same message as a float on the same inlet.
    gRSlider slider;
    slider.GetInlet(0)->SetList("10 90", YSE::T_GUI);
    slider.GetInlet(0)->SetList("25", YSE::T_GUI);
    CHECK(slider.GetGuiValue() == Range(25.f, 90.f));
  }

  TEST_CASE("rslider: the ends are reported in order however they were written (#553)") {
    // Neither inlet is *the low one* — Max: "in the right inlet, the number is
    // taken as one end of the range; the left inlet sets the other end" — and
    // the sorting happens on the way out: dragging "always outputs the lowest
    // value out the left outlet".
    MultiSink lowSink, highSink;
    gRSlider slider;
    TestHelpers::Wire(slider, 0, lowSink);
    TestHelpers::Wire(slider, 1, highSink);

    slider.GetInlet(0)->SetList("80 20", YSE::T_GUI);
    CHECK(lowSink.floatValue == doctest::Approx(20.f));
    CHECK(highSink.floatValue == doctest::Approx(80.f));
    CHECK(slider.GetGuiValue() == Range(20.f, 80.f));

    // Writing an end past the other one neither collides nor collapses the
    // span: the written end becomes the top, the untouched one the bottom. The
    // list above left inlet 0's end holding 80 and inlet 1's holding 20, so
    // this write replaces the 80 and the 20 stays put.
    slider.GetInlet(0)->SetFloat(100.f, YSE::T_GUI);
    CHECK(lowSink.floatValue == doctest::Approx(20.f));
    CHECK(highSink.floatValue == doctest::Approx(100.f));
  }

  // ─── the bounds ─────────────────────────────────────────────────────────────

  TEST_CASE("rslider: both ends are clamped into the bounds (#553)") {
    MultiSink lowSink, highSink;
    gRSlider slider;
    slider.SetParams("20 20000");
    TestHelpers::Wire(slider, 0, lowSink);
    TestHelpers::Wire(slider, 1, highSink);

    slider.GetInlet(1)->SetFloat(1e9f, YSE::T_GUI);
    slider.GetInlet(0)->SetFloat(-5.f, YSE::T_GUI);
    CHECK(lowSink.floatValue == doctest::Approx(20.f));
    CHECK(highSink.floatValue == doctest::Approx(20000.f));
  }

  TEST_CASE("rslider: reversed bounds still bound against the same two numbers (#553)") {
    gRSlider slider;
    slider.SetParams("100 0");
    slider.GetInlet(0)->SetList("-50 500", YSE::T_GUI);
    CHECK(slider.GetGuiValue() == Range(0.f, 100.f));
  }

  TEST_CASE("rslider: an untouched object reads back inside its bounds (#553)") {
    // .incdec's rule — bounding on the way out as well as in — and its reason:
    // the two ends start at 0, which a `.rslider 20 20000` does not contain.
    gRSlider slider;
    slider.SetParams("20 20000");
    CHECK(slider.GetGuiValue() == Range(20.f, 20.f));
    CHECK(slider.GetGuiValueAt(0) == Text(20.f));
    CHECK(slider.GetGuiValueAt(1) == Text(20.f));
  }

  TEST_CASE("rslider: a narrower range pulls the ends in immediately (#553)") {
    gRSlider slider;
    slider.SetParams("0 127");
    slider.GetInlet(0)->SetList("10 120", YSE::T_GUI);
    CHECK(slider.GetGuiValue() == Range(10.f, 120.f));

    slider.SetParams("0 100");
    CHECK(slider.GetGuiValue() == Range(10.f, 100.f));
  }

  // ─── the GUI value protocol (#551) ──────────────────────────────────────────

  TEST_CASE("rslider: it is a two-cell settable GUI object (#553)") {
    gRSlider slider;
    CHECK(slider.GetGuiValueCount() == 2u);
    CHECK(slider.GuiValueIsSettable());

    slider.GetInlet(0)->SetList("15 45", YSE::T_GUI);
    // The whole state is both cells, space separated, in index order — and for
    // a structured object cell 0 is the first cell, not the whole state.
    CHECK(slider.GetGuiValue() == Range(15.f, 45.f));
    CHECK(slider.GetGuiValueAt(0) == Text(15.f));
    CHECK(slider.GetGuiValueAt(1) == Text(45.f));
    CHECK(slider.GetGuiValueAt(0) != slider.GetGuiValue());
  }

  TEST_CASE("rslider: cell reads past the end answer \"\" rather than indexing (#553)") {
    gRSlider slider;
    CHECK(slider.GetGuiValueAt(2).empty());
    CHECK(slider.GetGuiValueAt(3).empty());
    CHECK(slider.GetGuiValueAt(0xFFFFFFFFu).empty());
  }

  TEST_CASE("rslider: its own GetGuiValue round-trips through inlet 0 (#553)") {
    // The promise GuiValueIsSettable() makes, and the whole of what `.preset`
    // needs: read one string, send it back as an ordinary list later, get the
    // range back. Through the object's own handler, on the control thread.
    gRSlider slider;
    slider.GetInlet(0)->SetList("35 65", YSE::T_GUI);
    const std::string stored = slider.GetGuiValue();

    slider.GetInlet(0)->SetList("0 0", YSE::T_GUI);
    REQUIRE(slider.GetGuiValue() == Range(0.f, 0.f));

    slider.GetInlet(0)->SetList(stored, YSE::T_GUI);
    CHECK(slider.GetGuiValue() == stored);
    CHECK(slider.GetGuiValueAt(0) == Text(35.f));
    CHECK(slider.GetGuiValueAt(1) == Text(65.f));
  }

  TEST_CASE("rslider: \"set <index> <value>\" moves one end and leaves the other (#553)") {
    gRSlider slider;
    slider.GetInlet(0)->SetList("35 65", YSE::T_GUI);

    slider.GetInlet(0)->SetList("set 1 90", YSE::T_GUI);
    CHECK(slider.GetGuiValue() == Range(35.f, 90.f));

    slider.GetInlet(0)->SetList("set 0 5", YSE::T_GUI);
    CHECK(slider.GetGuiValue() == Range(5.f, 90.f));

    // An out-of-range cell index is dropped, not folded onto a real end.
    slider.GetInlet(0)->SetList("set 2 111", YSE::T_GUI);
    CHECK(slider.GetGuiValue() == Range(5.f, 90.f));
    slider.GetInlet(0)->SetList("set -1 111", YSE::T_GUI);
    CHECK(slider.GetGuiValue() == Range(5.f, 90.f));
  }

  TEST_CASE("rslider: the \"set\" keyword is what disambiguates the two write forms (#553)") {
    // This object is the reason issue #551 spells the keyword out: with exactly
    // two numeric cells, "0 1" is both a legal whole state and a legal cell
    // write, and only the keyword tells them apart. Same two tokens, two
    // different results — that is the entire case.
    gRSlider slider;
    slider.GetInlet(0)->SetList("40 60", YSE::T_GUI);

    slider.GetInlet(0)->SetList("0 1", YSE::T_GUI);
    CHECK(slider.GetGuiValue() == Range(0.f, 1.f));

    slider.GetInlet(0)->SetList("40 60", YSE::T_GUI);
    slider.GetInlet(0)->SetList("set 0 1", YSE::T_GUI);
    CHECK(slider.GetGuiValue() == Range(1.f, 60.f));

    // A word that merely starts with "set" is not the keyword.
    slider.GetInlet(0)->SetList("settle 0 1", YSE::T_GUI);
    CHECK(slider.GetGuiValue() == Range(0.f, 1.f));
  }

  TEST_CASE("rslider: the list outlet carries exactly the GUI value's text (#553)") {
    // One spelling of the range, so a host that reads the state and a patch
    // that receives it downstream cannot disagree about what it says.
    MultiSink listSink;
    gRSlider slider;
    TestHelpers::Wire(slider, 2, listSink);

    slider.GetInlet(0)->SetList("0.5 12.25", YSE::T_GUI);
    CHECK(listSink.listValue == slider.GetGuiValue());
  }

  // ─── outlet order ───────────────────────────────────────────────────────────

  TEST_CASE("rslider: the three outlets fire right to left (#553)") {
    // The patcher's ordering guarantee (.trigger, .bangbang): outlet n-1 first,
    // outlet 0 last, each send completing in full before the next starts. It
    // puts the low end last, which is where Max's leftmost outlet sits.
    std::vector<char> log;
    OrderSink lowSink, highSink, listSink;
    lowSink.log = &log;
    lowSink.tag = 'l';
    highSink.log = &log;
    highSink.tag = 'h';
    listSink.log = &log;
    listSink.tag = 'r';

    gRSlider slider;
    TestHelpers::Wire(slider, 0, lowSink);
    TestHelpers::Wire(slider, 1, highSink);
    TestHelpers::Wire(slider, 2, listSink);

    slider.GetInlet(0)->SetList("2 8", YSE::T_GUI);
    REQUIRE(log.size() == 3);
    CHECK(log[0] == 'r');
    CHECK(log[1] == 'h');
    CHECK(log[2] == 'l');
  }

  // ─── end to end, through the public patcher API ─────────────────────────────

  TEST_CASE("rslider: a host drives a real patch through pHandle (#553)") {
    // The issue's use case as a host builds it: a range with real bounds, wired
    // to three readouts, driven through the public handle API and read back
    // through the readouts' own handles. No internal pokes.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* slider = p.CreateObject(YSE::OBJ::G_RSLIDER, "0 127");
    YSE::pHandle* lowOut = p.CreateObject(YSE::OBJ::G_FLOAT);
    YSE::pHandle* highOut = p.CreateObject(YSE::OBJ::G_FLOAT);
    YSE::pHandle* listOut = p.CreateObject(YSE::OBJ::G_LIST);
    REQUIRE(slider != nullptr);
    REQUIRE(lowOut != nullptr);
    REQUIRE(highOut != nullptr);
    REQUIRE(listOut != nullptr);
    p.Connect(slider, 0, lowOut, 0);
    p.Connect(slider, 1, highOut, 0);
    p.Connect(slider, 2, listOut, 0);

    // The whole state, as a list on inlet 0 — the protocol's write path.
    slider->SetListData(0, "36 96");
    CHECK(std::stof(lowOut->GetGuiValue()) == doctest::Approx(36.f));
    CHECK(std::stof(highOut->GetGuiValue()) == doctest::Approx(96.f));
    CHECK(listOut->GetGuiValue() == Range(36.f, 96.f));

    // And the cell write, through the same public path.
    slider->SetListData(0, "set 0 48");
    CHECK(std::stof(lowOut->GetGuiValue()) == doctest::Approx(48.f));
    CHECK(std::stof(highOut->GetGuiValue()) == doctest::Approx(96.f));

    // What a host polls to draw it.
    CHECK(slider->GetGuiValueCount() == 2u);
    CHECK(slider->GuiValueIsSettable());
    CHECK(slider->GetGuiValue() == Range(48.f, 96.f));
    CHECK(slider->GetGuiValueAt(0) == Text(48.f));
    CHECK(slider->GetGuiValueAt(1) == Text(96.f));
  }

  TEST_CASE("rslider: a stored GUI value restores the range on a fresh patch (#553)") {
    // What `.preset` will do: read the string out of one patcher and push it
    // into the equivalent object in another. Nothing but the public API.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* from = src.CreateObject(YSE::OBJ::G_RSLIDER, "0 127");
    REQUIRE(from != nullptr);
    from->SetListData(0, "12 34");
    const std::string stored = from->GetGuiValue();

    YSE::patcher dst;
    dst.create(2);
    YSE::pHandle* to = dst.CreateObject(YSE::OBJ::G_RSLIDER, "0 127");
    REQUIRE(to != nullptr);
    REQUIRE(to->GetGuiValue() != stored);

    to->SetListData(0, stored);
    CHECK(to->GetGuiValue() == stored);
  }

  TEST_CASE("rslider: params survive a DumpJSON / ParseJSON round trip (#553)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_RSLIDER, "20 20000") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".rslider") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".rslider"));
    CHECK(h->GetParams() == std::string("20 20000"));

    // The bounds have to still *bound*, not merely still be a string.
    YSE::pHandle* readout = loaded.CreateObject(YSE::OBJ::G_FLOAT);
    REQUIRE(readout != nullptr);
    loaded.Connect(h, 0, readout, 0);
    h->SetFloatData(0, -100.f);
    CHECK(std::stof(readout->GetGuiValue()) == doctest::Approx(20.f));
  }

  TEST_CASE("rslider: a live re-range rides the scalar plan, not a rebuild (#553)") {
    // Both params are scalars and the object registers no clear/parse
    // callbacks, so SetParams on a running patcher must defer to the audio
    // thread rather than replace the object (issue #234).
    patcherImplementation p(1, nullptr);
    YSE::pHandle* slider = p.CreateObject(YSE::OBJ::G_RSLIDER, "0 127");
    REQUIRE(slider != nullptr);

    // Wired to the high end, which is the one a narrowing bound moves here.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    p.Connect(slider, 1, &sinkHandle, 0);

    slider->SetListData(0, "0 100");
    CHECK(sink.floatValue == doctest::Approx(100.f));

    const std::size_t retiredBefore = p.PendingRetired();
    const unsigned int idBefore = slider->GetID();
    slider->SetParams("0 50");
    CHECK(p.PendingRetired() == retiredBefore);
    CHECK(slider->GetID() == idBefore);
    CHECK(slider->GetParams() == "0 50");

    // Deferred: not visible until the audio thread has drained the plan.
    slider->SetListData(0, "0 100");
    CHECK(sink.floatValue == doctest::Approx(100.f));

    p.Calculate(YSE::T_DSP);
    slider->SetListData(0, "0 100");
    CHECK(sink.floatValue == doctest::Approx(50.f));
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the parameter order, which is what a
  // binding generator and a saved patch both key on.

  TEST_CASE("rslider: documents itself as GUI with two ordered params (#553)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::G_RSLIDER));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GUI);
    CHECK(obj->NumInputs() == 2);
    CHECK(obj->NumOutputs() == 3);

    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 2);
    CHECK(docs[0].name == "minimum");
    CHECK(docs[1].name == "maximum");
  }

  // ─── real time ──────────────────────────────────────────────────────────────

  TEST_CASE("rslider: no message path allocates (#553)") {
    // Including the list send, which is the one that could: it renders both
    // ends into stack buffers and appends into a string reserved at
    // construction. The counter is read inside the scope and asserted outside
    // it, since doctest's own machinery allocates on first use.
    MultiSink lowSink, highSink, listSink;
    gRSlider slider;
    slider.SetParams("0 127");
    TestHelpers::Wire(slider, 0, lowSink);
    TestHelpers::Wire(slider, 1, highSink);
    TestHelpers::Wire(slider, 2, listSink);

    // Warm every path (and the sinks' own buffers) before arming.
    slider.GetInlet(0)->SetList("10 20", YSE::T_GUI);
    slider.GetInlet(0)->SetList("set 1 30", YSE::T_GUI);
    slider.GetInlet(0)->SetFloat(5.f, YSE::T_GUI);
    slider.GetInlet(0)->SetInt(6, YSE::T_GUI);
    slider.GetInlet(0)->SetBang(YSE::T_GUI);
    slider.GetInlet(1)->SetFloat(40.f, YSE::T_GUI);
    slider.GetInlet(1)->SetInt(41, YSE::T_GUI);
    REQUIRE(listSink.gotList);

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      slider.GetInlet(0)->SetList("11 21", YSE::T_GUI);
      slider.GetInlet(0)->SetList("set 0 12", YSE::T_GUI);
      slider.GetInlet(0)->SetFloat(7.f, YSE::T_GUI);
      slider.GetInlet(0)->SetInt(8, YSE::T_GUI);
      slider.GetInlet(0)->SetBang(YSE::T_GUI);
      slider.GetInlet(1)->SetFloat(42.f, YSE::T_GUI);
      slider.GetInlet(1)->SetInt(43, YSE::T_GUI);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);
  }

} // TEST_SUITE("patcher")
