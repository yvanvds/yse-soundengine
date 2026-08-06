// Tests for the exponential control-value scaler (issue #447): .linedrive
//
// Four inlets — value (hot), inputMax, outputMax, curve — one float outlet, and
// three creation parameters in Max's typed-in argument order. The mapping is
// Max's:
//
//     out = outputMax * curve^(input - inputMax)
//
// which Max writes as y = b e^(-a log c) e^(x log c). The input maximum is the
// anchor: feed it in and the output is exactly outputMax, whatever the curve.
//
// Three behaviours are choices rather than ports of Max and are pinned here
// rather than left implicit:
//
//   - Max's outlet emits a [value, ramp] list for a `line` object and its right
//     inlet carries the ramp time; .linedrive emits the scaled value alone,
//   - Max requires a curve above 1; any positive curve is accepted here, one
//     below 1 mirroring the response and 1 flattening it to a constant,
//   - a curve at or below 0 and any non-finite result emit 0, the convention
//     ./ , .sqrt, .scale and .zmap already use.
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
#include "patcher/math/gLinedrive.h"
#include "patcher/math/gScale.h"
#include "patcher/sinks.hpp"

using TestHelpers::FloatSink;

namespace {

  // Holds a .linedrive and its sink together, and offers the two ways a patch
  // drives it: through the cold inlets, or through a creation-argument string.
  struct LinedriveRig {
    YSE::PATCHER::gLinedrive op;
    FloatSink sink;

    LinedriveRig() {
      op.ConnectOutlet(sink.GetInlet(0), 0);
      sink.ConnectInlet(op.GetOutlet(0), 0);
    }

    void SetShape(float inputMax, float outputMax, float curve) {
      op.GetInlet(1)->SetFloat(inputMax, YSE::T_GUI);
      op.GetInlet(2)->SetFloat(outputMax, YSE::T_GUI);
      op.GetInlet(3)->SetFloat(curve, YSE::T_GUI);
    }

