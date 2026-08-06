// Tests for .past (issue #464) — bang once when a threshold is crossed.
//
// One inlet, one bang outlet. The object is trivial to implement wrongly in a
// way that looks right on a rising ramp and is wrong everywhere else, so this
// file exists to pin the edge behaviour rather than the comparison:
//
//   - **it fires once per crossing**, not on every message that happens to be
//     above the line. A .>= into a .sel 1 does the latter and is not this
//     object.
//   - **equality is on the "past it" side going up**: a value exactly at the
//     threshold has crossed it and bangs. Max's Arguments, `set` and Output
//     sections all say "equaled or exceeded"; only its int/float message entry
//     says "greater than", and the inclusive reading is the one that makes the
//     re-arm rule fit.
//   - **equality is on the "past it" side coming down too**: "goes back below
//     (is less than) its argument" is strict, so returning to exactly the
//     threshold does NOT re-arm. 10, 5, 10 bangs once. This is the single rule
//     that a naive implementation gets wrong, and it is stated only in the full
//     body of the Max page.
//   - **a fresh object is armed**, so a value that is already above the
//     threshold when the first message arrives bangs immediately — the state
//     `clear` restores, and the one a "remember the previous input" reading
//     would get wrong because there is no previous input.
//   - **a threshold change does not re-arm.** `set` is how a patch modulates
//     the threshold; re-arming on each one would turn the object back into a
//     .>=.
//   - **multiple thresholds are a conjunction over per-element flags**, so a
//     short list is a partial update rather than a reset.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/gPast.h"

namespace {

  using YSE::PATCHER::gPast;

  constexpr float INF = std::numeric_limits<float>::infinity();
  const float NOT_A_NUMBER = std::numeric_limits<float>::quiet_NaN();

  // Counts bangs. The object's whole output is "it happened", so counting is
  // the assertion — every test below is really about how many times.
  struct BangSink : YSE::PATCHER::pObject {
    int hits = 0;

    BangSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { hits++; });
    }
    const char* Type() const override {
      return "past_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  struct Rig {
    std::unique_ptr<gPast> op;
    BangSink out;

    Rig() : op(new gPast()) {
      op->ConnectOutlet(out.GetInlet(0), 0);
      out.ConnectInlet(op->GetOutlet(0), 0);
    }

    // Creation argument, applied the way a saved patch applies it.
    explicit Rig(const std::string& args) : Rig() {
      op->SetParams(args);
    }

    void Send(float v) {
      op->GetInlet(0)->SetFloat(v, YSE::T_GUI);
    }

    void SendInt(int v) {
      op->GetInlet(0)->SetInt(v, YSE::T_GUI);
    }

    void List(const std::string& text) {
      op->GetInlet(0)->SetList(text, YSE::T_GUI);
    }

    int Bangs() const {
      return out.hits;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("past: creatable through the registry with one inlet and a bang outlet (#464)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_PAST);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".past"));
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BANG);
  }

  TEST_CASE("past: listed by pRegistry::AllNames (#464)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".past")) != names.end());
  }

  // ─── the crossing ───────────────────────────────────────────────────────────

  TEST_CASE("past: bangs when the value rises through the threshold (#464)") {
    Rig rig("5");
    rig.Send(1.f);
    rig.Send(4.9f);
    CHECK(rig.Bangs() == 0);

    rig.Send(6.f);
    CHECK(rig.Bangs() == 1);
    CHECK(rig.op->Latched());
  }

  // The entire point of the object. A .>= would answer every one of these.
  TEST_CASE("past: stays quiet while the value stays above the threshold (#464)") {
    Rig rig("5");
    rig.Send(6.f);
    CHECK(rig.Bangs() == 1);

    rig.Send(7.f);
    rig.Send(1000.f);
    rig.Send(5.5f);
    CHECK(rig.Bangs() == 1);
  }

  TEST_CASE("past: drops below and crosses again for a second bang (#464)") {
    Rig rig("5");
    rig.Send(10.f);
    CHECK(rig.Bangs() == 1);

    rig.Send(4.f); // strictly below — re-armed
    CHECK(rig.Bangs() == 1);
    CHECK_FALSE(rig.op->Latched());

    rig.Send(10.f);
    CHECK(rig.Bangs() == 2);
  }

  // ─── equality, in both directions ───────────────────────────────────────────

  // Max: "Triggers output when the number is met or exceeded" / "If all of the
  // arguments are equaled or exceeded by the numbers received in the inlet,
  // past sends out a bang." A strict `>` here would silently swallow the event
  // on any stream that lands exactly on its threshold — a quantised controller,
  // a MIDI velocity, an integer counter.
  TEST_CASE("past: a value exactly at the threshold has crossed it (#464)") {
    Rig rig("5");
    rig.Send(5.f);
    CHECK(rig.Bangs() == 1);
    CHECK(rig.op->Latched());
  }

  // The load-bearing half, and the one only the full Max page states: "must
  // receive another list in which the number that equaled or exceeded its
  // argument goes back below (**is less than**) its argument."
  TEST_CASE("past: returning to exactly the threshold does not re-arm (#464)") {
    Rig rig("5");
    rig.Send(10.f);
    CHECK(rig.Bangs() == 1);

    rig.Send(5.f); // equal, not below — still latched
    CHECK(rig.Bangs() == 1);
    CHECK(rig.op->Latched());

    rig.Send(10.f);
    CHECK(rig.Bangs() == 1); // and so the second rise is not a crossing
  }

  // Stated as the sequence, because that is how the bug shows up in a patch:
  // an implementation using `> threshold` to fire and `< threshold` to re-arm
  // passes the rising test and fails this one.
  TEST_CASE("past: 10, 5, 10 bangs once but 10, 4, 10 bangs twice (#464)") {
    Rig equal("5");
    equal.Send(10.f);
    equal.Send(5.f);
    equal.Send(10.f);
    CHECK(equal.Bangs() == 1);

    Rig below("5");
    below.Send(10.f);
    below.Send(4.f);
    below.Send(10.f);
    CHECK(below.Bangs() == 2);
  }

  // ─── where the object starts ────────────────────────────────────────────────

  TEST_CASE("past: a first value already above the threshold bangs immediately (#464)") {
    Rig rig("5");
    // Nothing has met the threshold yet, so the first thing that does is a
    // crossing. A patch that loads with its controller already up must not
    // silently miss the event the object exists to catch.
    rig.Send(100.f);
    CHECK(rig.Bangs() == 1);
  }

  TEST_CASE("past: a bare .past watches a single threshold of 0 (#464)") {
    Rig rig;
    CHECK(rig.op->ThresholdCount() == 1);
    CHECK(rig.op->Threshold(0) == doctest::Approx(0.f));

    rig.Send(-1.f);
    CHECK(rig.Bangs() == 0);
    rig.Send(0.f); // at the threshold, so past it
    CHECK(rig.Bangs() == 1);
  }

  TEST_CASE("past: an int behaves exactly like the float (#464)") {
    Rig rig("5");
    rig.SendInt(4);
    CHECK(rig.Bangs() == 0);
    rig.SendInt(5);
    CHECK(rig.Bangs() == 1);
    rig.SendInt(9);
    CHECK(rig.Bangs() == 1);
  }

  TEST_CASE("past: negative and fractional thresholds compare as numbers (#464)") {
    Rig rig("-2.5");
    rig.Send(-3.f);
    CHECK(rig.Bangs() == 0);
    rig.Send(-2.75f);
    CHECK(rig.Bangs() == 0);
    rig.Send(-2.25f);
    CHECK(rig.Bangs() == 1);
  }

  // ─── clear ──────────────────────────────────────────────────────────────────

  TEST_CASE("past: 'clear' re-arms without a value ever dropping (#464)") {
    Rig rig("5");
    rig.Send(10.f);
    CHECK(rig.Bangs() == 1);

    // Max: "Causes past to forget previously received input, readying it to
    // send a bang message again."
    rig.List("clear");
    CHECK(rig.Bangs() == 1); // silent
    CHECK_FALSE(rig.op->Latched());

    rig.Send(10.f); // the same number, now a crossing again
    CHECK(rig.Bangs() == 2);
  }

  TEST_CASE("past: 'clear' leaves the thresholds alone (#464)") {
    Rig rig("60 100");
    rig.List("clear");
    CHECK(rig.op->ThresholdCount() == 2);
    CHECK(rig.op->Threshold(0) == doctest::Approx(60.f));
    CHECK(rig.op->Threshold(1) == doctest::Approx(100.f));
  }

  // ─── set ────────────────────────────────────────────────────────────────────

  TEST_CASE("past: 'set' replaces the thresholds, silently (#464)") {
    Rig rig("5");
    rig.List("set 50");
    CHECK(rig.Bangs() == 0);
    CHECK(rig.op->ThresholdCount() == 1);
    CHECK(rig.op->Threshold(0) == doctest::Approx(50.f));

    rig.Send(10.f);
    CHECK(rig.Bangs() == 0); // below the new line
    rig.Send(60.f);
    CHECK(rig.Bangs() == 1);
  }

  // The decision Max does not document, and the reason for it: `set` is how a
  // patch drives the threshold from elsewhere, so an object that re-armed on
  // every one would bang for every message above the line — a .>= rather than
  // a .past.
  TEST_CASE("past: 'set' does not re-arm, so a modulated threshold keeps the edge (#464)") {
    Rig rig("5");
    rig.Send(10.f);
    CHECK(rig.Bangs() == 1);

    // A stream of threshold updates with the value held above all of them.
    rig.List("set 6");
    rig.Send(10.f);
    rig.List("set 7");
    rig.Send(10.f);
    rig.List("set 8");
    rig.Send(10.f);
    CHECK(rig.Bangs() == 1);
    CHECK(rig.op->Latched());

    // `clear` is the way to ask for the fresh start explicitly.
    rig.List("clear");
    rig.Send(10.f);
    CHECK(rig.Bangs() == 2);
  }

  TEST_CASE("past: 'set' can grow the threshold list, and the new element starts un-met (#464)") {
    Rig rig("5");
    rig.Send(10.f);
    CHECK(rig.Bangs() == 1);
    CHECK(rig.op->Latched());

    // The conjunction is over a different set of elements now: element 1 has
    // had nothing compared against it, so the latch is honestly false again.
    rig.List("set 5 5");
    CHECK(rig.op->ThresholdCount() == 2);
    CHECK_FALSE(rig.op->Latched());
    CHECK(rig.op->Met(0)); // element 0 kept the flag the 10 gave it
    CHECK_FALSE(rig.op->Met(1));
    CHECK(rig.Bangs() == 1); // and growing the list emitted nothing

    rig.List("10 10");
    CHECK(rig.Bangs() == 2);
  }

  TEST_CASE("past: a malformed 'set' keeps the thresholds it had (#464)") {
    Rig rig("5");
    rig.List("set");
    rig.List("set wobble");
    CHECK(rig.Bangs() == 0);
    CHECK(rig.op->ThresholdCount() == 1);
    CHECK(rig.op->Threshold(0) == doctest::Approx(5.f));
  }

  // The word has to end where it ends. A bare prefix test would read this as
  // `set 3`; instead the shared list reader steps over the word it cannot parse
  // and the 3 is compared like any other number.
  TEST_CASE("past: 'settle 3' is not 'set 3' (#464)") {
    Rig rig("1");
    rig.List("settle 3");
    CHECK(rig.op->Threshold(0) == doctest::Approx(1.f)); // not installed as a threshold
    CHECK(rig.Bangs() == 1); // compared, and 3 is past 1
  }

  // ─── reset ──────────────────────────────────────────────────────────────────

  TEST_CASE("past: 'reset' returns to the creation-argument thresholds and re-arms (#464)") {
    Rig rig("5");
    rig.List("set 500");
    rig.Send(600.f);
    CHECK(rig.Bangs() == 1);

    rig.List("reset");
    CHECK(rig.Bangs() == 1); // silent
    CHECK(rig.op->ThresholdCount() == 1);
    CHECK(rig.op->Threshold(0) == doctest::Approx(5.f));
    CHECK_FALSE(rig.op->Latched());

    rig.Send(6.f);
    CHECK(rig.Bangs() == 2);
  }

  TEST_CASE("past: 'reset' restores a multi-threshold creation argument (#464)") {
    Rig rig("60 100");
    rig.List("set 1");
    CHECK(rig.op->ThresholdCount() == 1);

    rig.List("reset");
    CHECK(rig.op->ThresholdCount() == 2);
    CHECK(rig.op->Threshold(0) == doctest::Approx(60.f));
    CHECK(rig.op->Threshold(1) == doctest::Approx(100.f));
  }

  // ─── several thresholds ─────────────────────────────────────────────────────

  TEST_CASE("past: several thresholds are a conjunction (#464)") {
    Rig rig("60 100");
    // Max: "If all of the numbers in the list are greater than or equal to the
    // corresponding arguments, a bang is sent out the outlet."
    rig.List("70 50");
    CHECK(rig.Bangs() == 0);
    rig.List("50 200");
    CHECK(rig.Bangs() == 0);
    rig.List("70 200");
    CHECK(rig.Bangs() == 1);
  }

  TEST_CASE("past: with several thresholds one element dropping re-arms the conjunction (#464)") {
    Rig rig("60 100");
    rig.List("70 200");
    CHECK(rig.Bangs() == 1);

    rig.List("70 200"); // still up
    CHECK(rig.Bangs() == 1);

    rig.List("70 50"); // element 1 goes strictly below
    CHECK(rig.Bangs() == 1);
    CHECK_FALSE(rig.op->Latched());

    rig.List("70 200");
    CHECK(rig.Bangs() == 2);
  }

  // Per-element flags, which is what Max's re-arm wording describes. A list
  // shorter than the threshold list is a partial update, not a reset: the
  // elements it does not mention keep what they had.
  TEST_CASE("past: a short list updates only the elements it supplies (#464)") {
    Rig rig("60 100");
    rig.List("200"); // element 0 only
    CHECK(rig.op->Met(0));
    CHECK_FALSE(rig.op->Met(1));
    CHECK(rig.Bangs() == 0);

    rig.List("200 200"); // now both
    CHECK(rig.Bangs() == 1);

    // A single number is the one-element case of the same rule, so it can
    // re-arm the conjunction on its own.
    rig.Send(1.f);
    CHECK_FALSE(rig.op->Met(0));
    CHECK(rig.op->Met(1)); // untouched by the scalar
    CHECK_FALSE(rig.op->Latched());
    rig.Send(200.f);
    CHECK(rig.Bangs() == 2);
  }

  TEST_CASE("past: numbers past the last threshold are ignored (#464)") {
    Rig rig("60");
    // A second number with nothing to be compared against must not create a
    // threshold, and must not stop the first one from crossing.
    rig.List("70 -9999 -9999");
    CHECK(rig.op->ThresholdCount() == 1);
    CHECK(rig.Bangs() == 1);
  }

  // ─── ignored messages ───────────────────────────────────────────────────────

  TEST_CASE("past: a message with no numbers in it is ignored (#464)") {
    Rig rig("5");
    rig.List("wobble");
    rig.List("");
    rig.List("   ");
    CHECK(rig.Bangs() == 0);
    CHECK(rig.op->ThresholdCount() == 1);
    CHECK(rig.op->Threshold(0) == doctest::Approx(5.f));
    CHECK_FALSE(rig.op->Latched());
  }

  // A bang carries no number, so it cannot cross anything. Asserted through the
  // accepted-type mask as well, since "the handler ignores it" and "the inlet
  // never offered it" are different objects to a binding generator.
  TEST_CASE("past: a bang on the inlet does nothing (#464)") {
    Rig rig("5");
    rig.op->GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Bangs() == 0);

    rig.Send(10.f);
    CHECK(rig.Bangs() == 1);
    rig.op->GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Bangs() == 1);
  }

  // ─── non-finite input ───────────────────────────────────────────────────────

  TEST_CASE("past: a non-finite threshold reads as 0 rather than muting the object (#464)") {
    // A NaN compares false against everything, so an unfiltered NaN threshold
    // could never be met and the object would go permanently silent.
    Rig nan("nan");
    CHECK(nan.op->Threshold(0) == doctest::Approx(0.f));
    nan.Send(1.f);
    CHECK(nan.Bangs() == 1);

    Rig inf("inf");
    CHECK(inf.op->Threshold(0) == doctest::Approx(0.f));
    inf.Send(1.f);
    CHECK(inf.Bangs() == 1);

    // And through `set`, which is the other way one can arrive.
    Rig later("5");
    later.List("set inf");
    CHECK(later.op->Threshold(0) == doctest::Approx(0.f));
    later.Send(1.f);
    CHECK(later.Bangs() == 1);
  }

  TEST_CASE("past: a non-finite input reads as 0 (#464)") {
    Rig rig("5");
    rig.Send(NOT_A_NUMBER);
    CHECK(rig.Bangs() == 0); // 0 is below 5
    rig.Send(INF);
    CHECK(rig.Bangs() == 0);
    CHECK_FALSE(rig.op->Latched());

    // Against a threshold of 0 the substituted value is at the line, so it
    // counts as met — the same reading the rest of the patcher gives a NaN.
    Rig zero;
    zero.Send(NOT_A_NUMBER);
    CHECK(zero.Bangs() == 1);
  }

  TEST_CASE("past: a non-numeric token in the creation argument is skipped (#464)") {
    // Storing it as a 0 would insert a threshold every positive number meets,
    // silently turning `.past 60 wobble` into an object that bangs on 60 alone.
    Rig rig("60 wobble 100");
    CHECK(rig.op->ThresholdCount() == 2);
    CHECK(rig.op->Threshold(0) == doctest::Approx(60.f));
    CHECK(rig.op->Threshold(1) == doctest::Approx(100.f));
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("past: survives a DumpJSON / ParseJSON round trip (#464)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_PAST);
    REQUIRE(h != nullptr);
    h->SetParams("60 100");
    const std::string json = src.DumpJSON();
    CHECK(json.find(".past") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".past"));
    CHECK(copy->GetInputs() == 1);
    CHECK(copy->GetOutputs() == 1);
    CHECK(copy->GetParams() == std::string("60 100"));
  }

  TEST_CASE("past: re-parsing an empty parameter string returns to a bare .past (#464)") {
    Rig rig("60 100");
    REQUIRE(rig.op->ThresholdCount() == 2);
    rig.op->SetParams("");
    CHECK(rig.op->ThresholdCount() == 1);
    CHECK(rig.op->Threshold(0) == doctest::Approx(0.f));
    CHECK_FALSE(rig.op->Latched());
  }

  TEST_CASE("past: the GUI value reports the latch (#464)") {
    Rig rig("5");
    CHECK(rig.op->GetGuiValue() == "0");
    rig.Send(10.f);
    CHECK(rig.op->GetGuiValue() == "1");
    rig.Send(1.f);
    CHECK(rig.op->GetGuiValue() == "0");
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category, the port shape and the parameter name,
  // which is what a binding generator keys on.

  TEST_CASE("past: documents itself as MATH with 1 inlet and 1 bang outlet (#464)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_PAST));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
    CHECK_FALSE(obj->GetDescription().empty());
    REQUIRE(obj->NumInputs() == 1);
    CHECK(obj->GetInlet(0)->GetDocLabel() == "in");
    REQUIRE(obj->NumOutputs() == 1);
    CHECK(obj->GetOutlet(0)->GetDocLabel() == "cross");
    CHECK(obj->GetOutputType(0) == YSE::OUT_TYPE::BANG);
    REQUIRE(obj->GetParamDocs().size() == 1);
    CHECK(obj->GetParamDocs()[0].name == "thresholds");
    CHECK(obj->GetParamDocs()[0].defaultValue == "0");
  }

  TEST_CASE("past: the inlet accepts float, int and list but not bang (#464)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_PAST));
    REQUIRE(obj != nullptr);
    const unsigned int hot = obj->GetInlet(0)->GetAcceptedTypes();
    CHECK((hot & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((hot & YSE::PATCHER::IT_INT) != 0);
    CHECK((hot & YSE::PATCHER::IT_LIST) != 0);
    // A message carrying no number cannot cross anything.
    CHECK((hot & YSE::PATCHER::IT_BANG) == 0);
  }

} // TEST_SUITE("patcher")
