// Tests for the control-rate logarithmic smoother (issue #459): .slide
//
// A one-pole filter on control values, following Max's difference equation
//
//     y[n] = y[n-1] + (x[n] - y[n-1]) / slide
//
// with independent up and down amounts. Three inlets in Max's order — value
// (hot), slideUp, slideDown — one float outlet, two creation parameters.
//
// What is asserted here is the *filter*, not merely that the output moved:
// the step response is pinned against hand-computed values for a known
// slide-up and slide-down pair, an amount of 1 passes through exactly, the
// two amounts are shown to be independently selected by the direction of
// travel, and the value is shown to actually arrive at its target rather than
// stalling a rounding error short of it.
//
// Three behaviours are choices rather than ports of Max, which documents only
// the amounts 1 and 10, so they are pinned here rather than left implicit:
//
//   - an amount below 1 (including 0 and negatives) reads as 1, since below 1
//     the recursion overshoots and rings instead of smoothing,
//   - the running value snaps onto the target once it is within one part in a
//     million of it — that is what "reached" means for this object,
//   - a non-finite input reads as 0, the convention ./ , .sqrt, .zmap and
//     .clip already use, which is also what keeps an infinity from poisoning
//     the running value permanently.
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
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/gSlide.h"
#include "patcher/sinks.hpp"

using TestHelpers::FloatSink;
using YSE::PATCHER::patcherImplementation;

namespace {

  // Holds a .slide and its sink together, and offers the ways a patch drives
  // it: the cold inlets, the hot inlet, a bang, and the two word messages.
  struct SlideRig {
    YSE::PATCHER::gSlide op;
    FloatSink sink;

    SlideRig() {
      op.ConnectOutlet(sink.GetInlet(0), 0);
      sink.ConnectInlet(op.GetOutlet(0), 0);
    }

    void SetAmounts(float up, float down) {
      op.GetInlet(1)->SetFloat(up, YSE::T_GUI);
      op.GetInlet(2)->SetFloat(down, YSE::T_GUI);
    }

    // One filter step against `value`; returns what came out.
    float Feed(float value) {
      op.GetInlet(0)->SetFloat(value, YSE::T_GUI);
      return sink.received;
    }

    float Bang() {
      op.GetInlet(0)->SetBang(YSE::T_GUI);
      return sink.received;
    }

    void Message(const std::string& text) {
      op.GetInlet(0)->SetList(text, YSE::T_GUI);
    }

