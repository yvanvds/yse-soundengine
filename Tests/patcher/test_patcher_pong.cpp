// Tests for the control-rate folding/wrapping range limiter (issue #446): .pong
//
// Three inlets in Max's order — value (hot), low, high — one float outlet, and
// three creation parameters (low, high, mode). The mode has no inlet: in Max it
// is the @mode attribute, so it follows .scale's `clip` precedent and stays a
// parameter.
//
// The four modes carry Max's numbering: 0 none (pass through), 1 clip, 2 wrap,
// 3 fold. Three behaviours are choices rather than ports of Max and are pinned
// here rather than left implicit:
//
//   - the default mode is fold, not Max's `none`, so the object does out of the
//     box what it is named for,
//   - the limits are used as an *ordered* pair, so a range given high-to-low
//     still folds against the right two numbers,
//   - a NaN emits 0 folded into the range and an infinity emits the matching
//     limit, so in every mode but 0 the output is finite and inside the range.
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
#include "patcher/math/gPong.h"
#include "patcher/sinks.hpp"

using TestHelpers::FloatSink;

namespace {

  // Mode values, spelled out so the test cases below read as behaviour rather
  // than as magic numbers.
  constexpr int MODE_NONE = 0;
  constexpr int MODE_CLIP = 1;
  constexpr int MODE_WRAP = 2;
  constexpr int MODE_FOLD = 3;

  // Holds a .pong and its sink together, and offers the two ways a patch drives
  // it: through the cold inlets, or through a creation-argument string.
  struct PongRig {
    YSE::PATCHER::gPong op;
    FloatSink sink;

    PongRig() {
      op.ConnectOutlet(sink.GetInlet(0), 0);
      sink.ConnectInlet(op.GetOutlet(0), 0);
    }

    // Cold inlets only store; the mode is a parameter, so setting it goes
    // through SetParams together with the range.
    void SetRange(float low, float high) {
      op.GetInlet(1)->SetFloat(low, YSE::T_GUI);
      op.GetInlet(2)->SetFloat(high, YSE::T_GUI);
    }

    void Setup(float low, float high, int mode) {
      op.SetParams(std::to_string(low) + " " + std::to_string(high) + " " + std::to_string(mode));
    }

