// Tests for the unit conversion objects (issue #442):
//   .atodb .dbtoa .cartopol .poltocar
//
// Two shapes:
//
//   gUnaryMathBase (.atodb .dbtoa) — one inlet, one outlet, no parameters,
//                                    float in / float out.
//   gPolarBase (.cartopol .poltocar) — two inlets, two outlets: inlet 0 fires,
//                                    inlet 1 stores the second operand and is
//                                    the creation parameter.
//
// The decibel pair deliberately calls the engine's own YSE::DSP::rmsToDb /
// dbToRms, so it uses the engine's decibel reference (amplitude 1.0 == 100 dB,
// never negative) rather than Max's 0 dB reference. These tests pin that
// choice — if the reference ever changes, they should fail loudly.
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
#include "patcher/math/gDbConvert.h"
#include "patcher/math/gPolar.h"
#include "patcher/sinks.hpp"
#include "dsp/math_functions.h"

using TestHelpers::FloatSink;

namespace {

  // Drives a one-inlet conversion and returns what the sink saw.
  template <typename OpType> float EvaluateUnary(float in) {
    OpType op;
    FloatSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(0)->SetFloat(in, YSE::T_GUI);
    return sink.received;
  }

  // Holds both sinks of a two-outlet object alive for the duration of a check.
  template <typename OpType> struct PolarRig {
    OpType op;
    FloatSink left; // outlet 0
    FloatSink right; // outlet 1

    PolarRig() {
      op.ConnectOutlet(left.GetInlet(0), 0);
      left.ConnectInlet(op.GetOutlet(0), 0);
      op.ConnectOutlet(right.GetInlet(0), 1);
      right.ConnectInlet(op.GetOutlet(1), 0);
    }

    // Right (cold) inlet first, then the hot inlet, which fires.
    void Evaluate(float hot, float cold) {
      op.GetInlet(1)->SetFloat(cold, YSE::T_GUI);
      op.GetInlet(0)->SetFloat(hot, YSE::T_GUI);
    }
  };

  // Records the order in which outlets fired, so the right-to-left convention
  // can be asserted rather than assumed.
  struct OrderSink : YSE::PATCHER::pObject {
    std::vector<std::string>* log = nullptr;
    std::string label;
    OrderSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterFloat([this](float, int, YSE::THREAD) {
        if (log != nullptr) log->push_back(label);
      });
    }
    const char* Type() const override {
      return "order_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("unit conversions: all four objects are creatable through the registry (#442)") {
    struct Expected {
      const char* type;
      int inlets;
      int outlets;
    };
    const std::vector<Expected> types = {
        {YSE::OBJ::G_ATODB, 1, 1},
        {YSE::OBJ::G_DBTOA, 1, 1},
        {YSE::OBJ::G_CARTOPOL, 2, 2},
        {YSE::OBJ::G_POLTOCAR, 2, 2},
    };

    YSE::patcher p;
    p.create(2);
    for (const Expected& e : types) {
      CAPTURE(e.type);
      YSE::pHandle* h = p.CreateObject(e.type);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == std::string(e.type));
      CHECK(h->GetInputs() == e.inlets);
      CHECK(h->GetOutputs() == e.outlets);
      for (int i = 0; i < e.outlets; ++i) {
        CAPTURE(i);
        CHECK(h->OutputDataType(i) == YSE::OUT_TYPE::FLOAT);
      }
    }
  }

