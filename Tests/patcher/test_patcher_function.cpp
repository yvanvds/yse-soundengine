// Tests for `.function` — Max's function, the breakpoint envelope editor
// (issue #561), and the third structured control on the GUI value protocol
// from issue #551.
//
// Four claims, and the tests are organised around them:
//
//   - **the store is a sorted float function.** Points arrive in any order and
//     are kept ascending by x, one y per x, ys clamped into the creation
//     bounds, xs folded to at least 0, and the capacity — allocated exactly
//     once at SetParams time — refuses rather than grows.
//
//   - **a query interpolates, shaped by the arriving point's curve.** An int
//     or float in answers with the y between its neighbours out outlet 0,
//     clamped at both ends, t^((1+c)/(1-c)) in between, with +/-1 as honest
//     steps.
//
//   - **a bang emits the whole function as a ramp list `.line` / `.bline`
//     consume.** `y0 0 y1 (x1-x0) ...` — one <target> <time> pair per point —
//     is exactly the breakpoint grammar the line family reads, so the headline
//     case is driven end to end into a real `.bline` and a real `.line`.
//
//   - **the state round-trips.** The GUI value is one breakpoint per cell as
//     `<x> <y> <curve>`, the bulk read is the whole-state list inlet 0 takes
//     back (an empty function spells itself `clear`), and the breakpoints ride
//     DumpJSON / ParseJSON unconditionally — contents, not just parameters.
//
// Plus the doc metadata and an allocation probe over every message path.
//
// No audio device required.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "patcher/guiObjects/gFunction.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"
#include "patcher/time/gLine.h"
#include "support/alloc_probe.hpp"

using TestHelpers::FloatSink;
using TestHelpers::MultiSink;
using YSE::PATCHER::gBline;
using YSE::PATCHER::gFunction;
using YSE::PATCHER::Register;

namespace {

  // The text the object spells a value with — ExprFormatValue's shortest
  // round-tripping form, which the outlets and the GUI value carry.
  std::string Text(float value) {
    char buffer[YSE::PATCHER::kExprValueTextMax];
    const int written = YSE::PATCHER::ExprFormatValue(YSE::PATCHER::ExprValue::Float(value), buffer,
                                                      YSE::PATCHER::kExprValueTextMax);
    return std::string(buffer, written > 0 ? (std::size_t)written : 0);
  }

  // One breakpoint as a GUI cell spells it.
  std::string Cell(float x, float y, float curve) {
    return Text(x) + " " + Text(y) + " " + Text(curve);
  }

  // One dump line: `<index> <x> <y> <curve>`.
  std::string DumpLine(int index, float x, float y, float curve) {
    return std::to_string(index) + " " + Text(x) + " " + Text(y) + " " + Text(curve);
  }

  // Records every list that arrives, in order, so a dump can be asserted as
  // the sequence it is rather than by its last line.
  struct LinesSink : YSE::PATCHER::pObject {
    std::vector<std::string> lines;

    LinesSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { lines.push_back(v); });
    }
    const char* Type() const override {
      return "lines_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // Records every float that arrives, in order — what a `.bline` ramp is.
  struct SequenceSink : YSE::PATCHER::pObject {
    std::vector<float> values;

    SequenceSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) { values.push_back(v); });
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) { values.push_back((float)v); });
    }
    const char* Type() const override {
      return "sequence_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("function: type name, port counts and outlet types (#561)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_FUNCTION);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".function");
    // One inlet, Max's left inlet.
    CHECK(h->GetInputs() == 1);
    // Three outlets: the interpolated y, the ramp list, the dump.
    CHECK(h->GetOutputs() == 3);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::LIST);
    CHECK(h->OutputDataType(2) == YSE::OUT_TYPE::LIST);
  }

  TEST_CASE("function: registry name and validity (#561)") {
    CHECK(YSE::patcher::IsValidObject(".function"));
    auto names = Register().AllNames();
    bool found = false;
    for (const auto& n : names) {
      if (n == ".function") found = true;
    }
    CHECK(found);
  }

  TEST_CASE("function: the capacity is a clamped creation parameter (#561)") {
    gFunction f;
    CHECK(f.Capacity() == gFunction::DEFAULT_POINTS);

    f.SetParams("4 0 127");
    CHECK(f.Capacity() == 4);

    f.SetParams("0");
    CHECK(f.Capacity() == gFunction::MIN_POINTS);
    f.SetParams("-9");
    CHECK(f.Capacity() == gFunction::MIN_POINTS);
    f.SetParams("9999");
    CHECK(f.Capacity() == gFunction::MAX_POINTS);

    // Not a number at all leaves the default in place, as the rest of the
    // family does.
    f.SetParams("wibble");
    CHECK(f.Capacity() == gFunction::DEFAULT_POINTS);

    // And SetParams("") returns the object to the no-argument shape.
    f.SetParams("4 0 127");
    REQUIRE(f.Capacity() == 4);
    f.SetParams("");
    CHECK(f.Capacity() == gFunction::DEFAULT_POINTS);
  }

  // ─── the store ──────────────────────────────────────────────────────────────

  TEST_CASE("function: points arrive in any order and stay sorted by x (#561)") {
    gFunction f;
    f.GetInlet(0)->SetList("500 0.5", YSE::T_GUI);
    f.GetInlet(0)->SetList("0 0.1", YSE::T_GUI);
    f.GetInlet(0)->SetList("1000 1", YSE::T_GUI);

    REQUIRE(f.PointCount() == 3u);
    CHECK(f.XAt(0) == doctest::Approx(0.f));
    CHECK(f.XAt(1) == doctest::Approx(500.f));
    CHECK(f.XAt(2) == doctest::Approx(1000.f));
    CHECK(f.YAt(0) == doctest::Approx(0.1f));
    CHECK(f.YAt(2) == doctest::Approx(1.f));

    // One y per x: the point already at 500 is replaced whole, not
    // duplicated.
    f.GetInlet(0)->SetList("500 0.75", YSE::T_GUI);
    REQUIRE(f.PointCount() == 3u);
    CHECK(f.YAt(1) == doctest::Approx(0.75f));
  }

  TEST_CASE("function: ys are clamped into the bounds, xs folded to the origin (#561)") {
    gFunction f;
    f.SetParams("8 0 127");
    f.GetInlet(0)->SetList("0 -50", YSE::T_GUI);
    f.GetInlet(0)->SetList("10 500", YSE::T_GUI);
    // Max's domain starts at 0, so a negative x folds onto the origin —
    // replacing whatever already sits there.
    f.GetInlet(0)->SetList("-5 64", YSE::T_GUI);

    REQUIRE(f.PointCount() == 2u);
    CHECK(f.XAt(0) == doctest::Approx(0.f));
    CHECK(f.YAt(0) == doctest::Approx(64.f));
    CHECK(f.YAt(1) == doctest::Approx(127.f));

    // Reversed bounds still bound against the same two numbers.
    gFunction reversed;
    reversed.SetParams("8 100 0");
    reversed.GetInlet(0)->SetList("0 500", YSE::T_GUI);
    reversed.GetInlet(0)->SetList("1 -3", YSE::T_GUI);
    CHECK(reversed.YAt(0) == doctest::Approx(100.f));
    CHECK(reversed.YAt(1) == doctest::Approx(0.f));
  }

  TEST_CASE("function: a store past the capacity is refused whole (#561)") {
    gFunction f;
    f.SetParams("2 0 1");
    f.GetInlet(0)->SetList("0 0", YSE::T_GUI);
    f.GetInlet(0)->SetList("100 1", YSE::T_GUI);
    REQUIRE(f.PointCount() == 2u);

    f.GetInlet(0)->SetList("50 0.5", YSE::T_GUI);
    CHECK(f.PointCount() == 2u);
    CHECK(f.XAt(0) == doctest::Approx(0.f));
    CHECK(f.XAt(1) == doctest::Approx(100.f));

    // A replace still lands: the point at an exact x takes no new slot.
    f.GetInlet(0)->SetList("100 0.25", YSE::T_GUI);
    CHECK(f.PointCount() == 2u);
    CHECK(f.YAt(1) == doctest::Approx(0.25f));
  }

  // ─── the query ──────────────────────────────────────────────────────────────

  TEST_CASE("function: a float queries the interpolated y out outlet 0 (#561)") {
    FloatSink y;
    gFunction f;
    TestHelpers::Wire(f, 0, y);

    // Max: "the value is taken as an X value and outputs a corresponding Y
    // value ... produced by linear floating-point interpolation".
    f.GetInlet(0)->SetList("0 0", YSE::T_GUI);
    f.GetInlet(0)->SetList("100 1", YSE::T_GUI);

    f.GetInlet(0)->SetFloat(50.f, YSE::T_GUI);
    REQUIRE(y.gotFloat);
    CHECK(y.received == doctest::Approx(0.5f));

    f.GetInlet(0)->SetFloat(25.f, YSE::T_GUI);
    CHECK(y.received == doctest::Approx(0.25f));

    // Clamped at both ends, which is what every envelope does there.
    f.GetInlet(0)->SetFloat(-10.f, YSE::T_GUI);
    CHECK(y.received == doctest::Approx(0.f));
    f.GetInlet(0)->SetFloat(500.f, YSE::T_GUI);
    CHECK(y.received == doctest::Approx(1.f));

    // An int is the same question, and the answer is still a float.
    f.GetInlet(0)->SetInt(75, YSE::T_GUI);
    CHECK(y.received == doctest::Approx(0.75f));
  }

  TEST_CASE("function: an empty function answers nothing (#561)") {
    FloatSink y;
    gFunction f;
    TestHelpers::Wire(f, 0, y);
    f.GetInlet(0)->SetFloat(50.f, YSE::T_GUI);
    CHECK_FALSE(y.gotFloat);
  }

  TEST_CASE("function: the arriving point's curve shapes the segment (#561)") {
    FloatSink y;
    gFunction f;
    TestHelpers::Wire(f, 0, y);
    f.GetInlet(0)->SetList("0 0", YSE::T_GUI);
    f.GetInlet(0)->SetList("100 1", YSE::T_GUI);

    // t^((1+c)/(1-c)). A positive curve leaves the start slowly: at c = 0.5
    // the exponent is 3, so the midpoint answers 0.125 rather than 0.5.
    f.GetInlet(0)->SetList("setcurve 1 0.5", YSE::T_GUI);
    f.GetInlet(0)->SetFloat(50.f, YSE::T_GUI);
    REQUIRE(y.gotFloat);
    CHECK(y.received == doctest::Approx(0.125f));

    // A negative curve is the reflection across the segment's diagonal — the
    // reciprocal exponent — and moves early.
    f.GetInlet(0)->SetList("setcurve 1 -0.5", YSE::T_GUI);
    f.GetInlet(0)->SetFloat(50.f, YSE::T_GUI);
    CHECK(y.received == doctest::Approx(std::pow(0.5f, 1.f / 3.f)));

    // The endpoints stay exact whatever the curve.
    f.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
    CHECK(y.received == doctest::Approx(0.f));
    f.GetInlet(0)->SetFloat(100.f, YSE::T_GUI);
    CHECK(y.received == doctest::Approx(1.f));

    // The limits are honest steps: +1 holds its start until the end...
    f.GetInlet(0)->SetList("setcurve 1 1", YSE::T_GUI);
    f.GetInlet(0)->SetFloat(99.f, YSE::T_GUI);
    CHECK(y.received == doctest::Approx(0.f));
    f.GetInlet(0)->SetFloat(100.f, YSE::T_GUI);
    CHECK(y.received == doctest::Approx(1.f));

    // ...and -1 jumps at the start.
    f.GetInlet(0)->SetList("setcurve 1 -1", YSE::T_GUI);
    f.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    CHECK(y.received == doctest::Approx(1.f));

    // A curve outside -1..1 is clamped, not dropped.
    f.GetInlet(0)->SetList("setcurve 1 9", YSE::T_GUI);
    CHECK(f.CurveAt(1) == doctest::Approx(1.f));
  }

  TEST_CASE("function: 'nth' answers one point's y and misses stay quiet (#561)") {
    FloatSink y;
    gFunction f;
    TestHelpers::Wire(f, 0, y);
    f.GetInlet(0)->SetList("0 0.25", YSE::T_GUI);
    f.GetInlet(0)->SetList("100 0.75", YSE::T_GUI);

    f.GetInlet(0)->SetList("nth 1", YSE::T_GUI);
    REQUIRE(y.gotFloat);
    CHECK(y.received == doctest::Approx(0.75f));

    y.gotFloat = false;
    f.GetInlet(0)->SetList("nth 5", YSE::T_GUI);
    f.GetInlet(0)->SetList("nth -1", YSE::T_GUI);
    f.GetInlet(0)->SetList("nth", YSE::T_GUI);
    CHECK_FALSE(y.gotFloat);
  }

  // ─── the ramp list ──────────────────────────────────────────────────────────

  TEST_CASE("function: a bang emits the function as target/time pairs (#561)") {
    MultiSink ramp;
    gFunction f;
    TestHelpers::Wire(f, 1, ramp);

    f.GetInlet(0)->SetList("0 0", YSE::T_GUI);
    f.GetInlet(0)->SetList("250 1", YSE::T_GUI);
    f.GetInlet(0)->SetList("1000 0.25", YSE::T_GUI);

    // Nothing left the outlet while the function was being drawn: writes are
    // silent, because this outlet is a trigger rather than a mirror.
    CHECK_FALSE(ramp.gotList);

    f.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(ramp.gotList);
    // The jump to the first y is the zero-time pair; every later time is the
    // gap to the previous point.
    CHECK(ramp.listValue == Text(0.f) + " " + Text(0.f) + " " + Text(1.f) + " " + Text(250.f) +
                                " " + Text(0.25f) + " " + Text(750.f));
  }

  TEST_CASE("function: the first point's absolute x is dropped from the ramp (#561)") {
    // Max's default (outputmode off): the envelope starts *now* at the first
    // y, wherever the first point sits on the axis.
    MultiSink ramp;
    gFunction f;
    TestHelpers::Wire(f, 1, ramp);
    f.GetInlet(0)->SetList("100 0.5", YSE::T_GUI);
    f.GetInlet(0)->SetList("200 1", YSE::T_GUI);

    f.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(ramp.gotList);
    CHECK(ramp.listValue == Text(0.5f) + " " + Text(0.f) + " " + Text(1.f) + " " + Text(100.f));
  }

  TEST_CASE("function: an empty function bangs out nothing (#561)") {
    MultiSink ramp;
    gFunction f;
    TestHelpers::Wire(f, 1, ramp);
    f.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(ramp.gotList);
  }

  TEST_CASE("function: the ramp list drives a real .bline, step by step (#561)") {
    // The headline claim, end to end: the emitted list is exactly the
    // breakpoint grammar the line family reads. `.bline` is the consumer whose
    // clock is the patch, so the whole envelope can be walked bang by bang and
    // asserted as the sequence a downstream patch would hear.
    SequenceSink steps;
    gBline line;
    line.SetParams("0.");
    gFunction f;

    TestHelpers::Wire(f, 1, line);
    TestHelpers::Wire(line, 0, steps);

    f.GetInlet(0)->SetList("0 0", YSE::T_GUI);
    f.GetInlet(0)->SetList("4 1", YSE::T_GUI);
    f.GetInlet(0)->SetBang(YSE::T_GUI);

    // Nothing at message time — `.bline`'s clock is the patch.
    CHECK(steps.values.empty());

    for (int i = 0; i < 5; i++)
      line.GetInlet(0)->SetBang(YSE::T_GUI);

    // The zero-time first pair costs the one bang every segment costs, landing
    // on the first y; the second pair ramps to 1 in four steps.
    REQUIRE(steps.values.size() == 5u);
    CHECK(steps.values[0] == doctest::Approx(0.f));
    CHECK(steps.values[1] == doctest::Approx(0.25f));
    CHECK(steps.values[2] == doctest::Approx(0.5f));
    CHECK(steps.values[3] == doctest::Approx(0.75f));
    CHECK(steps.values[4] == doctest::Approx(1.f));
  }

  TEST_CASE("function: a host bangs a .function into a .line through pHandle (#561)") {
    // The same claim through the public patcher API, against the object issue
    // #561 names as the consumer. A single zero-time pair arrives immediately,
    // so no clock is needed.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* f = p.CreateObject(YSE::OBJ::G_FUNCTION);
    YSE::pHandle* line = p.CreateObject(YSE::OBJ::G_LINE, "0.");
    YSE::pHandle* readout = p.CreateObject(YSE::OBJ::G_FLOAT);
    REQUIRE(f != nullptr);
    REQUIRE(line != nullptr);
    REQUIRE(readout != nullptr);
    p.Connect(f, 1, line, 0);
    p.Connect(line, 0, readout, 0);

    f->SetListData(0, "0 0.7");
    f->SetBang(0);
    CHECK(std::stof(readout->GetGuiValue()) == doctest::Approx(0.7f));
  }

  // ─── the dump ───────────────────────────────────────────────────────────────

  TEST_CASE("function: 'dump' sends every point as index x y curve (#561)") {
    LinesSink dump;
    gFunction f;
    TestHelpers::Wire(f, 2, dump);

    f.GetInlet(0)->SetList("100 1", YSE::T_GUI);
    f.GetInlet(0)->SetList("0 0.5", YSE::T_GUI);
    f.GetInlet(0)->SetList("setcurve 1 0.25", YSE::T_GUI);

    f.GetInlet(0)->SetList("dump", YSE::T_GUI);
    REQUIRE(dump.lines.size() == 2u);
    CHECK(dump.lines[0] == DumpLine(0, 0.f, 0.5f, 0.f));
    CHECK(dump.lines[1] == DumpLine(1, 100.f, 1.f, 0.25f));

    // An empty store dumps nothing.
    dump.lines.clear();
    f.GetInlet(0)->SetList("clear", YSE::T_GUI);
    f.GetInlet(0)->SetList("dump", YSE::T_GUI);
    CHECK(dump.lines.empty());
  }

  // ─── editing ────────────────────────────────────────────────────────────────

  TEST_CASE("function: 'set' rewrites one point and the store re-sorts (#561)") {
    gFunction f;
    f.GetInlet(0)->SetList("0 0", YSE::T_GUI);
    f.GetInlet(0)->SetList("100 0.5", YSE::T_GUI);
    f.GetInlet(0)->SetList("200 1", YSE::T_GUI);
    f.GetInlet(0)->SetList("setcurve 1 0.5", YSE::T_GUI);

    // Without a curve the point keeps the one it had — Max's split between
    // his three- and four-element point messages.
    f.GetInlet(0)->SetList("set 1 150 0.6", YSE::T_GUI);
    REQUIRE(f.PointCount() == 3u);
    CHECK(f.XAt(1) == doctest::Approx(150.f));
    CHECK(f.YAt(1) == doctest::Approx(0.6f));
    CHECK(f.CurveAt(1) == doctest::Approx(0.5f));

    // With one, it is set.
    f.GetInlet(0)->SetList("set 1 150 0.6 -0.25", YSE::T_GUI);
    CHECK(f.CurveAt(1) == doctest::Approx(-0.25f));

    // A moved x lands in sorted position, so the index it answers to changes.
    f.GetInlet(0)->SetList("set 1 300 0.6", YSE::T_GUI);
    CHECK(f.XAt(1) == doctest::Approx(200.f));
    CHECK(f.XAt(2) == doctest::Approx(300.f));
    CHECK(f.CurveAt(2) == doctest::Approx(-0.25f));

    // Out of range is dropped, never folded onto a real point.
    f.GetInlet(0)->SetList("set 9 5 5", YSE::T_GUI);
    f.GetInlet(0)->SetList("set -1 5 5", YSE::T_GUI);
    CHECK(f.PointCount() == 3u);
    CHECK(f.XAt(0) == doctest::Approx(0.f));
  }

  TEST_CASE("function: 'clear' empties, 'clear <index>' removes one (#561)") {
    gFunction f;
    f.GetInlet(0)->SetList("0 0", YSE::T_GUI);
    f.GetInlet(0)->SetList("100 0.5", YSE::T_GUI);
    f.GetInlet(0)->SetList("200 1", YSE::T_GUI);

    f.GetInlet(0)->SetList("clear 1", YSE::T_GUI);
    REQUIRE(f.PointCount() == 2u);
    CHECK(f.XAt(0) == doctest::Approx(0.f));
    CHECK(f.XAt(1) == doctest::Approx(200.f));

    // A miss removes nothing.
    f.GetInlet(0)->SetList("clear 7", YSE::T_GUI);
    CHECK(f.PointCount() == 2u);

    f.GetInlet(0)->SetList("clear", YSE::T_GUI);
    CHECK(f.PointCount() == 0u);
  }

  TEST_CASE("function: a whole-state list replaces the function (#561)") {
    gFunction f;
    f.GetInlet(0)->SetList("0 0", YSE::T_GUI);
    f.GetInlet(0)->SetList("500 1", YSE::T_GUI);

    // 3N numbers: <x> <y> <curve> per point. A restore replaces — the state
    // described is the state that results.
    f.GetInlet(0)->SetList("0 1 0 100 0 0.5", YSE::T_GUI);
    REQUIRE(f.PointCount() == 2u);
    CHECK(f.XAt(0) == doctest::Approx(0.f));
    CHECK(f.YAt(0) == doctest::Approx(1.f));
    CHECK(f.XAt(1) == doctest::Approx(100.f));
    CHECK(f.CurveAt(1) == doctest::Approx(0.5f));

    // Three numbers are a one-point whole state — Max's bare index-edit list
    // is deliberately not ported, and this is the collision that decides it.
    f.GetInlet(0)->SetList("5 0.5 0", YSE::T_GUI);
    REQUIRE(f.PointCount() == 1u);
    CHECK(f.XAt(0) == doctest::Approx(5.f));

    // Any other length addresses nothing.
    f.GetInlet(0)->SetList("1 2 3 4", YSE::T_GUI);
    f.GetInlet(0)->SetList("", YSE::T_GUI);
    f.GetInlet(0)->SetList("   ", YSE::T_GUI);
    CHECK(f.PointCount() == 1u);
  }

  // ─── the GUI value protocol (#551) ──────────────────────────────────────────

  TEST_CASE("function: a cell is one breakpoint, x y curve (#561)") {
    gFunction f;
    CHECK(f.GuiValueIsSettable());

    f.GetInlet(0)->SetList("0 0.25", YSE::T_GUI);
    f.GetInlet(0)->SetList("100 1", YSE::T_GUI);
    f.GetInlet(0)->SetList("setcurve 1 0.5", YSE::T_GUI);

    CHECK(f.GetGuiValueCount() == 2u);
    CHECK(f.GetGuiValueAt(0) == Cell(0.f, 0.25f, 0.f));
    CHECK(f.GetGuiValueAt(1) == Cell(100.f, 1.f, 0.5f));
    CHECK(f.GetGuiValue() == Cell(0.f, 0.25f, 0.f) + " " + Cell(100.f, 1.f, 0.5f));
    CHECK(f.GetGuiValueAt(0) != f.GetGuiValue());

    // Past the end is "", never the whole state again.
    CHECK(f.GetGuiValueAt(2).empty());
    CHECK(f.GetGuiValueAt(0xFFFFFFFFu).empty());
  }

  TEST_CASE("function: its own GetGuiValue round-trips through inlet 0 (#561)") {
    gFunction f;
    f.GetInlet(0)->SetList("0 0.25", YSE::T_GUI);
    f.GetInlet(0)->SetList("100 1", YSE::T_GUI);
    f.GetInlet(0)->SetList("setcurve 1 -0.5", YSE::T_GUI);
    const std::string stored = f.GetGuiValue();

    f.GetInlet(0)->SetList("clear", YSE::T_GUI);
    REQUIRE(f.PointCount() == 0u);

    f.GetInlet(0)->SetList(stored, YSE::T_GUI);
    CHECK(f.GetGuiValue() == stored);
    CHECK(f.PointCount() == 2u);
    CHECK(f.CurveAt(1) == doctest::Approx(-0.5f));
  }

  TEST_CASE("function: an empty function spells itself 'clear', and that round-trips (#561)") {
    // The one wrinkle a variable-count store adds to the protocol: the
    // whole-state string of an empty function is the message that makes a
    // function empty. A zero-token list stays a no-op, so a stray empty
    // message down a cord cannot wipe an envelope.
    gFunction f;
    CHECK(f.GetGuiValueCount() == 0u);
    CHECK(f.GetGuiValue() == "clear");

    f.GetInlet(0)->SetList("0 0.5", YSE::T_GUI);
    REQUIRE(f.PointCount() == 1u);

    f.GetInlet(0)->SetList("clear", YSE::T_GUI);
    CHECK(f.PointCount() == 0u);
    CHECK(f.GetGuiValue() == "clear");
  }

  TEST_CASE("function: a stored GUI value restores the function on a fresh patch (#561)") {
    // What `.preset` will do: read the string out of one patcher and push it
    // into the equivalent object in another, through nothing but the public
    // API.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* from = src.CreateObject(YSE::OBJ::G_FUNCTION);
    REQUIRE(from != nullptr);
    from->SetListData(0, "0 0 0 250 1 0.5 1000 0.25 0");
    const std::string stored = from->GetGuiValue();

    YSE::patcher dst;
    dst.create(2);
    YSE::pHandle* to = dst.CreateObject(YSE::OBJ::G_FUNCTION);
    REQUIRE(to != nullptr);
    REQUIRE(to->GetGuiValue() != stored);

    to->SetListData(0, stored);
    CHECK(to->GetGuiValue() == stored);
    CHECK(to->GetGuiValueCount() == 3u);
  }

  // ─── persistence ────────────────────────────────────────────────────────────

  TEST_CASE("function: the breakpoints survive a DumpJSON / ParseJSON round trip (#561)") {
    // Contents, not just parameters — Max's function is a UI object whose
    // points save with the patch, which is what issue #561's "contents in the
    // JSON round trip" asks for.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_FUNCTION, "8 0 127");
    REQUIRE(h != nullptr);
    h->SetListData(0, "0 0 0 100 127 0.5");
    const std::string json = src.DumpJSON();
    CHECK(json.find(".function") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* back = loaded.GetHandleFromList(0);
    REQUIRE(back != nullptr);
    CHECK(std::string(back->Type()) == std::string(".function"));
    CHECK(back->GetParams() == std::string("8 0 127"));
    CHECK(back->GetGuiValueCount() == 2u);
    CHECK(back->GetGuiValue() == Cell(0.f, 0.f, 0.f) + " " + Cell(100.f, 127.f, 0.5f));
  }

  TEST_CASE("function: the points are contents, not parameters (#561)") {
    // pObject.h's rule: a parameter is what the object was created with. The
    // points ride the state hook instead, so drawing into a function never
    // rewrites the arguments the author typed.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_FUNCTION, "8 0 1");
    REQUIRE(h != nullptr);
    h->SetListData(0, "0 0.5");
    CHECK(h->GetParams() == std::string("8 0 1"));
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the parameter, which is what a binding
  // generator and a saved patch both key on.

  TEST_CASE("function: documents itself as GUI with one creation parameter (#561)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::G_FUNCTION));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GUI);
    CHECK(obj->NumInputs() == 1);
    CHECK(obj->NumOutputs() == 3);

    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 1);
    CHECK(docs[0].name == "points");
  }

  // ─── real time ──────────────────────────────────────────────────────────────

  TEST_CASE("function: no message path allocates (#561)") {
    // Including the bang and the dump, which are the ones that could: both
    // send buffers are reserved at construction, the list handler walks its
    // tokens in place, and the dump captures into a pre-allocated scratch
    // array. The counter is read inside the scope and asserted outside it,
    // since doctest's own machinery allocates on first use.
    MultiSink y, ramp, dump;
    gFunction f;
    f.SetParams("8 0 1");
    TestHelpers::Wire(f, 0, y);
    TestHelpers::Wire(f, 1, ramp);
    TestHelpers::Wire(f, 2, dump);

    // Warm every path, and the sinks' own buffers at the lengths the probe
    // below will send them.
    f.GetInlet(0)->SetList("0 0", YSE::T_GUI);
    f.GetInlet(0)->SetList("250 1", YSE::T_GUI);
    f.GetInlet(0)->SetList("setcurve 1 0.5", YSE::T_GUI);
    f.GetInlet(0)->SetList("set 1 250 1 0.5", YSE::T_GUI);
    f.GetInlet(0)->SetList("0 0 0 250 1 0.5", YSE::T_GUI);
    f.GetInlet(0)->SetFloat(125.f, YSE::T_GUI);
    f.GetInlet(0)->SetList("nth 1", YSE::T_GUI);
    f.GetInlet(0)->SetBang(YSE::T_GUI);
    f.GetInlet(0)->SetList("dump", YSE::T_GUI);
    f.GetInlet(0)->SetList("clear 1", YSE::T_GUI);
    f.GetInlet(0)->SetList("250 1", YSE::T_GUI);
    REQUIRE(y.gotFloat);
    REQUIRE(ramp.gotList);
    REQUIRE(dump.gotList);

    int allocations = -1;
    {
      TestHelpers::ProbeScope probe;
      f.GetInlet(0)->SetList("0 0", YSE::T_GUI);
      f.GetInlet(0)->SetList("setcurve 1 0.5", YSE::T_GUI);
      f.GetInlet(0)->SetList("set 1 250 1 0.5", YSE::T_GUI);
      f.GetInlet(0)->SetList("0 0 0 250 1 0.5", YSE::T_GUI);
      f.GetInlet(0)->SetFloat(125.f, YSE::T_GUI);
      f.GetInlet(0)->SetList("nth 1", YSE::T_GUI);
      f.GetInlet(0)->SetBang(YSE::T_GUI);
      f.GetInlet(0)->SetList("dump", YSE::T_GUI);
      f.GetInlet(0)->SetList("clear 1", YSE::T_GUI);
      f.GetInlet(0)->SetList("250 1", YSE::T_GUI);
      allocations = TestHelpers::g_alloc_count.load();
    }
    CHECK(allocations == 0);
  }

} // TEST_SUITE("patcher")
