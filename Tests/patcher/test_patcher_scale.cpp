// Tests for the range-mapping object (issue #443): .scale
//
// Six inlets in Max's order — value (hot), input low, input high, output low,
// output high, exponent — one float outlet, and six creation parameters: the
// five inlet-backed ones plus `clip`, which has no inlet because it mirrors a
// Max attribute rather than a signal.
//
// Two behaviours are deliberate choices rather than ports of Max, so they are
// pinned here rather than left implicit:
//
//   - the default mapping extrapolates (Max's `@classic 1`); `clip 1` gives
//     the clamped behaviour of `@classic 0`,
//   - a degenerate input range (low == high) emits the output low rather than
//     an infinity, a NaN, or 0.
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
#include "patcher/math/gScale.h"
#include "patcher/sinks.hpp"

using TestHelpers::FloatSink;

namespace {

  // Holds a .scale and its sink together, and offers the two ways a patch
  // drives it: through the cold inlets, or through a creation-argument string.
  struct ScaleRig {
    YSE::PATCHER::gScale op;
    FloatSink sink;

    ScaleRig() {
      op.ConnectOutlet(sink.GetInlet(0), 0);
      sink.ConnectInlet(op.GetOutlet(0), 0);
    }

    // Cold inlets first (they only store), then the hot inlet, which fires.
    void SetRange(float inLow, float inHigh, float outLow, float outHigh, float exponent = 1.f) {
      op.GetInlet(1)->SetFloat(inLow, YSE::T_GUI);
      op.GetInlet(2)->SetFloat(inHigh, YSE::T_GUI);
      op.GetInlet(3)->SetFloat(outLow, YSE::T_GUI);
      op.GetInlet(4)->SetFloat(outHigh, YSE::T_GUI);
      op.GetInlet(5)->SetFloat(exponent, YSE::T_GUI);
    }

