// Tests for `.xyslider` — Max's pictslider, "output numbers by moving a
// 2-dimensional slider" (issue #563), under a name that drops the picture:
// the image-based rendering is a host concern, the object is an XY pad.
//
// Two claims, and the tests are organised around them:
//
//   - **it holds a point, not a span.** Two independent axes, each clamped
//     into its *own* bounds pair, moving as one gesture: x and y out of two
//     separate outlets *and* as one two-element list — both forms the issue
//     asks for. The list is exactly the cursor message `.nodes` takes, which
//     is the issue's morph use case and is pinned here end to end. Unlike
//     `.rslider`, the cells are never sorted: x above y is a position, not a
//     collapsed range, and sorting would fold the plane along its diagonal.
//
//   - **it is a two-cell GUI object** on issue #551's structured protocol.
//     GetGuiValueCount() is 2, cell 0 is always x and cell 1 always y, the
//     whole state is "x y", and inlet 0 takes that string straight back —
//     plus "set <index> <value>" for one axis, the keyword the protocol
//     introduced because for a two-cell control "0 1" is otherwise both a
//     whole state and a cell write.
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
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "patcher/guiObjects/gXYSlider.h"
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
using YSE::PATCHER::gXYSlider;
using YSE::PATCHER::patcherImplementation;
using YSE::PATCHER::Register;

namespace {

  // The text the object spells a float with — ExprFormatValue's shortest
  // round-tripping form, which is what both the list outlet and the GUI value
  // carry. Written out here rather than hard-coded per case so a case reads as
  // "x is 20" instead of as a string literal.
  std::string Text(float value) {
    char buffer[YSE::PATCHER::kExprValueTextMax];
    const int written = YSE::PATCHER::ExprFormatValue(YSE::PATCHER::ExprValue::Float(value), buffer,
                                                      YSE::PATCHER::kExprValueTextMax);
    return std::string(buffer, written > 0 ? (std::size_t)written : 0);
  }

  // A list as the object spells it: every value, space separated, in order.
  std::string List(std::initializer_list<float> values) {
    std::string out;
    for (float value : values) {
      if (!out.empty()) out.push_back(' ');
      out += Text(value);
    }
    return out;
  }

  std::string Position(float x, float y) {
    return List({x, y});
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("xyslider: type name, port counts and outlet types (#563)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_XYSLIDER);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".xyslider");
    // Two inlets, the patcher's hot/cold split: x on the hot left one, y on
    // the cold right one.
    CHECK(h->GetInputs() == 2);
    // Three outlets: Max's x and y, plus the pair as one list — offered
    // together instead of behind a mode.
    CHECK(h->GetOutputs() == 3);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::FLOAT);
    CHECK(h->OutputDataType(2) == YSE::OUT_TYPE::LIST);
  }

  TEST_CASE("xyslider: registry name and validity (#563)") {
    CHECK(YSE::patcher::IsValidObject(".xyslider"));
    auto names = Register().AllNames();
    bool found = false;
    for (const auto& n : names) {
      if (n == ".xyslider") found = true;
    }
    CHECK(found);
  }

  // ─── the position itself ────────────────────────────────────────────────────

  TEST_CASE("xyslider: a list sets both axes and sends the position (#563)") {
    MultiSink xSink, ySink, listSink;
    gXYSlider pad;
    TestHelpers::Wire(pad, 0, xSink);
    TestHelpers::Wire(pad, 1, ySink);
    TestHelpers::Wire(pad, 2, listSink);

    pad.GetInlet(0)->SetList("20 80", YSE::T_GUI);

    CHECK(xSink.floatValue == doctest::Approx(20.f));
    CHECK(ySink.floatValue == doctest::Approx(80.f));
    CHECK(listSink.gotList);
    CHECK(listSink.listValue == Position(20.f, 80.f));
  }

