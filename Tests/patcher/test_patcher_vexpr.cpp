// Tests for the list expression object (issue #450): .vexpr, and the
// RT-safe number formatter it adds to the evaluator .expr (#449) already
// shares with it in patcher/math/gExprEval.h.
//
// The file is in two halves.
//
// The first half drives ExprFormatValue directly. It is the one genuinely new
// piece of machinery this object needs: .expr sends its single result as a
// typed int or float and never has to spell a number, while .vexpr has to
// build a whole list of them on the hot path, where snprintf's locale and
// allocation behaviour are not welcome. So the formatter is hand-written, and
// the tests pin what it promises: the shortest spelling that reads back as the
// same float, a decimal point that survives, a bounded length, and — the one
// that matters most — an exact round trip through ExprParseFloatList over a
// wide sweep of values, because the output of one .vexpr is the input of the
// next.
//
// The second half drives the object: element-wise mapping, scalar broadcast,
// shortest-list pairing, the hot/cold split, the single-result-is-not-a-list
// rule, truncation at the list cap, the params round trip and the doc
// metadata.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/gExprEval.h"
#include "patcher/math/gVexpr.h"
#include "patcher/sinks.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::ExprFormatValue;
using YSE::PATCHER::ExprParseFloatList;
using YSE::PATCHER::ExprValue;
using YSE::PATCHER::kExprValueTextMax;
using YSE::PATCHER::kVexprMaxList;

namespace {

  // Format and check the contract every call has to keep: NUL-terminated, the
  // returned length is the real one, and it fits the advertised buffer.
  std::string Fmt(const ExprValue& v) {
    char buf[kExprValueTextMax];
    std::memset(buf, '?', sizeof(buf));
    const int n = ExprFormatValue(v, buf, kExprValueTextMax);
    REQUIRE(n > 0);
    REQUIRE(n < kExprValueTextMax);
    CHECK(buf[n] == '\0');
    CHECK(n == (int)std::strlen(buf));
    return std::string(buf, (std::size_t)n);
  }

  std::string FmtF(float v) {
    return Fmt(ExprValue::Float(v));
  }
  std::string FmtI(int v) {
    return Fmt(ExprValue::Int(v));
  }

  // The property that actually matters: what .vexpr writes, the next object
  // reads back as the same float.
  bool RoundTrips(float v) {
    const std::string text = FmtF(v);
    float parsed = 1.f;
    INFO("formatted as: " << text);
    if (ExprParseFloatList(text.c_str(), &parsed, 1) != 1) return false;
    return parsed == v;
  }

  // Split a list the object sent back into numbers.
  std::vector<float> Items(const std::string& list) {
    std::vector<float> out((std::size_t)kVexprMaxList, 0.f);
    const int n = ExprParseFloatList(list.c_str(), out.data(), kVexprMaxList);
    out.resize((std::size_t)(n < 0 ? 0 : n));
    return out;
  }

  // Holds a .vexpr and its sink together. The expression is set *before* the
  // outlet is wired, mirroring the patcher's own order.
  struct VexprRig {
    YSE::PATCHER::gVexpr op;
    MultiSink sink;

    explicit VexprRig(const std::string& expression) {
      op.SetParams(expression);
      op.ConnectOutlet(sink.GetInlet(0), 0);
      sink.ConnectInlet(op.GetOutlet(0), 0);
    }

    // Cold inlets only store; use before a Feed.
    void SetList(int inlet, const std::string& list) {
      op.GetInlet(inlet)->SetList(list, YSE::T_GUI);
    }
    void Set(int inlet, float value) {
      op.GetInlet(inlet)->SetFloat(value, YSE::T_GUI);
    }

    void FeedList(const std::string& list) {
      sink.reset();
      op.GetInlet(0)->SetList(list, YSE::T_GUI);
    }
    void Feed(float value) {
      sink.reset();
      op.GetInlet(0)->SetFloat(value, YSE::T_GUI);
    }
    void Bang() {
      sink.reset();
      op.GetInlet(0)->SetBang(YSE::T_GUI);
    }

    std::vector<float> Out() const {
      return Items(sink.listValue);
    }
    // Whatever came out when a single result was expected.
    float Scalar() const {
      return sink.gotInt ? (float)sink.intValue : sink.floatValue;
    }
  };