    float Map(float value) {
      op.GetInlet(0)->SetFloat(value, YSE::T_GUI);
      return sink.received;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("scale: the object is creatable through the registry (#443)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_SCALE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".scale"));
    CHECK(h->GetInputs() == 6);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
  }

  TEST_CASE("scale: the object is listed by pRegistry::AllNames (#443)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".scale")) != names.end());
  }

  // ─── behaviour: the linear mapping ──────────────────────────────────────────

  TEST_CASE("scale: maps the input range onto the output range (#443)") {
    ScaleRig rig;
    rig.SetRange(0.f, 127.f, 0.f, 1.f);

    CHECK(rig.Map(0.f) == doctest::Approx(0.f));
    CHECK(rig.Map(127.f) == doctest::Approx(1.f));
    CHECK(rig.Map(63.5f) == doctest::Approx(0.5f));
  }

  // The single most common use: a normalised source onto a parameter range.
  TEST_CASE("scale: maps a normalised source onto an arbitrary range (#443)") {
    ScaleRig rig;
    rig.SetRange(0.f, 1.f, 20.f, 20000.f);

    CHECK(rig.Map(0.f) == doctest::Approx(20.f));
    CHECK(rig.Map(1.f) == doctest::Approx(20000.f));
    CHECK(rig.Map(0.5f) == doctest::Approx(10010.f));
  }

  // A descending output range is a legitimate mapping, not an error.
  TEST_CASE("scale: handles an inverted output range (#443)") {
    ScaleRig rig;
    rig.SetRange(0.f, 1.f, 1.f, 0.f);

    CHECK(rig.Map(0.f) == doctest::Approx(1.f));
    CHECK(rig.Map(1.f) == doctest::Approx(0.f));
    CHECK(rig.Map(0.25f) == doctest::Approx(0.75f));
  }

  TEST_CASE("scale: handles a negative input range (#443)") {
    ScaleRig rig;
    rig.SetRange(-1.f, 1.f, 0.f, 100.f);

    CHECK(rig.Map(-1.f) == doctest::Approx(0.f));
    CHECK(rig.Map(0.f) == doctest::Approx(50.f));
    CHECK(rig.Map(1.f) == doctest::Approx(100.f));
  }

  // ─── behaviour: the exponent ────────────────────────────────────────────────

  TEST_CASE("scale: an exponent bends the mapping but keeps the endpoints (#443)") {
    ScaleRig rig;
    rig.SetRange(0.f, 1.f, 0.f, 1.f, 2.f);

    // Endpoints are fixed points of any positive exponent.
    CHECK(rig.Map(0.f) == doctest::Approx(0.f));
    CHECK(rig.Map(1.f) == doctest::Approx(1.f));
    // The midpoint is pulled down by a squaring curve.
    CHECK(rig.Map(0.5f) == doctest::Approx(0.25f));
    CHECK(rig.Map(0.25f) == doctest::Approx(0.0625f));
  }

  TEST_CASE("scale: a fractional exponent pushes the curve the other way (#443)") {
    ScaleRig rig;
    rig.SetRange(0.f, 1.f, 0.f, 1.f, 0.5f);
    CHECK(rig.Map(0.25f) == doctest::Approx(0.5f));
    CHECK(rig.Map(1.f) == doctest::Approx(1.f));
  }

  // Max mirrors the curve below the input range rather than handing pow() a
  // negative base, which has no real result for a fractional exponent.
  TEST_CASE("scale: the curve is mirrored below the input range (#443)") {
    ScaleRig rig;
    rig.SetRange(0.f, 1.f, 0.f, 1.f, 2.f);

    // -0.5 normalises to -0.5; the mirrored square is -0.25.
    CHECK(rig.Map(-0.5f) == doctest::Approx(-0.25f));
    // A fractional exponent would be a NaN without the mirroring.
    rig.SetRange(0.f, 1.f, 0.f, 1.f, 0.5f);
    const float below = rig.Map(-0.25f);
    CHECK(std::isfinite(below));
    CHECK(below == doctest::Approx(-0.5f));
  }

  TEST_CASE("scale: exponent 1 is exactly linear (#443)") {
    ScaleRig rig;
    rig.SetRange(0.f, 10.f, 0.f, 10.f, 1.f);
    for (float v : {0.f, 1.f, 3.f, 7.f, 10.f}) {
      CAPTURE(v);
      CHECK(rig.Map(v) == doctest::Approx(v));
    }
  }

  // ─── behaviour: extrapolation vs clipping ───────────────────────────────────

  TEST_CASE("scale: extrapolates outside the input range by default (#443)") {
    ScaleRig rig;
    rig.SetRange(0.f, 1.f, 0.f, 10.f);

    CHECK(rig.Map(2.f) == doctest::Approx(20.f));
    CHECK(rig.Map(-1.f) == doctest::Approx(-10.f));
  }

  TEST_CASE("scale: clip 1 clamps to the output range (#443)") {
    ScaleRig rig;
    rig.op.SetParams("0 1 0 10 1 1"); // inLow inHigh outLow outHigh exponent clip

    CHECK(rig.Map(0.5f) == doctest::Approx(5.f)); // inside the range is untouched
    CHECK(rig.Map(2.f) == doctest::Approx(10.f));
    CHECK(rig.Map(-1.f) == doctest::Approx(0.f));
  }

  // Clipping must respect a descending output range, where outLow > outHigh.
  TEST_CASE("scale: clip 1 respects an inverted output range (#443)") {
    ScaleRig rig;
    rig.op.SetParams("0 1 10 0 1 1");

    CHECK(rig.Map(2.f) == doctest::Approx(0.f));
    CHECK(rig.Map(-1.f) == doctest::Approx(10.f));
  }

  // ─── behaviour: degenerate and non-finite inputs ────────────────────────────

  TEST_CASE("scale: a collapsed input range emits the output low, not an infinity (#443)") {
    ScaleRig rig;
    rig.SetRange(5.f, 5.f, 2.f, 8.f);

    for (float v : {0.f, 5.f, 100.f}) {
      CAPTURE(v);
      const float out = rig.Map(v);
      CHECK(std::isfinite(out));
      CHECK(out == doctest::Approx(2.f));
    }
  }

  // A zero base with a negative exponent is an infinity; nothing non-finite may
  // leave the object and poison everything downstream.
  TEST_CASE("scale: a non-finite result is replaced by 0 (#443)") {
    ScaleRig rig;
    rig.SetRange(0.f, 1.f, 0.f, 1.f, -2.f);
    const float out = rig.Map(0.f); // normalises to 0, 0 ^ -2 is +inf
    CHECK(std::isfinite(out));
    CHECK(out == doctest::Approx(0.f));
  }

  TEST_CASE("scale: an infinity arriving on the hot inlet does not escape (#443)") {
    ScaleRig rig;
    rig.SetRange(0.f, 1.f, 0.f, 1.f);
    const float out = rig.Map(std::numeric_limits<float>::infinity());
    CHECK(std::isfinite(out));
    CHECK(out == doctest::Approx(0.f));
  }

  // ─── inlet semantics ────────────────────────────────────────────────────────

  TEST_CASE("scale: only inlet 0 fires; inlets 1-5 store silently (#443)") {
    ScaleRig rig;

    for (int i = 1; i <= 5; ++i) {
      CAPTURE(i);
      rig.op.GetInlet(i)->SetFloat(1.f, YSE::T_GUI);
      CHECK_FALSE(rig.sink.gotFloat);
    }

    rig.op.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(rig.sink.gotFloat);
  }

  TEST_CASE("scale: re-firing the hot inlet re-evaluates against the stored range (#443)") {
    ScaleRig rig;
    rig.SetRange(0.f, 1.f, 0.f, 100.f);
    CHECK(rig.Map(0.5f) == doctest::Approx(50.f));

    // Change only the output high; the rest must survive.
    rig.op.GetInlet(4)->SetFloat(10.f, YSE::T_GUI);
    CHECK(rig.Map(0.5f) == doctest::Approx(5.f));
  }

  TEST_CASE("scale: every inlet accepts an int (#443)") {
    ScaleRig rig;
    rig.op.GetInlet(1)->SetInt(0, YSE::T_GUI);
    rig.op.GetInlet(2)->SetInt(10, YSE::T_GUI);
    rig.op.GetInlet(3)->SetInt(0, YSE::T_GUI);
    rig.op.GetInlet(4)->SetInt(100, YSE::T_GUI);
    rig.op.GetInlet(5)->SetInt(1, YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotFloat);

    rig.op.GetInlet(0)->SetInt(5, YSE::T_GUI);
    CHECK(rig.sink.gotFloat); // g-family convention is float out, as with .+
    CHECK(rig.sink.received == doctest::Approx(50.f));
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("scale: defaults to Max's 0-127 onto 0-1, linear, extrapolating (#443)") {
    ScaleRig rig;
    CHECK(rig.Map(127.f) == doctest::Approx(1.f));
    CHECK(rig.Map(0.f) == doctest::Approx(0.f));
    CHECK(rig.Map(254.f) == doctest::Approx(2.f)); // clip defaults to off
  }

  TEST_CASE("scale: creation arguments reach the mapping (#443)") {
    ScaleRig rig;
    rig.op.SetParams("0 1 20 20000 1 0");
    CHECK(rig.Map(0.5f) == doctest::Approx(10010.f));
  }

  TEST_CASE("scale: names all six parameters in order (#443)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_SCALE));
    REQUIRE(obj != nullptr);
    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 6);
    const std::vector<std::string> expected = {"inLow",   "inHigh",   "outLow",
                                               "outHigh", "exponent", "clip"};
    for (size_t i = 0; i < expected.size(); ++i) {
      CAPTURE(i);
      CHECK(docs[i].name == expected[i]);
    }
  }

  TEST_CASE("scale: params survive a DumpJSON / ParseJSON round trip (#443)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_SCALE, "0 1 20 20000 0.5 1") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".scale") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".scale"));
    CHECK(h->GetParams() == std::string("0 1 20 20000 0.5 1"));
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the port shape, which is what a binding
  // generator keys on.

  TEST_CASE("scale: documents itself as MATH with six labelled inlets (#443)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_SCALE));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
    CHECK_FALSE(obj->GetDescription().empty());
    REQUIRE(obj->NumInputs() == 6);
    const std::vector<std::string> labels = {"value",  "inLow",   "inHigh",
                                             "outLow", "outHigh", "exponent"};
    for (int i = 0; i < 6; ++i) {
      CAPTURE(i);
      CHECK(obj->GetInlet(i)->GetDocLabel() == labels[(size_t)i]);
    }
    REQUIRE(obj->NumOutputs() == 1);
    CHECK(obj->GetOutputType(0) == YSE::OUT_TYPE::FLOAT);
  }

} // TEST_SUITE("patcher")
