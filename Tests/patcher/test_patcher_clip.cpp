// Tests for the control-rate range-limiting object (issue #445): .clip
//
// The control-rate counterpart of ~clip. Three inlets in Max's order — value
// (hot), low, high — one float outlet, and two creation parameters. It pins,
// it does not rescale: that is the whole difference between .clip and .zmap,
// and the last block asserts it directly.
//
// Two behaviours are choices rather than ports of Max, so they are pinned here
// rather than left implicit:
//
//   - the limits are used as an *ordered* pair, so a range given high-to-low
//     still clips against the right two numbers,
//   - a NaN on the hot inlet emits the clipped 0 rather than propagating, the
//     convention ./ , .sqrt and .zmap already use, so the output is always
//     finite and always inside the range.
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
#include "patcher/math/gClip.h"
#include "patcher/math/gZmap.h"
#include "patcher/sinks.hpp"

using TestHelpers::FloatSink;

namespace {

  // Holds a .clip and its sink together, and offers the two ways a patch drives
  // it: through the cold inlets, or through a creation-argument string.
  struct ClipRig {
    YSE::PATCHER::gClip op;
    FloatSink sink;

    ClipRig() {
      op.ConnectOutlet(sink.GetInlet(0), 0);
      sink.ConnectInlet(op.GetOutlet(0), 0);
    }

    // Cold inlets first (they only store), then the hot inlet, which fires.
    void SetRange(float low, float high) {
      op.GetInlet(1)->SetFloat(low, YSE::T_GUI);
      op.GetInlet(2)->SetFloat(high, YSE::T_GUI);
    }

