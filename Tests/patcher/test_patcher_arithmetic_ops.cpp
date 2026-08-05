// Tests for the remaining patcher arithmetic operators (issue #439):
// .!- .!/ .% .div
//
// Two families, both with the two-inlet shape of the .+ family (inlet 0 is the
// left operand and fires, inlet 1 stores the right operand):
//
//   gReverseBase (.!- .!/) — floats in, float out, operands applied in reverse.
//   gIntDivBase  (.%  .div) — ints in, int out, divisor of 0 emits 0.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <climits>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/gIntDiv.h"
#include "patcher/math/gReverse.h"
#include "patcher/sinks.hpp"

using TestHelpers::FloatSink;
using TestHelpers::IntSink;

namespace {

  // Drives one operator through a right-then-left pair and returns what the
  // sink saw. Left is applied last because inlet 0 is the hot inlet.
  template <typename OpType> float EvaluateFloat(float left, float right) {
    OpType op;
    FloatSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(1)->SetFloat(right, YSE::T_GUI);
    op.GetInlet(0)->SetFloat(left, YSE::T_GUI);
    return sink.received;
  }

  template <typename OpType> int EvaluateInt(int left, int right) {
    OpType op;
    IntSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(1)->SetInt(right, YSE::T_GUI);
    op.GetInlet(0)->SetInt(left, YSE::T_GUI);
    return sink.received;
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("arithmetic ops: all four operators are creatable through the registry (#439)") {
    struct Expected {
      const char* type;
      YSE::OUT_TYPE outType;
    };
    const std::vector<Expected> types = {
        {YSE::OBJ::G_REVERSESUBSTRACT, YSE::OUT_TYPE::FLOAT},
        {YSE::OBJ::G_REVERSEDIVIDE, YSE::OUT_TYPE::FLOAT},
        {YSE::OBJ::G_MODULO, YSE::OUT_TYPE::INT},
        {YSE::OBJ::G_INTDIVIDE, YSE::OUT_TYPE::INT},
    };

    YSE::patcher p;
    p.create(2);
    for (const Expected& e : types) {
      CAPTURE(e.type);
      YSE::pHandle* h = p.CreateObject(e.type);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == std::string(e.type));
      CHECK(h->GetInputs() == 2);
      CHECK(h->GetOutputs() == 1);
      CHECK(h->OutputDataType(0) == e.outType);
    }
  }

