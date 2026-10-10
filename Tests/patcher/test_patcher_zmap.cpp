// Tests for the clipped range-mapping object (issue #444): .zmap
//
// The always-clipping sibling of .scale. Five inlets in Max's order — value
// (hot), input low, input high, output low, output high — one float outlet,
// and four creation parameters. No exponent and no `clip` parameter: the
// mapping is always linear and always clipped, which is the whole difference
// between the two objects.
//
// Three behaviours are choices rather than ports of Max, so they are pinned
// here rather than left implicit:
//
//   - the incoming value is clamped to the input range *before* the mapping,
//     so an infinity lands on the matching output limit instead of on the 0
//     that MapRange substitutes for a non-finite result,
//   - a degenerate input range (low == high) emits the output low rather than
//     an infinity or a NaN,
//   - a descending output range (outLow > outHigh) clips against the ordered
//     pair, so the mapping stays inside the range either way.
//
// The last block asserts .zmap and .scale agree wherever they should — that is
// the contract of the shared MapRange core in patcher/math/gRangeMap.h.
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
#include "patcher/math/gZmap.h"
#include "patcher/sinks.hpp"

using TestHelpers::FloatSink;

namespace {

  // Holds a .zmap and its sink together, and offers the two ways a patch drives
  // it: through the cold inlets, or through a creation-argument string.
  struct ZmapRig {
    YSE::PATCHER::gZmap op;
    FloatSink sink;

    ZmapRig() {
      op.ConnectOutlet(sink.GetInlet(0), 0);
      sink.ConnectInlet(op.GetOutlet(0), 0);
    }

    // Cold inlets first (they only store), then the hot inlet, which fires.
    void SetRange(float inLow, float inHigh, float outLow, float outHigh) {
      op.GetInlet(1)->SetFloat(inLow, YSE::T_GUI);
      op.GetInlet(2)->SetFloat(inHigh, YSE::T_GUI);
      op.GetInlet(3)->SetFloat(outLow, YSE::T_GUI);
      op.GetInlet(4)->SetFloat(outHigh, YSE::T_GUI);
    }

    float Map(float value) {
      op.GetInlet(0)->SetFloat(value, YSE::T_GUI);
      return sink.received;
    }
  };

  // The same rig for .scale, so the two can be compared directly.
  struct ScaleRef {
    YSE::PATCHER::gScale op;
    FloatSink sink;

    ScaleRef() {
      op.ConnectOutlet(sink.GetInlet(0), 0);
      sink.ConnectInlet(op.GetOutlet(0), 0);
    }

    void SetRange(float inLow, float inHigh, float outLow, float outHigh) {
      op.GetInlet(1)->SetFloat(inLow, YSE::T_GUI);
      op.GetInlet(2)->SetFloat(inHigh, YSE::T_GUI);
      op.GetInlet(3)->SetFloat(outLow, YSE::T_GUI);
      op.GetInlet(4)->SetFloat(outHigh, YSE::T_GUI);
      op.GetInlet(5)->SetFloat(1.f, YSE::T_GUI);
    }