    float Drive(float value) {
      op.GetInlet(0)->SetFloat(value, YSE::T_GUI);
      return sink.received;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("linedrive: the object is creatable through the registry (#447)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_LINEDRIVE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".linedrive"));
    CHECK(h->GetInputs() == 4);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
  }

  TEST_CASE("linedrive: the object is listed by pRegistry::AllNames (#447)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".linedrive")) != names.end());
  }

  // ─── the curve ──────────────────────────────────────────────────────────────

  // The input maximum is the anchor of the whole mapping: feed it in and the
  // output is exactly the output maximum, whatever the curve. Everything else
  // about the object follows from that.
  TEST_CASE("linedrive: the input maximum maps exactly onto the output maximum (#447)") {
    LinedriveRig rig;
    for (float c : {1.06f, 1.5f, 2.f, 10.f}) {
      CAPTURE(c);
      rig.SetShape(10.f, 8.f, c);
      CHECK(rig.Drive(10.f) == doctest::Approx(8.f));
    }
  }

  TEST_CASE("linedrive: follows Max's exponential formula (#447)") {
    LinedriveRig rig;
    rig.SetShape(10.f, 8.f, 2.f);

    CHECK(rig.Drive(10.f) == doctest::Approx(8.f)); // 8 * 2^0
    CHECK(rig.Drive(9.f) == doctest::Approx(4.f)); // 8 * 2^-1
    CHECK(rig.Drive(8.f) == doctest::Approx(2.f)); // 8 * 2^-2
    CHECK(rig.Drive(7.f) == doctest::Approx(1.f)); // 8 * 2^-3
    CHECK(rig.Drive(0.f) == doctest::Approx(0.0078125f)); // 8 * 2^-10
  }

  // The whole point of the object: equal steps of input give equal *ratios* of
  // output, which is what makes the perceived change even across the range.
  TEST_CASE("linedrive: equal input steps give equal output ratios (#447)") {
    LinedriveRig rig;
    rig.SetShape(127.f, 1.f, 1.06f);

    float previous = rig.Drive(0.f);
    for (int step = 1; step <= 127; ++step) {
      CAPTURE(step);
      const float current = rig.Drive((float)step);
      CHECK(current / previous == doctest::Approx(1.06f).epsilon(0.001));
      previous = current;
    }
  }

  // Nothing stops an input above the maximum; the curve simply carries on, the
  // same way Max's does.
  TEST_CASE("linedrive: extrapolates above the input maximum (#447)") {
    LinedriveRig rig;
    rig.SetShape(10.f, 8.f, 2.f);

    CHECK(rig.Drive(11.f) == doctest::Approx(16.f));
    CHECK(rig.Drive(12.f) == doctest::Approx(32.f));
  }

  TEST_CASE("linedrive: is monotonic over the whole input range (#447)") {
    LinedriveRig rig;
    rig.SetShape(127.f, 1.f, 1.06f);

    float previous = -1.f;
    for (int step = 0; step <= 127; ++step) {
      CAPTURE(step);
      const float current = rig.Drive((float)step);
      CHECK(std::isfinite(current));
      CHECK(current > previous);
      previous = current;
    }
    CHECK(previous == doctest::Approx(1.f));
  }

  TEST_CASE("linedrive: a negative output maximum mirrors the curve (#447)") {
    LinedriveRig rig;
    rig.SetShape(10.f, -8.f, 2.f);

    CHECK(rig.Drive(10.f) == doctest::Approx(-8.f));
    CHECK(rig.Drive(9.f) == doctest::Approx(-4.f));
    CHECK(rig.Drive(0.f) == doctest::Approx(-0.0078125f));
  }

  // ─── the curve parameter's edges ────────────────────────────────────────────

  // Max requires a curve above 1. Accepting anything positive costs nothing and
  // a curve of exactly 1 has an obvious answer: no curve at all.
  TEST_CASE("linedrive: a curve of 1 flattens to the output maximum (#447)") {
    LinedriveRig rig;
    rig.SetShape(10.f, 8.f, 1.f);

    for (float v : {-100.f, 0.f, 5.f, 10.f, 100.f}) {
      CAPTURE(v);
      CHECK(rig.Drive(v) == doctest::Approx(8.f));
    }
  }

  // A curve below 1 is legal here where Max forbids it — it simply front-loads
  // the response instead of back-loading it.
  TEST_CASE("linedrive: a curve below 1 mirrors the response (#447)") {
    LinedriveRig rig;
    rig.SetShape(10.f, 8.f, 0.5f);

    CHECK(rig.Drive(10.f) == doctest::Approx(8.f)); // the anchor still holds
    CHECK(rig.Drive(9.f) == doctest::Approx(16.f));
    CHECK(rig.Drive(11.f) == doctest::Approx(4.f));
  }

  // A negative base has no real power for most exponents — std::pow would hand
  // back a NaN for almost every input and a wildly signed finite value for the
  // few that land on a whole number, so the whole non-positive half is rejected.
  TEST_CASE("linedrive: a curve at or below 0 emits 0 (#447)") {
    LinedriveRig rig;
    for (float c : {0.f, -1.f, -2.5f}) {
      CAPTURE(c);
      rig.SetShape(10.f, 8.f, c);
      // 7 is an exact power step away from the anchor, so std::pow(-2, -3)
      // would have produced a finite -0.125 had the guard not been there.
      for (float v : {-3.f, 0.f, 7.f, 10.f, 13.f}) {
        CAPTURE(v);
        CHECK(rig.Drive(v) == doctest::Approx(0.f));
      }
    }
  }

  TEST_CASE("linedrive: a NaN curve emits 0 (#447)") {
    LinedriveRig rig;
    rig.SetShape(10.f, 8.f, std::numeric_limits<float>::quiet_NaN());
    CHECK(rig.Drive(10.f) == doctest::Approx(0.f));
    CHECK(rig.Drive(0.f) == doctest::Approx(0.f));
  }

  // ─── non-finite results ─────────────────────────────────────────────────────

  TEST_CASE("linedrive: an input that overflows the curve emits 0 (#447)") {
    LinedriveRig rig;
    rig.SetShape(0.f, 1.f, 2.f);

    const float out = rig.Drive(1000.f); // 2^1000 is far past float range
    CHECK(std::isfinite(out));
    CHECK(out == doctest::Approx(0.f));
  }

  TEST_CASE("linedrive: a NaN cannot escape (#447)") {
    LinedriveRig rig;
    rig.SetShape(10.f, 8.f, 2.f);

    const float out = rig.Drive(std::numeric_limits<float>::quiet_NaN());
    CHECK(std::isfinite(out));
    CHECK(out == doctest::Approx(0.f));
  }

  TEST_CASE("linedrive: an infinity cannot escape (#447)") {
    LinedriveRig rig;
    rig.SetShape(10.f, 8.f, 2.f);

    const float up = rig.Drive(std::numeric_limits<float>::infinity());
    CHECK(std::isfinite(up));
    CHECK(up == doctest::Approx(0.f));

    const float down = rig.Drive(-std::numeric_limits<float>::infinity());
    CHECK(std::isfinite(down));
    CHECK(down == doctest::Approx(0.f));
  }

  TEST_CASE("linedrive: a non-finite shape cannot produce a non-finite output (#447)") {
    LinedriveRig rig;
    rig.SetShape(std::numeric_limits<float>::infinity(), 8.f, 2.f);
    CHECK(std::isfinite(rig.Drive(5.f)));

    rig.SetShape(10.f, std::numeric_limits<float>::infinity(), 2.f);
    CHECK(std::isfinite(rig.Drive(5.f)));
  }

  // ─── inlet semantics ────────────────────────────────────────────────────────

  TEST_CASE("linedrive: only inlet 0 fires; inlets 1-3 store silently (#447)") {
    LinedriveRig rig;

    for (int i = 1; i <= 3; ++i) {
      CAPTURE(i);
      rig.op.GetInlet(i)->SetFloat(2.f, YSE::T_GUI);
      CHECK_FALSE(rig.sink.gotFloat);
    }

    rig.op.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    CHECK(rig.sink.gotFloat);
  }

  TEST_CASE("linedrive: re-firing the hot inlet re-evaluates against the stored shape (#447)") {
    LinedriveRig rig;
    rig.SetShape(10.f, 8.f, 2.f);
    CHECK(rig.Drive(9.f) == doctest::Approx(4.f));

    // Change only the curve; the two maxima must survive.
    rig.op.GetInlet(3)->SetFloat(4.f, YSE::T_GUI);
    CHECK(rig.Drive(9.f) == doctest::Approx(2.f));
    CHECK(rig.Drive(10.f) == doctest::Approx(8.f));
  }

  TEST_CASE("linedrive: every inlet accepts an int (#447)") {
    LinedriveRig rig;
    rig.op.GetInlet(1)->SetInt(10, YSE::T_GUI);
    rig.op.GetInlet(2)->SetInt(8, YSE::T_GUI);
    rig.op.GetInlet(3)->SetInt(2, YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotFloat);

    rig.op.GetInlet(0)->SetInt(7, YSE::T_GUI);
    CHECK(rig.sink.gotFloat); // g-family convention is float out, as with .+
    CHECK(rig.sink.received == doctest::Approx(1.f));
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  // The MIDI-fader use case from the issue, straight out of the box.
  TEST_CASE("linedrive: defaults to a 0-127 controller driving 0-1 on a 1.06 curve (#447)") {
    LinedriveRig rig;

    CHECK(rig.Drive(127.f) == doctest::Approx(1.f));
    CHECK(rig.Drive(0.f) == doctest::Approx(std::pow(1.06f, -127.f)));
    CHECK(rig.Drive(0.f) < 0.001f); // a fader at the bottom is effectively off
    CHECK(rig.Drive(64.f) == doctest::Approx(std::pow(1.06f, -63.f)));
  }

  TEST_CASE("linedrive: creation arguments reach the curve (#447)") {
    LinedriveRig rig;
    rig.op.SetParams("10 8 2");
    CHECK(rig.Drive(10.f) == doctest::Approx(8.f));
    CHECK(rig.Drive(9.f) == doctest::Approx(4.f));
    CHECK(rig.Drive(0.f) == doctest::Approx(0.0078125f));
  }

  TEST_CASE("linedrive: names all three parameters in Max's argument order (#447)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_LINEDRIVE));
    REQUIRE(obj != nullptr);
    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 3);
    const std::vector<std::string> expected = {"inputMax", "outputMax", "curve"};
    for (size_t i = 0; i < expected.size(); ++i) {
      CAPTURE(i);
      CHECK(docs[i].name == expected[i]);
    }
  }

  TEST_CASE("linedrive: params survive a DumpJSON / ParseJSON round trip (#447)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_LINEDRIVE, "10 8 2") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".linedrive") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".linedrive"));
    CHECK(h->GetParams() == std::string("10 8 2"));
  }

  // ─── .linedrive is not .scale ───────────────────────────────────────────────
  // Both map a controller onto an output range, but .scale's default mapping is
  // linear where .linedrive's is geometric. Asserting the difference guards
  // against one being quietly implemented in terms of the other.

  TEST_CASE("linedrive: curves where .scale is linear (#447)") {
    LinedriveRig drive; // defaults: 0-127 onto 0-1, curve 1.06

    YSE::PATCHER::gScale scale;
    FloatSink scaleSink;
    scale.ConnectOutlet(scaleSink.GetInlet(0), 0);
    scaleSink.ConnectInlet(scale.GetOutlet(0), 0);
    // The same range, mapped linearly: inLow inHigh outLow outHigh exponent clip
    scale.SetParams("0 127 0 1 1 0");

    // At the top of the range the two agree — both hand back the maximum.
    scale.GetInlet(0)->SetFloat(127.f, YSE::T_GUI);
    CHECK(scaleSink.received == doctest::Approx(1.f));
    CHECK(drive.Drive(127.f) == doctest::Approx(1.f));

    // Everywhere below it .linedrive sits far under the straight line, which is
    // exactly the reshaping the object exists to do.
    for (float v : {32.f, 64.f, 96.f}) {
      CAPTURE(v);
      scale.GetInlet(0)->SetFloat(v, YSE::T_GUI);
      CHECK(drive.Drive(v) < scaleSink.received * 0.5f);
    }
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the port shape, which is what a binding
  // generator keys on.

  TEST_CASE("linedrive: documents itself as MATH with four labelled inlets (#447)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_LINEDRIVE));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
    CHECK_FALSE(obj->GetDescription().empty());
    REQUIRE(obj->NumInputs() == 4);
    const std::vector<std::string> labels = {"value", "inputMax", "outputMax", "curve"};
    for (int i = 0; i < 4; ++i) {
      CAPTURE(i);
      CHECK(obj->GetInlet(i)->GetDocLabel() == labels[(size_t)i]);
    }
    REQUIRE(obj->NumOutputs() == 1);
    CHECK(obj->GetOutputType(0) == YSE::OUT_TYPE::FLOAT);
  }

} // TEST_SUITE("patcher")
