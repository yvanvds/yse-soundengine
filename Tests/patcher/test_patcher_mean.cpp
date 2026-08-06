// Tests for the running average (issue #460): .mean
//
// One inlet, two outlets — the mean out outlet 0 and the count out outlet 1,
// sent right to left as in Max. Every number received is folded into an
// average of everything before it, and the average never forgets: the
// thousandth value counts exactly as much as the first.
//
// The interesting part of this object is numerical, so that is where the
// weight of this file sits. Three numbers averaged by hand prove nothing about
// an object whose failure mode only appears after a million messages, so the
// suite drives real streams:
//
//   - a million values, where a float running sum would have drifted
//     measurably and the double one must still be exact to a part in 1e12;
//   - values of widely differing magnitude, where a float sum *stagnates* —
//     adding 1.0 to a sum of 1e9 rounds straight back to 1e9, so every later
//     value contributes nothing while the count keeps climbing and the
//     reported mean slides towards zero;
//   - the documented error bound itself, asserted as a bound rather than as a
//     single lucky number.
//
// The chosen behaviours that Max does not document are pinned here too: what a
// bang before any input reports, what `clear` / `reset` do and that they are
// silent, what a non-finite input counts as, and where the count outlet
// saturates.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/gMean.h"

namespace {

  // A sink that records *when* it was hit as well as what it received, so the
  // right-to-left outlet order can be asserted rather than assumed. One type
  // serves both outlets: the mean arrives as a float, the count as an int.
  struct OrderSink : YSE::PATCHER::pObject {
    std::vector<char>* log = nullptr;
    char tag = '?';
    float lastFloat = 0.f;
    int lastInt = -1;
    int floatCount = 0;
    int intCount = 0;

    OrderSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) {
        lastFloat = v;
        floatCount++;
        if (log) log->push_back(tag);
      });
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        lastInt = v;
        intCount++;
        if (log) log->push_back(tag);
      });
    }
    const char* Type() const override {
      return "order_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // Holds a .mean wired to both of its outlets, and offers the ways a patch
  // drives it.
  struct MeanRig {
    YSE::PATCHER::gMean op;
    OrderSink mean;
    OrderSink count;
    std::vector<char> order;

    MeanRig() {
      mean.tag = 'm';
      count.tag = 'c';
      mean.log = &order;
      count.log = &order;

      op.ConnectOutlet(mean.GetInlet(0), 0);
      mean.ConnectInlet(op.GetOutlet(0), 0);
      op.ConnectOutlet(count.GetInlet(0), 1);
      count.ConnectInlet(op.GetOutlet(1), 0);
    }

    // Feeds one float; returns the mean that came out.
    float Feed(float value) {
      op.GetInlet(0)->SetFloat(value, YSE::T_GUI);
      return mean.lastFloat;
    }

    float FeedInt(int value) {
      op.GetInlet(0)->SetInt(value, YSE::T_GUI);
      return mean.lastFloat;
    }

    float Bang() {
      op.GetInlet(0)->SetBang(YSE::T_GUI);
      return mean.lastFloat;
    }

    void Message(const std::string& text) {
      op.GetInlet(0)->SetList(text, YSE::T_GUI);
    }

    int Emitted() const {
      return count.intCount;
    }
  };

  // Renders a float with enough digits to survive the round trip, so a list
  // built for a test says exactly what the test means.
  std::string Num(double v) {
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.9g", v);
    return std::string(buffer);
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("mean: the object is creatable through the registry (#460)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_MEAN);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".mean"));
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 2);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::INT);
  }

  TEST_CASE("mean: the object is listed by pRegistry::AllNames (#460)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".mean")) != names.end());
  }

  // ─── the running average itself ─────────────────────────────────────────────

  TEST_CASE("mean: each number moves the average of everything before it (#460)") {
    MeanRig rig;
    CHECK(rig.Feed(1.f) == doctest::Approx(1.f));
    CHECK(rig.Feed(2.f) == doctest::Approx(1.5f));
    CHECK(rig.Feed(3.f) == doctest::Approx(2.f));
    CHECK(rig.Feed(10.f) == doctest::Approx(4.f));
    CHECK(rig.count.lastInt == 4);
  }

  TEST_CASE("mean: negatives average like anything else (#460)") {
    MeanRig rig;
    CHECK(rig.Feed(-4.f) == doctest::Approx(-4.f));
    CHECK(rig.Feed(4.f) == doctest::Approx(0.f));
    CHECK(rig.Feed(-6.f) == doctest::Approx(-2.f));
  }

  TEST_CASE("mean: an int is folded into the same sum as a float (#460)") {
    MeanRig rig;
    CHECK(rig.FeedInt(1) == doctest::Approx(1.f));
    CHECK(rig.Feed(2.5f) == doctest::Approx(1.75f));
    CHECK(rig.FeedInt(4) == doctest::Approx(2.5f));
    CHECK(rig.count.lastInt == 3);
  }

  // The whole point of an accumulating mean: an old value keeps its weight.
  TEST_CASE("mean: never forgets — an early value keeps its full weight (#460)") {
    MeanRig rig;
    REQUIRE(rig.Feed(100.f) == doctest::Approx(100.f));
    for (int i = 0; i < 99; i++)
      rig.Feed(0.f);
    // 100 values, one of them 100: the mean is exactly 1, not something that
    // decayed away.
    CHECK(rig.mean.lastFloat == doctest::Approx(1.f));
    CHECK(rig.count.lastInt == 100);
  }

  // ─── outlets ────────────────────────────────────────────────────────────────

  TEST_CASE("mean: the count is sent before the mean (#460)") {
    MeanRig rig;
    rig.Feed(1.f);
    rig.Feed(2.f);
    // Right to left, as in Max: a patch triggered by the mean already holds
    // the matching count, so the pair cannot be read half-updated.
    REQUIRE(rig.order.size() == 4);
    CHECK(rig.order[0] == 'c');
    CHECK(rig.order[1] == 'm');
    CHECK(rig.order[2] == 'c');
    CHECK(rig.order[3] == 'm');
  }

  TEST_CASE("mean: the count outlet counts messages, not distinct values (#460)") {
    MeanRig rig;
    for (int i = 0; i < 7; i++)
      rig.Feed(3.f);
    CHECK(rig.count.lastInt == 7);
    CHECK(rig.mean.lastFloat == doctest::Approx(3.f));
  }

  TEST_CASE("mean: the count outlet saturates at INT_MAX (#460)") {
    // Reaching this through the inlet would take two billion messages, so the
    // ceiling is asserted on the function that implements it.
    using YSE::PATCHER::gMean;
    CHECK(gMean::ReportableCount(0) == 0);
    CHECK(gMean::ReportableCount(1234) == 1234);
    CHECK(gMean::ReportableCount(2147483647LL) == 2147483647);
    CHECK(gMean::ReportableCount(2147483648LL) == 2147483647);
    CHECK(gMean::ReportableCount(9000000000000LL) == 2147483647);
  }

  // ─── numerics: a long stream ────────────────────────────────────────────────
  // A float running sum drifts systematically here, because a stream of equal
  // values rounds the same way every time rather than cancelling. The double
  // sum must be exact to far better than the float on the outlet can express.

  TEST_CASE("mean: a million equal values do not drift the average (#460)") {
    MeanRig rig;
    const int N = 1000000;
    const float v = 0.1f; // not exactly 0.1 in binary — that is the point

    for (int i = 0; i < N; i++)
      rig.op.GetInlet(0)->SetFloat(v, YSE::T_GUI);

    CHECK(rig.op.Count() == N);
    CHECK(rig.count.lastInt == N);

    // Against the exact value of the float that was sent, not against 0.1.
    const double exact = static_cast<double>(v);
    const double got = rig.op.Mean();
    CAPTURE(got);
    // The documented bound is (n-1)*u relative, u = 2^-53: about 1.1e-10 here.
    // Asserting an order of magnitude tighter than the bound would be asserting
    // luck, so this checks the bound itself, with a decade of headroom.
    CHECK(std::fabs(got - exact) <= 1e-9 * exact);

    // And the float that actually left the outlet is the input, to the bit.
    CHECK(rig.mean.lastFloat == v);
  }

  TEST_CASE("mean: a million alternating values average to their midpoint (#460)") {
    MeanRig rig;
    // 500000 of each: the exact mean is 1.5, and a sum that drifted would land
    // beside it.
    for (int i = 0; i < 1000000; i++)
      rig.op.GetInlet(0)->SetFloat((i % 2 == 0) ? 1.f : 2.f, YSE::T_GUI);

    CHECK(rig.op.Count() == 1000000);
    CHECK(std::fabs(rig.op.Mean() - 1.5) <= 1e-12);
    CHECK(rig.mean.lastFloat == doctest::Approx(1.5f).epsilon(1e-7));
  }

  // ─── numerics: widely differing magnitudes ──────────────────────────────────
  // This is the case a float sum does not merely drift on but *stagnates* on:
  // the ulp of 1e9 in float is 64, so 1e9 + 1 rounds back to 1e9 and every
  // subsequent 1.0 is discarded outright.

  TEST_CASE("mean: a large value does not swallow the small ones after it (#460)") {
    MeanRig rig;
    const int N = 10000;

    REQUIRE(rig.Feed(1e9f) == doctest::Approx(1e9f));
    for (int i = 0; i < N; i++)
      rig.op.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);

    const double exact = (1e9 + static_cast<double>(N)) / static_cast<double>(N + 1);
    const double got = rig.op.Mean();
    CAPTURE(got);
    CAPTURE(exact);
    CHECK(rig.op.Count() == N + 1);
    CHECK(std::fabs(got - exact) <= 1e-9 * exact);

    // The direct statement of what stagnation would have destroyed: the ten
    // thousand unit values are worth exactly 10000 in the sum, and a float sum
    // — whose ulp at 1e9 is 64 — would have absorbed *none* of them and left
    // the sum sitting on 1e9. Checked on the sum rather than the mean because
    // the mean only moves by one for it, the ten thousand values being spread
    // across ten thousand-and-one of them.
    const double excess = rig.op.Sum() - 1e9;
    CAPTURE(excess);
    CHECK(excess == doctest::Approx(static_cast<double>(N)));
  }

  TEST_CASE("mean: values seven decades apart interleave without loss (#460)") {
    MeanRig rig;
    // 1e7 and 1.0 alternating, ten thousand of each, so the running sum climbs
    // to 1e11 while values of magnitude 1 keep arriving. The exact mean is
    // 5000000.5 and the whole of that trailing .5 comes from the small values,
    // so it is precisely the term a stagnating sum loses. In float the sum's
    // ulp at 1e11 is 8192 and *neither* value survives; in double the ulp is
    // 2e-5 and both land exactly.
    for (int i = 0; i < 20000; i++)
      rig.op.GetInlet(0)->SetFloat((i % 2 == 0) ? 1e7f : 1.f, YSE::T_GUI);

    const double exact = 5000000.5;
    const double got = rig.op.Mean();
    CAPTURE(got);
    CHECK(rig.op.Count() == 20000);
    CHECK(std::fabs(got - exact) <= 1e-12 * exact);
    // The .5 is the small values' entire contribution: losing them would land
    // on a round 5000000.
    CHECK(got != doctest::Approx(5000000.0).epsilon(1e-9));
  }

  TEST_CASE("mean: the sum absorbs a value far below its own magnitude (#460)") {
    MeanRig rig;
    // One 1e12 then a hundred 1.0s. The float ulp at 1e12 is 65536, so a float
    // sum would not move at all after the first value; in double the ulp is
    // 0.0001 and every one of them lands exactly.
    rig.Feed(1e12f);
    for (int i = 0; i < 100; i++)
      rig.Feed(1.f);

    CHECK(rig.op.Count() == 101);
    const double excess = rig.op.Sum() - static_cast<double>(1e12f);
    CAPTURE(excess);
    CHECK(excess == doctest::Approx(100.0));
  }

  // ─── bang ───────────────────────────────────────────────────────────────────

  TEST_CASE("mean: a bang before any input reports 0 with a count of 0 (#460)") {
    MeanRig rig;
    // The mean of nothing is chosen rather than computed: 0/0 would put a NaN
    // on the outlet, and silence would leave a patch that bangs on load with
    // no value at all. The count outlet is what says which of the two zeroes
    // this is, and it arrives first.
    CHECK(rig.Bang() == 0.f);
    CHECK(std::isfinite(rig.mean.lastFloat));
    CHECK(rig.count.lastInt == 0);
    REQUIRE(rig.order.size() == 2);
    CHECK(rig.order[0] == 'c');
  }

  TEST_CASE("mean: a bang re-sends the stored mean without adding anything (#460)") {
    MeanRig rig;
    REQUIRE(rig.Feed(2.f) == doctest::Approx(2.f));
    REQUIRE(rig.Feed(4.f) == doctest::Approx(3.f));

    for (int i = 0; i < 5; i++) {
      CAPTURE(i);
      CHECK(rig.Bang() == doctest::Approx(3.f));
      CHECK(rig.count.lastInt == 2); // the count did not move either
    }
    CHECK(rig.op.Count() == 2);
  }

  // ─── clear / reset ──────────────────────────────────────────────────────────

  TEST_CASE("mean: 'clear' zeroes the sum and the count without emitting (#460)") {
    MeanRig rig;
    REQUIRE(rig.Feed(6.f) == doctest::Approx(6.f));
    const int emittedBefore = rig.Emitted();

    rig.Message("clear");
    CHECK(rig.Emitted() == emittedBefore); // silent, like .histo's clear
    CHECK(rig.op.Count() == 0);
    CHECK(rig.op.Sum() == 0.0);

    // ... and a bang after it reports the empty state, not the old mean.
    CHECK(rig.Bang() == 0.f);
    CHECK(rig.count.lastInt == 0);
  }

  TEST_CASE("mean: 'reset' is accepted as a synonym for 'clear' (#460)") {
    MeanRig rig;
    REQUIRE(rig.Feed(6.f) == doctest::Approx(6.f));
    rig.Message("reset");
    CHECK(rig.op.Count() == 0);
    CHECK(rig.Bang() == 0.f);
  }

  TEST_CASE("mean: after a clear the average starts from the next number (#460)") {
    MeanRig rig;
    rig.Feed(100.f);
    rig.Feed(100.f);
    rig.Message("clear");

    CHECK(rig.Feed(1.f) == doctest::Approx(1.f)); // not 67, not 50
    CHECK(rig.count.lastInt == 1);
    CHECK(rig.Feed(3.f) == doctest::Approx(2.f));
  }

  // ─── list ───────────────────────────────────────────────────────────────────

  TEST_CASE("mean: a list reports its own mean and forgets what came before (#460)") {
    MeanRig rig;
    REQUIRE(rig.Feed(1000.f) == doctest::Approx(1000.f));

    // Max: "The numbers in the list are added together, the sum is divided by
    // the number of items in the list, and the mean is sent out. All
    // previously received numbers are cleared from memory."
    rig.Message("1 2 3 4");
    CHECK(rig.mean.lastFloat == doctest::Approx(2.5f));
    CHECK(rig.count.lastInt == 4);
    CHECK(rig.op.Count() == 4);
  }

  TEST_CASE("mean: a list emits exactly once, not once per item (#460)") {
    MeanRig rig;
    const int before = rig.Emitted();
    rig.Message("1 2 3 4 5 6");
    CHECK(rig.Emitted() == before + 1);
    CHECK(rig.mean.lastFloat == doctest::Approx(3.5f));
  }

  TEST_CASE("mean: a following number continues from the list, not from zero (#460)") {
    MeanRig rig;
    rig.Message("2 4");
    REQUIRE(rig.mean.lastFloat == doctest::Approx(3.f));
    // Sum 6 over 2 values; adding 9 gives 15/3 = 5.
    CHECK(rig.Feed(9.f) == doctest::Approx(5.f));
    CHECK(rig.count.lastInt == 3);
  }

  TEST_CASE("mean: a list accepts negatives and fractions (#460)") {
    MeanRig rig;
    rig.Message("-1.5 0.5 2.5 -3.5");
    CHECK(rig.mean.lastFloat == doctest::Approx(-0.5f));
    CHECK(rig.count.lastInt == 4);
  }

  TEST_CASE("mean: a long list is truncated at 256 items (#460)") {
    MeanRig rig;
    // 300 numbers: the first 256 are 1, the rest 1000. Truncation is visible
    // both in the count and in the mean, which is what makes it debuggable
    // rather than merely documented.
    std::string list;
    for (int i = 0; i < 300; i++) {
      if (i > 0) list += ' ';
      list += Num(i < 256 ? 1.0 : 1000.0);
    }
    rig.Message(list);

    CHECK(rig.count.lastInt == YSE::PATCHER::gMean::MAX_LIST_ITEMS);
    CHECK(rig.mean.lastFloat == doctest::Approx(1.f));
  }

  TEST_CASE("mean: a one-item list is just that number (#460)") {
    MeanRig rig;
    rig.Message("7");
    CHECK(rig.mean.lastFloat == doctest::Approx(7.f));
    CHECK(rig.count.lastInt == 1);
  }

  TEST_CASE("mean: a message with no numbers in it is ignored (#460)") {
    MeanRig rig;
    REQUIRE(rig.Feed(5.f) == doctest::Approx(5.f));
    const int before = rig.Emitted();

    // Not an empty list — an unknown message. Treating it as an empty list
    // would make every typo a silent clear.
    rig.Message("wobble");
    rig.Message("");
    rig.Message("   ");
    CHECK(rig.Emitted() == before);
    CHECK(rig.op.Count() == 1);
    CHECK(rig.Bang() == doctest::Approx(5.f));
  }

  // ─── non-finite input ───────────────────────────────────────────────────────

  TEST_CASE("mean: a NaN is folded in as 0 and still counted (#460)") {
    MeanRig rig;
    REQUIRE(rig.Feed(4.f) == doctest::Approx(4.f));

    const float out = rig.Feed(std::numeric_limits<float>::quiet_NaN());
    CHECK(std::isfinite(out));
    CHECK(out == doctest::Approx(2.f)); // (4 + 0) / 2
    CHECK(rig.count.lastInt == 2);
  }

  // An infinity is the dangerous one: left alone it would pin the sum at
  // infinity, and a following -inf would turn it into a NaN that no later
  // input could clear.
  TEST_CASE("mean: an infinity cannot poison the running sum (#460)") {
    MeanRig rig;
    CHECK(std::isfinite(rig.Feed(std::numeric_limits<float>::infinity())));
    CHECK(std::isfinite(rig.Feed(-std::numeric_limits<float>::infinity())));
    CHECK(std::isfinite(rig.Feed(std::numeric_limits<float>::infinity())));

    // Still a working average afterwards: three zeroes plus a 4 is 1.
    CHECK(rig.Feed(4.f) == doctest::Approx(1.f));
    CHECK(rig.count.lastInt == 4);
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("mean: declares no creation parameters, as in Max (#460)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_MEAN));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetParamDocs().empty());
  }

  TEST_CASE("mean: survives a DumpJSON / ParseJSON round trip (#460)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_MEAN) != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".mean") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".mean"));
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 2);
  }

  TEST_CASE("mean: the GUI value reports the current average (#460)") {
    MeanRig rig;
    CHECK(std::stod(rig.op.GetGuiValue()) == doctest::Approx(0.0));
    rig.Feed(2.f);
    rig.Feed(6.f);
    CHECK(std::stod(rig.op.GetGuiValue()) == doctest::Approx(4.0));
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the port shape, which is what a binding
  // generator keys on.

  TEST_CASE("mean: documents itself as MATH with one inlet and two outlets (#460)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_MEAN));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
    CHECK_FALSE(obj->GetDescription().empty());
    REQUIRE(obj->NumInputs() == 1);
    CHECK(obj->GetInlet(0)->GetDocLabel() == "in");
    REQUIRE(obj->NumOutputs() == 2);
    CHECK(obj->GetOutlet(0)->GetDocLabel() == "mean");
    CHECK(obj->GetOutlet(1)->GetDocLabel() == "count");
    CHECK(obj->GetOutputType(0) == YSE::OUT_TYPE::FLOAT);
    CHECK(obj->GetOutputType(1) == YSE::OUT_TYPE::INT);
  }

  TEST_CASE("mean: inlet 0 accepts float, int, bang and list (#460)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_MEAN));
    REQUIRE(obj != nullptr);
    const unsigned int types = obj->GetInlet(0)->GetAcceptedTypes();
    CHECK((types & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((types & YSE::PATCHER::IT_INT) != 0);
    CHECK((types & YSE::PATCHER::IT_BANG) != 0);
    CHECK((types & YSE::PATCHER::IT_LIST) != 0);
  }

} // TEST_SUITE("patcher")