    // Feeds `value` until the output stops moving or `limit` steps have gone
    // by; returns how many steps it took. Used for the convergence cases.
    int FeedUntilSettled(float value, int limit) {
      for (int i = 1; i <= limit; ++i) {
        if (Feed(value) == value) return i;
      }
      return -1;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("slide: the object is creatable through the registry (#459)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_SLIDE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".slide"));
    CHECK(h->GetInputs() == 3);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
  }

  TEST_CASE("slide: the object is listed by pRegistry::AllNames (#459)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".slide")) != names.end());
  }

  // The names are one character apart and the GUI slider got there first.
  TEST_CASE("slide: does not shadow the .slider GUI object (#459)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".slider")) != names.end());
    CHECK(std::string(YSE::OBJ::G_SLIDE) != std::string(YSE::OBJ::G_SLIDER));
  }

  // ─── the step response, against hand-computed values ────────────────────────
  // These are the numbers y[n] = y[n-1] + (x[n] - y[n-1]) / slide gives from a
  // running value of 0, worked out by hand. They are exact in binary, so they
  // are checked as such rather than fudged with a loose tolerance.

  TEST_CASE("slide: sliding up follows the documented equation (#459)") {
    SlideRig rig;
    rig.SetAmounts(2.f, 2.f);

    // 0 -> 0.5 -> 0.75 -> 0.875 -> 0.9375 -> 0.96875, each step halving the
    // remaining distance to 1.
    CHECK(rig.Feed(1.f) == doctest::Approx(0.5f));
    CHECK(rig.Feed(1.f) == doctest::Approx(0.75f));
    CHECK(rig.Feed(1.f) == doctest::Approx(0.875f));
    CHECK(rig.Feed(1.f) == doctest::Approx(0.9375f));
    CHECK(rig.Feed(1.f) == doctest::Approx(0.96875f));
  }

  TEST_CASE("slide: a larger amount moves proportionally less per step (#459)") {
    SlideRig rig;
    rig.SetAmounts(4.f, 4.f);

    // A quarter of the remaining distance each time: 0.25, 0.4375, 0.578125,
    // 0.68359375 — Max's "a slide value of 10 changes 1/10th as quickly",
    // stated at 4 where the values stay exact.
    CHECK(rig.Feed(1.f) == doctest::Approx(0.25f));
    CHECK(rig.Feed(1.f) == doctest::Approx(0.4375f));
    CHECK(rig.Feed(1.f) == doctest::Approx(0.578125f));
    CHECK(rig.Feed(1.f) == doctest::Approx(0.68359375f));
  }

  TEST_CASE("slide: sliding down follows the same equation (#459)") {
    SlideRig rig;

    // Load the running value with 10 through the pass-through amount, then
    // slide down to 0 with an amount of 5: 8, 6.4, 5.12, 4.096.
    rig.SetAmounts(1.f, 1.f);
    REQUIRE(rig.Feed(10.f) == doctest::Approx(10.f));

    rig.SetAmounts(1.f, 5.f);
    CHECK(rig.Feed(0.f) == doctest::Approx(8.f));
    CHECK(rig.Feed(0.f) == doctest::Approx(6.4f));
    CHECK(rig.Feed(0.f) == doctest::Approx(5.12f));
    CHECK(rig.Feed(0.f) == doctest::Approx(4.096f));
  }

  // ─── an amount of 1 is the pass-through case ────────────────────────────────

  TEST_CASE("slide: an amount of 1 passes the input straight through (#459)") {
    SlideRig rig;
    rig.SetAmounts(1.f, 1.f);

    // Max: "Given a slide value of 1, the output will therefore always equal
    // the input." Exact equality, not an approximation — a value that has
    // arrived must compare equal to what was sent.
    for (float v : {0.3f, -2.f, 127.f, 0.f, 20000.f, -0.001f}) {
      CAPTURE(v);
      CHECK(rig.Feed(v) == v);
    }
  }

  TEST_CASE("slide: 1 is the default, so an unconfigured .slide is transparent (#459)") {
    SlideRig rig;
    CHECK(rig.Feed(0.75f) == 0.75f);
    CHECK(rig.Feed(-30.f) == -30.f);
  }

  // Below 1 the error is multiplied by more than one per step, so the value
  // would overshoot and ring. Everything below 1 reads as 1 instead.
  TEST_CASE("slide: an amount below 1 reads as 1 rather than overshooting (#459)") {
    for (float amount : {0.5f, 0.f, -4.f, -0.001f}) {
      CAPTURE(amount);
      SlideRig rig;
      rig.SetAmounts(amount, amount);
      CHECK(rig.Feed(1.f) == 1.f);
      CHECK(rig.Feed(-1.f) == -1.f);
    }
  }

  TEST_CASE("slide: a NaN amount reads as 1 too (#459)") {
    SlideRig rig;
    rig.SetAmounts(std::numeric_limits<float>::quiet_NaN(),
                   std::numeric_limits<float>::quiet_NaN());
    CHECK(rig.Feed(5.f) == 5.f);
  }

  // ─── the two amounts are independent ────────────────────────────────────────

  TEST_CASE("slide: the direction of travel picks the amount (#459)") {
    SlideRig rig;
    rig.SetAmounts(2.f, 10.f); // fast rise, slow fall

    // Up uses slideUp: half the distance from 0 to 1.
    CHECK(rig.Feed(1.f) == doctest::Approx(0.5f));
    // Down uses slideDown: a tenth of the distance from 0.5 to 0.
    CHECK(rig.Feed(0.f) == doctest::Approx(0.45f));
    // And back up again on slideUp: half of 0.45 -> 1.
    CHECK(rig.Feed(1.f) == doctest::Approx(0.725f));
  }

  TEST_CASE("slide: a fast fall with a slow rise is the mirror image (#459)") {
    SlideRig rig;
    rig.SetAmounts(10.f, 2.f);

    CHECK(rig.Feed(1.f) == doctest::Approx(0.1f));
    CHECK(rig.Feed(0.f) == doctest::Approx(0.05f));
  }

  TEST_CASE("slide: the output never overshoots the input (#459)") {
    SlideRig rig;
    rig.SetAmounts(3.f, 7.f);

    float previous = 0.f;
    for (int i = 0; i < 40; ++i) {
      const float out = rig.Feed(1.f);
      CHECK(out >= previous);
      CHECK(out <= 1.f);
      previous = out;
    }
    for (int i = 0; i < 80; ++i) {
      const float out = rig.Feed(-1.f);
      CHECK(out <= previous);
      CHECK(out >= -1.f);
      previous = out;
    }
  }

  // ─── convergence: "reached" means within 1e-6 relative ──────────────────────
  // The recursion only ever approaches its target, and in floating point it
  // stops moving a hair short of it, so the object defines arrival instead of
  // waiting for it. These cases assert both halves: that it does not snap
  // early, and that it does eventually land on the target exactly.

  TEST_CASE("slide: does not snap to the target early (#459)") {
    SlideRig rig;
    rig.SetAmounts(10.f, 10.f);

    for (int i = 0; i < 10; ++i) {
      const float out = rig.Feed(1.f);
      CHECK(out < 1.f); // still smoothing, nowhere near the tolerance
    }
  }

  TEST_CASE("slide: reaches the target exactly rather than stalling short (#459)") {
    SlideRig rig;
    rig.SetAmounts(10.f, 10.f);

    // 0.9^n <= 1e-6 needs 132 steps; anything under 500 proves it arrived
    // rather than merely got close and stopped moving.
    const int steps = rig.FeedUntilSettled(1.f, 500);
    CAPTURE(steps);
    CHECK(steps > 100); // it really smoothed on the way
    CHECK(steps < 500); // and it really arrived
  }

  TEST_CASE("slide: reaches a target of zero on the way down (#459)") {
    SlideRig rig;
    rig.SetAmounts(1.f, 1.f);
    REQUIRE(rig.Feed(10.f) == 10.f);

    rig.SetAmounts(10.f, 10.f);
    const int steps = rig.FeedUntilSettled(0.f, 500);
    CAPTURE(steps);
    CHECK(steps > 100);
    CHECK(steps < 500);
  }

  // A large amount is what a float running value cannot survive: the step
  // becomes smaller than an ulp long before the tolerance is met, and the
  // value stalls forever. The running value is a double for exactly this.
  TEST_CASE("slide: a large amount still reaches the target (#459)") {
    SlideRig rig;
    rig.SetAmounts(1000.f, 1000.f);

    // 0.999^n <= 1e-6 needs about 13800 steps.
    const int steps = rig.FeedUntilSettled(1.f, 40000);
    CAPTURE(steps);
    CHECK(steps > 5000);
    CHECK(steps < 40000);
  }

  TEST_CASE("slide: a large target reaches too, the tolerance being relative (#459)") {
    SlideRig rig;
    rig.SetAmounts(20.f, 20.f);

    const int steps = rig.FeedUntilSettled(20000.f, 2000);
    CAPTURE(steps);
    CHECK(steps > 100);
    CHECK(steps < 2000);
  }

  TEST_CASE("slide: repeating a value that has arrived is idempotent (#459)") {
    SlideRig rig;
    rig.SetAmounts(4.f, 4.f);
    REQUIRE(rig.FeedUntilSettled(2.5f, 500) > 0);

    for (int i = 0; i < 20; ++i)
      CHECK(rig.Feed(2.5f) == 2.5f);
  }

  // ─── bang, set, reset ───────────────────────────────────────────────────────

  TEST_CASE("slide: a bang re-runs the filter on the last value (#459)") {
    SlideRig rig;
    rig.SetAmounts(2.f, 2.f);
    REQUIRE(rig.Feed(1.f) == doctest::Approx(0.5f));

    // Max: "Performs the same function as float using the last input value."
    // The running value has moved, so the bang takes the next step.
    CHECK(rig.Bang() == doctest::Approx(0.75f));
    CHECK(rig.Bang() == doctest::Approx(0.875f));
  }

  TEST_CASE("slide: 'set' stages a value without emitting (#459)") {
    SlideRig rig;
    rig.SetAmounts(2.f, 2.f);

    rig.Message("set 1");
    CHECK_FALSE(rig.sink.gotFloat); // no output, which is the whole point

    // ... and a following bang is exactly the float that was not sent.
    CHECK(rig.Bang() == doctest::Approx(0.5f));
  }

  TEST_CASE("slide: 'set' accepts a negative and a fractional value (#459)") {
    SlideRig rig;
    rig.SetAmounts(2.f, 2.f);

    rig.Message("set -0.5");
    CHECK_FALSE(rig.sink.gotFloat);
    CHECK(rig.Bang() == doctest::Approx(-0.25f));
  }

  TEST_CASE("slide: 'reset' zeroes the running value without emitting (#459)") {
    SlideRig rig;
    rig.SetAmounts(1.f, 1.f);
    REQUIRE(rig.Feed(8.f) == 8.f);
    rig.sink.gotFloat = false;

    rig.Message("reset");
    CHECK_FALSE(rig.sink.gotFloat);

    // The staged input survives the reset, so the next bang slides from 0
    // back towards 8 — with an amount of 1 that is the whole way.
    CHECK(rig.Bang() == doctest::Approx(8.f));
  }

  TEST_CASE("slide: 'reset' really moved the running value (#459)") {
    SlideRig rig;
    rig.SetAmounts(1.f, 1.f);
    REQUIRE(rig.Feed(8.f) == 8.f);

    rig.SetAmounts(2.f, 2.f);
    rig.Message("reset");
    // From 0 towards the staged 8, halfway.
    CHECK(rig.Bang() == doctest::Approx(4.f));
  }

  TEST_CASE("slide: an unknown word message is ignored (#459)") {
    SlideRig rig;
    rig.SetAmounts(2.f, 2.f);
    REQUIRE(rig.Feed(1.f) == doctest::Approx(0.5f));

    rig.Message("wobble 3");
    rig.Message("set");
    rig.Message("");
    CHECK(rig.Bang() == doctest::Approx(0.75f)); // state untouched
  }

  // ─── non-finite input ───────────────────────────────────────────────────────

  TEST_CASE("slide: a NaN input reads as 0 (#459)") {
    SlideRig rig;
    rig.SetAmounts(2.f, 2.f);
    REQUIRE(rig.Feed(1.f) == doctest::Approx(0.5f));

    const float out = rig.Feed(std::numeric_limits<float>::quiet_NaN());
    CHECK(std::isfinite(out));
    CHECK(out == doctest::Approx(0.25f)); // halfway from 0.5 down to 0
  }

  // An infinity is the dangerous one: left alone it would pin the running
  // value at infinity, and the next finite input would then compute
  // inf + (x - inf) = NaN and poison the object for good.
  TEST_CASE("slide: an infinity cannot poison the running value (#459)") {
    SlideRig rig;
    rig.SetAmounts(2.f, 2.f);

    CHECK(std::isfinite(rig.Feed(std::numeric_limits<float>::infinity())));
    CHECK(std::isfinite(rig.Feed(-std::numeric_limits<float>::infinity())));

    // Still a working smoother afterwards.
    CHECK(rig.Feed(1.f) == doctest::Approx(0.5f));
    CHECK(rig.Feed(1.f) == doctest::Approx(0.75f));
  }

  // ─── inlet semantics ────────────────────────────────────────────────────────

  TEST_CASE("slide: only inlet 0 fires; inlets 1-2 store silently (#459)") {
    SlideRig rig;

    for (int i = 1; i <= 2; ++i) {
      CAPTURE(i);
      rig.op.GetInlet(i)->SetFloat(4.f, YSE::T_GUI);
      CHECK_FALSE(rig.sink.gotFloat);
    }

    rig.op.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    CHECK(rig.sink.gotFloat);
    CHECK(rig.sink.received == doctest::Approx(0.25f));
  }

  TEST_CASE("slide: every inlet accepts an int (#459)") {
    SlideRig rig;
    rig.op.GetInlet(1)->SetInt(2, YSE::T_GUI);
    rig.op.GetInlet(2)->SetInt(2, YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotFloat);

    rig.op.GetInlet(0)->SetInt(10, YSE::T_GUI);
    CHECK(rig.sink.gotFloat); // g-family convention is float out, as with .+
    CHECK(rig.sink.received == doctest::Approx(5.f));
  }

  TEST_CASE("slide: a bang on a cold inlet does nothing (#459)") {
    SlideRig rig;
    rig.SetAmounts(2.f, 2.f);
    rig.op.GetInlet(1)->SetBang(YSE::T_GUI);
    rig.op.GetInlet(2)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotFloat);
  }