    float Map(float value) {
      op.GetInlet(0)->SetFloat(value, YSE::T_GUI);
      return sink.received;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("zmap: the object is creatable through the registry (#444)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ZMAP);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".zmap"));
    CHECK(h->GetInputs() == 5);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
  }

  TEST_CASE("zmap: the object is listed by pRegistry::AllNames (#444)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".zmap")) != names.end());
  }

  // ─── behaviour: the linear mapping ──────────────────────────────────────────

  TEST_CASE("zmap: maps the input range onto the output range (#444)") {
    ZmapRig rig;
    rig.SetRange(0.f, 127.f, 0.f, 1.f);

    CHECK(rig.Map(0.f) == doctest::Approx(0.f));
    CHECK(rig.Map(127.f) == doctest::Approx(1.f));
    CHECK(rig.Map(63.5f) == doctest::Approx(0.5f));
  }

  TEST_CASE("zmap: maps a normalised source onto an arbitrary range (#444)") {
    ZmapRig rig;
    rig.SetRange(0.f, 1.f, 20.f, 20000.f);

    CHECK(rig.Map(0.f) == doctest::Approx(20.f));
    CHECK(rig.Map(1.f) == doctest::Approx(20000.f));
    CHECK(rig.Map(0.5f) == doctest::Approx(10010.f));
  }

  TEST_CASE("zmap: handles a negative input range (#444)") {
    ZmapRig rig;
    rig.SetRange(-1.f, 1.f, 0.f, 100.f);

    CHECK(rig.Map(-1.f) == doctest::Approx(0.f));
    CHECK(rig.Map(0.f) == doctest::Approx(50.f));
    CHECK(rig.Map(1.f) == doctest::Approx(100.f));
  }

  // ─── behaviour: the clipping, which is the point of the object ──────────────

  TEST_CASE("zmap: clips instead of extrapolating (#444)") {
    ZmapRig rig;
    rig.SetRange(0.f, 1.f, 0.f, 10.f);

    CHECK(rig.Map(0.5f) == doctest::Approx(5.f)); // inside the range is untouched
    CHECK(rig.Map(2.f) == doctest::Approx(10.f));
    CHECK(rig.Map(100.f) == doctest::Approx(10.f));
    CHECK(rig.Map(-1.f) == doctest::Approx(0.f));
    CHECK(rig.Map(-100.f) == doctest::Approx(0.f));
  }

  // A descending mapping is legitimate, and clipping must follow the range
  // rather than assume outLow < outHigh.
  TEST_CASE("zmap: clips an inverted output range against the right limits (#444)") {
    ZmapRig rig;
    rig.SetRange(0.f, 1.f, 10.f, 0.f);

    CHECK(rig.Map(0.f) == doctest::Approx(10.f));
    CHECK(rig.Map(1.f) == doctest::Approx(0.f));
    CHECK(rig.Map(0.25f) == doctest::Approx(7.5f));
    CHECK(rig.Map(2.f) == doctest::Approx(0.f));
    CHECK(rig.Map(-1.f) == doctest::Approx(10.f));
  }

  // An input range given high-to-low still has to clamp against both ends.
  TEST_CASE("zmap: clips an inverted input range (#444)") {
    ZmapRig rig;
    rig.SetRange(1.f, 0.f, 0.f, 10.f);

    CHECK(rig.Map(1.f) == doctest::Approx(0.f));
    CHECK(rig.Map(0.f) == doctest::Approx(10.f));
    CHECK(rig.Map(5.f) == doctest::Approx(0.f)); // above both ends
    CHECK(rig.Map(-5.f) == doctest::Approx(10.f)); // below both ends
  }

  TEST_CASE("zmap: every mapped value stays inside the output range (#444)") {
    ZmapRig rig;
    rig.SetRange(0.f, 1.f, -5.f, 5.f);

    for (float v : {-1000.f, -1.f, 0.f, 0.5f, 1.f, 2.f, 1000.f}) {
      CAPTURE(v);
      const float out = rig.Map(v);
      CHECK(out >= -5.f);
      CHECK(out <= 5.f);
    }
  }

  // ─── behaviour: degenerate and non-finite inputs ────────────────────────────

  TEST_CASE("zmap: a collapsed input range emits the output low, not an infinity (#444)") {
    ZmapRig rig;
    rig.SetRange(5.f, 5.f, 2.f, 8.f);

    for (float v : {0.f, 5.f, 100.f}) {
      CAPTURE(v);
      const float out = rig.Map(v);
      CHECK(std::isfinite(out));
      CHECK(out == doctest::Approx(2.f));
    }
  }

  // The clamp happens before the mapping, so an infinity is just a very
  // out-of-range value and lands on the matching limit.
  TEST_CASE("zmap: an infinity on the hot inlet lands on the matching limit (#444)") {
    ZmapRig rig;
    rig.SetRange(0.f, 1.f, 20.f, 20000.f);

    const float up = rig.Map(std::numeric_limits<float>::infinity());
    CHECK(std::isfinite(up));
    CHECK(up == doctest::Approx(20000.f));

    const float down = rig.Map(-std::numeric_limits<float>::infinity());
    CHECK(std::isfinite(down));
    CHECK(down == doctest::Approx(20.f));
  }

  // A NaN survives the clamp (no ordering to clamp it with), so the non-finite
  // guard in MapRange has to catch it and the output clip pull it into range.
  TEST_CASE("zmap: a NaN on the hot inlet cannot escape (#444)") {
    ZmapRig rig;
    rig.SetRange(0.f, 1.f, 20.f, 20000.f);

    const float out = rig.Map(std::numeric_limits<float>::quiet_NaN());
    CHECK(std::isfinite(out));
    CHECK(out >= 20.f);
    CHECK(out <= 20000.f);
  }

  // ─── inlet semantics ────────────────────────────────────────────────────────

  TEST_CASE("zmap: only inlet 0 fires; inlets 1-4 store silently (#444)") {
    ZmapRig rig;

    for (int i = 1; i <= 4; ++i) {
      CAPTURE(i);
      rig.op.GetInlet(i)->SetFloat(1.f, YSE::T_GUI);
      CHECK_FALSE(rig.sink.gotFloat);
    }

    rig.op.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(rig.sink.gotFloat);
  }

  TEST_CASE("zmap: re-firing the hot inlet re-evaluates against the stored range (#444)") {
    ZmapRig rig;
    rig.SetRange(0.f, 1.f, 0.f, 100.f);
    CHECK(rig.Map(0.5f) == doctest::Approx(50.f));

    // Change only the output high; the rest must survive.
    rig.op.GetInlet(4)->SetFloat(10.f, YSE::T_GUI);
    CHECK(rig.Map(0.5f) == doctest::Approx(5.f));
  }

  TEST_CASE("zmap: every inlet accepts an int (#444)") {
    ZmapRig rig;
    rig.op.GetInlet(1)->SetInt(0, YSE::T_GUI);
    rig.op.GetInlet(2)->SetInt(10, YSE::T_GUI);
    rig.op.GetInlet(3)->SetInt(0, YSE::T_GUI);
    rig.op.GetInlet(4)->SetInt(100, YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotFloat);

    rig.op.GetInlet(0)->SetInt(5, YSE::T_GUI);
    CHECK(rig.sink.gotFloat); // g-family convention is float out, as with .+
    CHECK(rig.sink.received == doctest::Approx(50.f));
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("zmap: defaults to Max's 0-127 onto 0-1, clipped (#444)") {
    ZmapRig rig;
    CHECK(rig.Map(0.f) == doctest::Approx(0.f));
    CHECK(rig.Map(127.f) == doctest::Approx(1.f));
    CHECK(rig.Map(254.f) == doctest::Approx(1.f)); // .scale would give 2
  }

  TEST_CASE("zmap: creation arguments reach the mapping (#444)") {
    ZmapRig rig;
    rig.op.SetParams("0 1 20 20000");
    CHECK(rig.Map(0.5f) == doctest::Approx(10010.f));
    CHECK(rig.Map(3.f) == doctest::Approx(20000.f));
  }

  TEST_CASE("zmap: names all four parameters in order (#444)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_ZMAP));
    REQUIRE(obj != nullptr);
    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 4);
    const std::vector<std::string> expected = {"inLow", "inHigh", "outLow", "outHigh"};
    for (size_t i = 0; i < expected.size(); ++i) {
      CAPTURE(i);
      CHECK(docs[i].name == expected[i]);
    }
  }

  TEST_CASE("zmap: params survive a DumpJSON / ParseJSON round trip (#444)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_ZMAP, "0 1 20 20000") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".zmap") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".zmap"));
    CHECK(h->GetParams() == std::string("0 1 20 20000"));
  }

  // ─── the shared MapRange core ───────────────────────────────────────────────
  // .zmap is meant to be .scale with clipping forced on, so inside the input
  // range the two must be indistinguishable, and outside it .zmap must agree
  // with a .scale that has clip switched on.

  TEST_CASE("zmap: agrees with .scale inside the input range (#444)") {
    ZmapRig zmap;
    ScaleRef scale;
    zmap.SetRange(-1.f, 1.f, 20.f, 20000.f);
    scale.SetRange(-1.f, 1.f, 20.f, 20000.f);

    for (float v : {-1.f, -0.75f, -0.25f, 0.f, 0.3f, 0.5f, 1.f}) {
      CAPTURE(v);
      CHECK(zmap.Map(v) == doctest::Approx(scale.Map(v)));
    }
  }

  TEST_CASE("zmap: agrees with a clipping .scale outside the input range (#444)") {
    ZmapRig zmap;
    ScaleRef scale;
    zmap.SetRange(0.f, 1.f, 0.f, 10.f);
    scale.op.SetParams("0 1 0 10 1 1"); // inLow inHigh outLow outHigh exponent clip

    for (float v : {-50.f, -1.f, 1.5f, 50.f}) {
      CAPTURE(v);
      CHECK(zmap.Map(v) == doctest::Approx(scale.Map(v)));
    }
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the port shape, which is what a binding
  // generator keys on.

  TEST_CASE("zmap: documents itself as MATH with five labelled inlets (#444)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_ZMAP));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
    CHECK_FALSE(obj->GetDescription().empty());
    REQUIRE(obj->NumInputs() == 5);
    const std::vector<std::string> labels = {"value", "inLow", "inHigh", "outLow", "outHigh"};
    for (int i = 0; i < 5; ++i) {
      CAPTURE(i);
      CHECK(obj->GetInlet(i)->GetDocLabel() == labels[(size_t)i]);
    }
    REQUIRE(obj->NumOutputs() == 1);
    CHECK(obj->GetOutputType(0) == YSE::OUT_TYPE::FLOAT);
  }

} // TEST_SUITE("patcher")