  std::string Repeat(const char* item, int times) {
    std::string s;
    for (int i = 0; i < times; i++) {
      if (i != 0) s += ' ';
      s += item;
    }
    return s;
  }

} // namespace

TEST_SUITE("patcher") {

  // ═══ the formatter ════════════════════════════════════════════════════════

  TEST_CASE("vexpr: an int formats as plain digits (#450)") {
    CHECK(FmtI(0) == "0");
    CHECK(FmtI(7) == "7");
    CHECK(FmtI(-7) == "-7");
    CHECK(FmtI(1234567) == "1234567");
    CHECK(FmtI(2147483647) == "2147483647");
    // INT_MIN is the one the naive negate-then-print gets wrong.
    CHECK(FmtI(-2147483647 - 1) == "-2147483648");
  }

  TEST_CASE("vexpr: a float always carries a decimal point or an exponent (#450)") {
    // Otherwise a whole-numbered float would arrive downstream looking like an
    // int, and the type the expression chose would be lost in transit.
    CHECK(FmtF(0.f) == "0.");
    CHECK(FmtF(2.f) == "2.");
    CHECK(FmtF(-2.f) == "-2.");
    CHECK(FmtF(100.f) == "100.");
    CHECK(FmtF(1e9f) == "1e+09"); // past the fixed-point window

    // ...and every one of them is read back as a float, not an int.
    for (const char* text : {"0.", "2.", "-2.", "100.", "1e+09"}) {
      CAPTURE(text);
      float parsed = -1.f;
      CHECK(ExprParseFloatList(text, &parsed, 1) == 1);
    }
  }

  TEST_CASE("vexpr: a float formats with the fewest digits that read back (#450)") {
    // The whole point of the hand-written formatter: 0.3f is 0.300000011920929
    // in full, and printing that would make every list unreadable.
    CHECK(FmtF(0.3f) == "0.3");
    CHECK(FmtF(0.1f) == "0.1");
    CHECK(FmtF(-1.5f) == "-1.5");
    CHECK(FmtF(1234.5678f) == "1234.5678");
    CHECK(FmtF(0.001f) == "0.001");
    CHECK(FmtF(3.14159f) == "3.14159");
  }

  TEST_CASE("vexpr: large and small magnitudes take the exponent form (#450)") {
    CHECK(FmtF(1e-5f) == "1e-05");
    CHECK(FmtF(2.5e12f) == "2.5e+12");
    CHECK(FmtF(-4e-30f) == "-4e-30");
    // The extremes of the type, including a subnormal.
    CHECK_FALSE(FmtF(std::numeric_limits<float>::max()).empty());
    CHECK_FALSE(FmtF(std::numeric_limits<float>::denorm_min()).empty());
  }

  TEST_CASE("vexpr: the fixed-point window covers the range a patch works in (#450)") {
    CHECK(FmtF(0.0001f) == "0.0001");
    CHECK(FmtF(999999.9f).find('e') == std::string::npos);
    CHECK(FmtF(60.f) == "60.");
    CHECK(FmtF(440.f) == "440.");
    CHECK(FmtF(0.5f) == "0.5");
  }

  // The property the object depends on, over a wide sweep rather than a
  // handful of literals: a .vexpr feeding a .vexpr must not lose a bit.
  TEST_CASE("vexpr: every formatted float parses back to the identical float (#450)") {
    const std::vector<float> corners = {0.f,
                                        1.f,
                                        -1.f,
                                        0.1f,
                                        0.3f,
                                        1.f / 3.f,
                                        2.f / 3.f,
                                        3.14159265f,
                                        2.71828183f,
                                        1e-4f,
                                        1e-5f,
                                        1.5e-8f,
                                        123456792.f,
                                        16777216.f,
                                        16777215.f,
                                        -0.000123456f,
                                        std::numeric_limits<float>::max(),
                                        -std::numeric_limits<float>::max(),
                                        std::numeric_limits<float>::min(),
                                        std::numeric_limits<float>::denorm_min()};
    for (float v : corners) {
      CAPTURE(v);
      CHECK(RoundTrips(v));
    }

    // A deterministic sweep across the exponent range, both signs.
    for (int e = -30; e <= 30; e++) {
      for (int m = 1; m <= 20; m++) {
        const float v = (float)m * 1.0839f * std::pow(10.f, (float)e);
        if (!std::isfinite(v) || v == 0.f) continue;
        CAPTURE(v);
        CHECK(RoundTrips(v));
        CHECK(RoundTrips(-v));
      }
    }
  }

  TEST_CASE("vexpr: a non-finite value formats as zero (#450)") {
    // Matching what ExprProgram::Evaluate already substitutes, so a value that
    // sneaks past it cannot put an "inf" or a "nan" into a list of numbers.
    CHECK(FmtF(std::numeric_limits<float>::infinity()) == "0.");
    CHECK(FmtF(-std::numeric_limits<float>::infinity()) == "0.");
    CHECK(FmtF(std::numeric_limits<float>::quiet_NaN()) == "0.");
  }

  TEST_CASE("vexpr: the formatter refuses a buffer it cannot fit (#450)") {
    // Rather than writing past the end of it.
    char small[4] = {'x', 'x', 'x', 'x'};
    CHECK(ExprFormatValue(ExprValue::Int(123456), small, 4) == 0);
    CHECK(small[0] == '\0');
    CHECK(ExprFormatValue(ExprValue::Int(1), nullptr, kExprValueTextMax) == 0);
    CHECK(ExprFormatValue(ExprValue::Int(1), small, 0) == 0);
  }

  // ═══ the object ═══════════════════════════════════════════════════════════

  TEST_CASE("vexpr: the object is creatable through the registry (#450)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_VEXPR);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".vexpr"));
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 1);
    // ANY, because a one-element result leaves as an int or a float.
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::ANY);
  }

  TEST_CASE("vexpr: the object is listed by pRegistry::AllNames (#450)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".vexpr")) != names.end());
  }

  TEST_CASE("vexpr: the expression grows the inlets, as in .expr (#450)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* one = p.CreateObject(YSE::OBJ::G_VEXPR, "$f1 * 2.");
    REQUIRE(one != nullptr);
    CHECK(one->GetInputs() == 1);

    YSE::pHandle* three = p.CreateObject(YSE::OBJ::G_VEXPR, "$f1 + $f2 + $f3");
    REQUIRE(three != nullptr);
    CHECK(three->GetInputs() == 3);

    // A gap still gets its inlet, so the numbering matches the placeholders.
    YSE::pHandle* gap = p.CreateObject(YSE::OBJ::G_VEXPR, "$f1 * $f4");
    REQUIRE(gap != nullptr);
    CHECK(gap->GetInputs() == 4);
  }

  // ─── element-wise mapping ───────────────────────────────────────────────

  TEST_CASE("vexpr: the expression is evaluated once per list element (#450)") {
    VexprRig rig("$f1 * 2.");
    rig.FeedList("1 2 3");
    REQUIRE(rig.sink.gotList);
    const std::vector<float> out = rig.Out();
    REQUIRE(out.size() == 3);
    CHECK(out[0] == doctest::Approx(2.f));
    CHECK(out[1] == doctest::Approx(4.f));
    CHECK(out[2] == doctest::Approx(6.f));
  }

  TEST_CASE("vexpr: two lists are paired element by element (#450)") {
    VexprRig rig("$f1 + $f2");
    REQUIRE(rig.op.NumInputs() == 2);
    rig.SetList(1, "10 20 30");
    rig.FeedList("1 2 3");

    const std::vector<float> out = rig.Out();
    REQUIRE(out.size() == 3);
    CHECK(out[0] == doctest::Approx(11.f));
    CHECK(out[1] == doctest::Approx(22.f));
    CHECK(out[2] == doctest::Approx(33.f));
  }

  // The use case from the issue: scale a chord, transpose a motif.
  TEST_CASE("vexpr: a single value on an inlet is broadcast across the list (#450)") {
    VexprRig scale("$f1 * $f2");
    scale.Set(1, 0.5f);
    scale.FeedList("2 4 6 8");
    std::vector<float> out = scale.Out();
    REQUIRE(out.size() == 4);
    CHECK(out[0] == doctest::Approx(1.f));
    CHECK(out[3] == doctest::Approx(4.f));

    // A one-item *list* is the same thing, which is what Max's scalarmode says.
    VexprRig transpose("$i1 + $i2");
    transpose.SetList(1, "12");
    transpose.FeedList("60 64 67");
    out = transpose.Out();
    REQUIRE(out.size() == 3);
    CHECK(out[0] == doctest::Approx(72.f));
    CHECK(out[1] == doctest::Approx(76.f));
    CHECK(out[2] == doctest::Approx(79.f));

    // ...and the broadcast inlet can be the hot one just as well.
    VexprRig other("$f1 * $f2");
    other.SetList(1, "1 2 3");
    other.Feed(10.f);
    out = other.Out();
    REQUIRE(out.size() == 3);
    CHECK(out[2] == doctest::Approx(30.f));
  }

  TEST_CASE("vexpr: lists of different lengths stop at the shortest (#450)") {
    VexprRig rig("$f1 + $f2");
    rig.SetList(1, "10 20");
    rig.FeedList("1 2 3 4 5");

    const std::vector<float> out = rig.Out();
    REQUIRE(out.size() == 2);
    CHECK(out[0] == doctest::Approx(11.f));
    CHECK(out[1] == doctest::Approx(22.f));
  }

  TEST_CASE("vexpr: an inlet the expression never reads cannot truncate the result (#450)") {
    // Inlet 1 exists only so that $f3 keeps its number; a stale two-item list
    // sitting on it must not cut a five-item mapping down to two.
    VexprRig rig("$f1 + $f3");
    REQUIRE(rig.op.NumInputs() == 3);
    rig.SetList(1, "100 200");
    rig.SetList(2, "10 20 30 40 50");
    rig.FeedList("1 2 3 4 5");

    const std::vector<float> out = rig.Out();
    REQUIRE(out.size() == 5);
    CHECK(out[0] == doctest::Approx(11.f));
    CHECK(out[4] == doctest::Approx(55.f));
  }

  TEST_CASE("vexpr: the int and float type rules carry through to the list (#450)") {
    // $i truncates per element, and an all-int expression writes plain digits.
    VexprRig ints("$i1 * 2");
    ints.FeedList("1.7 2.9 -3.5");
    CHECK(ints.sink.listValue == "2 4 -6");

    // A float expression keeps the decimal point on every element.
    VexprRig floats("$f1 * 0.5");
    floats.FeedList("1 2 3");
    CHECK(floats.sink.listValue == "0.5 1. 1.5");
  }

  // ─── one result is not a list ───────────────────────────────────────────

  TEST_CASE("vexpr: a single result is sent as an int or a float, not a list (#450)") {
    VexprRig floats("$f1 * 2.");
    floats.Feed(3.f);
    CHECK(floats.sink.gotFloat);
    CHECK_FALSE(floats.sink.gotList);
    CHECK(floats.Scalar() == doctest::Approx(6.f));

    VexprRig ints("$i1 * 2");
    ints.Feed(3.4f);
    CHECK(ints.sink.gotInt);
    CHECK_FALSE(ints.sink.gotList);
    CHECK(ints.sink.intValue == 6);

    // A one-item list is a single value too.
    VexprRig one("$f1 + 1.");
    one.FeedList("41");
    CHECK(one.sink.gotFloat);
    CHECK_FALSE(one.sink.gotList);
    CHECK(one.Scalar() == doctest::Approx(42.f));

    // ...and an expression with no placeholder at all still answers a bang.
    VexprRig constant("6 * 7");
    constant.Bang();
    CHECK(constant.sink.gotInt);
    CHECK(constant.sink.intValue == 42);
  }

  // ─── the hot / cold split ───────────────────────────────────────────────

  TEST_CASE("vexpr: inlet 0 evaluates and the others only store (#450)") {
    VexprRig rig("$f1 + $f2 + $f3");
    REQUIRE(rig.op.NumInputs() == 3);

    rig.sink.reset();
    rig.SetList(1, "10 10 10");
    CHECK_FALSE(rig.sink.gotList);
    CHECK_FALSE(rig.sink.gotFloat);
    rig.SetList(2, "100 100 100");
    CHECK_FALSE(rig.sink.gotList);
    CHECK_FALSE(rig.sink.gotFloat);

    rig.FeedList("1 2 3");
    REQUIRE(rig.sink.gotList);
    const std::vector<float> out = rig.Out();
    REQUIRE(out.size() == 3);
    CHECK(out[0] == doctest::Approx(111.f));
    CHECK(out[2] == doctest::Approx(113.f));
  }

  TEST_CASE("vexpr: the stored lists survive between evaluations (#450)") {
    VexprRig rig("$f1 * $f2");
    rig.SetList(1, "1 2 3");

    rig.FeedList("10 10 10");
    std::vector<float> out = rig.Out();
    REQUIRE(out.size() == 3);
    CHECK(out[2] == doctest::Approx(30.f));

    // Inlet 1 still holds its list.
    rig.Bang();
    out = rig.Out();
    REQUIRE(out.size() == 3);
    CHECK(out[2] == doctest::Approx(30.f));

    rig.FeedList("1 1 1");
    out = rig.Out();
    REQUIRE(out.size() == 3);
    CHECK(out[2] == doctest::Approx(3.f));
  }

  TEST_CASE("vexpr: a bang on inlet 0 re-evaluates from the stored lists (#450)") {
    VexprRig rig("$f1 + $f2");
    rig.SetList(1, "1 2");
    rig.FeedList("10 20");
    CHECK(rig.Out().size() == 2);

    rig.Bang();
    REQUIRE(rig.sink.gotList);
    const std::vector<float> out = rig.Out();
    REQUIRE(out.size() == 2);
    CHECK(out[0] == doctest::Approx(11.f));
    CHECK(out[1] == doctest::Approx(22.f));

    // A bang before anything arrives maps the single stored 0.
    VexprRig fresh("$f1 + $f2");
    fresh.Bang();
    CHECK(fresh.sink.gotFloat);
    CHECK(fresh.Scalar() == doctest::Approx(0.f));
  }

  // The one real difference from .expr, and the reason the object exists.
  TEST_CASE("vexpr: a list on inlet 0 is inlet 0's data, unlike .expr (#450)") {
    // .expr would read "1 2 3" as $f1=1, $f2=2, $f3=3 and answer 7 once.
    VexprRig rig("$f1 + $f2 * $f3");
    REQUIRE(rig.op.NumInputs() == 3);
    rig.SetList(1, "2");
    rig.SetList(2, "3");
    rig.FeedList("1 2 3");

    const std::vector<float> out = rig.Out();
    REQUIRE(out.size() == 3);
    CHECK(out[0] == doctest::Approx(7.f)); // 1 + 2*3
    CHECK(out[1] == doctest::Approx(8.f));
    CHECK(out[2] == doctest::Approx(9.f));
  }

  // ─── the list cap ───────────────────────────────────────────────────────

  TEST_CASE("vexpr: a list is held at the cap and the rest is dropped (#450)") {
    VexprRig rig("$i1 + 1");
    rig.FeedList(Repeat("1", kVexprMaxList + 40));
    CHECK(rig.op.StoredCount(0) == kVexprMaxList);
    const std::vector<float> out = rig.Out();
    REQUIRE(out.size() == (std::size_t)kVexprMaxList);
    CHECK(out[0] == doctest::Approx(2.f));
    CHECK(out[(std::size_t)kVexprMaxList - 1] == doctest::Approx(2.f));
  }

  TEST_CASE("vexpr: a list with nothing numeric in it leaves the inlet alone (#450)") {
    // There is no such thing as a zero-item inlet, so ignoring the message
    // beats silently substituting a 0.
    VexprRig rig("$f1 * 2.");
    rig.FeedList("1 2 3");
    CHECK(rig.op.StoredCount(0) == 3);

    rig.FeedList("bang set");
    CHECK(rig.op.StoredCount(0) == 3);
    const std::vector<float> out = rig.Out();
    REQUIRE(out.size() == 3);
    CHECK(out[2] == doctest::Approx(6.f));
  }

  TEST_CASE("vexpr: an inlet that has received nothing holds a single 0 (#450)") {
    VexprRig rig("$f1 + $f2");
    CHECK(rig.op.StoredCount(0) == 1);
    CHECK(rig.op.StoredCount(1) == 1);
    rig.FeedList("1 2 3");
    const std::vector<float> out = rig.Out();
    REQUIRE(out.size() == 3);
    CHECK(out[0] == doctest::Approx(1.f)); // $f2 broadcasts its 0
    CHECK(out[2] == doctest::Approx(3.f));
  }

  // ─── malformed input ────────────────────────────────────────────────────

  TEST_CASE("vexpr: a malformed expression leaves one inlet and sends 0 (#450)") {
    VexprRig rig("$f1 *");
    CHECK_FALSE(rig.op.CompileError().empty());
    CHECK(rig.op.NumInputs() == 1);

    rig.FeedList("1 2 3");
    CHECK_FALSE(rig.sink.gotList);
    CHECK(rig.Scalar() == 0.f);
  }

  // ─── the RT contract ────────────────────────────────────────────────────

  TEST_CASE("vexpr: mapping a full-length list does not change the program (#450)") {
    // The RT argument in one assertion, mirroring .expr's: Calculate only
    // walks the compiled program and appends into a buffer reserved at
    // construction. If it ever recompiled, resized a per-inlet list or grew
    // the instruction array, this would move.
    VexprRig rig("sqrt($f1 * $f1) + $f2 * 0.5");
    rig.SetList(1, Repeat("2", kVexprMaxList));
    rig.SetList(0, Repeat("3", kVexprMaxList));

    const std::size_t size = rig.op.Program().Size();
    const int depth = rig.op.Program().StackDepth();

    for (int i = 0; i < 200; i++) {
      rig.Bang();
      REQUIRE(rig.sink.gotList);
    }
    const std::vector<float> out = rig.Out();
    REQUIRE(out.size() == (std::size_t)kVexprMaxList);
    CHECK(out[0] == doctest::Approx(4.f));
    CHECK(rig.op.Program().Size() == size);
    CHECK(rig.op.Program().StackDepth() == depth);
    CHECK(rig.op.StoredCount(0) == kVexprMaxList);
  }

  // ─── params / persistence ───────────────────────────────────────────────

  TEST_CASE("vexpr: re-setting the expression recompiles and resizes (#450)") {
    YSE::PATCHER::gVexpr op;
    CHECK(op.NumInputs() == 1);

    op.SetParams("$f1 + $f2 + $f3");
    CHECK(op.NumInputs() == 3);
    CHECK(op.CompileError().empty());

    // Shrinking works as well as growing, and the stored lists go with it.
    op.GetInlet(0)->SetList("1 2 3 4", YSE::T_GUI);
    CHECK(op.StoredCount(0) == 4);
    op.SetParams("$f1 * 2.");
    CHECK(op.NumInputs() == 1);
    CHECK(op.StoredCount(0) == 1);

    // An empty argument returns the object to its blank state.
    op.SetParams("");
    CHECK(op.NumInputs() == 1);
    CHECK_FALSE(op.Program().Valid());
  }

  TEST_CASE("vexpr: params survive a DumpJSON / ParseJSON round trip (#450)") {
    const std::string source = "($f1 * 0.5) + pow($f2, 2)";

    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_VEXPR, source) != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".vexpr") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".vexpr"));
    // The spacing the caller typed is preserved verbatim.
    CHECK(h->GetParams() == source);
    // ...and the reloaded object really did recompile it.
    CHECK(h->GetInputs() == 2);
  }

  TEST_CASE("vexpr: names its single parameter (#450)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_VEXPR));
    REQUIRE(obj != nullptr);
    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 1);
    CHECK(docs[0].name == "expression");
  }

  // ─── documentation ──────────────────────────────────────────────────────

  TEST_CASE("vexpr: documents itself as MATH with a labelled port pair (#450)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_VEXPR));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
    CHECK_FALSE(obj->GetDescription().empty());

    REQUIRE(obj->NumInputs() == 1);
    CHECK(obj->GetInlet(0)->GetDocLabel() == std::string("$1"));
    CHECK_FALSE(obj->GetInlet(0)->GetDocDescription().empty());

    REQUIRE(obj->NumOutputs() == 1);
    CHECK(obj->GetOutputType(0) == YSE::OUT_TYPE::ANY);
    CHECK(obj->GetOutlet(0)->GetDocLabel() == std::string("out"));
  }

  // The doc-coverage test only sees the object the registry builds, which has
  // one inlet; the inlets ParseParams adds must carry labels too.
  TEST_CASE("vexpr: the inlets created from the expression are documented (#450)") {
    YSE::PATCHER::gVexpr op;
    op.SetParams("$f1 + $f3");
    REQUIRE(op.NumInputs() == 3);

    const std::vector<std::string> labels = {"$1", "$2", "$3"};
    for (int i = 0; i < 3; i++) {
      CAPTURE(i);
      CHECK(op.GetInlet(i)->GetDocLabel() == labels[(std::size_t)i]);
      CHECK_FALSE(op.GetInlet(i)->GetDocDescription().empty());
    }
    // The gap inlet says it is a gap rather than pretending to be useful.
    CHECK(op.GetInlet(1)->GetDocDescription().find("Unused") != std::string::npos);
  }

} // TEST_SUITE("patcher")