  TEST_CASE("slide: a list on a cold inlet does nothing (#459)") {
    SlideRig rig;
    rig.SetAmounts(1.f, 1.f);
    REQUIRE(rig.Feed(6.f) == 6.f);

    rig.op.GetInlet(1)->SetList("reset", YSE::T_GUI);
    CHECK(rig.Bang() == doctest::Approx(6.f)); // running value untouched
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("slide: creation arguments reach the filter (#459)") {
    SlideRig rig;
    rig.op.SetParams("2 10");
    CHECK(rig.Feed(1.f) == doctest::Approx(0.5f)); // slideUp = 2
    CHECK(rig.Feed(0.f) == doctest::Approx(0.45f)); // slideDown = 10
  }

  TEST_CASE("slide: names both parameters in order (#459)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_SLIDE));
    REQUIRE(obj != nullptr);
    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 2);
    const std::vector<std::string> expected = {"slideUp", "slideDown"};
    for (size_t i = 0; i < expected.size(); ++i) {
      CAPTURE(i);
      CHECK(docs[i].name == expected[i]);
    }
  }

  TEST_CASE("slide: params survive a DumpJSON / ParseJSON round trip (#459)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_SLIDE, "2 10") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".slide") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".slide"));
    CHECK(h->GetParams() == std::string("2 10"));
  }

