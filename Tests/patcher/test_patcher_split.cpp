// Tests for the control-rate range-routing object (issue #448): .split
//
// Three inlets in Max's order — value (hot), low, high — two float outlets, and
// two creation parameters. The value is never modified: it leaves outlet 0 when
// it falls inside [low, high] and outlet 1 when it does not, and exactly one of
// the two fires per evaluation. That pass-through-and-route behaviour is what
// separates .split from the rest of the range family (.clip, .pong, .scale,
// .zmap all reshape the value and always fire their single outlet), so the last
// block asserts the difference against .clip directly.
//
// Both bounds are inclusive, as in Max. Three behaviours are choices rather
// than ports of Max and are pinned here rather than left implicit:
//
//   - the limits are used as an *ordered* pair, so a range given high-to-low
//     still splits on the right two numbers,
//   - the defaults are 0 and 127 (the MIDI range the object's use cases live
//     in) rather than Max's zero-initialised pair,
//   - nothing is computed and so nothing is substituted: a non-finite value is
//     routed rather than replaced, and a NaN — which fails both comparisons —
//     leaves the out-of-range outlet.
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
#include "patcher/math/gSplit.h"
#include "patcher/sinks.hpp"

using TestHelpers::FloatSink;

namespace {

  // Holds a .split and both of its sinks alive together, and offers the two
  // ways a patch drives it: through the cold inlets, or through a
  // creation-argument string.
  struct SplitRig {
    YSE::PATCHER::gSplit op;
    FloatSink inside; // outlet 0 — value fell inside the range
    FloatSink outside; // outlet 1 — value fell outside it

    SplitRig() {
      op.ConnectOutlet(inside.GetInlet(0), 0);
      inside.ConnectInlet(op.GetOutlet(0), 0);
      op.ConnectOutlet(outside.GetInlet(0), 1);
      outside.ConnectInlet(op.GetOutlet(1), 0);
    }

    // Cold inlets first (they only store), then the hot inlet, which fires.
    void SetRange(float low, float high) {
      op.GetInlet(1)->SetFloat(low, YSE::T_GUI);
      op.GetInlet(2)->SetFloat(high, YSE::T_GUI);
    }

    void Reset() {
      inside.gotFloat = false;
      outside.gotFloat = false;
      inside.received = 0.f;
      outside.received = 0.f;
    }

    // Fires the hot inlet after clearing both sinks, so the flags describe this
    // evaluation alone.
    void Feed(float value) {
      Reset();
      op.GetInlet(0)->SetFloat(value, YSE::T_GUI);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("split: the object is creatable through the registry (#448)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_SPLIT);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".split"));
    CHECK(h->GetInputs() == 3);
    CHECK(h->GetOutputs() == 2);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::FLOAT);
  }

  TEST_CASE("split: the object is listed by pRegistry::AllNames (#448)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".split")) != names.end());
  }

  // ─── the split ──────────────────────────────────────────────────────────────

  TEST_CASE("split: a value inside the range leaves the left outlet (#448)") {
    SplitRig rig;
    rig.SetRange(60.f, 71.f);

    for (float v : {60.5f, 64.f, 70.f}) {
      CAPTURE(v);
      rig.Feed(v);
      CHECK(rig.inside.gotFloat);
      CHECK_FALSE(rig.outside.gotFloat);
      CHECK(rig.inside.received == doctest::Approx(v));
    }
  }

  TEST_CASE("split: a value outside the range leaves the right outlet (#448)") {
    SplitRig rig;
    rig.SetRange(60.f, 71.f);

    for (float v : {-1000.f, 0.f, 59.9f, 71.1f, 1000.f}) {
      CAPTURE(v);
      rig.Feed(v);
      CHECK(rig.outside.gotFloat);
      CHECK_FALSE(rig.inside.gotFloat);
      CHECK(rig.outside.received == doctest::Approx(v));
    }
  }