    float Clip(float value) {
      op.GetInlet(0)->SetFloat(value, YSE::T_GUI);
      return sink.received;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("clip: the object is creatable through the registry (#445)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_CLIP);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".clip"));
    CHECK(h->GetInputs() == 3);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
  }

  TEST_CASE("clip: the object is listed by pRegistry::AllNames (#445)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".clip")) != names.end());
  }

  // The audio-rate sibling keeps its own name; the two must not collide.
  TEST_CASE("clip: does not shadow the audio-rate ~clip (#445)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string("~clip")) != names.end());
    CHECK(std::string(YSE::OBJ::G_CLIP) != std::string(YSE::OBJ::D_CLIP));
  }

  // ─── behaviour: the limiting, which is the point of the object ──────────────

  TEST_CASE("clip: passes a value inside the range through untouched (#445)") {
    ClipRig rig;
    rig.SetRange(0.f, 10.f);

    CHECK(rig.Clip(0.f) == doctest::Approx(0.f));
    CHECK(rig.Clip(2.5f) == doctest::Approx(2.5f));
    CHECK(rig.Clip(9.999f) == doctest::Approx(9.999f));
    CHECK(rig.Clip(10.f) == doctest::Approx(10.f)); // the limits are inclusive
  }

  TEST_CASE("clip: pins a value outside the range to the nearest limit (#445)") {
    ClipRig rig;
    rig.SetRange(0.f, 10.f);

    CHECK(rig.Clip(10.5f) == doctest::Approx(10.f));
    CHECK(rig.Clip(1000.f) == doctest::Approx(10.f));
    CHECK(rig.Clip(-0.5f) == doctest::Approx(0.f));
    CHECK(rig.Clip(-1000.f) == doctest::Approx(0.f));
  }

  TEST_CASE("clip: handles a range that straddles zero (#445)") {
    ClipRig rig;
    rig.SetRange(-5.f, 5.f);

    CHECK(rig.Clip(-7.f) == doctest::Approx(-5.f));
    CHECK(rig.Clip(-2.f) == doctest::Approx(-2.f));
    CHECK(rig.Clip(0.f) == doctest::Approx(0.f));
    CHECK(rig.Clip(7.f) == doctest::Approx(5.f));
  }

  TEST_CASE("clip: handles a range entirely below zero (#445)") {
    ClipRig rig;
    rig.SetRange(-20.f, -10.f);

    CHECK(rig.Clip(0.f) == doctest::Approx(-10.f));
    CHECK(rig.Clip(-15.f) == doctest::Approx(-15.f));
    CHECK(rig.Clip(-100.f) == doctest::Approx(-20.f));
  }

  // Limits given the wrong way round are a plausible patching mistake; they
  // must still bound the output rather than collapse it onto one number.
  TEST_CASE("clip: treats inverted limits as an ordered pair (#445)") {
    ClipRig rig;
    rig.SetRange(10.f, 0.f);

    CHECK(rig.Clip(5.f) == doctest::Approx(5.f));
    CHECK(rig.Clip(20.f) == doctest::Approx(10.f));
    CHECK(rig.Clip(-20.f) == doctest::Approx(0.f));
  }

  TEST_CASE("clip: a collapsed range emits that single value (#445)") {
    ClipRig rig;
    rig.SetRange(3.f, 3.f);

    for (float v : {-100.f, 0.f, 3.f, 100.f}) {
      CAPTURE(v);
      CHECK(rig.Clip(v) == doctest::Approx(3.f));
    }
  }

  TEST_CASE("clip: every output stays inside the range (#445)") {
    ClipRig rig;
    rig.SetRange(-2.f, 7.f);

    for (float v : {-1e9f, -3.f, -2.f, 0.f, 6.9f, 7.f, 8.f, 1e9f}) {
      CAPTURE(v);
      const float out = rig.Clip(v);
      CHECK(out >= -2.f);
      CHECK(out <= 7.f);
    }
  }

  // ─── behaviour: non-finite inputs ───────────────────────────────────────────

  TEST_CASE("clip: an infinity lands on the matching limit (#445)") {
    ClipRig rig;
    rig.SetRange(20.f, 20000.f);

    const float up = rig.Clip(std::numeric_limits<float>::infinity());
    CHECK(std::isfinite(up));
    CHECK(up == doctest::Approx(20000.f));

    const float down = rig.Clip(-std::numeric_limits<float>::infinity());
    CHECK(std::isfinite(down));
    CHECK(down == doctest::Approx(20.f));
  }

  // A NaN survives std::min/std::max untouched, so the guard in Calculate() is
  // what keeps it from escaping.
  TEST_CASE("clip: a NaN cannot escape (#445)") {
    ClipRig rig;
    rig.SetRange(20.f, 20000.f);

    const float out = rig.Clip(std::numeric_limits<float>::quiet_NaN());
    CHECK(std::isfinite(out));
    CHECK(out >= 20.f);
    CHECK(out <= 20000.f);
  }

  // With a range around zero the substituted 0 passes straight through, which
  // is the more common patching case.
  TEST_CASE("clip: a NaN in a range containing zero emits zero (#445)") {
    ClipRig rig;
    rig.SetRange(-1.f, 1.f);

    const float out = rig.Clip(std::numeric_limits<float>::quiet_NaN());
    CHECK(std::isfinite(out));
    CHECK(out == doctest::Approx(0.f));
  }

  // ─── inlet semantics ────────────────────────────────────────────────────────

  TEST_CASE("clip: only inlet 0 fires; inlets 1-2 store silently (#445)") {
    ClipRig rig;

    for (int i = 1; i <= 2; ++i) {
      CAPTURE(i);
      rig.op.GetInlet(i)->SetFloat(1.f, YSE::T_GUI);
      CHECK_FALSE(rig.sink.gotFloat);
    }

    rig.op.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(rig.sink.gotFloat);
  }

  TEST_CASE("clip: re-firing the hot inlet re-evaluates against the stored limits (#445)") {
    ClipRig rig;
    rig.SetRange(0.f, 10.f);
    CHECK(rig.Clip(20.f) == doctest::Approx(10.f));

    // Change only the upper limit; the lower one must survive.
    rig.op.GetInlet(2)->SetFloat(15.f, YSE::T_GUI);
    CHECK(rig.Clip(20.f) == doctest::Approx(15.f));
    CHECK(rig.Clip(-1.f) == doctest::Approx(0.f));
  }

  TEST_CASE("clip: every inlet accepts an int (#445)") {
    ClipRig rig;
    rig.op.GetInlet(1)->SetInt(0, YSE::T_GUI);
    rig.op.GetInlet(2)->SetInt(10, YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotFloat);

    rig.op.GetInlet(0)->SetInt(50, YSE::T_GUI);
    CHECK(rig.sink.gotFloat); // g-family convention is float out, as with .+
    CHECK(rig.sink.received == doctest::Approx(10.f));
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("clip: defaults to ~clip's -1 to 1 range (#445)") {
    ClipRig rig;
    CHECK(rig.Clip(0.5f) == doctest::Approx(0.5f));
    CHECK(rig.Clip(2.f) == doctest::Approx(1.f));
    CHECK(rig.Clip(-2.f) == doctest::Approx(-1.f));
  }

  TEST_CASE("clip: creation arguments reach the limiting (#445)") {
    ClipRig rig;
    rig.op.SetParams("0 127");
    CHECK(rig.Clip(64.f) == doctest::Approx(64.f));
    CHECK(rig.Clip(200.f) == doctest::Approx(127.f));
    CHECK(rig.Clip(-5.f) == doctest::Approx(0.f));
  }

  TEST_CASE("clip: names both parameters in order (#445)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_CLIP));
    REQUIRE(obj != nullptr);
    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 2);
    const std::vector<std::string> expected = {"low", "high"};
    for (size_t i = 0; i < expected.size(); ++i) {
      CAPTURE(i);
      CHECK(docs[i].name == expected[i]);
    }
  }

  TEST_CASE("clip: params survive a DumpJSON / ParseJSON round trip (#445)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_CLIP, "0 127") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".clip") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".clip"));
    CHECK(h->GetParams() == std::string("0 127"));
  }

  // ─── .clip is not .zmap ─────────────────────────────────────────────────────
  // Both keep their output inside a range, but .clip limits where .zmap
  // rescales. Asserting the difference guards against one being quietly
  // implemented in terms of the other.

  TEST_CASE("clip: limits where .zmap rescales (#445)") {
    ClipRig clip;
    clip.SetRange(0.f, 10.f);

    YSE::PATCHER::gZmap zmap;
    FloatSink zmapSink;
    zmap.ConnectOutlet(zmapSink.GetInlet(0), 0);
    zmapSink.ConnectInlet(zmap.GetOutlet(0), 0);
    zmap.GetInlet(1)->SetFloat(0.f, YSE::T_GUI); // input range 0-1
    zmap.GetInlet(2)->SetFloat(1.f, YSE::T_GUI);
    zmap.GetInlet(3)->SetFloat(0.f, YSE::T_GUI); // output range 0-10
    zmap.GetInlet(4)->SetFloat(10.f, YSE::T_GUI);

    // .zmap stretches 0.5 across the output range; .clip leaves it alone.
    zmap.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(zmapSink.received == doctest::Approx(5.f));
    CHECK(clip.Clip(0.5f) == doctest::Approx(0.5f));

    // Above both ranges the two pin at the same limit, and there they agree.
    zmap.GetInlet(0)->SetFloat(30.f, YSE::T_GUI);
    CHECK(zmapSink.received == doctest::Approx(10.f));
    CHECK(clip.Clip(30.f) == doctest::Approx(10.f));

    // 3 is above .zmap's input range but inside .clip's limits, so only .zmap
    // pins — a value .clip must pass through untouched.
    zmap.GetInlet(0)->SetFloat(3.f, YSE::T_GUI);
    CHECK(zmapSink.received == doctest::Approx(10.f));
    CHECK(clip.Clip(3.f) == doctest::Approx(3.f));
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the port shape, which is what a binding
  // generator keys on.

  TEST_CASE("clip: documents itself as MATH with three labelled inlets (#445)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_CLIP));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
    CHECK_FALSE(obj->GetDescription().empty());
    REQUIRE(obj->NumInputs() == 3);
    const std::vector<std::string> labels = {"value", "low", "high"};
    for (int i = 0; i < 3; ++i) {
      CAPTURE(i);
      CHECK(obj->GetInlet(i)->GetDocLabel() == labels[(size_t)i]);
    }
    REQUIRE(obj->NumOutputs() == 1);
    CHECK(obj->GetOutputType(0) == YSE::OUT_TYPE::FLOAT);
  }

} // TEST_SUITE("patcher")