  TEST_CASE("xyslider: the left inlet emits, the right one stores silently (#563)") {
    // The patcher's hot/cold rule: x arrives hot and fires the send, y
    // arrives cold and waits for it.
    MultiSink xSink, ySink;
    gXYSlider pad;
    TestHelpers::Wire(pad, 0, xSink);
    TestHelpers::Wire(pad, 1, ySink);

    pad.GetInlet(1)->SetFloat(90.f, YSE::T_GUI);
    CHECK_FALSE(xSink.gotFloat);
    CHECK_FALSE(ySink.gotFloat);
    CHECK(pad.GetGuiValue() == Position(0.f, 90.f));

    pad.GetInlet(0)->SetFloat(10.f, YSE::T_GUI);
    CHECK(xSink.floatValue == doctest::Approx(10.f));
    CHECK(ySink.floatValue == doctest::Approx(90.f));
  }

  TEST_CASE("xyslider: a bang re-sends the position without moving it (#563)") {
    MultiSink xSink, ySink, listSink;
    gXYSlider pad;
    TestHelpers::Wire(pad, 0, xSink);
    TestHelpers::Wire(pad, 1, ySink);
    TestHelpers::Wire(pad, 2, listSink);

    pad.GetInlet(0)->SetList("30 60", YSE::T_GUI);
    xSink.reset();
    ySink.reset();
    listSink.reset();

    pad.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(xSink.floatValue == doctest::Approx(30.f));
    CHECK(ySink.floatValue == doctest::Approx(60.f));
    CHECK(listSink.listValue == Position(30.f, 60.f));
    CHECK(pad.GetGuiValue() == Position(30.f, 60.f));
  }

  TEST_CASE("xyslider: an int is a float on either inlet (#563)") {
    MultiSink xSink;
    gXYSlider pad;
    TestHelpers::Wire(pad, 0, xSink);

    pad.GetInlet(1)->SetInt(70, YSE::T_GUI);
    pad.GetInlet(0)->SetInt(40, YSE::T_GUI);
    CHECK(xSink.floatValue == doctest::Approx(40.f));
    CHECK(pad.GetGuiValue() == Position(40.f, 70.f));
  }

  TEST_CASE("xyslider: a single-number list moves x alone (#563)") {
    // The first two numbers in the list are used, so one number is the same
    // message as a float on the same inlet.
    gXYSlider pad;
    pad.GetInlet(0)->SetList("10 90", YSE::T_GUI);
    pad.GetInlet(0)->SetList("25", YSE::T_GUI);
    CHECK(pad.GetGuiValue() == Position(25.f, 90.f));
  }

  TEST_CASE("xyslider: the axes are independent — a position is never sorted (#563)") {
    // The deliberate divergence from `.rslider`, whose two cells are ends of
    // one range and leave in order. Here cell 0 is always x however it
    // compares to y: sorting a coordinate pair would fold the plane along its
    // diagonal, and (100, 20) is not the same position as (20, 100).
    MultiSink xSink, ySink;
    gXYSlider pad;
    TestHelpers::Wire(pad, 0, xSink);
    TestHelpers::Wire(pad, 1, ySink);

    pad.GetInlet(0)->SetList("100 20", YSE::T_GUI);
    CHECK(xSink.floatValue == doctest::Approx(100.f));
    CHECK(ySink.floatValue == doctest::Approx(20.f));
    CHECK(pad.GetGuiValueAt(0) == Text(100.f));
    CHECK(pad.GetGuiValueAt(1) == Text(20.f));
  }

  // ─── the bounds ─────────────────────────────────────────────────────────────

  TEST_CASE("xyslider: each axis is clamped into its own bounds (#563)") {
    // One range per axis — a cutoff on x and a normalised resonance on y do
    // not share a scale, which is why the object carries four bounds and not
    // two.
    MultiSink xSink, ySink;
    gXYSlider pad;
    pad.SetParams("20 20000 0 1");
    TestHelpers::Wire(pad, 0, xSink);
    TestHelpers::Wire(pad, 1, ySink);

    pad.GetInlet(1)->SetFloat(5.f, YSE::T_GUI);
    pad.GetInlet(0)->SetFloat(1e9f, YSE::T_GUI);
    CHECK(xSink.floatValue == doctest::Approx(20000.f));
    CHECK(ySink.floatValue == doctest::Approx(1.f));

    pad.GetInlet(1)->SetFloat(-5.f, YSE::T_GUI);
    pad.GetInlet(0)->SetFloat(-5.f, YSE::T_GUI);
    CHECK(xSink.floatValue == doctest::Approx(20.f));
    CHECK(ySink.floatValue == doctest::Approx(0.f));
  }