  TEST_CASE("arithmetic ops: all four operators are listed by pRegistry::AllNames (#439)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    for (const char* type : {".!-", ".!/", ".%", ".div"}) {
      CAPTURE(type);
      CHECK(std::find(names.begin(), names.end(), std::string(type)) != names.end());
    }
  }

  // ─── behaviour: reverse operands ────────────────────────────────────────────

  TEST_CASE("arithmetic ops: .!- subtracts left from right (#439)") {
    CHECK(EvaluateFloat<YSE::PATCHER::gReverseSubstract>(3.f, 10.f) == doctest::Approx(7.f));
    CHECK(EvaluateFloat<YSE::PATCHER::gReverseSubstract>(10.f, 3.f) == doctest::Approx(-7.f));
    CHECK(EvaluateFloat<YSE::PATCHER::gReverseSubstract>(0.f, 0.f) == doctest::Approx(0.f));
    CHECK(EvaluateFloat<YSE::PATCHER::gReverseSubstract>(-2.5f, 1.5f) == doctest::Approx(4.f));
  }

  TEST_CASE("arithmetic ops: .!/ divides right by left (#439)") {
    CHECK(EvaluateFloat<YSE::PATCHER::gReverseDivide>(2.f, 10.f) == doctest::Approx(5.f));
    CHECK(EvaluateFloat<YSE::PATCHER::gReverseDivide>(4.f, 1.f) == doctest::Approx(0.25f));
    CHECK(EvaluateFloat<YSE::PATCHER::gReverseDivide>(-2.f, 7.f) == doctest::Approx(-3.5f));
  }

  // ./ already emits 0 rather than an infinity; .!/ has to match, and here it
  // is the *left* inlet that carries the divisor.
  TEST_CASE("arithmetic ops: .!/ emits 0 when the left operand is zero (#439)") {
    CHECK(EvaluateFloat<YSE::PATCHER::gReverseDivide>(0.f, 10.f) == doctest::Approx(0.f));
    CHECK(EvaluateFloat<YSE::PATCHER::gReverseDivide>(0.f, 0.f) == doctest::Approx(0.f));
  }

  // ─── behaviour: integer division ────────────────────────────────────────────

  TEST_CASE("arithmetic ops: .% emits the remainder (#439)") {
    CHECK(EvaluateInt<YSE::PATCHER::gModulo>(7, 3) == 1);
    CHECK(EvaluateInt<YSE::PATCHER::gModulo>(9, 3) == 0);
    CHECK(EvaluateInt<YSE::PATCHER::gModulo>(2, 5) == 2);
    // The remainder takes the sign of the dividend, as in C and in Max.
    CHECK(EvaluateInt<YSE::PATCHER::gModulo>(-7, 3) == -1);
    CHECK(EvaluateInt<YSE::PATCHER::gModulo>(7, -3) == 1);
  }

  // The stated use case: a counter wrapped into 0..n-1.
  TEST_CASE("arithmetic ops: .% wraps a rising counter (#439)") {
    YSE::PATCHER::gModulo op;
    IntSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(1)->SetInt(4, YSE::T_GUI);
    const std::vector<int> expected = {0, 1, 2, 3, 0, 1, 2, 3, 0};
    for (int i = 0; i < (int)expected.size(); ++i) {
      CAPTURE(i);
      op.GetInlet(0)->SetInt(i, YSE::T_GUI);
      CHECK(sink.received == expected[i]);
    }
  }

  TEST_CASE("arithmetic ops: .div truncates towards zero (#439)") {
    CHECK(EvaluateInt<YSE::PATCHER::gIntDivide>(7, 2) == 3);
    CHECK(EvaluateInt<YSE::PATCHER::gIntDivide>(6, 3) == 2);
    CHECK(EvaluateInt<YSE::PATCHER::gIntDivide>(2, 5) == 0);
    // Towards zero, not towards negative infinity: -7 / 2 is -3, not -4.
    CHECK(EvaluateInt<YSE::PATCHER::gIntDivide>(-7, 2) == -3);
    CHECK(EvaluateInt<YSE::PATCHER::gIntDivide>(7, -2) == -3);
  }

  TEST_CASE("arithmetic ops: .% and .div emit 0 on a zero divisor (#439)") {
    CHECK(EvaluateInt<YSE::PATCHER::gModulo>(7, 0) == 0);
    CHECK(EvaluateInt<YSE::PATCHER::gModulo>(0, 0) == 0);
    CHECK(EvaluateInt<YSE::PATCHER::gIntDivide>(7, 0) == 0);
    CHECK(EvaluateInt<YSE::PATCHER::gIntDivide>(0, 0) == 0);
  }

  // INT_MIN / -1 is the one quotient that does not fit in an int; handed to the
  // raw C++ operator it is undefined behaviour and traps on x86. A patch can
  // reach it, so it must produce a value instead.
  TEST_CASE("arithmetic ops: INT_MIN divided by -1 does not trap (#439)") {
    CHECK(EvaluateInt<YSE::PATCHER::gIntDivide>(INT_MIN, -1) == INT_MIN);
    CHECK(EvaluateInt<YSE::PATCHER::gModulo>(INT_MIN, -1) == 0);
    CHECK(EvaluateInt<YSE::PATCHER::gModulo>(42, -1) == 0);
  }

  // ─── inlet semantics ────────────────────────────────────────────────────────

  TEST_CASE("arithmetic ops: inlet 1 stores silently, inlet 0 fires (#439)") {
    YSE::PATCHER::gIntDivide op;
    IntSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(1)->SetInt(4, YSE::T_GUI);
    CHECK_FALSE(sink.gotInt); // right inlet must not produce output

    op.GetInlet(0)->SetInt(9, YSE::T_GUI);
    CHECK(sink.gotInt);
    CHECK(sink.received == 2);

    // Re-firing the left inlet re-evaluates against the stored right operand.
    op.GetInlet(0)->SetInt(20, YSE::T_GUI);
    CHECK(sink.received == 5);
  }

  TEST_CASE("arithmetic ops: .!- stores silently on inlet 1 too (#439)") {
    YSE::PATCHER::gReverseSubstract op;
    FloatSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(1)->SetFloat(10.f, YSE::T_GUI);
    CHECK_FALSE(sink.gotFloat);

    op.GetInlet(0)->SetFloat(4.f, YSE::T_GUI);
    CHECK(sink.gotFloat);
    CHECK(sink.received == doctest::Approx(6.f));
  }

  TEST_CASE("arithmetic ops: .% and .div truncate float input towards zero (#439)") {
    YSE::PATCHER::gModulo op;
    IntSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(1)->SetFloat(3.9f, YSE::T_GUI); // -> 3
    op.GetInlet(0)->SetFloat(7.8f, YSE::T_GUI); // -> 7
    CHECK(sink.received == 1);

    op.GetInlet(0)->SetFloat(-7.8f, YSE::T_GUI); // -> -7
    CHECK(sink.received == -1);
  }

  TEST_CASE("arithmetic ops: .!- and .!/ keep float precision on their inlets (#439)") {
    // Unlike .% / .div these are float objects, so 7.5 must stay 7.5.
    CHECK(EvaluateFloat<YSE::PATCHER::gReverseSubstract>(0.5f, 7.5f) == doctest::Approx(7.f));
    CHECK(EvaluateFloat<YSE::PATCHER::gReverseDivide>(0.5f, 7.5f) == doctest::Approx(15.f));
  }

  TEST_CASE("arithmetic ops: the right operand defaults to 0 when never set (#439)") {
    // Both families default the stored right operand to 0, which for the
    // integer pair means the divide-by-zero path.
    CHECK(EvaluateInt<YSE::PATCHER::gIntDivide>(255, 0) == 0);

    YSE::PATCHER::gReverseSubstract op;
    FloatSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);
    op.GetInlet(0)->SetFloat(3.f, YSE::T_GUI);
    CHECK(sink.received == doctest::Approx(-3.f)); // 0 - 3
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("arithmetic ops: the right operand can be set as a creation parameter (#439)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_MODULO, "12");
    REQUIRE(h != nullptr);
    CHECK(h->GetParams() == "12");
  }

  TEST_CASE("arithmetic ops: params survive a DumpJSON / ParseJSON round trip (#439)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_MODULO, "12") != nullptr);
    REQUIRE(src.CreateObject(YSE::OBJ::G_INTDIVIDE, "4") != nullptr);
    REQUIRE(src.CreateObject(YSE::OBJ::G_REVERSESUBSTRACT, "2.5") != nullptr);
    REQUIRE(src.CreateObject(YSE::OBJ::G_REVERSEDIVIDE, "8") != nullptr);
    const std::string json = src.DumpJSON();
    for (const char* type : {".%", ".div", ".!-", ".!/"}) {
      CAPTURE(type);
      CHECK(json.find(type) != std::string::npos);
    }

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 4);

    std::vector<std::string> restored;
    for (int i = 0; i < 4; ++i) {
      YSE::pHandle* h = loaded.GetHandleFromList(i);
      REQUIRE(h != nullptr);
      restored.push_back(std::string(h->Type()) + " " + h->GetParams());
    }
    for (const char* expected : {".% 12", ".div 4", ".!- 2.5", ".!/ 8"}) {
      CAPTURE(expected);
      CHECK(std::find(restored.begin(), restored.end(), std::string(expected)) != restored.end());
    }
  }

  // A restored .% must still compute; the round trip has to bring the divisor
  // back into the object, not just into its parameter string.
  TEST_CASE("arithmetic ops: a restored .% still wraps against its stored divisor (#439)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_MODULO, "5") != nullptr);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(src.DumpJSON());
    REQUIRE(loaded.Objects() == 1);
    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(h->GetParams() == "5");
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the outlet types specifically, which is
  // what a binding generator keys on.

  TEST_CASE("arithmetic ops: every operator documents itself as MATH (#439)") {
    const std::vector<std::pair<const char*, YSE::OUT_TYPE>> expected = {
        {".!-", YSE::OUT_TYPE::FLOAT},
        {".!/", YSE::OUT_TYPE::FLOAT},
        {".%", YSE::OUT_TYPE::INT},
        {".div", YSE::OUT_TYPE::INT},
    };
    for (const auto& e : expected) {
      CAPTURE(e.first);
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(e.first));
      REQUIRE(obj != nullptr);
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
      CHECK(obj->GetOutputType(0) == e.second);
      CHECK_FALSE(obj->GetDescription().empty());
      REQUIRE(obj->GetParamDocs().size() == 1);
      CHECK(obj->GetParamDocs()[0].name == "right");
    }
  }

} // TEST_SUITE("patcher")