  TEST_CASE("unit conversions: all four objects are listed by pRegistry::AllNames (#442)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    for (const char* type : {".atodb", ".dbtoa", ".cartopol", ".poltocar"}) {
      CAPTURE(type);
      CHECK(std::find(names.begin(), names.end(), std::string(type)) != names.end());
    }
  }

  // ─── behaviour: .atodb ──────────────────────────────────────────────────────

  // The whole point of reusing the engine's converter is that the patcher and
  // the DSP modules agree; assert that directly rather than restating a formula.
  TEST_CASE("unit conversions: .atodb matches the engine's rmsToDb (#442)") {
    for (float amp : {0.f, 0.1f, 0.25f, 0.5f, 1.f, 2.f, 10.f}) {
      CAPTURE(amp);
      CHECK(EvaluateUnary<YSE::PATCHER::gAToDb>(amp) ==
            doctest::Approx(YSE::DSP::rmsToDb(amp)).epsilon(0.0001));
    }
  }

  // The engine's decibel reference is not Max's: amplitude 1.0 reads as 100 dB.
  TEST_CASE("unit conversions: .atodb uses the engine's 100 dB reference (#442)") {
    CHECK(EvaluateUnary<YSE::PATCHER::gAToDb>(1.f) == doctest::Approx(100.f).epsilon(0.0001));
    // Halving the amplitude is ~6 dB down on any 20*log10 scale.
    CHECK(EvaluateUnary<YSE::PATCHER::gAToDb>(0.5f) ==
          doctest::Approx(100.f - 6.0206f).epsilon(0.001));
  }

  // Silence and negative amplitudes must not produce -inf.
  TEST_CASE("unit conversions: .atodb emits 0 for zero and negative amplitudes (#442)") {
    for (float amp : {0.f, -0.5f, -100.f}) {
      CAPTURE(amp);
      const float db = EvaluateUnary<YSE::PATCHER::gAToDb>(amp);
      CHECK(std::isfinite(db));
      CHECK(db == doctest::Approx(0.f));
    }
  }

  // ─── behaviour: .dbtoa ──────────────────────────────────────────────────────

  TEST_CASE("unit conversions: .dbtoa matches the engine's dbToRms (#442)") {
    for (float db : {0.f, 50.f, 94.f, 100.f, 106.f, 120.f}) {
      CAPTURE(db);
      CHECK(EvaluateUnary<YSE::PATCHER::gDbToA>(db) ==
            doctest::Approx(YSE::DSP::dbToRms(db)).epsilon(0.0001));
    }
  }

  TEST_CASE("unit conversions: .dbtoa uses the engine's 100 dB reference (#442)") {
    CHECK(EvaluateUnary<YSE::PATCHER::gDbToA>(100.f) == doctest::Approx(1.f).epsilon(0.0001));
    CHECK(EvaluateUnary<YSE::PATCHER::gDbToA>(0.f) == doctest::Approx(0.f));
  }

  // A level far above the clamp must still emit a finite amplitude.
  TEST_CASE("unit conversions: .dbtoa stays finite for an extreme level (#442)") {
    const float amp = EvaluateUnary<YSE::PATCHER::gDbToA>(100000.f);
    CHECK(std::isfinite(amp));
  }

  // ─── behaviour: .atodb / .dbtoa round trip ──────────────────────────────────

  TEST_CASE("unit conversions: .dbtoa inverts .atodb (#442)") {
    for (float amp : {0.05f, 0.25f, 0.5f, 1.f, 4.f}) {
      CAPTURE(amp);
      const float db = EvaluateUnary<YSE::PATCHER::gAToDb>(amp);
      CHECK(EvaluateUnary<YSE::PATCHER::gDbToA>(db) == doctest::Approx(amp).epsilon(0.001));
    }
  }

  TEST_CASE("unit conversions: .atodb and .dbtoa take ints on their inlet (#442)") {
    YSE::PATCHER::gDbToA op;
    FloatSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(0)->SetInt(100, YSE::T_GUI);
    CHECK(sink.gotFloat); // g-family convention is float out, as with .+
    CHECK(sink.received == doctest::Approx(1.f).epsilon(0.0001));
  }

  // ─── behaviour: .cartopol ───────────────────────────────────────────────────

  TEST_CASE("unit conversions: .cartopol emits amplitude and angle (#442)") {
    PolarRig<YSE::PATCHER::gCarToPol> rig;

    rig.Evaluate(3.f, 4.f); // x = 3, y = 4
    CHECK(rig.left.received == doctest::Approx(5.f));
    CHECK(rig.right.received == doctest::Approx(std::atan2(4.f, 3.f)));

    // On the positive x axis the angle is 0.
    rig.Evaluate(2.f, 0.f);
    CHECK(rig.left.received == doctest::Approx(2.f));
    CHECK(rig.right.received == doctest::Approx(0.f));

    // Quadrant resolution is the reason this is not just .atan — a point in the
    // third quadrant must not report the same angle as one in the first.
    rig.Evaluate(-1.f, -1.f);
    CHECK(rig.left.received == doctest::Approx(std::sqrt(2.f)));
    CHECK(rig.right.received == doctest::Approx(std::atan2(-1.f, -1.f)));
  }

  TEST_CASE("unit conversions: .cartopol at the origin emits zeroes, not a NaN (#442)") {
    PolarRig<YSE::PATCHER::gCarToPol> rig;
    rig.Evaluate(0.f, 0.f);
    CHECK(std::isfinite(rig.left.received));
    CHECK(std::isfinite(rig.right.received));
    CHECK(rig.left.received == doctest::Approx(0.f));
    CHECK(rig.right.received == doctest::Approx(0.f));
  }

  // ─── behaviour: .poltocar ───────────────────────────────────────────────────

  TEST_CASE("unit conversions: .poltocar emits x and y (#442)") {
    PolarRig<YSE::PATCHER::gPolToCar> rig;

    rig.Evaluate(1.f, 0.f); // amplitude 1, angle 0 -> (1, 0)
    CHECK(rig.left.received == doctest::Approx(1.f));
    CHECK(rig.right.received == doctest::Approx(0.f));

    const float halfPi = 1.57079632679f;
    rig.Evaluate(2.f, halfPi); // straight up
    CHECK(rig.left.received == doctest::Approx(0.f).epsilon(0.0001));
    CHECK(rig.right.received == doctest::Approx(2.f));

    rig.Evaluate(0.f, 1.234f); // zero radius collapses to the origin
    CHECK(rig.left.received == doctest::Approx(0.f));
    CHECK(rig.right.received == doctest::Approx(0.f));
  }

  TEST_CASE("unit conversions: .poltocar inverts .cartopol (#442)") {
    PolarRig<YSE::PATCHER::gCarToPol> toPolar;
    PolarRig<YSE::PATCHER::gPolToCar> toCartesian;

    struct Point {
      float x;
      float y;
    };
    for (const Point& p :
         {Point{3.f, 4.f}, Point{-2.f, 5.f}, Point{-1.f, -1.f}, Point{0.f, -7.f}}) {
      CAPTURE(p.x);
      CAPTURE(p.y);
      toPolar.Evaluate(p.x, p.y);
      toCartesian.Evaluate(toPolar.left.received, toPolar.right.received);
      CHECK(toCartesian.left.received == doctest::Approx(p.x).epsilon(0.0001));
      CHECK(toCartesian.right.received == doctest::Approx(p.y).epsilon(0.0001));
    }
  }

  // ─── inlet / outlet semantics ───────────────────────────────────────────────

  TEST_CASE("unit conversions: inlet 1 stores silently, inlet 0 fires (#442)") {
    PolarRig<YSE::PATCHER::gCarToPol> rig;

    rig.op.GetInlet(1)->SetFloat(4.f, YSE::T_GUI);
    CHECK_FALSE(rig.left.gotFloat); // cold inlet must not produce output
    CHECK_FALSE(rig.right.gotFloat);

    rig.op.GetInlet(0)->SetFloat(3.f, YSE::T_GUI);
    CHECK(rig.left.gotFloat);
    CHECK(rig.right.gotFloat);
    CHECK(rig.left.received == doctest::Approx(5.f));

    // Re-firing the hot inlet re-evaluates against the stored operand.
    rig.op.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
    CHECK(rig.left.received == doctest::Approx(4.f));
  }

  TEST_CASE("unit conversions: .cartopol takes ints on both inlets (#442)") {
    PolarRig<YSE::PATCHER::gCarToPol> rig;
    rig.op.GetInlet(1)->SetInt(4, YSE::T_GUI);
    CHECK_FALSE(rig.left.gotFloat);
    rig.op.GetInlet(0)->SetInt(3, YSE::T_GUI);
    CHECK(rig.left.gotFloat);
    CHECK(rig.left.received == doctest::Approx(5.f));
  }

  // Max fires outlets right to left; a downstream object fed from both outlets
  // depends on that ordering, so pin it.
  TEST_CASE("unit conversions: both outlets fire, right one first (#442)") {
    YSE::PATCHER::gCarToPol op;
    std::vector<std::string> order;
    OrderSink left;
    OrderSink right;
    left.log = &order;
    left.label = "outlet0";
    right.log = &order;
    right.label = "outlet1";

    op.ConnectOutlet(left.GetInlet(0), 0);
    left.ConnectInlet(op.GetOutlet(0), 0);
    op.ConnectOutlet(right.GetInlet(0), 1);
    right.ConnectInlet(op.GetOutlet(1), 0);

    op.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    REQUIRE(order.size() == 2);
    CHECK(order[0] == "outlet1");
    CHECK(order[1] == "outlet0");
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("unit conversions: .atodb and .dbtoa register no parameters (#442)") {
    for (const char* type : {YSE::OBJ::G_ATODB, YSE::OBJ::G_DBTOA}) {
      CAPTURE(type);
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK(obj->GetParamDocs().empty());
    }
  }

  TEST_CASE("unit conversions: the polar pair names its stored operand (#442)") {
    std::unique_ptr<YSE::PATCHER::pObject> cartopol(
        YSE::PATCHER::Register().Get(YSE::OBJ::G_CARTOPOL));
    REQUIRE(cartopol != nullptr);
    REQUIRE(cartopol->GetParamDocs().size() == 1);
    CHECK(cartopol->GetParamDocs()[0].name == std::string("y"));

    std::unique_ptr<YSE::PATCHER::pObject> poltocar(
        YSE::PATCHER::Register().Get(YSE::OBJ::G_POLTOCAR));
    REQUIRE(poltocar != nullptr);
    REQUIRE(poltocar->GetParamDocs().size() == 1);
    CHECK(poltocar->GetParamDocs()[0].name == std::string("angle"));
  }

  TEST_CASE("unit conversions: params survive a DumpJSON / ParseJSON round trip (#442)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_ATODB) != nullptr);
    REQUIRE(src.CreateObject(YSE::OBJ::G_DBTOA) != nullptr);
    REQUIRE(src.CreateObject(YSE::OBJ::G_CARTOPOL, "4") != nullptr);
    REQUIRE(src.CreateObject(YSE::OBJ::G_POLTOCAR, "1.5") != nullptr);
    const std::string json = src.DumpJSON();
    for (const char* type : {".atodb", ".dbtoa", ".cartopol", ".poltocar"}) {
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
    for (const char* expected : {".atodb ", ".dbtoa ", ".cartopol 4", ".poltocar 1.5"}) {
      CAPTURE(expected);
      CHECK(std::find(restored.begin(), restored.end(), std::string(expected)) != restored.end());
    }
  }

  // A creation argument must actually reach the stored operand, not just the
  // parameter string.
  TEST_CASE("unit conversions: .cartopol computes against its creation argument (#442)") {
    YSE::PATCHER::gCarToPol op;
    FloatSink amplitude;
    op.ConnectOutlet(amplitude.GetInlet(0), 0);
    amplitude.ConnectInlet(op.GetOutlet(0), 0);

    op.SetParams("4"); // y = 4
    op.GetInlet(0)->SetFloat(3.f, YSE::T_GUI);
    CHECK(amplitude.received == doctest::Approx(5.f));
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the outlet types specifically, which is
  // what a binding generator keys on.

  TEST_CASE("unit conversions: every object documents itself as MATH (#442)") {
    const std::vector<std::pair<const char*, int>> objects = {
        {".atodb", 1}, {".dbtoa", 1}, {".cartopol", 2}, {".poltocar", 2}};
    for (const auto& o : objects) {
      CAPTURE(o.first);
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(o.first));
      REQUIRE(obj != nullptr);
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
      CHECK_FALSE(obj->GetDescription().empty());
      for (int i = 0; i < o.second; ++i) {
        CAPTURE(i);
        CHECK(obj->GetOutputType(i) == YSE::OUT_TYPE::FLOAT);
      }
    }
  }

} // TEST_SUITE("patcher")