    float Pong(float value) {
      op.GetInlet(0)->SetFloat(value, YSE::T_GUI);
      return sink.received;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("pong: the object is creatable through the registry (#446)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_PONG);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".pong"));
    CHECK(h->GetInputs() == 3);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
  }

  TEST_CASE("pong: the object is listed by pRegistry::AllNames (#446)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".pong")) != names.end());
  }

  // ─── mode 3, fold: the behaviour the object is named for ────────────────────

  TEST_CASE("pong: fold passes a value inside the range through untouched (#446)") {
    PongRig rig;
    rig.Setup(0.f, 10.f, MODE_FOLD);

    CHECK(rig.Pong(0.f) == doctest::Approx(0.f));
    CHECK(rig.Pong(2.5f) == doctest::Approx(2.5f));
    CHECK(rig.Pong(10.f) == doctest::Approx(10.f)); // both limits are fixed points
  }

  TEST_CASE("pong: fold reflects an out-of-range value back into the range (#446)") {
    PongRig rig;
    rig.Setup(0.f, 10.f, MODE_FOLD);

    CHECK(rig.Pong(11.f) == doctest::Approx(9.f));
    CHECK(rig.Pong(12.f) == doctest::Approx(8.f));
    CHECK(rig.Pong(-1.f) == doctest::Approx(1.f));
    CHECK(rig.Pong(-3.f) == doctest::Approx(3.f));
  }

  // The reflection is periodic over twice the span, so a value many spans away
  // keeps folding rather than stopping at the first bounce.
  TEST_CASE("pong: fold is periodic over twice the span (#446)") {
    PongRig rig;
    rig.Setup(0.f, 10.f, MODE_FOLD);

    CHECK(rig.Pong(25.f) == doctest::Approx(5.f)); // 25 -> 15 -> 5
    CHECK(rig.Pong(35.f) == doctest::Approx(5.f));
    CHECK(rig.Pong(-25.f) == doctest::Approx(5.f));
    CHECK(rig.Pong(20.f) == doctest::Approx(0.f)); // a whole period back to lo
    CHECK(rig.Pong(30.f) == doctest::Approx(10.f));
  }

  TEST_CASE("pong: fold works on a range that straddles zero (#446)") {
    PongRig rig;
    rig.Setup(-1.f, 1.f, MODE_FOLD);

    CHECK(rig.Pong(0.5f) == doctest::Approx(0.5f));
    CHECK(rig.Pong(1.5f) == doctest::Approx(0.5f));
    CHECK(rig.Pong(-1.5f) == doctest::Approx(-0.5f));
    CHECK(rig.Pong(3.f) == doctest::Approx(-1.f));
  }

  // The MIDI-register use case from the issue: material has to stay inside a
  // register without every excursion collapsing onto the boundary note.
  TEST_CASE("pong: fold keeps generative material off the boundary (#446)") {
    PongRig rig;
    rig.Setup(60.f, 72.f, MODE_FOLD);

    // Three notes above the top fold to three distinct notes below it, where
    // .clip would have flattened all three onto 72.
    CHECK(rig.Pong(73.f) == doctest::Approx(71.f));
    CHECK(rig.Pong(75.f) == doctest::Approx(69.f));
    CHECK(rig.Pong(78.f) == doctest::Approx(66.f));
  }

  // ─── mode 2, wrap ───────────────────────────────────────────────────────────

  TEST_CASE("pong: wrap carries a value around to the other side (#446)") {
    PongRig rig;
    rig.Setup(0.f, 10.f, MODE_WRAP);

    CHECK(rig.Pong(3.f) == doctest::Approx(3.f));
    CHECK(rig.Pong(11.f) == doctest::Approx(1.f));
    CHECK(rig.Pong(23.f) == doctest::Approx(3.f));
    CHECK(rig.Pong(-1.f) == doctest::Approx(9.f));
    CHECK(rig.Pong(-11.f) == doctest::Approx(9.f));
  }

  // The range is half-open at the top — that is what makes a counter driven
  // through .pong behave like a phase rather than repeat a value.
  TEST_CASE("pong: wrap treats the top of the range as half-open (#446)") {
    PongRig rig;
    rig.Setup(0.f, 1.f, MODE_WRAP);

    CHECK(rig.Pong(0.f) == doctest::Approx(0.f));
    CHECK(rig.Pong(1.f) == doctest::Approx(0.f));
    CHECK(rig.Pong(2.f) == doctest::Approx(0.f));
    CHECK(rig.Pong(1.25f) == doctest::Approx(0.25f));
  }

  TEST_CASE("pong: wrap works on a range that straddles zero (#446)") {
    PongRig rig;
    rig.Setup(-1.f, 1.f, MODE_WRAP);

    CHECK(rig.Pong(1.5f) == doctest::Approx(-0.5f));
    CHECK(rig.Pong(-1.5f) == doctest::Approx(0.5f));
    CHECK(rig.Pong(-3.f) == doctest::Approx(-1.f));
  }

  // ─── mode 1, clip and mode 0, none ──────────────────────────────────────────

  // Mode 1 must agree with .clip exactly, or the two objects have quietly
  // drifted apart.
  TEST_CASE("pong: clip mode agrees with .clip (#446)") {
    PongRig rig;
    rig.Setup(0.f, 10.f, MODE_CLIP);

    YSE::PATCHER::gClip clip;
    FloatSink clipSink;
    clip.ConnectOutlet(clipSink.GetInlet(0), 0);
    clipSink.ConnectInlet(clip.GetOutlet(0), 0);
    clip.SetParams("0 10");

    for (float v : {-100.f, -1.f, 0.f, 5.f, 10.f, 11.f, 100.f}) {
      CAPTURE(v);
      clip.GetInlet(0)->SetFloat(v, YSE::T_GUI);
      CHECK(rig.Pong(v) == doctest::Approx(clipSink.received));
    }
  }

  TEST_CASE("pong: mode 0 passes everything through unchanged (#446)") {
    PongRig rig;
    rig.Setup(0.f, 10.f, MODE_NONE);

    for (float v : {-100.f, -1.f, 0.f, 5.f, 10.f, 11.f, 100.f}) {
      CAPTURE(v);
      CHECK(rig.Pong(v) == doctest::Approx(v));
    }
  }

  // An unrecognised mode must not silently become the pass-through — folding is
  // the safer guess and matches the default.
  TEST_CASE("pong: an unrecognised mode folds (#446)") {
    PongRig rig;
    rig.Setup(0.f, 10.f, 99);

    CHECK(rig.Pong(11.f) == doctest::Approx(9.f));
    CHECK(rig.Pong(-3.f) == doctest::Approx(3.f));
  }

  // ─── range edge cases ───────────────────────────────────────────────────────

  // Limits given the wrong way round are a plausible patching mistake; they
  // must still bound the output rather than divide by a negative span.
  TEST_CASE("pong: treats inverted limits as an ordered pair (#446)") {
    PongRig rig;
    rig.Setup(10.f, 0.f, MODE_FOLD);

    CHECK(rig.Pong(5.f) == doctest::Approx(5.f));
    CHECK(rig.Pong(12.f) == doctest::Approx(8.f));
    CHECK(rig.Pong(-2.f) == doctest::Approx(2.f));
  }

  TEST_CASE("pong: a collapsed range emits that single value in every mode (#446)") {
    for (int mode : {MODE_CLIP, MODE_WRAP, MODE_FOLD}) {
      CAPTURE(mode);
      PongRig rig;
      rig.Setup(3.f, 3.f, mode);
      for (float v : {-100.f, 0.f, 3.f, 100.f}) {
        CAPTURE(v);
        CHECK(rig.Pong(v) == doctest::Approx(3.f));
      }
    }
  }

  TEST_CASE("pong: every output stays inside the range (#446)") {
    for (int mode : {MODE_CLIP, MODE_WRAP, MODE_FOLD}) {
      CAPTURE(mode);
      PongRig rig;
      rig.Setup(-2.f, 7.f, mode);
      for (float v : {-1e6f, -12.5f, -2.f, 0.f, 6.9f, 7.f, 8.f, 1e6f}) {
        CAPTURE(v);
        const float out = rig.Pong(v);
        CHECK(std::isfinite(out));
        CHECK(out >= -2.f);
        CHECK(out <= 7.f);
      }
    }
  }

  // ─── non-finite inputs ──────────────────────────────────────────────────────

  // std::fmod hands back a NaN for an infinite left operand, so the guard in
  // Calculate() is what keeps the promise above true.
  TEST_CASE("pong: an infinity lands on the matching limit (#446)") {
    for (int mode : {MODE_CLIP, MODE_WRAP, MODE_FOLD}) {
      CAPTURE(mode);
      PongRig rig;
      rig.Setup(20.f, 20000.f, mode);

      const float up = rig.Pong(std::numeric_limits<float>::infinity());
      CHECK(std::isfinite(up));
      CHECK(up == doctest::Approx(20000.f));

      const float down = rig.Pong(-std::numeric_limits<float>::infinity());
      CHECK(std::isfinite(down));
      CHECK(down == doctest::Approx(20.f));
    }
  }

  TEST_CASE("pong: a NaN cannot escape (#446)") {
    for (int mode : {MODE_CLIP, MODE_WRAP, MODE_FOLD}) {
      CAPTURE(mode);
      PongRig rig;
      rig.Setup(20.f, 20000.f, mode);

      const float out = rig.Pong(std::numeric_limits<float>::quiet_NaN());
      CHECK(std::isfinite(out));
      CHECK(out >= 20.f);
      CHECK(out <= 20000.f);
    }
  }

  // With a range containing zero the substituted 0 passes straight through,
  // which is the more common patching case.
  TEST_CASE("pong: a NaN in a range containing zero emits zero (#446)") {
    PongRig rig;
    rig.Setup(-1.f, 1.f, MODE_FOLD);

    const float out = rig.Pong(std::numeric_limits<float>::quiet_NaN());
    CHECK(std::isfinite(out));
    CHECK(out == doctest::Approx(0.f));
  }

  // ─── inlet semantics ────────────────────────────────────────────────────────

  TEST_CASE("pong: only inlet 0 fires; inlets 1-2 store silently (#446)") {
    PongRig rig;

    for (int i = 1; i <= 2; ++i) {
      CAPTURE(i);
      rig.op.GetInlet(i)->SetFloat(1.f, YSE::T_GUI);
      CHECK_FALSE(rig.sink.gotFloat);
    }

    rig.op.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(rig.sink.gotFloat);
  }

  TEST_CASE("pong: re-firing the hot inlet re-evaluates against the stored limits (#446)") {
    PongRig rig;
    rig.SetRange(0.f, 10.f);
    CHECK(rig.Pong(11.f) == doctest::Approx(9.f));

    // Change only the upper limit; the lower one must survive.
    rig.op.GetInlet(2)->SetFloat(20.f, YSE::T_GUI);
    CHECK(rig.Pong(11.f) == doctest::Approx(11.f));
    CHECK(rig.Pong(21.f) == doctest::Approx(19.f));
  }

  TEST_CASE("pong: every inlet accepts an int (#446)") {
    PongRig rig;
    rig.op.GetInlet(1)->SetInt(0, YSE::T_GUI);
    rig.op.GetInlet(2)->SetInt(10, YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotFloat);

    rig.op.GetInlet(0)->SetInt(12, YSE::T_GUI);
    CHECK(rig.sink.gotFloat); // g-family convention is float out, as with .+
    CHECK(rig.sink.received == doctest::Approx(8.f));
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  // Max defaults @mode to `none`, which makes the object a no-op; .pong
  // deliberately defaults to fold over .clip's -1 to 1 range.
  TEST_CASE("pong: defaults to folding over the -1 to 1 range (#446)") {
    PongRig rig;
    CHECK(rig.Pong(0.5f) == doctest::Approx(0.5f));
    CHECK(rig.Pong(1.5f) == doctest::Approx(0.5f));
    CHECK(rig.Pong(-1.5f) == doctest::Approx(-0.5f));
  }

  TEST_CASE("pong: creation arguments reach the limiting (#446)") {
    PongRig rig;
    rig.op.SetParams("0 127 2");
    CHECK(rig.Pong(64.f) == doctest::Approx(64.f));
    CHECK(rig.Pong(128.f) == doctest::Approx(1.f)); // wrap, not fold
  }

  TEST_CASE("pong: names all three parameters in order (#446)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_PONG));
    REQUIRE(obj != nullptr);
    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 3);
    const std::vector<std::string> expected = {"low", "high", "mode"};
    for (size_t i = 0; i < expected.size(); ++i) {
      CAPTURE(i);
      CHECK(docs[i].name == expected[i]);
    }
  }

  TEST_CASE("pong: params survive a DumpJSON / ParseJSON round trip (#446)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_PONG, "0 127 2") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".pong") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".pong"));
    CHECK(h->GetParams() == std::string("0 127 2"));
  }

  // ─── .pong is not .clip ─────────────────────────────────────────────────────
  // Both keep their output inside a range, but .pong reflects where .clip pins.
  // Asserting the difference guards against one being quietly implemented in
  // terms of the other.

  TEST_CASE("pong: reflects where .clip pins (#446)") {
    PongRig pong;
    pong.Setup(0.f, 10.f, MODE_FOLD);

    YSE::PATCHER::gClip clip;
    FloatSink clipSink;
    clip.ConnectOutlet(clipSink.GetInlet(0), 0);
    clipSink.ConnectInlet(clip.GetOutlet(0), 0);
    clip.SetParams("0 10");

    // Two different values above the top: .clip flattens both onto 10, .pong
    // keeps them apart.
    clip.GetInlet(0)->SetFloat(11.f, YSE::T_GUI);
    CHECK(clipSink.received == doctest::Approx(10.f));
    CHECK(pong.Pong(11.f) == doctest::Approx(9.f));

    clip.GetInlet(0)->SetFloat(13.f, YSE::T_GUI);
    CHECK(clipSink.received == doctest::Approx(10.f));
    CHECK(pong.Pong(13.f) == doctest::Approx(7.f));

    // Inside the range the two agree — the difference is only at the boundary.
    clip.GetInlet(0)->SetFloat(4.f, YSE::T_GUI);
    CHECK(clipSink.received == doctest::Approx(4.f));
    CHECK(pong.Pong(4.f) == doctest::Approx(4.f));
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the port shape, which is what a binding
  // generator keys on.

  TEST_CASE("pong: documents itself as MATH with three labelled inlets (#446)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_PONG));
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