  TEST_CASE("xyslider: reversed bounds still bound against the same two numbers (#563)") {
    gXYSlider pad;
    pad.SetParams("100 0 1 -1");
    pad.GetInlet(0)->SetList("-50 500", YSE::T_GUI);
    CHECK(pad.GetGuiValue() == Position(0.f, 1.f));
  }

  TEST_CASE("xyslider: an untouched object reads back inside its bounds (#563)") {
    // .incdec's rule — bounding on the way out as well as in — and its
    // reason: both axes start at 0, which a `.xyslider 20 20000 5 10` does
    // not contain.
    gXYSlider pad;
    pad.SetParams("20 20000 5 10");
    CHECK(pad.GetGuiValue() == Position(20.f, 5.f));
    CHECK(pad.GetGuiValueAt(0) == Text(20.f));
    CHECK(pad.GetGuiValueAt(1) == Text(5.f));
  }

  TEST_CASE("xyslider: a narrower range pulls the position in immediately (#563)") {
    gXYSlider pad;
    pad.SetParams("0 127 0 127");
    pad.GetInlet(0)->SetList("10 120", YSE::T_GUI);
    CHECK(pad.GetGuiValue() == Position(10.f, 120.f));

    pad.SetParams("0 100 0 100");
    CHECK(pad.GetGuiValue() == Position(10.f, 100.f));
  }

  // ─── the GUI value protocol (#551) ──────────────────────────────────────────

  TEST_CASE("xyslider: it is a two-cell settable GUI object (#563)") {
    gXYSlider pad;
    CHECK(pad.GetGuiValueCount() == 2u);
    CHECK(pad.GuiValueIsSettable());

    pad.GetInlet(0)->SetList("15 45", YSE::T_GUI);
    // The whole state is both cells, space separated, in index order — and
    // for a structured object cell 0 is the first cell, not the whole state.
    CHECK(pad.GetGuiValue() == Position(15.f, 45.f));
    CHECK(pad.GetGuiValueAt(0) == Text(15.f));
    CHECK(pad.GetGuiValueAt(1) == Text(45.f));
    CHECK(pad.GetGuiValueAt(0) != pad.GetGuiValue());
  }

  TEST_CASE("xyslider: cell reads past the end answer \"\" rather than indexing (#563)") {
    gXYSlider pad;
    CHECK(pad.GetGuiValueAt(2).empty());
    CHECK(pad.GetGuiValueAt(3).empty());
    CHECK(pad.GetGuiValueAt(0xFFFFFFFFu).empty());
  }

  TEST_CASE("xyslider: its own GetGuiValue round-trips through inlet 0 (#563)") {
    // The promise GuiValueIsSettable() makes, and the whole of what `.preset`
    // needs: read one string, send it back as an ordinary list later, get the
    // position back. Through the object's own handler, on the control thread.
    gXYSlider pad;
    pad.GetInlet(0)->SetList("35 65", YSE::T_GUI);
    const std::string stored = pad.GetGuiValue();

    pad.GetInlet(0)->SetList("0 0", YSE::T_GUI);
    REQUIRE(pad.GetGuiValue() == Position(0.f, 0.f));

    pad.GetInlet(0)->SetList(stored, YSE::T_GUI);
    CHECK(pad.GetGuiValue() == stored);
    CHECK(pad.GetGuiValueAt(0) == Text(35.f));
    CHECK(pad.GetGuiValueAt(1) == Text(65.f));
  }

  TEST_CASE("xyslider: \"set <index> <value>\" moves one axis and leaves the other (#563)") {
    gXYSlider pad;
    pad.GetInlet(0)->SetList("35 65", YSE::T_GUI);

    pad.GetInlet(0)->SetList("set 1 90", YSE::T_GUI);
    CHECK(pad.GetGuiValue() == Position(35.f, 90.f));

    pad.GetInlet(0)->SetList("set 0 5", YSE::T_GUI);
    CHECK(pad.GetGuiValue() == Position(5.f, 90.f));

    // An out-of-range cell index is dropped, not folded onto a real axis.
    pad.GetInlet(0)->SetList("set 2 111", YSE::T_GUI);
    CHECK(pad.GetGuiValue() == Position(5.f, 90.f));
    pad.GetInlet(0)->SetList("set -1 111", YSE::T_GUI);
    CHECK(pad.GetGuiValue() == Position(5.f, 90.f));
  }

