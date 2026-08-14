// Tests for .array.min / .array.max / .array.mean / .array.median /
// .array.mode / .array.stddev (issue #790) — Max's array statistics on the
// name-addressed value model .array settled (#548).
//
// What has to be proven, and what every case below is one of:
//
//   - **the array is bound from the creation argument.** An array never
//     travels down a cord, so ".array.mean <name>" resolves the name once,
//     on the control thread, and an `array <name>` message is honoured only
//     when it names the array already bound — ArrayReferenceNames' bounded
//     compare, never a registry lookup on a message path.
//   - **the population decision is #790's, written down.** Five statistics
//     reduce the *numeric* elements — a symbol is skipped, not part of the
//     population — and mode counts every element by its spelling, so 7 and
//     7. are one number to the mean and two elements to the mode.
//   - **an empty population bangs the empty outlet.** The minimum of
//     nothing does not exist and a sentinel would be indistinguishable from
//     a real answer — .array.pop's empty-outlet rule.
//   - **a statistic never reorders the store.** median and mode sort a
//     scratch the object owns; the shared array is read, only.
//   - **the ask crosses the control/audio boundary and nothing allocates.**
//     In-patcher delivery dispatches on T_DSP, so "the audio thread asks for
//     the mean" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses names of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <cstdint>
#include <memory>
#include <string>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArrayStats.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObject.h"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::BangSink;
using TestHelpers::MultiSink;
using TestHelpers::Wire;
using YSE::PATCHER::gArray;
using YSE::PATCHER::gArrayMax;
using YSE::PATCHER::gArrayMean;
using YSE::PATCHER::gArrayMedian;
using YSE::PATCHER::gArrayMin;
using YSE::PATCHER::gArrayMode;
using YSE::PATCHER::gArrayStdDev;

namespace {

  // The six, for the cases that loop over the whole family.
  const char* const kStatTypes[] = {
      YSE::OBJ::G_ARRAY_MIN,    YSE::OBJ::G_ARRAY_MAX,  YSE::OBJ::G_ARRAY_MEAN,
      YSE::OBJ::G_ARRAY_MEDIAN, YSE::OBJ::G_ARRAY_MODE, YSE::OBJ::G_ARRAY_STDDEV,
  };

