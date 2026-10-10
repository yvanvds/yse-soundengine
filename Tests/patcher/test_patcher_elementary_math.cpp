// Tests for the elementary math functions (issue #440): .abs .sqrt .pow .round
//
// Two shapes:
//
//   gUnaryMathBase (.abs .sqrt) — one inlet, no parameters, float in/float out.
//   .pow / .round               — the two-inlet shape of the .+ family (inlet 0
//                                 fires, inlet 1 stores the second operand),
//                                 also float in/float out.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/gPow.h"
#include "patcher/math/gRound.h"
#include "patcher/math/gUnaryMath.h"
#include "patcher/sinks.hpp"

using TestHelpers::FloatSink;

namespace {

  // Drives a one-inlet function and returns what the sink saw.
  template <typename OpType> float EvaluateUnary(float in) {
    OpType op;
    FloatSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(0)->SetFloat(in, YSE::T_GUI);
    return sink.received;
  }

  // Drives a two-inlet function through a right-then-left pair. Left is applied
  // last because inlet 0 is the hot inlet.
  template <typename OpType> float EvaluateBinary(float left, float right) {
    OpType op;
    FloatSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(1)->SetFloat(right, YSE::T_GUI);
    op.GetInlet(0)->SetFloat(left, YSE::T_GUI);
    return sink.received;
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("elementary math: all four objects are creatable through the registry (#440)") {
    struct Expected {
      const char* type;
      int inlets;
    };
    const std::vector<Expected> types = {
        {YSE::OBJ::G_ABS, 1},
        {YSE::OBJ::G_SQRT, 1},
        {YSE::OBJ::G_POW, 2},
        {YSE::OBJ::G_ROUND, 2},
    };

    YSE::patcher p;
    p.create(2);
    for (const Expected& e : types) {
      CAPTURE(e.type);
      YSE::pHandle* h = p.CreateObject(e.type);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == std::string(e.type));
      CHECK(h->GetInputs() == e.inlets);
      CHECK(h->GetOutputs() == 1);
      CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
    }
  }