  TEST_CASE("xyslider: the \"set\" keyword is what disambiguates the two write forms (#563)") {
    // With exactly two numeric cells, "0 1" is both a legal whole state and a
    // legal cell write, and only the keyword tells them apart — the reason
    // issue #551 spells it out. Same two tokens, two different results.
    gXYSlider pad;
    pad.GetInlet(0)->SetList("40 60", YSE::T_GUI);

    pad.GetInlet(0)->SetList("0 1", YSE::T_GUI);
    CHECK(pad.GetGuiValue() == Position(0.f, 1.f));

    pad.GetInlet(0)->SetList("40 60", YSE::T_GUI);
    pad.GetInlet(0)->SetList("set 0 1", YSE::T_GUI);
    CHECK(pad.GetGuiValue() == Position(1.f, 60.f));

    // A word that merely starts with "set" is not the keyword.
    pad.GetInlet(0)->SetList("settle 0 1", YSE::T_GUI);
    CHECK(pad.GetGuiValue() == Position(0.f, 1.f));
  }

  TEST_CASE("xyslider: the list outlet carries exactly the GUI value's text (#563)") {
    // One spelling of the position, so a host that reads the state and a
    // patch that receives it downstream cannot disagree about what it says.
    MultiSink listSink;
    gXYSlider pad;
    TestHelpers::Wire(pad, 2, listSink);

    pad.GetInlet(0)->SetList("0.5 12.25", YSE::T_GUI);
    CHECK(listSink.listValue == pad.GetGuiValue());
  }

  // ─── outlet order ───────────────────────────────────────────────────────────

  TEST_CASE("xyslider: the three outlets fire right to left (#563)") {
    // The patcher's ordering guarantee (.trigger, .bangbang): outlet n-1
    // first, outlet 0 last, each send completing in full before the next
    // starts. It puts x last, which is where Max's leftmost outlet sits.
    std::vector<char> log;
    OrderSink xSink, ySink, listSink;
    xSink.log = &log;
    xSink.tag = 'x';
    ySink.log = &log;
    ySink.tag = 'y';
    listSink.log = &log;
    listSink.tag = 'p';

    gXYSlider pad;
    TestHelpers::Wire(pad, 0, xSink);
    TestHelpers::Wire(pad, 1, ySink);
    TestHelpers::Wire(pad, 2, listSink);

    pad.GetInlet(0)->SetList("2 8", YSE::T_GUI);
    REQUIRE(log.size() == 3);
    CHECK(log[0] == 'p');
    CHECK(log[1] == 'y');
    CHECK(log[2] == 'x');
  }

  // ─── end to end, through the public patcher API ─────────────────────────────

  TEST_CASE("xyslider: a host drives a real patch through pHandle (#563)") {
    // The issue's use case as a host builds it: an XY pad with a real range
    // per axis, wired to three readouts, driven through the public handle API
    // and read back through the readouts' own handles. No internal pokes.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* pad = p.CreateObject(YSE::OBJ::G_XYSLIDER, "0 127 0 127");
    YSE::pHandle* xOut = p.CreateObject(YSE::OBJ::G_FLOAT);
    YSE::pHandle* yOut = p.CreateObject(YSE::OBJ::G_FLOAT);
    YSE::pHandle* listOut = p.CreateObject(YSE::OBJ::G_LIST);
    REQUIRE(pad != nullptr);
    REQUIRE(xOut != nullptr);
    REQUIRE(yOut != nullptr);
    REQUIRE(listOut != nullptr);
    p.Connect(pad, 0, xOut, 0);
    p.Connect(pad, 1, yOut, 0);
    p.Connect(pad, 2, listOut, 0);

    // The whole state, as a list on inlet 0 — the protocol's write path.
    pad->SetListData(0, "36 96");
    CHECK(std::stof(xOut->GetGuiValue()) == doctest::Approx(36.f));
    CHECK(std::stof(yOut->GetGuiValue()) == doctest::Approx(96.f));
    CHECK(listOut->GetGuiValue() == Position(36.f, 96.f));

    // And the cell write, through the same public path.
    pad->SetListData(0, "set 0 48");
    CHECK(std::stof(xOut->GetGuiValue()) == doctest::Approx(48.f));
    CHECK(std::stof(yOut->GetGuiValue()) == doctest::Approx(96.f));

    // What a host polls to draw it.
    CHECK(pad->GetGuiValueCount() == 2u);
    CHECK(pad->GuiValueIsSettable());
    CHECK(pad->GetGuiValue() == Position(48.f, 96.f));
    CHECK(pad->GetGuiValueAt(0) == Text(48.f));
    CHECK(pad->GetGuiValueAt(1) == Text(96.f));
  }