  // An .array and one statistic on one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sinks are declared before the objects so they are torn down last, while
  // the outlets wired to them still exist (see sinks.hpp on why that
  // matters).
  template <typename ObjectT> struct Rig {
    MultiSink result;
    BangSink empty;
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray array;
    ObjectT stat;

    Rig(const std::string& patcherName, const std::string& name) {
      p.SetName(patcherName);
      array.SetParent(&p);
      array.SetParams(name);
      stat.SetParent(&p);
      stat.SetParams(name);
      Wire(stat, 0, result);
      Wire(stat, 1, empty);
    }

    void Store(const std::string& message) {
      array.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Ask() {
      stat.GetInlet(0)->SetBang(YSE::T_GUI);
    }
    void Reset() {
      result.reset();
      empty.gotBang = false;
      empty.bangCount = 0;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("array.stats: all six registered, two inlets, two outlets (#790)") {
    YSE::patcher p;
    p.create(2);
    for (const char* type : kStatTypes) {
      CAPTURE(type);
      YSE::pHandle* h = p.CreateObject(type);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == std::string(type));
      CHECK(h->GetInputs() == 2);
      CHECK(h->GetOutputs() == 2);
    }

    auto names = YSE::PATCHER::Register().AllNames();
    for (const char* type : kStatTypes) {
      CAPTURE(type);
      bool found = false;
      for (const auto& name : names) {
        if (name == std::string(type)) found = true;
      }
      CHECK(found);
    }
  }

  TEST_CASE("array.stats: the trigger inlet takes the ask, and only the ask (#790)") {
    // Inlet 0 is the ask — a bang, or the "array <name>" reference, which is
    // list text. A statistic is asked for, never addressed, so there is no
    // int or float method anywhere: a number on either inlet names nothing.
    for (const char* type : kStatTypes) {
      CAPTURE(type);
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(type));
      REQUIRE(obj != nullptr);
      const unsigned int triggerIn = obj->GetInlet(0)->GetAcceptedTypes();
      CHECK((triggerIn & YSE::PATCHER::IT_BANG) != 0);
      CHECK((triggerIn & YSE::PATCHER::IT_LIST) != 0);
      CHECK((triggerIn & YSE::PATCHER::IT_INT) == 0);
      CHECK((triggerIn & YSE::PATCHER::IT_FLOAT) == 0);
      const unsigned int refIn = obj->GetInlet(1)->GetAcceptedTypes();
      CHECK((refIn & YSE::PATCHER::IT_LIST) != 0);
      CHECK((refIn & YSE::PATCHER::IT_BANG) == 0);
      CHECK((refIn & YSE::PATCHER::IT_INT) == 0);
      CHECK((refIn & YSE::PATCHER::IT_FLOAT) == 0);
    }
  }

  // ─── min / max ──────────────────────────────────────────────────────────────

  TEST_CASE("array.min/max: the winning element leaves typed by its spelling (#790)") {
    // The answer is the element, not a re-spelling of its value: -3 leaves
    // as the int it is, 7.5 as the float it is. Symbols are skipped — the
    // population is the numeric elements.
    Rig<gArrayMin> min("ast790a", "a790a");
    Rig<gArrayMax> max("ast790a2", "a790a2");
    min.Store("append 10 c4 7.5 -3");
    max.Store("append 10 c4 7.5 -3");

    min.Ask();
    CHECK(min.result.gotInt);
    CHECK(min.result.intValue == -3);
    CHECK_FALSE(min.empty.gotBang);

    max.Ask();
    CHECK(max.result.gotInt);
    CHECK(max.result.intValue == 10);
    CHECK_FALSE(max.empty.gotBang);

    // All-float population: the float spelling survives to the outlet.
    Rig<gArrayMin> fmin("ast790a3", "a790a3");
    fmin.Store("append 9.25 7.5");
    fmin.Ask();
    CHECK(fmin.result.gotFloat);
    CHECK(fmin.result.floatValue == doctest::Approx(7.5f));
  }

  TEST_CASE("array.min/max: values compare numerically and a tie keeps the first (#790)") {
    // 7. and 7 are one number to the comparison — spelling decides how the
    // element leaves, never whether it wins — and the strict comparison
    // never replaces an equal earlier winner, so both answers are the "7."
    // that arrived first, leaving as the float its spelling makes it.
    Rig<gArrayMin> min("ast790b", "a790b");
    Rig<gArrayMax> max("ast790b2", "a790b2");
    min.Store("append 7. 7");
    max.Store("append 7. 7");

    min.Ask();
    CHECK(min.result.gotFloat);
    CHECK(min.result.floatValue == doctest::Approx(7.f));
    CHECK_FALSE(min.result.gotInt);

    max.Ask();
    CHECK(max.result.gotFloat);
    CHECK(max.result.floatValue == doctest::Approx(7.f));
    CHECK_FALSE(max.result.gotInt);
  }

  TEST_CASE("array.min: no numeric element bangs the empty outlet (#790)") {
    // An all-symbol array holds nothing a minimum is over — the empty
    // outlet answers, the result outlet stays silent, and nothing is
    // counted as refused: "no data" is a state, not an error.
    Rig<gArrayMin> rig("ast790c", "a790c");
    rig.Store("append c4 d4 e4");

    rig.Ask();
    CHECK(rig.empty.gotBang);
    CHECK_FALSE(rig.result.gotInt);
    CHECK_FALSE(rig.result.gotFloat);
    CHECK_FALSE(rig.result.gotList);
    CHECK(rig.stat.Dropped() == 0);
  }

  // ─── mean ───────────────────────────────────────────────────────────────────

  TEST_CASE("array.mean: the mean of the numeric elements, always a float (#790)") {
    Rig<gArrayMean> rig("ast790d", "a790d");
    rig.Store("append 10 4 7");

    rig.Ask();
    CHECK(rig.result.gotFloat);
    CHECK(rig.result.floatValue == doctest::Approx(7.f));

    // A symbol is skipped, not part of the population: the mean is over
    // {10, 4, 7} exactly as before.
    rig.Reset();
    rig.Store("insert 1 c4");
    rig.Ask();
    CHECK(rig.result.gotFloat);
    CHECK(rig.result.floatValue == doctest::Approx(7.f));
    CHECK_FALSE(rig.empty.gotBang);
  }

  // ─── median ─────────────────────────────────────────────────────────────────

  TEST_CASE("array.median: the middle value, the mean of two middles when even (#790)") {
    Rig<gArrayMedian> rig("ast790e", "a790e");
    rig.Store("append 9 3 7");

    // Odd population: the middle value in sorted order.
    rig.Ask();
    CHECK(rig.result.gotFloat);
    CHECK(rig.result.floatValue == doctest::Approx(7.f));

    // Even population: the mean of the two middle values.
    rig.Reset();
    rig.Store("clear");
    rig.Store("append 4 1 3 2");
    rig.Ask();
    CHECK(rig.result.gotFloat);
    CHECK(rig.result.floatValue == doctest::Approx(2.5f));
  }

  TEST_CASE("array.median: sorts a scratch of its own, never the store (#790)") {
    // The whole reason median is not "ask .array.sort first": a statistic
    // must not move the array under everything else reading it. The answer
    // is the sorted middle, and the store is untouched.
    Rig<gArrayMedian> rig("ast790f", "a790f");
    rig.Store("append 9 3 7");

    rig.Ask();
    CHECK(rig.result.gotFloat);
    CHECK(rig.result.floatValue == doctest::Approx(7.f));
    CHECK(rig.array.Count() == 3);
    CHECK(rig.array.ElementAt(0) == "9");
    CHECK(rig.array.ElementAt(1) == "3");
    CHECK(rig.array.ElementAt(2) == "7");
  }

  // ─── mode ───────────────────────────────────────────────────────────────────

  TEST_CASE("array.mode: the most frequent element, over every element (#790)") {
    // The one statistic that counts symbols too — a mode is about identity,
    // not magnitude — and the answer is the element itself, typed the way
    // the patcher spells it.
    Rig<gArrayMode> rig("ast790g", "a790g");
    rig.Store("append a b a c a");

    rig.Ask();
    CHECK(rig.result.gotList);
    CHECK(rig.result.listValue == "a");
    CHECK(rig.array.Count() == 5);
    CHECK(rig.array.ElementAt(0) == "a");
    CHECK(rig.array.ElementAt(1) == "b");
  }

  TEST_CASE("array.mode: equality is the spelling, and a tie keeps the earliest (#790)") {
    // 7 and 7. are different elements to a mode — ArrayFind's byte compare,
    // the same fact getvalue spells them differently — so two "7"s beat one
    // "7.". And between two runs of equal length, the element whose first
    // occurrence is earliest wins, which the stable order makes
    // deterministic.
    Rig<gArrayMode> spelling("ast790h", "a790h");
    spelling.Store("append 7 7. 7");
    spelling.Ask();
    CHECK(spelling.result.gotInt);
    CHECK(spelling.result.intValue == 7);
    CHECK_FALSE(spelling.result.gotFloat);

    Rig<gArrayMode> tie("ast790h2", "a790h2");
    tie.Store("append b a b a");
    tie.Ask();
    CHECK(tie.result.gotList);
    CHECK(tie.result.listValue == "b");
  }

  // ─── stddev ─────────────────────────────────────────────────────────────────

  TEST_CASE("array.stddev: the population standard deviation, N not N-1 (#790)") {
    // The textbook population: {2,4,4,4,5,5,7,9} has mean 5 and population
    // standard deviation exactly 2. The array is the whole population, not
    // a sample of one — dividing by N-1 would answer ~2.14 here.
    Rig<gArrayStdDev> rig("ast790i", "a790i");
    rig.Store("append 2 4 4 4 5 5 7 9");

    rig.Ask();
    CHECK(rig.result.gotFloat);
    CHECK(rig.result.floatValue == doctest::Approx(2.f));

    // A one-element population spreads not at all — 0, not a refusal and
    // not the division-by-zero N-1 would set up.
    Rig<gArrayStdDev> one("ast790i2", "a790i2");
    one.Store("append 5");
    one.Ask();
    CHECK(one.result.gotFloat);
    CHECK(one.result.floatValue == doctest::Approx(0.f));
    CHECK_FALSE(one.empty.gotBang);
  }

  // ─── the empty population, and the unnamed object ───────────────────────────

  TEST_CASE("array.stats: an empty array bangs the empty outlet on all six (#790)") {
    MultiSink result;
    BangSink empty;
    YSE::pHandle resultHandle(&result);
    YSE::pHandle emptyHandle(&empty);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ast790j");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a790j");

    for (const char* type : kStatTypes) {
      CAPTURE(type);
      YSE::pHandle* h = p.CreateObject(type, "a790j");
      REQUIRE(h != nullptr);
      p.Connect(h, 0, &resultHandle, 0);
      p.Connect(h, 1, &emptyHandle, 0);

      result.reset();
      empty.gotBang = false;
      h->SetBang(0);
      CHECK(empty.gotBang);
      CHECK_FALSE(result.gotInt);
      CHECK_FALSE(result.gotFloat);
      CHECK_FALSE(result.gotList);
    }
  }

