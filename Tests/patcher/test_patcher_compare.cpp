// Tests for the patcher comparison + logic operator family (issue #437):
// .== .!= .< .<= .> .>= .&& .||
//
// All eight share gCompareBase: inlet 0 is the left operand and fires the
// evaluation, inlet 1 stores the right operand, and the result leaves outlet 0
// as an int (1 = true, 0 = false).
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/gCompare.h"
#include "patcher/sinks.hpp"

using TestHelpers::IntSink;

namespace {

  // Drives one operator through a right-then-left pair and returns what the
  // sink saw. Left is applied last because inlet 0 is the hot inlet.
  template <typename OpType> int Evaluate(float left, float right) {
    OpType op;
    IntSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(1)->SetFloat(right, YSE::T_GUI);
    op.GetInlet(0)->SetFloat(left, YSE::T_GUI);
    return sink.received;
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("compare: all eight operators are creatable through the registry (#437)") {
    const std::vector<const char*> types = {
        YSE::OBJ::G_EQUAL,      YSE::OBJ::G_NOTEQUAL,  YSE::OBJ::G_LESS,
        YSE::OBJ::G_LESSEQUAL,  YSE::OBJ::G_GREATER,   YSE::OBJ::G_GREATEREQUAL,
        YSE::OBJ::G_LOGICALAND, YSE::OBJ::G_LOGICALOR,
    };

    YSE::patcher p;
    p.create(2);
    for (const char* type : types) {
      CAPTURE(type);
      YSE::pHandle* h = p.CreateObject(type);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == std::string(type));
      CHECK(h->GetInputs() == 2);
      CHECK(h->GetOutputs() == 1);
      CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::INT);
    }
  }

  TEST_CASE("compare: all eight operators are listed by pRegistry::AllNames (#437)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    for (const char* type : {".==", ".!=", ".<", ".<=", ".>", ".>=", ".&&", ".||"}) {
      CAPTURE(type);
      CHECK(std::find(names.begin(), names.end(), std::string(type)) != names.end());
    }
  }

  // ─── behaviour ──────────────────────────────────────────────────────────────

  TEST_CASE("compare: .== emits 1 only on equality") {
    CHECK(Evaluate<YSE::PATCHER::gEqual>(3.f, 3.f) == 1);
    CHECK(Evaluate<YSE::PATCHER::gEqual>(3.f, 4.f) == 0);
    CHECK(Evaluate<YSE::PATCHER::gEqual>(-2.5f, -2.5f) == 1);
  }

  TEST_CASE("compare: .!= emits 1 only on inequality") {
    CHECK(Evaluate<YSE::PATCHER::gNotEqual>(3.f, 3.f) == 0);
    CHECK(Evaluate<YSE::PATCHER::gNotEqual>(3.f, 4.f) == 1);
  }

  TEST_CASE("compare: .< is strict") {
    CHECK(Evaluate<YSE::PATCHER::gLess>(2.f, 3.f) == 1);
    CHECK(Evaluate<YSE::PATCHER::gLess>(3.f, 3.f) == 0);
    CHECK(Evaluate<YSE::PATCHER::gLess>(4.f, 3.f) == 0);
  }

  TEST_CASE("compare: .<= includes equality") {
    CHECK(Evaluate<YSE::PATCHER::gLessEqual>(2.f, 3.f) == 1);
    CHECK(Evaluate<YSE::PATCHER::gLessEqual>(3.f, 3.f) == 1);
    CHECK(Evaluate<YSE::PATCHER::gLessEqual>(4.f, 3.f) == 0);
  }

  TEST_CASE("compare: .> is strict") {
    CHECK(Evaluate<YSE::PATCHER::gGreater>(4.f, 3.f) == 1);
    CHECK(Evaluate<YSE::PATCHER::gGreater>(3.f, 3.f) == 0);
    CHECK(Evaluate<YSE::PATCHER::gGreater>(2.f, 3.f) == 0);
  }

  TEST_CASE("compare: .>= includes equality") {
    CHECK(Evaluate<YSE::PATCHER::gGreaterEqual>(4.f, 3.f) == 1);
    CHECK(Evaluate<YSE::PATCHER::gGreaterEqual>(3.f, 3.f) == 1);
    CHECK(Evaluate<YSE::PATCHER::gGreaterEqual>(2.f, 3.f) == 0);
  }

  TEST_CASE("compare: .&& is true only when both operands are non-zero") {
    CHECK(Evaluate<YSE::PATCHER::gLogicalAnd>(1.f, 1.f) == 1);
    CHECK(Evaluate<YSE::PATCHER::gLogicalAnd>(-7.f, 0.5f) == 1);
    CHECK(Evaluate<YSE::PATCHER::gLogicalAnd>(1.f, 0.f) == 0);
    CHECK(Evaluate<YSE::PATCHER::gLogicalAnd>(0.f, 1.f) == 0);
    CHECK(Evaluate<YSE::PATCHER::gLogicalAnd>(0.f, 0.f) == 0);
  }

  TEST_CASE("compare: .|| is true when either operand is non-zero") {
    CHECK(Evaluate<YSE::PATCHER::gLogicalOr>(1.f, 1.f) == 1);
    CHECK(Evaluate<YSE::PATCHER::gLogicalOr>(1.f, 0.f) == 1);
    CHECK(Evaluate<YSE::PATCHER::gLogicalOr>(0.f, -3.f) == 1);
    CHECK(Evaluate<YSE::PATCHER::gLogicalOr>(0.f, 0.f) == 0);
  }

  // ─── inlet semantics ────────────────────────────────────────────────────────

  TEST_CASE("compare: inlet 1 stores silently, inlet 0 fires") {
    YSE::PATCHER::gGreater op;
    IntSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(1)->SetFloat(10.f, YSE::T_GUI);
    CHECK_FALSE(sink.gotInt); // right inlet must not produce output

    op.GetInlet(0)->SetFloat(20.f, YSE::T_GUI);
    CHECK(sink.gotInt);
    CHECK(sink.received == 1);

    // Re-firing the left inlet re-evaluates against the stored right operand.
    op.GetInlet(0)->SetFloat(5.f, YSE::T_GUI);
    CHECK(sink.received == 0);
  }

  TEST_CASE("compare: int inlets are accepted on both sides") {
    YSE::PATCHER::gLessEqual op;
    IntSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(1)->SetInt(7, YSE::T_GUI);
    op.GetInlet(0)->SetInt(7, YSE::T_GUI);
    CHECK(sink.received == 1);

    op.GetInlet(0)->SetInt(8, YSE::T_GUI);
    CHECK(sink.received == 0);
  }

  TEST_CASE("compare: the right operand defaults to 0 when never set") {
    YSE::PATCHER::gEqual op;
    IntSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
    CHECK(sink.received == 1);
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("compare: the right operand can be set as a creation parameter (#437)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_GREATER, "10");
    REQUIRE(h != nullptr);
    CHECK(h->GetParams() == "10");
  }

  TEST_CASE("compare: params survive a DumpJSON / ParseJSON round trip (#437)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_LOGICALAND, "1") != nullptr);
    REQUIRE(src.CreateObject(YSE::OBJ::G_LESSEQUAL, "0.5") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".&&") != std::string::npos);
    CHECK(json.find(".<=") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 2);

    std::vector<std::string> restored;
    for (int i = 0; i < 2; ++i) {
      YSE::pHandle* h = loaded.GetHandleFromList(i);
      REQUIRE(h != nullptr);
      restored.push_back(std::string(h->Type()) + " " + h->GetParams());
    }
    CHECK(std::find(restored.begin(), restored.end(), std::string(".&& 1")) != restored.end());
    CHECK(std::find(restored.begin(), restored.end(), std::string(".<= 0.5")) != restored.end());
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the int outlet type specifically, which
  // is what a binding generator keys on.

  TEST_CASE("compare: every operator documents itself as MATH with an int outlet (#437)") {
    for (const char* type : {".==", ".!=", ".<", ".<=", ".>", ".>=", ".&&", ".||"}) {
      CAPTURE(type);
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
      CHECK(obj->GetOutputType(0) == YSE::OUT_TYPE::INT);
      CHECK_FALSE(obj->GetDescription().empty());
      REQUIRE(obj->GetParamDocs().size() == 1);
      CHECK(obj->GetParamDocs()[0].name == "right");
    }
  }

} // TEST_SUITE("patcher")