  // Max's wording: "greater than or equal to the specified minimum, and ... less
  // than or equal to the specified maximum". Both ends belong to the range.
  TEST_CASE("split: both bounds are inclusive (#448)") {
    SplitRig rig;
    rig.SetRange(60.f, 71.f);

    rig.Feed(60.f);
    CHECK(rig.inside.gotFloat);
    CHECK(rig.inside.received == doctest::Approx(60.f));

    rig.Feed(71.f);
    CHECK(rig.inside.gotFloat);
    CHECK(rig.inside.received == doctest::Approx(71.f));
  }

  // The defining property: .split decides *where* a value goes, never *what* it
  // is. Nothing is clipped, rounded or rescaled on either branch.
  TEST_CASE("split: the value passes through both branches unchanged (#448)") {
    SplitRig rig;
    rig.SetRange(-1.f, 1.f);

    rig.Feed(0.123456f);
    CHECK(rig.inside.received == doctest::Approx(0.123456f));

    rig.Feed(1234.5f);
    CHECK(rig.outside.received == doctest::Approx(1234.5f));

    rig.Feed(-1e-8f);
    CHECK(rig.inside.received == doctest::Approx(-1e-8f));
  }

  TEST_CASE("split: exactly one outlet fires per evaluation (#448)") {
    SplitRig rig;
    rig.SetRange(0.f, 10.f);

    for (float v : {-5.f, 0.f, 5.f, 10.f, 15.f}) {
      CAPTURE(v);
      rig.Feed(v);
      // Never both, and never neither.
      CHECK(rig.inside.gotFloat != rig.outside.gotFloat);
    }
  }

  // ─── range edges ────────────────────────────────────────────────────────────

  // The choice .clip, .pong, .scale and .zmap all make. Max would send
  // everything right for a range it considers empty.
  TEST_CASE("split: a reversed range is treated as an ordered pair (#448)") {
    SplitRig rig;
    rig.SetRange(71.f, 60.f);

    rig.Feed(64.f);
    CHECK(rig.inside.gotFloat);
    CHECK(rig.inside.received == doctest::Approx(64.f));

    rig.Feed(80.f);
    CHECK(rig.outside.gotFloat);
  }

  TEST_CASE("split: a collapsed range matches only that one value (#448)") {
    SplitRig rig;
    rig.SetRange(5.f, 5.f);

    rig.Feed(5.f);
    CHECK(rig.inside.gotFloat);
    CHECK(rig.inside.received == doctest::Approx(5.f));

    for (float v : {4.999f, 5.001f, 0.f}) {
      CAPTURE(v);
      rig.Feed(v);
      CHECK(rig.outside.gotFloat);
    }
  }

  // ─── non-finite values ──────────────────────────────────────────────────────

  // A NaN fails both comparisons, so it takes the reject branch — which is the
  // right answer for a router: a value that cannot be shown to be inside the
  // range is outside it. It is routed, not substituted, so the reject branch
  // stays the single place a patch has to guard.
  TEST_CASE("split: a NaN leaves the out-of-range outlet (#448)") {
    SplitRig rig;
    rig.SetRange(0.f, 127.f);

    rig.Feed(std::numeric_limits<float>::quiet_NaN());
    CHECK(rig.outside.gotFloat);
    CHECK_FALSE(rig.inside.gotFloat);
    CHECK(std::isnan(rig.outside.received));
  }

  TEST_CASE("split: a NaN limit sends everything out-of-range (#448)") {
    SplitRig rig;
    rig.SetRange(std::numeric_limits<float>::quiet_NaN(), 127.f);

    for (float v : {-1.f, 0.f, 64.f, 200.f}) {
      CAPTURE(v);
      rig.Feed(v);
      CHECK(rig.outside.gotFloat);
      CHECK_FALSE(rig.inside.gotFloat);
    }
  }

  TEST_CASE("split: infinities route by sign and pass through unchanged (#448)") {
    SplitRig rig;
    rig.SetRange(0.f, 127.f);

    rig.Feed(std::numeric_limits<float>::infinity());
    CHECK(rig.outside.gotFloat);
    CHECK(rig.outside.received == std::numeric_limits<float>::infinity());

    rig.Feed(-std::numeric_limits<float>::infinity());
    CHECK(rig.outside.gotFloat);
    CHECK(rig.outside.received == -std::numeric_limits<float>::infinity());
  }