  TEST_CASE("array.stats: an unnamed object reads a private, empty array (#790)") {
    // Not "shares the empty name" — gArray's rule, inherited whole. A
    // private array holds no population, so the ask bangs the empty outlet.
    MultiSink result;
    BangSink empty;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ast790k");
    gArrayMin g;
    g.SetParent(&p);
    Wire(g, 0, result);
    Wire(g, 1, empty);

    CHECK(g.Address().empty());
    g.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(empty.gotBang);
    CHECK_FALSE(result.gotInt);
    CHECK(g.Dropped() == 0);
  }

  // ─── the reference ──────────────────────────────────────────────────────────

  TEST_CASE("array.stats: the reference asks on the trigger inlet, anything else refused (#790)") {
    // "array <name>" is the message an .array's reference outlet emits on a
    // bang; on the trigger inlet it asks for the statistic — the family
    // gesture. A reference to an array this object is not bound to is
    // refused, never resolved: a registry lookup is a mutex, and this may be
    // the audio thread.
    Rig<gArrayMean> rig("ast790l", "a790l");
    rig.Store("append 10 4 7");

    const std::uint64_t before = rig.stat.Dropped();
    rig.stat.GetInlet(0)->SetList("array a790l", YSE::T_GUI);
    CHECK(rig.result.gotFloat);
    CHECK(rig.result.floatValue == doctest::Approx(7.f));
    CHECK(rig.stat.Dropped() == before);

    rig.Reset();
    rig.stat.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    rig.stat.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK_FALSE(rig.result.gotFloat);
    CHECK_FALSE(rig.empty.gotBang);
    CHECK(rig.stat.Dropped() == before + 2);
  }