  TEST_CASE("xyslider: the pair outlet drives a .nodes morph field (#563)") {
    // The issue names its own destination: "a morph coordinate feeding
    // .nodes". Outlet 2 carries the position as a two-element list, which is
    // exactly the cursor message `.nodes` takes — so the whole morph gesture
    // is one cord, wired here through the real graph and read back through a
    // readout's handle.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* pad = p.CreateObject(YSE::OBJ::G_XYSLIDER, "0 1 0 1");
    YSE::pHandle* field = p.CreateObject(YSE::OBJ::G_NODES, "2");
    YSE::pHandle* weights = p.CreateObject(YSE::OBJ::G_LIST);
    REQUIRE(pad != nullptr);
    REQUIRE(field != nullptr);
    REQUIRE(weights != nullptr);
    p.Connect(pad, 2, field, 0);
    p.Connect(field, 0, weights, 0);

    // Two unit-radius nodes on opposite corners of the pad's field.
    field->SetListData(0, "0 0 1 1 1 1");

    // The gesture a host performs: y on the cold inlet, then x on the hot
    // one, and the pair leaves as one message.
    pad->SetFloatData(1, 0.f);
    pad->SetFloatData(0, 0.f);
    CHECK(weights->GetGuiValue() == List({1.f, 0.f}));

    pad->SetListData(0, "1 1");
    CHECK(weights->GetGuiValue() == List({0.f, 1.f}));

    // The midpoint blends both equally — the morph the issue asks for.
    pad->SetListData(0, "0.5 0.5");
    CHECK(weights->GetGuiValue() == List({0.5f, 0.5f}));

    // And the pad's bounds hold the cursor inside the field: an overshoot
    // clamps to the corner rather than leaving the nodes behind.
    pad->SetListData(0, "9 9");
    CHECK(weights->GetGuiValue() == List({0.f, 1.f}));
  }

  TEST_CASE("xyslider: a stored GUI value restores the position on a fresh patch (#563)") {
    // What `.preset` will do: read the string out of one patcher and push it
    // into the equivalent object in another. Nothing but the public API.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* from = src.CreateObject(YSE::OBJ::G_XYSLIDER, "0 127 0 127");
    REQUIRE(from != nullptr);
    from->SetListData(0, "12 34");
    const std::string stored = from->GetGuiValue();

    YSE::patcher dst;
    dst.create(2);
    YSE::pHandle* to = dst.CreateObject(YSE::OBJ::G_XYSLIDER, "0 127 0 127");
    REQUIRE(to != nullptr);
    REQUIRE(to->GetGuiValue() != stored);

    to->SetListData(0, stored);
    CHECK(to->GetGuiValue() == stored);
  }

  TEST_CASE("xyslider: params survive a DumpJSON / ParseJSON round trip (#563)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_XYSLIDER, "20 20000 0 1") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".xyslider") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".xyslider"));
    CHECK(h->GetParams() == std::string("20 20000 0 1"));

    // The bounds have to still *bound*, not merely still be a string — and
    // per axis, so both pairs are checked.
    YSE::pHandle* xOut = loaded.CreateObject(YSE::OBJ::G_FLOAT);
    YSE::pHandle* yOut = loaded.CreateObject(YSE::OBJ::G_FLOAT);
    REQUIRE(xOut != nullptr);
    REQUIRE(yOut != nullptr);
    loaded.Connect(h, 0, xOut, 0);
    loaded.Connect(h, 1, yOut, 0);
    h->SetFloatData(1, 100.f);
    h->SetFloatData(0, -100.f);
    CHECK(std::stof(xOut->GetGuiValue()) == doctest::Approx(20.f));
    CHECK(std::stof(yOut->GetGuiValue()) == doctest::Approx(1.f));
  }