  // An unbounded range is legal and useful: it turns .split into a sign test.
  TEST_CASE("split: an infinite limit widens the range (#448)") {
    SplitRig rig;
    rig.SetRange(0.f, std::numeric_limits<float>::infinity());

    rig.Feed(1e30f);
    CHECK(rig.inside.gotFloat);

    rig.Feed(-0.001f);
    CHECK(rig.outside.gotFloat);
  }

  // ─── inlet semantics ────────────────────────────────────────────────────────

  TEST_CASE("split: only inlet 0 fires; inlets 1 and 2 store silently (#448)") {
    SplitRig rig;
    rig.Reset();

    // A range of [0, 10], set one cold inlet at a time — neither may emit.
    const float limits[] = {0.f, 10.f};
    for (int i = 1; i <= 2; ++i) {
      CAPTURE(i);
      rig.op.GetInlet(i)->SetFloat(limits[i - 1], YSE::T_GUI);
      CHECK_FALSE(rig.inside.gotFloat);
      CHECK_FALSE(rig.outside.gotFloat);
    }

    rig.op.GetInlet(0)->SetFloat(5.f, YSE::T_GUI);
    CHECK(rig.inside.gotFloat);
  }

  TEST_CASE("split: re-firing the hot inlet re-evaluates against the stored range (#448)") {
    SplitRig rig;
    rig.SetRange(0.f, 10.f);
    rig.Feed(20.f);
    CHECK(rig.outside.gotFloat);

    // Move only the upper limit; the lower one must survive.
    rig.op.GetInlet(2)->SetFloat(30.f, YSE::T_GUI);
    rig.Feed(20.f);
    CHECK(rig.inside.gotFloat);

    rig.Feed(-1.f);
    CHECK(rig.outside.gotFloat);
  }

  TEST_CASE("split: every inlet accepts an int and the outlets stay float (#448)") {
    SplitRig rig;
    rig.op.GetInlet(1)->SetInt(60, YSE::T_GUI);
    rig.op.GetInlet(2)->SetInt(71, YSE::T_GUI);
    rig.Reset();

    rig.op.GetInlet(0)->SetInt(64, YSE::T_GUI);
    // g-family convention is float out, as with .+ — the int is widened, not
    // forwarded as an int the way Max's typed split would.
    CHECK(rig.inside.gotFloat);
    CHECK(rig.inside.received == doctest::Approx(64.f));

    rig.Reset();
    rig.op.GetInlet(0)->SetInt(72, YSE::T_GUI);
    CHECK(rig.outside.gotFloat);
    CHECK(rig.outside.received == doctest::Approx(72.f));
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  // Max leaves both arguments zero-initialised, which makes a fresh object route
  // all but a single value to the same outlet. 0-127 is the MIDI range the
  // object's own use cases live in, and .linedrive's default input range.
  TEST_CASE("split: defaults to the 0-127 MIDI range (#448)") {
    SplitRig rig;

    for (float v : {0.f, 64.f, 127.f}) {
      CAPTURE(v);
      rig.Feed(v);
      CHECK(rig.inside.gotFloat);
    }
    for (float v : {-1.f, 127.5f, 200.f}) {
      CAPTURE(v);
      rig.Feed(v);
      CHECK(rig.outside.gotFloat);
    }
  }

  TEST_CASE("split: creation arguments reach the range (#448)") {
    SplitRig rig;
    rig.op.SetParams("60 71");

    rig.Feed(64.f);
    CHECK(rig.inside.gotFloat);

    rig.Feed(59.f);
    CHECK(rig.outside.gotFloat);

    rig.Feed(72.f);
    CHECK(rig.outside.gotFloat);
  }

  TEST_CASE("split: names both parameters in Max's argument order (#448)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_SPLIT));
    REQUIRE(obj != nullptr);
    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 2);
    CHECK(docs[0].name == "low");
    CHECK(docs[1].name == "high");
  }

