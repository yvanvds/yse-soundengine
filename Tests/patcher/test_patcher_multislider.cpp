// Tests for `.multislider` — Max's multislider, "an array of sliders" (issue
// #554), and the N-cell case of the structured GUI value protocol from issue
// #551.
//
// Three claims, and the tests are organised around them:
//
//   - **it is a bank, addressable whole or one cell at a time.** A list sets
//     every cell and the whole bank comes back out as one list; `set <index>
//     <value>` writes one cell and `fetch <index>` reads one, which is the
//     issue's "input accepts a whole list or an (index, value) pair; output
//     likewise". Max's own `set` and #551's cell write are the *same* message
//     here, which on `.rslider` they could not be.
//
//   - **N is variable, and a resize costs nothing and loses nothing.** The
//     count comes from the `size` argument and from any list of a different
//     length (Max's `listresize`), and both are one atomic store into a bank
//     allocated whole at construction. So a live re-size rides the wait-free
//     scalar plan (#234) rather than replacing the object, and shrinking hides
//     the tail rather than clearing it — shortening a sequence and lengthening
//     it again gets the steps back. That is the property nothing in the patcher
//     had exercised before this object, so it is pinned from both directions.
//
//   - **it is an N-cell GUI object.** GetGuiValueCount() is the live count, the
//     cells are the values, the whole state is the bank space-separated, and
//     inlet 0 takes that string straight back.
//
// End-to-end cases build a real patcher through the public API — real
// CreateObject with real bounds, real Connect, driven through pHandle the way a
// host drives a control, read back through the downstream objects' own handles —
// because that composition is what a host actually runs. Plus the JSON round
// trip, the doc metadata and an allocation probe over every message path.
//
// No audio device required.

#include <doctest/doctest.h>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "patcher/guiObjects/gMultiSlider.h"
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
using YSE::PATCHER::gMultiSlider;
using YSE::PATCHER::patcherImplementation;
using YSE::PATCHER::Register;

namespace {

  // The text the object spells a float with — ExprFormatValue's shortest
  // round-tripping form, which is what both the list outlet and the GUI value
  // carry.
  std::string Text(float value) {
    char buffer[YSE::PATCHER::kExprValueTextMax];
    const int written = YSE::PATCHER::ExprFormatValue(YSE::PATCHER::ExprValue::Float(value), buffer,
                                                      YSE::PATCHER::kExprValueTextMax);
    return std::string(buffer, written > 0 ? (std::size_t)written : 0);
  }

  // The bank as the object spells it: every cell, space separated, in order.
  std::string Bank(std::initializer_list<float> values) {
    std::string out;
    for (float value : values) {
      if (!out.empty()) out.push_back(' ');
      out += Text(value);
    }
    return out;
  }