  TEST_CASE("array.stats: the reference inlet acknowledges its own array and nothing more (#790)") {
    Rig<gArrayMean> rig("ast790m", "a790m");
    rig.Store("append 10 4 7");

    const std::uint64_t before = rig.stat.Dropped();
    rig.stat.GetInlet(1)->SetList("array a790m", YSE::T_GUI);
    CHECK_FALSE(rig.result.gotFloat);
    CHECK(rig.stat.Dropped() == before);

    rig.stat.GetInlet(1)->SetList("array somewhere_else", YSE::T_GUI);
    rig.stat.GetInlet(1)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.stat.Dropped() == before + 2);
  }

  TEST_CASE("array.stats: wired from the array's reference outlet, banging the array asks (#790)") {
    // The flow a patch actually wires, end to end through the public patcher
    // API: the .array's reference outlet into the trigger inlet gives the
    // family gesture — bang the array, out comes its statistic.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::patcher p;
    p.create(2);
    p.name("ast790n");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a790n");
    YSE::pHandle* mean = p.CreateObject(YSE::OBJ::G_ARRAY_MEAN, "a790n");
    REQUIRE(array != nullptr);
    REQUIRE(mean != nullptr);
    p.Connect(array, 1, mean, 0);
    p.Connect(mean, 0, &sinkHandle, 0);

    array->SetListData(0, "append 60 64 67");
    array->SetBang(0);
    REQUIRE(sink.gotFloat);
    CHECK(sink.floatValue == doctest::Approx(63.666668f));

    sink.reset();
    array->SetListData(0, "append 72");
    array->SetBang(0);
    REQUIRE(sink.gotFloat);
    CHECK(sink.floatValue == doctest::Approx(65.75f));
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("array.stats: patcherImplementation::SetName re-anchors all six (#790)") {
    // The rename dispatch itself: an object created *inside* a patcher must
    // be re-anchored by the patcher, without anybody calling RefreshBinding
    // by hand. The keeper holds the old-address store (it is not in the
    // patcher's object map, so the rename does not touch it): before the
    // rename every ask answers the keeper's elements; after it every ask
    // reads a fresh empty array under the new prefix, so the empty outlet
    // bangs.
    MultiSink result;
    BangSink empty;
    YSE::pHandle resultHandle(&result);
    YSE::pHandle emptyHandle(&empty);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ast790o_before");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a790o");
    keeper.GetInlet(0)->SetList("append 10 4 7", YSE::T_GUI);

    YSE::pHandle* handles[6] = {};
    for (int i = 0; i < 6; i++) {
      handles[i] = p.CreateObject(kStatTypes[i], "a790o");
      REQUIRE(handles[i] != nullptr);
      p.Connect(handles[i], 0, &resultHandle, 0);
      p.Connect(handles[i], 1, &emptyHandle, 0);
    }

    for (int i = 0; i < 6; i++) {
      CAPTURE(kStatTypes[i]);
      result.reset();
      empty.gotBang = false;
      handles[i]->SetBang(0);
      CHECK((result.gotInt || result.gotFloat || result.gotList));
      CHECK_FALSE(empty.gotBang);
    }

    p.SetName("ast790o_after");
    for (int i = 0; i < 6; i++) {
      CAPTURE(kStatTypes[i]);
      result.reset();
      empty.gotBang = false;
      handles[i]->SetBang(0);
      CHECK(empty.gotBang);
      CHECK_FALSE(result.gotInt);
      CHECK_FALSE(result.gotFloat);
      CHECK_FALSE(result.gotList);
    }
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.stats: an ask over in-patcher delivery lands on T_DSP (#790)") {
    // A .r feeding the trigger inlet dispatches on T_DSP when the block
    // drains it (issue #225) — "the audio thread asks for the mean" is the
    // ordinary case, and the whole path is one guard hold, one bounded
    // reduction and a send of one float.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ast790p");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go790p");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a790p");
    YSE::pHandle* mean = p.CreateObject(YSE::OBJ::G_ARRAY_MEAN, "a790p");
    REQUIRE(recv != nullptr);
    REQUIRE(array != nullptr);
    REQUIRE(mean != nullptr);
    p.Connect(recv, 0, mean, 0);
    p.Connect(mean, 0, &sinkHandle, 0);

    array->SetListData(0, "append 10 4 7");

    p.PassData(std::string("array a790p"), "go790p", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotFloat);
    CHECK(sink.floatValue == doctest::Approx(7.f));
  }

  TEST_CASE("array.stats: no message path allocates (#790)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every reduce path: the extremum scan, the mean, the median's scratch
    // sort, the mode's spelling order (including the symbol-typed answer)
    // and the stddev — plus the refusal and acknowledgement paths on one of
    // them.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "array probeAS790";
    const std::string wrongName = "array somewhere_else_long";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ast790q");
    MultiSink minSink;
    MultiSink maxSink;
    MultiSink meanSink;
    MultiSink medianSink;
    MultiSink modeSink;
    MultiSink stddevSink;
    gArray array;
    gArrayMin min;
    gArrayMax max;
    gArrayMean mean;
    gArrayMedian median;
    gArrayMode mode;
    gArrayStdDev stddev;

    array.SetParent(&p);
    array.SetParams("probeAS790");
    YSE::PATCHER::pObject* stats[6] = {&min, &max, &mean, &median, &mode, &stddev};
    MultiSink* sinks[6] = {&minSink, &maxSink, &meanSink, &medianSink, &modeSink, &stddevSink};
    for (int i = 0; i < 6; i++) {
      stats[i]->SetParams("probeAS790");
      stats[i]->SetParent(&p);
      Wire(*stats[i], 0, *sinks[i]);
    }

    // A mixed population, with a symbol mode so the probe covers the
    // symbol-typed answer (the scratch-rendered SendList path) too.
    array.GetInlet(0)->SetList("append 10 c4 7.5 c4 -3", YSE::T_GUI);

    // Warm every path — including the sinks' assignments — so first-call
    // machinery is not what the probe catches.
    for (auto* stat : stats) {
      stat->GetInlet(0)->SetBang(YSE::T_GUI);
      stat->GetInlet(0)->SetList(reference, YSE::T_GUI);
    }
    mean.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    mean.GetInlet(1)->SetList(reference, YSE::T_GUI);
    mean.GetInlet(1)->SetList(wrongName, YSE::T_GUI);
    const std::uint64_t meanDroppedBefore = mean.Dropped();

    for (auto* sink : sinks)
      sink->reset();
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      for (auto* stat : stats) {
        stat->GetInlet(0)->SetBang(YSE::T_DSP);
        stat->GetInlet(0)->SetList(reference, YSE::T_DSP);
      }
      mean.GetInlet(0)->SetList(wrongName, YSE::T_DSP);
      mean.GetInlet(1)->SetList(reference, YSE::T_DSP);
      mean.GetInlet(1)->SetList(wrongName, YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. Every reducer answered, the mode's answer
    // was the symbol path, and both wrong-name refusals were counted.
    CHECK(minSink.gotInt);
    CHECK(minSink.intValue == -3);
    CHECK(maxSink.gotInt);
    CHECK(maxSink.intValue == 10);
    CHECK(meanSink.gotFloat);
    CHECK(meanSink.floatValue == doctest::Approx(4.8333335f));
    CHECK(medianSink.gotFloat);
    CHECK(medianSink.floatValue == doctest::Approx(7.5f));
    CHECK(modeSink.gotList);
    CHECK(modeSink.listValue == "c4");
    CHECK(stddevSink.gotFloat);
    CHECK(stddevSink.floatValue == doctest::Approx(5.6322484f));
    CHECK(mean.Dropped() == meanDroppedBefore + 2);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.stats: params survive a DumpJSON / ParseJSON round trip (#790)") {
    YSE::patcher src;
    src.create(2);
    for (const char* type : kStatTypes) {
      YSE::pHandle* h = src.CreateObject(type, "notes790");
      REQUIRE(h != nullptr);
    }
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 6);

    for (const char* type : kStatTypes) {
      CAPTURE(type);
      // Found by type rather than by list position: the loaded patcher's
      // enumeration order is not the creation order.
      YSE::pHandle* copy = nullptr;
      for (unsigned int i = 0; i < loaded.Objects(); i++) {
        YSE::pHandle* handle = loaded.GetHandleFromList(static_cast<int>(i));
        REQUIRE(handle != nullptr);
        if (std::string(handle->Type()) == std::string(type)) copy = handle;
      }
      REQUIRE(copy != nullptr);
      // The analyzer cannot see that a failed REQUIRE aborts the case
      // (doctest's failure path is a runtime jump), so it assumes `copy` may
      // be null here.
      // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
      CHECK(copy->GetParams() == std::string("notes790"));
      CHECK(copy->GetInputs() == 2);
      CHECK(copy->GetOutputs() == 2);
    }
  }

  TEST_CASE("array.stats: all six carry complete documentation metadata (#790)") {
    for (const char* type : kStatTypes) {
      CAPTURE(type);
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK_FALSE(obj->GetDescription().empty());
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GENERIC);
      const auto& docs = obj->GetParamDocs();
      REQUIRE(docs.size() == 1);
      CHECK(docs[0].name == "name");
    }
  }
}
