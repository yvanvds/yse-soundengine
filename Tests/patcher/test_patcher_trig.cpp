// Tests for the trigonometric + hyperbolic function family (issue #441):
// .sin .cos .tan .asin .acos .atan .atan2 .sinh .cosh .tanh .asinh .acosh .atanh
//
// Two shapes:
//
//   gUnaryMathBase (the twelve unary ones) — one inlet, no parameters,
//                                            float in / float out.
//   .atan2                                 — the two-inlet shape of the .+
//                                            family (inlet 0 = y fires,
//                                            inlet 1 = x stores).
//
// Angles are radians throughout, matching Max. Every object emits 0 rather than
// letting a NaN or an infinity escape into the patch.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/gAtan2.h"
#include "patcher/math/gTrig.h"
#include "patcher/sinks.hpp"

using TestHelpers::FloatSink;

namespace {

  constexpr float kPi = 3.14159265358979323846f;

  // Every object in the family, so the registry / documentation sweeps below
  // cannot silently miss one.
  const std::vector<const char*> kAllNames = {".sin",   ".cos",   ".tan",  ".asin", ".acos",
                                              ".atan",  ".atan2", ".sinh", ".cosh", ".tanh",
                                              ".asinh", ".acosh", ".atanh"};

  // Drives a one-inlet function and returns what the sink saw.
  template <typename OpType> float EvaluateUnary(float in) {
    OpType op;
    FloatSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(0)->SetFloat(in, YSE::T_GUI);
    return sink.received;
  }

  // Drives .atan2 through a right-then-left pair. Left is applied last because
  // inlet 0 is the hot inlet.
  float EvaluateAtan2(float y, float x) {
    YSE::PATCHER::gAtan2 op;
    FloatSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(1)->SetFloat(x, YSE::T_GUI);
    op.GetInlet(0)->SetFloat(y, YSE::T_GUI);
    return sink.received;
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("trig: all thirteen objects are creatable through the registry (#441)") {
    YSE::patcher p;
    p.create(2);
    for (const char* type : kAllNames) {
      CAPTURE(type);
      YSE::pHandle* h = p.CreateObject(type);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == std::string(type));
      // .atan2 is the only two-inlet member of the family.
      CHECK(h->GetInputs() == (std::string(type) == ".atan2" ? 2 : 1));
      CHECK(h->GetOutputs() == 1);
      CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
    }
  }