  // The state lives in the object, so a live re-parse must patch the two
  // amounts in place rather than build a replacement — otherwise editing the
  // slide time restarts the smoother and the value it was driving jumps.
  TEST_CASE("slide: takes the in-place scalar route on a live SetParams (#459)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_SLIDE));
    REQUIRE(obj != nullptr);
    CHECK_FALSE(obj->ParamsNeedRebuild());
  }

  TEST_CASE("slide: the running value survives a live SetParams (#459)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_SLIDE, "2 2");
    REQUIRE(h != nullptr);

    h->SetFloatData(0, 1.f);
    REQUIRE(std::stof(h->GetGuiValue()) == doctest::Approx(0.5f));

    // A scalar re-parse queues a plan; nothing is replaced or retired.
    const std::size_t retiredBefore = p.PendingRetired();
    const unsigned int idBefore = h->GetID();
    h->SetParams("4 4");
    CHECK(p.PendingRetired() == retiredBefore);
    CHECK(h->GetID() == idBefore);

    p.Calculate(YSE::T_DSP); // the audio thread applies the plan
    CHECK(std::stof(h->GetGuiValue()) == doctest::Approx(0.5f)); // state kept

    // ... and the new amount is what the next step uses: a quarter of the
    // remaining distance from 0.5 to 1.
    h->SetFloatData(0, 1.f);
    CHECK(std::stof(h->GetGuiValue()) == doctest::Approx(0.625f));
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the port shape, which is what a binding
  // generator keys on.

  TEST_CASE("slide: documents itself as MATH with three labelled inlets (#459)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_SLIDE));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
    CHECK_FALSE(obj->GetDescription().empty());
    REQUIRE(obj->NumInputs() == 3);
    const std::vector<std::string> labels = {"value", "slideUp", "slideDown"};
    for (int i = 0; i < 3; ++i) {
      CAPTURE(i);
      CHECK(obj->GetInlet(i)->GetDocLabel() == labels[(size_t)i]);
    }
    REQUIRE(obj->NumOutputs() == 1);
    CHECK(obj->GetOutputType(0) == YSE::OUT_TYPE::FLOAT);
  }

  TEST_CASE("slide: inlet 0 accepts float, int, bang and list (#459)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_SLIDE));
    REQUIRE(obj != nullptr);
    const unsigned int types = obj->GetInlet(0)->GetAcceptedTypes();
    CHECK((types & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((types & YSE::PATCHER::IT_INT) != 0);
    CHECK((types & YSE::PATCHER::IT_BANG) != 0);
    CHECK((types & YSE::PATCHER::IT_LIST) != 0);
  }

} // TEST_SUITE("patcher")