  std::string Repeated(float value, int count) {
    std::string out;
    for (int i = 0; i < count; i++) {
      if (!out.empty()) out.push_back(' ');
      out += Text(value);
    }
    return out;
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("multislider: type name, port counts and outlet types (#554)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_MULTISLIDER);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".multislider");
    // One inlet, Max's.
    CHECK(h->GetInputs() == 1);
    // Two outlets, Max's: the whole bank and one fetched cell.
    CHECK(h->GetOutputs() == 2);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::LIST);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::FLOAT);
  }

  TEST_CASE("multislider: registry name and validity (#554)") {
    CHECK(YSE::patcher::IsValidObject(".multislider"));
    auto names = Register().AllNames();
    bool found = false;
    for (const auto& n : names) {
      if (n == ".multislider") found = true;
    }
    CHECK(found);
  }

  TEST_CASE("multislider: a bare object is eight cells at rest (#554)") {
    // Not Max's default of 1: Max can default to 1 because listresize reshapes
    // the object from its first list, and a one-cell multislider written out in
    // a patch is a .slider with a longer name.
    gMultiSlider bank;
    CHECK(bank.Cells() == 8u);
    CHECK(bank.GetGuiValueCount() == 8u);
    CHECK(bank.GetGuiValue() == Repeated(0.f, 8));
  }

  // ─── the bank ───────────────────────────────────────────────────────────────

  TEST_CASE("multislider: a list sets every cell and sends the bank (#554)") {
    MultiSink bankSink;
    gMultiSlider bank;
    bank.SetParams("4 0 127");
    TestHelpers::Wire(bank, 0, bankSink);

    bank.GetInlet(0)->SetList("10 20 30 40", YSE::T_GUI);

    CHECK(bankSink.gotList);
    CHECK(bankSink.listValue == Bank({10.f, 20.f, 30.f, 40.f}));
    CHECK(bank.GetGuiValue() == Bank({10.f, 20.f, 30.f, 40.f}));
  }

  TEST_CASE("multislider: an int or float sets every live cell (#554)") {
    // Max's int/float method: "sets all sliders to the received number".
    MultiSink bankSink;
    gMultiSlider bank;
    bank.SetParams("3 0 127");
    TestHelpers::Wire(bank, 0, bankSink);

    bank.GetInlet(0)->SetFloat(5.f, YSE::T_GUI);
    CHECK(bankSink.listValue == Repeated(5.f, 3));

    bank.GetInlet(0)->SetInt(7, YSE::T_GUI);
    CHECK(bankSink.listValue == Repeated(7.f, 3));
  }

  TEST_CASE("multislider: a bang re-sends the bank without changing it (#554)") {
    MultiSink bankSink;
    gMultiSlider bank;
    bank.SetParams("3 0 127");
    TestHelpers::Wire(bank, 0, bankSink);

    bank.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    bankSink.reset();

    bank.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(bankSink.listValue == Bank({1.f, 2.f, 3.f}));
    CHECK(bank.GetGuiValue() == Bank({1.f, 2.f, 3.f}));
  }

  // ─── N ──────────────────────────────────────────────────────────────────────

  TEST_CASE("multislider: a list of a different length resizes the bank (#554)") {
    // Max's `listresize` attribute, on by default — and the reason Max's `size`
    // defaults to 1. Portable here because a resize is one atomic store into a
    // bank allocated whole at construction.
    MultiSink bankSink;
    gMultiSlider bank;
    bank.SetParams("4 0 127");
    TestHelpers::Wire(bank, 0, bankSink);

    bank.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    CHECK(bank.Cells() == 3u);
    CHECK(bankSink.listValue == Bank({1.f, 2.f, 3.f}));

    bank.GetInlet(0)->SetList("9 8 7 6 5 4", YSE::T_GUI);
    CHECK(bank.Cells() == 6u);
    CHECK(bank.GetGuiValueCount() == 6u);
    CHECK(bankSink.listValue == Bank({9.f, 8.f, 7.f, 6.f, 5.f, 4.f}));
  }

  TEST_CASE("multislider: an empty list resizes nothing (#554)") {
    // It must not collapse the bank to zero cells; it falls through to the
    // re-send a bang gives.
    MultiSink bankSink;
    gMultiSlider bank;
    bank.SetParams("3 0 127");
    TestHelpers::Wire(bank, 0, bankSink);
    bank.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);

    bank.GetInlet(0)->SetList("", YSE::T_GUI);
    CHECK(bank.Cells() == 3u);
    bank.GetInlet(0)->SetList("   ", YSE::T_GUI);
    CHECK(bank.Cells() == 3u);
    CHECK(bank.GetGuiValue() == Bank({1.f, 2.f, 3.f}));
  }

  TEST_CASE("multislider: shrinking hides the tail rather than clearing it (#554)") {
    // The decision the variable cell count forced, stated in the class comment:
    // clearing on shrink would be an O(N) write on whichever thread the resize
    // arrived on, and a resize arrives on the audio thread by both routes. So a
    // sequence shortened and lengthened again gets its steps back.
    gMultiSlider bank;
    bank.SetParams("4 0 127");
    bank.GetInlet(0)->SetList("10 20 30 40", YSE::T_GUI);

    bank.GetInlet(0)->SetList("1 2", YSE::T_GUI);
    REQUIRE(bank.Cells() == 2u);
    CHECK(bank.GetGuiValue() == Bank({1.f, 2.f}));
    // The hidden cells are not readable while they are hidden.
    CHECK(bank.GetGuiValueAt(2).empty());

    bank.SetParams("4 0 127");
    CHECK(bank.Cells() == 4u);
    CHECK(bank.GetGuiValue() == Bank({1.f, 2.f, 30.f, 40.f}));
  }

  TEST_CASE("multislider: the size argument is clamped, not indexed (#554)") {
    gMultiSlider bank;
    bank.SetParams("0 0 127");
    CHECK(bank.Cells() == 1u);
    bank.SetParams("-5 0 127");
    CHECK(bank.Cells() == 1u);
    bank.SetParams("99999 0 127");
    CHECK(bank.Cells() == (unsigned int)gMultiSlider::MAX_CELLS);
  }

  TEST_CASE("multislider: a list longer than the bank stops at the ceiling (#554)") {
    gMultiSlider bank;
    std::string huge;
    for (std::size_t i = 0; i < gMultiSlider::MAX_CELLS + 20; i++) {
      if (i > 0) huge.push_back(' ');
      huge += "1";
    }
    bank.GetInlet(0)->SetList(huge, YSE::T_GUI);
    CHECK(bank.Cells() == (unsigned int)gMultiSlider::MAX_CELLS);
  }

  // ─── the bounds ─────────────────────────────────────────────────────────────

  TEST_CASE("multislider: every cell is clamped into the bounds (#554)") {
    gMultiSlider bank;
    bank.SetParams("3 20 20000");
    bank.GetInlet(0)->SetList("-5 400 1e9", YSE::T_GUI);
    CHECK(bank.GetGuiValue() == Bank({20.f, 400.f, 20000.f}));
  }

  TEST_CASE("multislider: reversed bounds still bound against the same numbers (#554)") {
    gMultiSlider bank;
    bank.SetParams("2 100 0");
    bank.GetInlet(0)->SetList("-50 500", YSE::T_GUI);
    CHECK(bank.GetGuiValue() == Bank({0.f, 100.f}));
  }

  TEST_CASE("multislider: an untouched bank reads back inside its bounds (#554)") {
    // .incdec's rule — bounding on the way out as well as in — and its reason:
    // the cells start at 0, which a `.multislider 4 20 20000` does not contain.
    gMultiSlider bank;
    bank.SetParams("4 20 20000");
    CHECK(bank.GetGuiValue() == Repeated(20.f, 4));
    CHECK(bank.GetGuiValueAt(3) == Text(20.f));
  }

  TEST_CASE("multislider: a narrower range pulls the cells in immediately (#554)") {
    gMultiSlider bank;
    bank.SetParams("3 0 127");
    bank.GetInlet(0)->SetList("10 60 120", YSE::T_GUI);
    CHECK(bank.GetGuiValue() == Bank({10.f, 60.f, 120.f}));

    bank.SetParams("3 0 100");
    CHECK(bank.GetGuiValue() == Bank({10.f, 60.f, 100.f}));
  }

  // ─── one cell at a time ─────────────────────────────────────────────────────

  TEST_CASE("multislider: \"set <index> <value>\" writes one cell (#554)") {
    // Max's own `set` message and issue #551's cell write are the same message
    // on this object — unlike .rslider, where Max's `set <min> <max>` collided
    // with the protocol's keyword and had to be dropped.
    gMultiSlider bank;
    bank.SetParams("4 0 127");
    bank.GetInlet(0)->SetList("10 20 30 40", YSE::T_GUI);

    bank.GetInlet(0)->SetList("set 2 99", YSE::T_GUI);
    CHECK(bank.GetGuiValue() == Bank({10.f, 20.f, 99.f, 40.f}));
    // It writes a cell; it does not reshape the bank into a two-cell one.
    CHECK(bank.Cells() == 4u);

    // Out of range is dropped, never folded onto a real cell.
    bank.GetInlet(0)->SetList("set 4 111", YSE::T_GUI);
    bank.GetInlet(0)->SetList("set -1 111", YSE::T_GUI);
    CHECK(bank.GetGuiValue() == Bank({10.f, 20.f, 99.f, 40.f}));

    // A word that merely starts with "set" is not the keyword — it is a list
    // whose first token is not a number, so the numbers in it set the bank.
    bank.GetInlet(0)->SetList("settle 1 2", YSE::T_GUI);
    CHECK(bank.GetGuiValue() == Bank({1.f, 2.f}));
  }

  TEST_CASE("multislider: \"fetch <index>\" answers on outlet 1 (#554)") {
    MultiSink bankSink, cellSink;
    gMultiSlider bank;
    bank.SetParams("4 0 127");
    TestHelpers::Wire(bank, 0, bankSink);
    TestHelpers::Wire(bank, 1, cellSink);

    bank.GetInlet(0)->SetList("10 20 30 40", YSE::T_GUI);
    cellSink.reset();

    bank.GetInlet(0)->SetList("fetch 2", YSE::T_GUI);
    CHECK(cellSink.gotFloat);
    CHECK(cellSink.floatValue == doctest::Approx(30.f));
    // Everything on the hot inlet emits, so the bank follows the fetched cell.
    CHECK(bankSink.listValue == Bank({10.f, 20.f, 30.f, 40.f}));

    // A fetch outside the live cells has no answer and sends nothing.
    cellSink.reset();
    bank.GetInlet(0)->SetList("fetch 9", YSE::T_GUI);
    CHECK_FALSE(cellSink.gotFloat);
    bank.GetInlet(0)->SetList("fetch -1", YSE::T_GUI);
    CHECK_FALSE(cellSink.gotFloat);
  }

  TEST_CASE("multislider: the fetched cell lands before the bank (#554)") {
    // The patcher's right-to-left ordering: outlet 1 is sent from the handler,
    // so it completes before Calculate() sends outlet 0.
    std::vector<char> log;
    OrderSink bankSink, cellSink;
    bankSink.log = &log;
    bankSink.tag = 'b';
    cellSink.log = &log;
    cellSink.tag = 'c';

    gMultiSlider bank;
    bank.SetParams("3 0 127");
    TestHelpers::Wire(bank, 0, bankSink);
    TestHelpers::Wire(bank, 1, cellSink);

    bank.GetInlet(0)->SetList("fetch 1", YSE::T_GUI);
    REQUIRE(log.size() == 2);
    CHECK(log[0] == 'c');
    CHECK(log[1] == 'b');
  }

  // ─── the GUI value protocol (#551) ──────────────────────────────────────────

  TEST_CASE("multislider: it is an N-cell settable GUI object (#554)") {
    gMultiSlider bank;
    bank.SetParams("4 0 127");
    CHECK(bank.GuiValueIsSettable());
    CHECK(bank.GetGuiValueCount() == 4u);

    bank.GetInlet(0)->SetList("15 45 75 105", YSE::T_GUI);
    CHECK(bank.GetGuiValue() == Bank({15.f, 45.f, 75.f, 105.f}));
    CHECK(bank.GetGuiValueAt(0) == Text(15.f));
    CHECK(bank.GetGuiValueAt(1) == Text(45.f));
    CHECK(bank.GetGuiValueAt(2) == Text(75.f));
    CHECK(bank.GetGuiValueAt(3) == Text(105.f));
    CHECK(bank.GetGuiValueAt(0) != bank.GetGuiValue());
  }

  TEST_CASE("multislider: the cell count follows a resize (#554)") {
    // What the protocol warns a host about: the count is live, so a poll reads
    // it now rather than caching it.
    gMultiSlider bank;
    bank.SetParams("2 0 127");
    CHECK(bank.GetGuiValueCount() == 2u);
    bank.GetInlet(0)->SetList("1 2 3 4 5", YSE::T_GUI);
    CHECK(bank.GetGuiValueCount() == 5u);
    CHECK(bank.GetGuiValueAt(4) == Text(5.f));
  }

  TEST_CASE("multislider: cell reads past the end answer \"\" rather than indexing (#554)") {
    gMultiSlider bank;
    bank.SetParams("3 0 127");
    CHECK(bank.GetGuiValueAt(3).empty());
    CHECK(bank.GetGuiValueAt(50).empty());
    CHECK(bank.GetGuiValueAt(0xFFFFFFFFu).empty());
  }

  TEST_CASE("multislider: its own GetGuiValue round-trips through inlet 0 (#554)") {
    // The promise GuiValueIsSettable() makes, and the whole of what `.preset`
    // needs: read one string, send it back as an ordinary list later, get the
    // bank back — the *count* included, since the string carries it.
    gMultiSlider bank;
    bank.SetParams("5 0 127");
    bank.GetInlet(0)->SetList("35 65 95 12 3", YSE::T_GUI);
    const std::string stored = bank.GetGuiValue();

    bank.GetInlet(0)->SetList("0 0", YSE::T_GUI);
    REQUIRE(bank.GetGuiValue() == Bank({0.f, 0.f}));
    REQUIRE(bank.Cells() == 2u);

    bank.GetInlet(0)->SetList(stored, YSE::T_GUI);
    CHECK(bank.GetGuiValue() == stored);
    CHECK(bank.Cells() == 5u);
    CHECK(bank.GetGuiValueAt(3) == Text(12.f));
  }

  TEST_CASE("multislider: the list outlet carries exactly the GUI value's text (#554)") {
    // One spelling of the bank, so a host that reads the state and a patch that
    // receives it downstream cannot disagree about what it says.
    MultiSink bankSink;
    gMultiSlider bank;
    bank.SetParams("3 0 127");
    TestHelpers::Wire(bank, 0, bankSink);

    bank.GetInlet(0)->SetList("0.5 12.25 100", YSE::T_GUI);
    CHECK(bankSink.listValue == bank.GetGuiValue());
  }

  // ─── end to end, through the public patcher API ─────────────────────────────

  TEST_CASE("multislider: a host drives a real patch through pHandle (#554)") {
    // The issue's use case as a host builds it: a bank with real bounds, wired
    // to a list readout and a value readout, driven through the public handle
    // API and read back through the readouts' own handles. No internal pokes.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* bank = p.CreateObject(YSE::OBJ::G_MULTISLIDER, "4 0 127");
    YSE::pHandle* bankOut = p.CreateObject(YSE::OBJ::G_LIST);
    YSE::pHandle* cellOut = p.CreateObject(YSE::OBJ::G_FLOAT);
    REQUIRE(bank != nullptr);
    REQUIRE(bankOut != nullptr);
    REQUIRE(cellOut != nullptr);
    p.Connect(bank, 0, bankOut, 0);
    p.Connect(bank, 1, cellOut, 0);

    // The whole state, as a list on inlet 0 — the protocol's write path.
    bank->SetListData(0, "10 20 30 40");
    CHECK(bankOut->GetGuiValue() == Bank({10.f, 20.f, 30.f, 40.f}));

    // One cell in, and one cell out, through the same public path.
    bank->SetListData(0, "set 2 99");
    bank->SetListData(0, "fetch 2");
    CHECK(std::stof(cellOut->GetGuiValue()) == doctest::Approx(99.f));

    // A shorter list reshapes the bank, and the readout follows.
    bank->SetListData(0, "1 2");
    CHECK(bankOut->GetGuiValue() == Bank({1.f, 2.f}));
    CHECK(bank->GetGuiValueCount() == 2u);

    // What a host polls to draw it.
    CHECK(bank->GuiValueIsSettable());
    CHECK(bank->GetGuiValue() == Bank({1.f, 2.f}));
    CHECK(bank->GetGuiValueAt(0) == Text(1.f));
    CHECK(bank->GetGuiValueAt(1) == Text(2.f));
    CHECK(bank->GetGuiValueAt(2).empty());
  }

  TEST_CASE("multislider: a stored GUI value restores the bank on a fresh patch (#554)") {
    // What `.preset` will do: read the string out of one patcher and push it
    // into the equivalent object in another. Nothing but the public API — and
    // the restored bank takes the stored *length* as well as the values, which
    // is what makes one string enough for a variable-length control.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* from = src.CreateObject(YSE::OBJ::G_MULTISLIDER, "5 0 127");
    REQUIRE(from != nullptr);
    from->SetListData(0, "12 34 56 78 90");
    const std::string stored = from->GetGuiValue();

    YSE::patcher dst;
    dst.create(2);
    YSE::pHandle* to = dst.CreateObject(YSE::OBJ::G_MULTISLIDER, "2 0 127");
    REQUIRE(to != nullptr);
    REQUIRE(to->GetGuiValue() != stored);

    to->SetListData(0, stored);
    CHECK(to->GetGuiValue() == stored);
    CHECK(to->GetGuiValueCount() == 5u);
  }

  TEST_CASE("multislider: params survive a DumpJSON / ParseJSON round trip (#554)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_MULTISLIDER, "6 20 20000") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".multislider") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".multislider"));
    CHECK(h->GetParams() == std::string("6 20 20000"));

    // The size and the bounds have to still *work*, not merely still be a
    // string: six cells, all of them clamped up to the minimum.
    CHECK(h->GetGuiValueCount() == 6u);
    CHECK(h->GetGuiValue() == Repeated(20.f, 6));
  }

  TEST_CASE("multislider: a live re-size rides the scalar plan, not a rebuild (#554)") {
    // All three params are scalars and the object registers no clear/parse
    // callbacks, so SetParams on a running patcher must defer to the audio
    // thread rather than replace the object (issue #234) — which is also what
    // makes the values survive the resize.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* bank = p.CreateObject(YSE::OBJ::G_MULTISLIDER, "4 0 127");
    REQUIRE(bank != nullptr);

    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    p.Connect(bank, 0, &sinkHandle, 0);

    bank->SetListData(0, "10 20 30 40");
    CHECK(sink.listValue == Bank({10.f, 20.f, 30.f, 40.f}));

    const std::size_t retiredBefore = p.PendingRetired();
    const unsigned int idBefore = bank->GetID();
    bank->SetParams("2 0 127");
    CHECK(p.PendingRetired() == retiredBefore);
    CHECK(bank->GetID() == idBefore);
    CHECK(bank->GetParams() == "2 0 127");

    // Deferred: not visible until the audio thread has drained the plan.
    CHECK(bank->GetGuiValueCount() == 4u);

    p.Calculate(YSE::T_DSP);
    CHECK(bank->GetGuiValueCount() == 2u);
    CHECK(bank->GetGuiValue() == Bank({10.f, 20.f}));

    // And the object survived it, so growing back finds the cells it hid.
    bank->SetParams("4 0 127");
    p.Calculate(YSE::T_DSP);
    CHECK(bank->GetID() == idBefore);
    CHECK(bank->GetGuiValue() == Bank({10.f, 20.f, 30.f, 40.f}));
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the parameter order, which is what a
  // binding generator and a saved patch both key on.

  TEST_CASE("multislider: documents itself as GUI with three ordered params (#554)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::G_MULTISLIDER));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GUI);
    CHECK(obj->NumInputs() == 1);
    CHECK(obj->NumOutputs() == 2);

    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 3);
    CHECK(docs[0].name == "size");
    CHECK(docs[1].name == "minimum");
    CHECK(docs[2].name == "maximum");
  }

  // ─── real time ──────────────────────────────────────────────────────────────

  TEST_CASE("multislider: no message path allocates (#554)") {
    // Including the whole-bank send, which is the one that could: it renders
    // every cell into a stack buffer and appends into a string reserved at
    // construction, and the list handler walks its numbers in place rather than
    // into a MAX_CELLS-wide stack buffer. The counter is read inside the scope
    // and asserted outside it, since doctest's own machinery allocates on first
    // use.
    MultiSink bankSink, cellSink;
    gMultiSlider bank;
    bank.SetParams("4 0 127");
    TestHelpers::Wire(bank, 0, bankSink);
    TestHelpers::Wire(bank, 1, cellSink);

    // Warm every path — and the sinks' own buffers, at the longest length the
    // probe below will send them.
    bank.GetInlet(0)->SetList("10 20 30 40", YSE::T_GUI);
    bank.GetInlet(0)->SetList("11 21 31 41 51 61", YSE::T_GUI);
    bank.GetInlet(0)->SetList("set 1 30", YSE::T_GUI);
    bank.GetInlet(0)->SetList("fetch 1", YSE::T_GUI);
    bank.GetInlet(0)->SetFloat(5.f, YSE::T_GUI);
    bank.GetInlet(0)->SetInt(6, YSE::T_GUI);
    bank.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(bankSink.gotList);
    REQUIRE(cellSink.gotFloat);

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      bank.GetInlet(0)->SetList("12 22 32 42", YSE::T_GUI);
      bank.GetInlet(0)->SetList("13 23 33 43 53 63", YSE::T_GUI);
      bank.GetInlet(0)->SetList("set 0 12", YSE::T_GUI);
      bank.GetInlet(0)->SetList("fetch 0", YSE::T_GUI);
      bank.GetInlet(0)->SetFloat(7.f, YSE::T_GUI);
      bank.GetInlet(0)->SetInt(8, YSE::T_GUI);
      bank.GetInlet(0)->SetBang(YSE::T_GUI);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);
  }

} // TEST_SUITE("patcher")