  TEST_CASE("xyslider: a live re-range rides the scalar plan, not a rebuild (#563)") {
    // All four params are scalars and the object registers no clear/parse
    // callbacks, so SetParams on a running patcher must defer to the audio
    // thread rather than replace the object (issue #234).
    patcherImplementation p(1, nullptr);
    YSE::pHandle* pad = p.CreateObject(YSE::OBJ::G_XYSLIDER, "0 127 0 127");
    REQUIRE(pad != nullptr);

    // Wired to x, which is the axis the narrowing bound moves here.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    p.Connect(pad, 0, &sinkHandle, 0);

    pad->SetListData(0, "100 100");
    CHECK(sink.floatValue == doctest::Approx(100.f));

    const std::size_t retiredBefore = p.PendingRetired();
    const unsigned int idBefore = pad->GetID();
    pad->SetParams("0 50 0 127");
    CHECK(p.PendingRetired() == retiredBefore);
    CHECK(pad->GetID() == idBefore);
    CHECK(pad->GetParams() == "0 50 0 127");

    // Deferred: not visible until the audio thread has drained the plan.
    pad->SetListData(0, "100 100");
    CHECK(sink.floatValue == doctest::Approx(100.f));

    p.Calculate(YSE::T_DSP);
    pad->SetListData(0, "100 100");
    CHECK(sink.floatValue == doctest::Approx(50.f));
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the parameter order, which is what a
  // binding generator and a saved patch both key on.

  TEST_CASE("xyslider: documents itself as GUI with four ordered params (#563)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::G_XYSLIDER));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GUI);
    CHECK(obj->NumInputs() == 2);
    CHECK(obj->NumOutputs() == 3);

    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 4);
    CHECK(docs[0].name == "xminimum");
    CHECK(docs[1].name == "xmaximum");
    CHECK(docs[2].name == "yminimum");
    CHECK(docs[3].name == "ymaximum");
  }

  // ─── real time ──────────────────────────────────────────────────────────────

  TEST_CASE("xyslider: no message path allocates (#563)") {
    // Including the list send, which is the one that could: it renders both
    // axes into stack buffers and appends into a string reserved at
    // construction. The counter is read inside the scope and asserted outside
    // it, since doctest's own machinery allocates on first use.
    MultiSink xSink, ySink, listSink;
    gXYSlider pad;
    pad.SetParams("0 127 0 127");
    TestHelpers::Wire(pad, 0, xSink);
    TestHelpers::Wire(pad, 1, ySink);
    TestHelpers::Wire(pad, 2, listSink);

    // Warm every path (and the sinks' own buffers) before arming.
    pad.GetInlet(0)->SetList("10 20", YSE::T_GUI);
    pad.GetInlet(0)->SetList("set 1 30", YSE::T_GUI);
    pad.GetInlet(0)->SetFloat(5.f, YSE::T_GUI);
    pad.GetInlet(0)->SetInt(6, YSE::T_GUI);
    pad.GetInlet(0)->SetBang(YSE::T_GUI);
    pad.GetInlet(1)->SetFloat(40.f, YSE::T_GUI);
    pad.GetInlet(1)->SetInt(41, YSE::T_GUI);
    REQUIRE(listSink.gotList);

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      pad.GetInlet(0)->SetList("11 21", YSE::T_GUI);
      pad.GetInlet(0)->SetList("set 0 12", YSE::T_GUI);
      pad.GetInlet(0)->SetFloat(7.f, YSE::T_GUI);
      pad.GetInlet(0)->SetInt(8, YSE::T_GUI);
      pad.GetInlet(0)->SetBang(YSE::T_GUI);
      pad.GetInlet(1)->SetFloat(42.f, YSE::T_GUI);
      pad.GetInlet(1)->SetInt(43, YSE::T_GUI);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);
  }

} // TEST_SUITE("patcher")