  TEST_CASE("trig: all thirteen objects are listed by pRegistry::AllNames (#441)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    for (const char* type : kAllNames) {
      CAPTURE(type);
      CHECK(std::find(names.begin(), names.end(), std::string(type)) != names.end());
    }
  }

  // ─── behaviour: circular functions ──────────────────────────────────────────

  TEST_CASE("trig: .sin .cos .tan take radians (#441)") {
    CHECK(EvaluateUnary<YSE::PATCHER::gSin>(0.f) == doctest::Approx(0.f));
    CHECK(EvaluateUnary<YSE::PATCHER::gSin>(kPi / 2.f) == doctest::Approx(1.f));
    CHECK(EvaluateUnary<YSE::PATCHER::gSin>(-kPi / 2.f) == doctest::Approx(-1.f));
    // 90 would be degrees; in radians it is nowhere near 1.
    CHECK(EvaluateUnary<YSE::PATCHER::gSin>(90.f) == doctest::Approx(std::sin(90.f)));

    CHECK(EvaluateUnary<YSE::PATCHER::gCos>(0.f) == doctest::Approx(1.f));
    CHECK(EvaluateUnary<YSE::PATCHER::gCos>(kPi) == doctest::Approx(-1.f));

    CHECK(EvaluateUnary<YSE::PATCHER::gTan>(0.f) == doctest::Approx(0.f));
    CHECK(EvaluateUnary<YSE::PATCHER::gTan>(kPi / 4.f) == doctest::Approx(1.f));
  }

  TEST_CASE("trig: .asin .acos .atan invert their circular partners (#441)") {
    CHECK(EvaluateUnary<YSE::PATCHER::gAsin>(0.f) == doctest::Approx(0.f));
    CHECK(EvaluateUnary<YSE::PATCHER::gAsin>(1.f) == doctest::Approx(kPi / 2.f));
    CHECK(EvaluateUnary<YSE::PATCHER::gAsin>(-1.f) == doctest::Approx(-kPi / 2.f));

    CHECK(EvaluateUnary<YSE::PATCHER::gAcos>(1.f) == doctest::Approx(0.f));
    CHECK(EvaluateUnary<YSE::PATCHER::gAcos>(0.f) == doctest::Approx(kPi / 2.f));
    CHECK(EvaluateUnary<YSE::PATCHER::gAcos>(-1.f) == doctest::Approx(kPi));

    CHECK(EvaluateUnary<YSE::PATCHER::gAtan>(0.f) == doctest::Approx(0.f));
    CHECK(EvaluateUnary<YSE::PATCHER::gAtan>(1.f) == doctest::Approx(kPi / 4.f));
    CHECK(EvaluateUnary<YSE::PATCHER::gAtan>(-1.f) == doctest::Approx(-kPi / 4.f));
  }

  // ─── behaviour: hyperbolic functions ────────────────────────────────────────

  TEST_CASE("trig: .sinh .cosh .tanh match <cmath> (#441)") {
    CHECK(EvaluateUnary<YSE::PATCHER::gSinh>(0.f) == doctest::Approx(0.f));
    CHECK(EvaluateUnary<YSE::PATCHER::gSinh>(1.f) == doctest::Approx(std::sinh(1.f)));
    CHECK(EvaluateUnary<YSE::PATCHER::gSinh>(-2.f) == doctest::Approx(std::sinh(-2.f)));

    CHECK(EvaluateUnary<YSE::PATCHER::gCosh>(0.f) == doctest::Approx(1.f));
    CHECK(EvaluateUnary<YSE::PATCHER::gCosh>(1.f) == doctest::Approx(std::cosh(1.f)));

    // The waveshaping curve: bounded, odd, and steep near 0.
    CHECK(EvaluateUnary<YSE::PATCHER::gTanh>(0.f) == doctest::Approx(0.f));
    CHECK(EvaluateUnary<YSE::PATCHER::gTanh>(1.f) == doctest::Approx(std::tanh(1.f)));
    CHECK(EvaluateUnary<YSE::PATCHER::gTanh>(50.f) == doctest::Approx(1.f));
    CHECK(EvaluateUnary<YSE::PATCHER::gTanh>(-50.f) == doctest::Approx(-1.f));
  }

  TEST_CASE("trig: .asinh .acosh .atanh invert their hyperbolic partners (#441)") {
    CHECK(EvaluateUnary<YSE::PATCHER::gAsinh>(0.f) == doctest::Approx(0.f));
    CHECK(EvaluateUnary<YSE::PATCHER::gAsinh>(std::sinh(1.5f)) == doctest::Approx(1.5f));

    CHECK(EvaluateUnary<YSE::PATCHER::gAcosh>(1.f) == doctest::Approx(0.f));
    CHECK(EvaluateUnary<YSE::PATCHER::gAcosh>(std::cosh(1.5f)) == doctest::Approx(1.5f));

    CHECK(EvaluateUnary<YSE::PATCHER::gAtanh>(0.f) == doctest::Approx(0.f));
    CHECK(EvaluateUnary<YSE::PATCHER::gAtanh>(std::tanh(1.5f)) == doctest::Approx(1.5f));
  }

  // ─── domain edges: nothing non-finite escapes ───────────────────────────────
  // A NaN leaving one of these would poison every object downstream, so the
  // family follows the convention ./ and .sqrt set: emit 0 instead.

  TEST_CASE("trig: .asin and .acos emit 0 outside -1 to 1 (#441)") {
    for (float out : {1.5f, -1.5f, 100.f}) {
      CAPTURE(out);
      const float asinResult = EvaluateUnary<YSE::PATCHER::gAsin>(out);
      CHECK_FALSE(std::isnan(asinResult));
      CHECK(asinResult == doctest::Approx(0.f));

      const float acosResult = EvaluateUnary<YSE::PATCHER::gAcos>(out);
      CHECK_FALSE(std::isnan(acosResult));
      CHECK(acosResult == doctest::Approx(0.f));
    }
  }

  TEST_CASE("trig: .acosh emits 0 below 1 (#441)") {
    for (float below : {0.5f, 0.f, -3.f}) {
      CAPTURE(below);
      const float result = EvaluateUnary<YSE::PATCHER::gAcosh>(below);
      CHECK_FALSE(std::isnan(result));
      CHECK(result == doctest::Approx(0.f));
    }
  }

  // atanh is unbounded at ±1 and undefined beyond, so both edges are covered.
  TEST_CASE("trig: .atanh emits 0 at and outside -1 to 1 (#441)") {
    for (float edge : {1.f, -1.f, 2.f, -2.f}) {
      CAPTURE(edge);
      const float result = EvaluateUnary<YSE::PATCHER::gAtanh>(edge);
      CHECK(std::isfinite(result));
      CHECK(result == doctest::Approx(0.f));
    }
  }

  TEST_CASE("trig: .sinh and .cosh emit 0 rather than overflowing to infinity (#441)") {
    const float sinhResult = EvaluateUnary<YSE::PATCHER::gSinh>(500.f);
    CHECK(std::isfinite(sinhResult));
    CHECK(sinhResult == doctest::Approx(0.f));

    const float coshResult = EvaluateUnary<YSE::PATCHER::gCosh>(500.f);
    CHECK(std::isfinite(coshResult));
    CHECK(coshResult == doctest::Approx(0.f));
  }

  // sin/cos/tan are finite for every finite input, but a patch can feed them an
  // infinity from a neighbouring object; <cmath> answers NaN for that.
  TEST_CASE("trig: a non-finite input still produces a finite output (#441)") {
    const float infinity = std::numeric_limits<float>::infinity();
    CHECK(std::isfinite(EvaluateUnary<YSE::PATCHER::gSin>(infinity)));
    CHECK(std::isfinite(EvaluateUnary<YSE::PATCHER::gCos>(infinity)));
    CHECK(std::isfinite(EvaluateUnary<YSE::PATCHER::gTan>(infinity)));
    CHECK(std::isfinite(EvaluateAtan2(infinity, std::numeric_limits<float>::quiet_NaN())));
  }

  // ─── behaviour: .atan2 ──────────────────────────────────────────────────────

  TEST_CASE("trig: .atan2 resolves the quadrant from both operands (#441)") {
    CHECK(EvaluateAtan2(0.f, 1.f) == doctest::Approx(0.f));
    CHECK(EvaluateAtan2(1.f, 1.f) == doctest::Approx(kPi / 4.f));
    CHECK(EvaluateAtan2(1.f, 0.f) == doctest::Approx(kPi / 2.f));
    CHECK(EvaluateAtan2(0.f, -1.f) == doctest::Approx(kPi));
    CHECK(EvaluateAtan2(-1.f, -1.f) == doctest::Approx(-3.f * kPi / 4.f));
    CHECK(EvaluateAtan2(-1.f, 0.f) == doctest::Approx(-kPi / 2.f));
  }

  // The whole reason .atan2 exists next to .atan: y/x is 1 in both the first
  // and the third quadrant, and only .atan2 tells them apart.
  TEST_CASE("trig: .atan2 separates quadrants .atan cannot (#441)") {
    const float first = EvaluateAtan2(1.f, 1.f);
    const float third = EvaluateAtan2(-1.f, -1.f);
    CHECK(first != doctest::Approx(third));
    CHECK(EvaluateUnary<YSE::PATCHER::gAtan>(1.f) == doctest::Approx(first));
  }

  TEST_CASE("trig: .atan2 inlet 1 stores silently, inlet 0 fires (#441)") {
    YSE::PATCHER::gAtan2 op;
    FloatSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(1)->SetFloat(1.f, YSE::T_GUI);
    CHECK_FALSE(sink.gotFloat); // right inlet must not produce output

    op.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    CHECK(sink.gotFloat);
    CHECK(sink.received == doctest::Approx(kPi / 4.f));

    // Re-firing the left inlet re-evaluates against the stored x.
    op.GetInlet(0)->SetFloat(-1.f, YSE::T_GUI);
    CHECK(sink.received == doctest::Approx(-kPi / 4.f));
  }

  TEST_CASE("trig: .atan2 defaults its x to 0 (#441)") {
    YSE::PATCHER::gAtan2 op;
    FloatSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    CHECK(sink.received == doctest::Approx(kPi / 2.f)); // atan2(1, 0)
  }

  // ─── int inlets ─────────────────────────────────────────────────────────────

  TEST_CASE("trig: the family takes ints on its inlets and still emits floats (#441)") {
    YSE::PATCHER::gCos unary;
    FloatSink unarySink;
    unary.ConnectOutlet(unarySink.GetInlet(0), 0);
    unarySink.ConnectInlet(unary.GetOutlet(0), 0);

    unary.GetInlet(0)->SetInt(0, YSE::T_GUI);
    CHECK(unarySink.gotFloat); // the g-family convention is float out, as with .+
    CHECK(unarySink.received == doctest::Approx(1.f));

    YSE::PATCHER::gAtan2 binary;
    FloatSink binarySink;
    binary.ConnectOutlet(binarySink.GetInlet(0), 0);
    binarySink.ConnectInlet(binary.GetOutlet(0), 0);

    binary.GetInlet(1)->SetInt(1, YSE::T_GUI);
    CHECK_FALSE(binarySink.gotFloat);
    binary.GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(binarySink.gotFloat);
    CHECK(binarySink.received == doctest::Approx(kPi / 4.f));
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("trig: the twelve unary objects register no parameters (#441)") {
    for (const char* type : kAllNames) {
      if (std::string(type) == ".atan2") continue;
      CAPTURE(type);
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK(obj->GetParamDocs().empty());
    }
  }

  TEST_CASE("trig: .atan2 takes its x as a creation parameter (#441)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ATAN2, "2");
    REQUIRE(h != nullptr);
    CHECK(h->GetParams() == "2");
  }

  TEST_CASE("trig: params survive a DumpJSON / ParseJSON round trip (#441)") {
    YSE::patcher src;
    src.create(2);
    for (const char* type : kAllNames) {
      CAPTURE(type);
      REQUIRE(src.CreateObject(type, std::string(type) == ".atan2" ? "2" : "") != nullptr);
    }
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == (int)kAllNames.size());

    std::vector<std::string> restored;
    for (int i = 0; i < (int)kAllNames.size(); ++i) {
      YSE::pHandle* h = loaded.GetHandleFromList(i);
      REQUIRE(h != nullptr);
      restored.push_back(std::string(h->Type()) + " " + h->GetParams());
    }
    for (const char* type : kAllNames) {
      CAPTURE(type);
      const std::string expected =
          std::string(type) + " " + (std::string(type) == ".atan2" ? "2" : "");
      CHECK(std::find(restored.begin(), restored.end(), expected) != restored.end());
    }
  }

  // A restored .atan2 must still compute against its stored x, not just carry
  // the parameter string.
  TEST_CASE("trig: a restored .atan2 still evaluates against its stored x (#441)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_ATAN2, "-1") != nullptr);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(src.DumpJSON());
    REQUIRE(loaded.Objects() == 1);
    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(h->GetParams() == "-1");
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the outlet type specifically, which is
  // what a binding generator keys on.

  TEST_CASE("trig: every object documents itself as MATH (#441)") {
    for (const char* type : kAllNames) {
      CAPTURE(type);
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
      CHECK(obj->GetOutputType(0) == YSE::OUT_TYPE::FLOAT);
      CHECK_FALSE(obj->GetDescription().empty());
    }
  }

  TEST_CASE("trig: .atan2 names its stored operand x (#441)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(".atan2"));
    REQUIRE(obj != nullptr);
    REQUIRE(obj->GetParamDocs().size() == 1);
    CHECK(obj->GetParamDocs()[0].name == std::string("x"));
  }

} // TEST_SUITE("patcher")