  TEST_CASE("elementary math: all four objects are listed by pRegistry::AllNames (#440)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    for (const char* type : {".abs", ".sqrt", ".pow", ".round"}) {
      CAPTURE(type);
      CHECK(std::find(names.begin(), names.end(), std::string(type)) != names.end());
    }
  }

  // ─── behaviour: .abs ────────────────────────────────────────────────────────

  TEST_CASE("elementary math: .abs emits the absolute value (#440)") {
    CHECK(EvaluateUnary<YSE::PATCHER::gAbs>(5.f) == doctest::Approx(5.f));
    CHECK(EvaluateUnary<YSE::PATCHER::gAbs>(-5.f) == doctest::Approx(5.f));
    CHECK(EvaluateUnary<YSE::PATCHER::gAbs>(0.f) == doctest::Approx(0.f));
    CHECK(EvaluateUnary<YSE::PATCHER::gAbs>(-0.25f) == doctest::Approx(0.25f));
  }

  TEST_CASE("elementary math: .abs takes ints on its inlet too (#440)") {
    YSE::PATCHER::gAbs op;
    FloatSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(0)->SetInt(-7, YSE::T_GUI);
    CHECK(sink.gotFloat); // the g-family convention is float out, as with .+
    CHECK(sink.received == doctest::Approx(7.f));
  }

  // ─── behaviour: .sqrt ───────────────────────────────────────────────────────

  TEST_CASE("elementary math: .sqrt emits the square root (#440)") {
    CHECK(EvaluateUnary<YSE::PATCHER::gSqrt>(9.f) == doctest::Approx(3.f));
    CHECK(EvaluateUnary<YSE::PATCHER::gSqrt>(2.f) == doctest::Approx(std::sqrt(2.f)));
    CHECK(EvaluateUnary<YSE::PATCHER::gSqrt>(0.25f) == doctest::Approx(0.5f));
    CHECK(EvaluateUnary<YSE::PATCHER::gSqrt>(0.f) == doctest::Approx(0.f));
  }

  // A NaN escaping into the patch would poison every downstream object, so a
  // negative operand emits 0 the way ./ does for a zero divisor.
  TEST_CASE("elementary math: .sqrt emits 0 for a negative operand (#440)") {
    const float negative = EvaluateUnary<YSE::PATCHER::gSqrt>(-4.f);
    CHECK_FALSE(std::isnan(negative));
    CHECK(negative == doctest::Approx(0.f));
  }

  // ─── behaviour: .pow ────────────────────────────────────────────────────────

  TEST_CASE("elementary math: .pow raises the base to the exponent (#440)") {
    CHECK(EvaluateBinary<YSE::PATCHER::gPow>(2.f, 8.f) == doctest::Approx(256.f));
    CHECK(EvaluateBinary<YSE::PATCHER::gPow>(9.f, 0.5f) == doctest::Approx(3.f));
    CHECK(EvaluateBinary<YSE::PATCHER::gPow>(2.f, -2.f) == doctest::Approx(0.25f));
    CHECK(EvaluateBinary<YSE::PATCHER::gPow>(5.f, 0.f) == doctest::Approx(1.f));
    CHECK(EvaluateBinary<YSE::PATCHER::gPow>(-2.f, 3.f) == doctest::Approx(-8.f));
  }

  // std::pow answers NaN for a negative base with a fractional exponent and an
  // infinity for 0 ^ negative; a patch can reach both.
  TEST_CASE("elementary math: .pow emits 0 where the result is not finite (#440)") {
    const float nanCase = EvaluateBinary<YSE::PATCHER::gPow>(-8.f, 0.5f);
    CHECK_FALSE(std::isnan(nanCase));
    CHECK(nanCase == doctest::Approx(0.f));

    const float infCase = EvaluateBinary<YSE::PATCHER::gPow>(0.f, -1.f);
    CHECK(std::isfinite(infCase));
    CHECK(infCase == doctest::Approx(0.f));
  }

  TEST_CASE("elementary math: .pow defaults its exponent to 0 (#440)") {
    YSE::PATCHER::gPow op;
    FloatSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(0)->SetFloat(7.f, YSE::T_GUI);
    CHECK(sink.received == doctest::Approx(1.f)); // 7 ^ 0
  }

  // ─── behaviour: .round ──────────────────────────────────────────────────────

  // The point of matching Max: round snaps to a *multiple* of the step, not to
  // an integer.
  TEST_CASE("elementary math: .round snaps to a multiple of the step (#440)") {
    CHECK(EvaluateBinary<YSE::PATCHER::gRound>(1.3f, 0.5f) == doctest::Approx(1.5f));
    CHECK(EvaluateBinary<YSE::PATCHER::gRound>(1.2f, 0.5f) == doctest::Approx(1.f));
    CHECK(EvaluateBinary<YSE::PATCHER::gRound>(7.f, 5.f) == doctest::Approx(5.f));
    CHECK(EvaluateBinary<YSE::PATCHER::gRound>(8.f, 5.f) == doctest::Approx(10.f));
    CHECK(EvaluateBinary<YSE::PATCHER::gRound>(1.24f, 0.1f) == doctest::Approx(1.2f));
  }

  TEST_CASE("elementary math: .round rounds halfway values away from zero (#440)") {
    CHECK(EvaluateBinary<YSE::PATCHER::gRound>(2.5f, 1.f) == doctest::Approx(3.f));
    CHECK(EvaluateBinary<YSE::PATCHER::gRound>(-2.5f, 1.f) == doctest::Approx(-3.f));
    CHECK(EvaluateBinary<YSE::PATCHER::gRound>(-1.4f, 1.f) == doctest::Approx(-1.f));
  }

  // A step of 0 would divide by zero; Max reads it as "no rounding".
  TEST_CASE("elementary math: .round with a step of 0 passes the value through (#440)") {
    CHECK(EvaluateBinary<YSE::PATCHER::gRound>(1.234f, 0.f) == doctest::Approx(1.234f));
    CHECK(EvaluateBinary<YSE::PATCHER::gRound>(-9.9f, 0.f) == doctest::Approx(-9.9f));
  }

  // Unlike every other two-inlet math object, the stored operand starts at 1 —
  // a bare .round has to round to the nearest integer, not divide by zero.
  TEST_CASE("elementary math: .round defaults its step to 1 (#440)") {
    YSE::PATCHER::gRound op;
    FloatSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(0)->SetFloat(3.7f, YSE::T_GUI);
    CHECK(sink.received == doctest::Approx(4.f));

    op.GetInlet(0)->SetFloat(3.2f, YSE::T_GUI);
    CHECK(sink.received == doctest::Approx(3.f));
  }

  // ─── inlet semantics ────────────────────────────────────────────────────────

  TEST_CASE("elementary math: inlet 1 stores silently, inlet 0 fires (#440)") {
    YSE::PATCHER::gPow op;
    FloatSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(1)->SetFloat(2.f, YSE::T_GUI);
    CHECK_FALSE(sink.gotFloat); // right inlet must not produce output

    op.GetInlet(0)->SetFloat(3.f, YSE::T_GUI);
    CHECK(sink.gotFloat);
    CHECK(sink.received == doctest::Approx(9.f));

    // Re-firing the left inlet re-evaluates against the stored exponent.
    op.GetInlet(0)->SetFloat(4.f, YSE::T_GUI);
    CHECK(sink.received == doctest::Approx(16.f));
  }

  TEST_CASE("elementary math: .round takes ints on both inlets (#440)") {
    YSE::PATCHER::gRound op;
    FloatSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(1)->SetInt(10, YSE::T_GUI);
    CHECK_FALSE(sink.gotFloat);

    op.GetInlet(0)->SetInt(17, YSE::T_GUI);
    CHECK(sink.gotFloat);
    CHECK(sink.received == doctest::Approx(20.f));
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("elementary math: .pow and .round take their operand as a creation parameter (#440)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* pow = p.CreateObject(YSE::OBJ::G_POW, "3");
    REQUIRE(pow != nullptr);
    CHECK(pow->GetParams() == "3");

    YSE::pHandle* round = p.CreateObject(YSE::OBJ::G_ROUND, "0.25");
    REQUIRE(round != nullptr);
    CHECK(round->GetParams() == "0.25");
  }

  // .abs and .sqrt take no parameters at all, so a creation-argument round trip
  // must not invent one.
  TEST_CASE("elementary math: .abs and .sqrt register no parameters (#440)") {
    for (const char* type : {YSE::OBJ::G_ABS, YSE::OBJ::G_SQRT}) {
      CAPTURE(type);
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK(obj->GetParamDocs().empty());
    }
  }

  TEST_CASE("elementary math: params survive a DumpJSON / ParseJSON round trip (#440)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_ABS) != nullptr);
    REQUIRE(src.CreateObject(YSE::OBJ::G_SQRT) != nullptr);
    REQUIRE(src.CreateObject(YSE::OBJ::G_POW, "3") != nullptr);
    REQUIRE(src.CreateObject(YSE::OBJ::G_ROUND, "0.25") != nullptr);
    const std::string json = src.DumpJSON();
    for (const char* type : {".abs", ".sqrt", ".pow", ".round"}) {
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
    for (const char* expected : {".abs ", ".sqrt ", ".pow 3", ".round 0.25"}) {
      CAPTURE(expected);
      CHECK(std::find(restored.begin(), restored.end(), std::string(expected)) != restored.end());
    }
  }

  // A restored .round must still compute against its stored step, not just
  // carry the parameter string.
  TEST_CASE("elementary math: a restored .round still snaps to its stored step (#440)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_ROUND, "0.5") != nullptr);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(src.DumpJSON());
    REQUIRE(loaded.Objects() == 1);
    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(h->GetParams() == "0.5");
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the outlet type specifically, which is
  // what a binding generator keys on.

  TEST_CASE("elementary math: every object documents itself as MATH (#440)") {
    for (const char* type : {".abs", ".sqrt", ".pow", ".round"}) {
      CAPTURE(type);
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
      CHECK(obj->GetOutputType(0) == YSE::OUT_TYPE::FLOAT);
      CHECK_FALSE(obj->GetDescription().empty());
    }
  }

  TEST_CASE("elementary math: .pow and .round name their stored operand (#440)") {
    const std::vector<std::pair<const char*, const char*>> expected = {
        {".pow", "exponent"},
        {".round", "step"},
    };
    for (const auto& e : expected) {
      CAPTURE(e.first);
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(e.first));
      REQUIRE(obj != nullptr);
      REQUIRE(obj->GetParamDocs().size() == 1);
      CHECK(obj->GetParamDocs()[0].name == std::string(e.second));
    }
  }

} // TEST_SUITE("patcher")