  TEST_CASE("split: params survive a DumpJSON / ParseJSON round trip (#448)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_SPLIT, "60 71") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".split") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".split"));
    CHECK(h->GetParams() == std::string("60 71"));
  }

  // ─── zone chaining ──────────────────────────────────────────────────────────
  // The use case from the issue: the reject outlet of one .split feeds the next,
  // so a chain of them dispatches a keyboard into zones with no external logic.

  TEST_CASE("split: chains through the reject outlet into a second zone (#448)") {
    YSE::PATCHER::gSplit low;
    YSE::PATCHER::gSplit high;
    FloatSink lowZone;
    FloatSink highZone;
    FloatSink rest;

    low.ConnectOutlet(lowZone.GetInlet(0), 0);
    lowZone.ConnectInlet(low.GetOutlet(0), 0);
    low.ConnectOutlet(high.GetInlet(0), 1);
    high.ConnectInlet(low.GetOutlet(1), 0);

    high.ConnectOutlet(highZone.GetInlet(0), 0);
    highZone.ConnectInlet(high.GetOutlet(0), 0);
    high.ConnectOutlet(rest.GetInlet(0), 1);
    rest.ConnectInlet(high.GetOutlet(1), 0);

    low.SetParams("0 59"); // everything below middle C
    high.SetParams("60 71"); // the octave above it

    low.GetInlet(0)->SetFloat(48.f, YSE::T_GUI);
    CHECK(lowZone.gotFloat);
    CHECK(lowZone.received == doctest::Approx(48.f));
    CHECK_FALSE(highZone.gotFloat);
    CHECK_FALSE(rest.gotFloat);

    low.GetInlet(0)->SetFloat(64.f, YSE::T_GUI);
    CHECK(highZone.gotFloat);
    CHECK(highZone.received == doctest::Approx(64.f));
    CHECK_FALSE(rest.gotFloat);

    low.GetInlet(0)->SetFloat(96.f, YSE::T_GUI);
    CHECK(rest.gotFloat);
    CHECK(rest.received == doctest::Approx(96.f));
  }

  // ─── .split is not .clip ────────────────────────────────────────────────────
  // Both take a value and a [low, high] pair. .clip bends an out-of-range value
  // onto the boundary and always emits it; .split leaves it alone and sends it
  // elsewhere. Asserting the difference guards against one being quietly
  // implemented in terms of the other.

  TEST_CASE("split: routes where .clip pins (#448)") {
    SplitRig rig;
    rig.SetRange(0.f, 10.f);

    YSE::PATCHER::gClip clip;
    FloatSink clipSink;
    clip.ConnectOutlet(clipSink.GetInlet(0), 0);
    clipSink.ConnectInlet(clip.GetOutlet(0), 0);
    clip.SetParams("0 10");

    clip.GetInlet(0)->SetFloat(25.f, YSE::T_GUI);
    CHECK(clipSink.received == doctest::Approx(10.f)); // pinned to the boundary

    rig.Feed(25.f);
    CHECK_FALSE(rig.inside.gotFloat); // never on the in-range outlet
    CHECK(rig.outside.received == doctest::Approx(25.f)); // and never modified
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the port shape, which is what a binding
  // generator keys on.

  TEST_CASE("split: documents itself as MATH with three inlets and two outlets (#448)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_SPLIT));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
    CHECK_FALSE(obj->GetDescription().empty());

    REQUIRE(obj->NumInputs() == 3);
    const std::vector<std::string> labels = {"value", "low", "high"};
    for (int i = 0; i < 3; ++i) {
      CAPTURE(i);
      CHECK(obj->GetInlet(i)->GetDocLabel() == labels[(size_t)i]);
    }

    REQUIRE(obj->NumOutputs() == 2);
    CHECK(obj->GetOutputType(0) == YSE::OUT_TYPE::FLOAT);
    CHECK(obj->GetOutputType(1) == YSE::OUT_TYPE::FLOAT);
    CHECK(obj->GetOutlet(0)->GetDocLabel() == std::string("in"));
    CHECK(obj->GetOutlet(1)->GetDocLabel() == std::string("out"));
  }

} // TEST_SUITE("patcher")
