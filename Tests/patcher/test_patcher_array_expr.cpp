// Tests for the per-element expression family (issue #799) — Max's
// array.expr / array.map / array.filter / array.reduce / array.every /
// array.some / array.foreach on the name-addressed value model .array
// settled (#548).
//
// What has to be proven, and what every case below is one of:
//
//   - **the family shares one model, settled on gArrayExprBase.** The
//     expression is a creation argument, compiled once on the control
//     thread by .expr's own compiler; $1 binds the element and $2 its
//     position (reduce alone shifts: $1 accumulator, $2 element, $3
//     position); a malformed expression fails loudly at parse time and the
//     object then refuses every trigger, counted — never a silent 0
//     written into shared data.
//   - **a symbol element is never seen by the expression.** expr, map and
//     foreach pass it through unchanged (position-stable), filter cannot
//     keep what the expression never accepted, and the fold and the
//     quantifiers skip it — the statistics' population rule (#790).
//   - **map writes back and filter compacts in place**, each one hold of
//     the store's guard, announcing the reference afterwards; expr is the
//     emitting form and foreach the streaming one, .array.iter's snapshot
//     walk with the expression applied in flight.
//   - **the operations cross the control/audio boundary and nothing
//     allocates.** In-patcher delivery dispatches on T_DSP, so "the audio
//     thread runs the map" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses names of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArrayExpr.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::BangSink;
using TestHelpers::IntSink;
using TestHelpers::MultiSink;
using TestHelpers::Wire;
using YSE::PATCHER::gArray;
using YSE::PATCHER::gArrayEvery;
using YSE::PATCHER::gArrayExpr;
using YSE::PATCHER::gArrayFilter;
using YSE::PATCHER::gArrayForeach;
using YSE::PATCHER::gArrayMap;
using YSE::PATCHER::gArrayReduce;
using YSE::PATCHER::gArraySome;

namespace {

  // Records every message it receives, in order and with its type, into a
  // shared log — gArrayIter's test sink, reused because .array.foreach's
  // output *is* a sequence (results first to last, done bang last, each
  // typed). `onAny` lets a case act from *inside* the walk — the mid-walk
  // mutation and the loop-back trigger are both things the results' own
  // subgraph does. Floats are logged doubled to dodge float text.
  struct SeqSink : YSE::PATCHER::pObject {
    std::vector<std::string>* log = nullptr;
    std::string prefix;
    std::function<void()> onAny;

    SeqSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        if (log != nullptr) log->push_back(prefix + "i:" + std::to_string(v));
        if (onAny) onAny();
      });
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) {
        if (log != nullptr) log->push_back(prefix + "f:" + std::to_string(static_cast<int>(v * 2)));
        if (onAny) onAny();
      });
      inputs.back().RegisterList([this](const std::string& v, int, YSE::THREAD) {
        if (log != nullptr) log->push_back(prefix + "l:" + v);
        if (onAny) onAny();
      });
      inputs.back().RegisterBang([this](int, YSE::THREAD) {
        if (log != nullptr) log->push_back(prefix + "<done>");
      });
    }
    const char* Type() const override {
      return "seq_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("array.expr family: all seven registered, with the family's pin shapes (#799)") {
    const struct {
      const char* type;
      int ins;
      int outs;
    } expected[] = {
        {YSE::OBJ::G_ARRAY_EXPR, 2, 2},    {YSE::OBJ::G_ARRAY_MAP, 2, 1},
        {YSE::OBJ::G_ARRAY_FILTER, 2, 1},  {YSE::OBJ::G_ARRAY_REDUCE, 2, 2},
        {YSE::OBJ::G_ARRAY_EVERY, 2, 1},   {YSE::OBJ::G_ARRAY_SOME, 2, 1},
        {YSE::OBJ::G_ARRAY_FOREACH, 2, 2},
    };

    auto names = YSE::PATCHER::Register().AllNames();
    YSE::patcher p;
    p.create(2);
    for (const auto& e : expected) {
      CAPTURE(e.type);
      YSE::pHandle* h = p.CreateObject(e.type);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == e.type);
      CHECK(h->GetInputs() == e.ins);
      CHECK(h->GetOutputs() == e.outs);

      bool found = false;
      for (const auto& name : names) {
        if (name == std::string(e.type)) found = true;
      }
      CHECK(found);
    }
  }

  TEST_CASE("array.expr family: the trigger takes bang and list and no bare number, the "
            "reference inlet only list text (#799)") {
    // No int or float handler anywhere: a bare number names no array —
    // gArrayStatsBase's shape, and on the streaming object .uzi's trap
    // besides.
    gArrayMap g;
    const unsigned int trigger = g.GetInlet(0)->GetAcceptedTypes();
    CHECK((trigger & YSE::PATCHER::IT_BANG) != 0);
    CHECK((trigger & YSE::PATCHER::IT_LIST) != 0);
    CHECK((trigger & YSE::PATCHER::IT_INT) == 0);
    CHECK((trigger & YSE::PATCHER::IT_FLOAT) == 0);
    const unsigned int ref = g.GetInlet(1)->GetAcceptedTypes();
    CHECK((ref & YSE::PATCHER::IT_LIST) != 0);
    CHECK((ref & YSE::PATCHER::IT_BANG) == 0);
  }

  // ─── the compile, and the refusal of a misconfigured object ─────────────────

  TEST_CASE("array.expr family: a malformed expression fails loudly at parse time and the "
            "object then refuses every trigger (#799)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ae799a");
    MultiSink out;
    gArray array;
    gArrayMap map;
    array.SetParent(&p);
    array.SetParams("a799a");
    array.GetInlet(0)->SetList("append 60", YSE::T_GUI);

    map.SetParent(&p);
    map.SetParams("a799a $f1 +");
    Wire(map, 0, out);
    CHECK_FALSE(map.ProgramReady());
    CHECK_FALSE(map.CompileError().empty());

    // The trigger refuses, counted — never .expr's evaluate-to-0, which here
    // would overwrite the shared array with zeros. Nothing is sent and
    // nothing is rewritten.
    const std::uint64_t before = map.Dropped();
    map.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(map.Dropped() == before + 1);
    CHECK_FALSE(out.gotList);
    CHECK(array.ElementAt(0) == "60");
  }

  TEST_CASE("array.expr family: a placeholder past what the object binds is rejected — $2 is "
            "the last of the element-wise six, $3 of reduce (#799)") {
    gArrayMap map;
    map.SetParams("x799b $f3 * 2");
    CHECK_FALSE(map.ProgramReady());
    CHECK_FALSE(map.CompileError().empty());

    gArrayMap map2;
    map2.SetParams("x799b $f2 * 2");
    CHECK(map2.ProgramReady());

    gArrayReduce fold;
    fold.SetParams("x799b $f1 + $f2 * $f3");
    CHECK(fold.ProgramReady());

    gArrayReduce fold2;
    fold2.SetParams("x799b $f4");
    CHECK_FALSE(fold2.ProgramReady());
  }

  TEST_CASE("array.expr family: a name-only object refuses silently — nothing malformed to "
            "report, no work to run (#799)") {
    gArrayExpr g;
    g.SetParams("a799c");
    CHECK_FALSE(g.ProgramReady());
    CHECK(g.CompileError().empty());

    const std::uint64_t before = g.Dropped();
    g.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(g.Dropped() == before + 1);
  }

  // ─── .array.expr ────────────────────────────────────────────────────────────

  TEST_CASE("array.expr: per-element results leave as one typed message, symbols passing "
            "through unchanged (#799)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ae799d");
    MultiSink out;
    BangSink empty;
    gArray array;
    gArrayExpr expr;
    array.SetParent(&p);
    array.SetParams("a799d");
    array.GetInlet(0)->SetList("append 60 2.5 kick", YSE::T_GUI);

    expr.SetParent(&p);
    expr.SetParams("a799d $i1 + 1");
    Wire(expr, 0, out);
    Wire(expr, 1, empty);
    REQUIRE(expr.ProgramReady());

    // $i1 truncates the element towards zero, .expr's own reading: 60 -> 61,
    // 2.5 -> 3, and the symbol stands in for the result the expression never
    // produced — the pass-through, decision 3 — so the output spells exactly
    // the array .array.map would have produced. The array itself is
    // untouched: expr is the emitting twin.
    expr.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(out.gotList);
    CHECK(out.listValue == "61 3 kick");
    CHECK_FALSE(empty.gotBang);
    CHECK(expr.Dropped() == 0);
    CHECK(array.Count() == 3);
    CHECK(array.ElementAt(0) == "60");
    CHECK(array.ElementAt(1) == "2.5");
    CHECK(array.ElementAt(2) == "kick");
  }

  TEST_CASE("array.expr: $2 is the element's position, and a single result leaves as the "
            "value it is (#799)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ae799e");
    MultiSink out;
    BangSink empty;
    gArray array;
    gArrayExpr positions;
    array.SetParent(&p);
    array.SetParams("a799e");
    array.GetInlet(0)->SetList("append 10 x 20", YSE::T_GUI);

    positions.SetParent(&p);
    positions.SetParams("a799e $i2");
    Wire(positions, 0, out);
    Wire(positions, 1, empty);

    // The numeric elements sit at positions 0 and 2 and the symbol passes
    // through between them — which is also the proof the position is the
    // store's, not a numeric ordinal.
    positions.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(out.gotList);
    CHECK(out.listValue == "0 x 2");

    // One numeric element, one result: it leaves as the value it is rather
    // than as a list of one — SendAtoms' rule.
    MultiSink single;
    gArray one;
    gArrayExpr halve;
    one.SetParent(&p);
    one.SetParams("b799e");
    one.GetInlet(0)->SetList("append 60", YSE::T_GUI);
    halve.SetParent(&p);
    halve.SetParams("b799e $f1 / 2");
    Wire(halve, 0, single);
    halve.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(single.gotFloat);
    CHECK(single.floatValue == 30.f);
  }

  TEST_CASE("array.expr: an empty or unnamed array bangs the empty outlet (#799)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ae799f");
    MultiSink out;
    BangSink empty;
    gArray array;
    gArrayExpr expr;
    array.SetParent(&p);
    array.SetParams("a799f");
    expr.SetParent(&p);
    expr.SetParams("a799f $f1 * 2");
    Wire(expr, 0, out);
    Wire(expr, 1, empty);

    expr.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(out.gotList);
    CHECK(empty.bangCount == 1);
    CHECK(expr.Dropped() == 0);
  }

  // ─── .array.map ─────────────────────────────────────────────────────────────

  TEST_CASE("array.map: writes the results back in place, typed by the expression, symbols "
            "staying put, and announces the reference (#799)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("am799a");
    MultiSink out;
    gArray array;
    gArrayMap map;
    array.SetParent(&p);
    array.SetParams("a799g");
    array.GetInlet(0)->SetList("append 60 2.5 kick", YSE::T_GUI);

    map.SetParent(&p);
    map.SetParams("a799g $i1 * 2");
    Wire(map, 0, out);
    REQUIRE(map.ProgramReady());
    CHECK(map.Reference() == "array a799g");

    // $i1 * 2 is an int expression, so the results are spelled as ints; the
    // symbol stays exactly where and what it was, so length and every
    // position survive. The reference leaves after the rewrite, so the
    // family chains.
    map.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(out.gotList);
    CHECK(out.listValue == "array a799g");
    CHECK(array.Count() == 3);
    CHECK(array.ElementAt(0) == "120");
    CHECK(array.ElementAt(1) == "4"); // $i1 truncates 2.5 to 2
    CHECK(array.ElementAt(2) == "kick");
    CHECK(map.Dropped() == 0);
  }

  TEST_CASE("array.map: a float expression's results stay visibly floats (#799)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("am799b");
    gArray array;
    gArrayMap map;
    array.SetParent(&p);
    array.SetParams("a799h");
    array.GetInlet(0)->SetList("append 60 61", YSE::T_GUI);

    map.SetParent(&p);
    map.SetParams("a799h $f1 / 2");
    map.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(array.ElementAt(0) == "30.");
    CHECK(array.ElementAt(1) == "30.5");
  }

  // ─── .array.filter ──────────────────────────────────────────────────────────

  TEST_CASE("array.filter: compacts in place to the accepted elements, in order — a symbol is "
            "never accepted (#799)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("af799a");
    MultiSink out;
    gArray array;
    gArrayFilter filter;
    array.SetParent(&p);
    array.SetParams("a799i");
    array.GetInlet(0)->SetList("append 1 5 kick 10", YSE::T_GUI);

    filter.SetParent(&p);
    filter.SetParams("a799i $f1 >= 5");
    Wire(filter, 0, out);
    REQUIRE(filter.ProgramReady());

    // The kept elements are the ones the expression accepted — it cannot
    // accept what it cannot see, so the symbol goes with the 1 — and they
    // close ranks in their original order. The reference announces the
    // rewrite.
    filter.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(out.gotList);
    CHECK(out.listValue == "array a799i");
    CHECK(array.Count() == 2);
    CHECK(array.ElementAt(0) == "5");
    CHECK(array.ElementAt(1) == "10");
    CHECK(filter.Dropped() == 0);
  }

  TEST_CASE("array.filter: $2 filters by position (#799)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("af799b");
    gArray array;
    gArrayFilter filter;
    array.SetParent(&p);
    array.SetParams("a799j");
    array.GetInlet(0)->SetList("append 10 20 30 40", YSE::T_GUI);

    filter.SetParent(&p);
    filter.SetParams("a799j $i2 % 2 == 0");
    filter.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(array.Count() == 2);
    CHECK(array.ElementAt(0) == "10");
    CHECK(array.ElementAt(1) == "30");
  }

  // ─── .array.reduce ──────────────────────────────────────────────────────────

  TEST_CASE("array.reduce: folds the numeric population, seeded by the first numeric element, "
            "typed by the expression (#799)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ar799a");
    MultiSink out;
    BangSink empty;
    gArray array;
    gArrayReduce sum;
    array.SetParent(&p);
    array.SetParams("a799k");
    array.GetInlet(0)->SetList("append 1 2 pad 3 4", YSE::T_GUI);

    sum.SetParent(&p);
    sum.SetParams("a799k $i1 + $i2");
    Wire(sum, 0, out);
    Wire(sum, 1, empty);
    REQUIRE(sum.ProgramReady());

    // The symbol is skipped — the fold runs over the numeric population, the
    // statistics' rule — and an int expression answers an int.
    sum.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(out.gotInt);
    CHECK(out.intValue == 10);
    CHECK_FALSE(empty.gotBang);

    // A float fold answers a float: a running maximum through max(), which
    // only the first-element seed leaves meaning what it says.
    MultiSink out2;
    BangSink empty2;
    gArrayReduce peak;
    peak.SetParent(&p);
    peak.SetParams("a799k max($f1, $f2)");
    Wire(peak, 0, out2);
    Wire(peak, 1, empty2);
    peak.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(out2.gotFloat);
    CHECK(out2.floatValue == 4.f);
  }

  TEST_CASE("array.reduce: one numeric element answers itself, typed by its spelling — the "
            "expression never runs (#799)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ar799b");
    MultiSink out;
    BangSink empty;
    gArray array;
    gArrayReduce fold;
    array.SetParent(&p);
    array.SetParams("a799l");
    array.GetInlet(0)->SetList("append lead 7 pad", YSE::T_GUI);

    fold.SetParent(&p);
    fold.SetParams("a799l $f1 + $f2");
    Wire(fold, 0, out);
    Wire(fold, 1, empty);
    fold.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(out.gotInt); // "7" spells an int, so the seed answers as one
    CHECK(out.intValue == 7);
    CHECK_FALSE(empty.gotBang);
  }

  TEST_CASE("array.reduce: an empty population bangs the empty outlet — the fold of nothing "
            "does not exist (#799)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ar799c");
    MultiSink out;
    BangSink empty;
    gArray array;
    gArrayReduce fold;
    array.SetParent(&p);
    array.SetParams("a799m");
    array.GetInlet(0)->SetList("append lead pad", YSE::T_GUI);

    fold.SetParent(&p);
    fold.SetParams("a799m $f1 + $f2");
    Wire(fold, 0, out);
    Wire(fold, 1, empty);
    fold.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(out.gotInt);
    CHECK_FALSE(out.gotFloat);
    CHECK(empty.bangCount == 1);
    CHECK(fold.Dropped() == 0);
  }

  // ─── .array.every / .array.some ─────────────────────────────────────────────

  TEST_CASE("array.every / array.some: quantify the numeric population, short-circuited, and "
            "answer vacuously on an empty one (#799)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aq799a");
    gArray array;
    array.SetParent(&p);
    array.SetParams("a799n");
    array.GetInlet(0)->SetList("append 2 4 kick 6", YSE::T_GUI);

    // every: 1 while every numeric element passes — the symbol is not part
    // of the population, so it cannot fail the claim.
    IntSink everyOut;
    gArrayEvery every;
    every.SetParent(&p);
    every.SetParams("a799n $i1 % 2 == 0");
    Wire(every, 0, everyOut);
    REQUIRE(every.ProgramReady());
    every.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(everyOut.gotInt);
    CHECK(everyOut.received == 1);

    // ... and 0 at the first element the expression rejects.
    array.GetInlet(0)->SetList("append 3", YSE::T_GUI);
    everyOut.gotInt = false;
    every.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(everyOut.gotInt);
    CHECK(everyOut.received == 0);

    // some: 1 at the first element the expression accepts, 0 past the last.
    IntSink someOut;
    gArraySome some;
    some.SetParent(&p);
    some.SetParams("a799n $f1 > 5");
    Wire(some, 0, someOut);
    some.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(someOut.gotInt);
    CHECK(someOut.received == 1);

    IntSink noneOut;
    gArraySome none;
    none.SetParent(&p);
    none.SetParams("a799n $f1 > 100");
    Wire(none, 0, noneOut);
    none.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(noneOut.gotInt);
    CHECK(noneOut.received == 0);

    // The vacuous answers, on an array holding no numeric element at all:
    // a claim about nothing is true (every -> 1), and nothing satisfied it
    // (some -> 0). No empty outlet anywhere — a quantifier always has an
    // answer.
    gArray symbols;
    symbols.SetParent(&p);
    symbols.SetParams("b799n");
    symbols.GetInlet(0)->SetList("append lead pad", YSE::T_GUI);

    IntSink vacuousEvery;
    gArrayEvery every2;
    every2.SetParent(&p);
    every2.SetParams("b799n $f1 > 100");
    Wire(every2, 0, vacuousEvery);
    every2.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(vacuousEvery.gotInt);
    CHECK(vacuousEvery.received == 1);

    IntSink vacuousSome;
    gArraySome some2;
    some2.SetParent(&p);
    some2.SetParams("b799n $f1 || 1"); // always true — the vacuity is the population's
    Wire(some2, 0, vacuousSome);
    some2.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(vacuousSome.gotInt);
    CHECK(vacuousSome.received == 0);
  }

  // ─── .array.foreach ─────────────────────────────────────────────────────────

  TEST_CASE("array.foreach: streams one typed result per element, symbols unchanged, done "
            "bang last (#799)") {
    std::vector<std::string> events;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ah799a");
    SeqSink resultOut;
    SeqSink doneOut;
    resultOut.log = &events;
    doneOut.log = &events;
    gArray array;
    gArrayForeach each;
    array.SetParent(&p);
    array.SetParams("a799o");
    array.GetInlet(0)->SetList("append 60 2.5 kick", YSE::T_GUI);

    each.SetParent(&p);
    each.SetParams("a799o $f1 * 2");
    Wire(each, 0, resultOut);
    Wire(each, 1, doneOut);
    REQUIRE(each.ProgramReady());

    // One send per element, first to last: the expression's result for the
    // numeric elements ($f1 * 2 is a float expression, so floats, logged
    // doubled), the symbol as the symbol it is, then the done bang, last.
    each.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(events.size() == 4);
    CHECK(events[0] == "f:240"); // 120, logged doubled
    CHECK(events[1] == "f:10"); // 5, logged doubled
    CHECK(events[2] == "l:kick");
    CHECK(events[3] == "<done>");
    CHECK(each.Dropped() == 0);

    // An empty walk is just the done bang — .uzi's rule for a count of
    // zero, .array.iter's inheritance.
    array.GetInlet(0)->SetList("clear", YSE::T_GUI);
    events.clear();
    each.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(events.size() == 1);
    CHECK(events[0] == "<done>");
  }

  TEST_CASE("array.foreach: the walk is a snapshot, and a re-entrant trigger is refused "
            "(#799)") {
    std::vector<std::string> events;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ah799b");
    SeqSink resultOut;
    SeqSink doneOut;
    resultOut.log = &events;
    doneOut.log = &events;
    gArray array;
    gArrayForeach each;
    array.SetParent(&p);
    array.SetParams("a799p");
    array.GetInlet(0)->SetList("append 1 2 3", YSE::T_GUI);

    each.SetParent(&p);
    each.SetParams("a799p $i1 * 10");
    Wire(each, 0, resultOut);
    Wire(each, 1, doneOut);

    // From inside the first result's send: a renumbering delete, an append,
    // and a loop-back trigger. The walk still emits every element the array
    // held at the trigger — it walks the snapshot, not the live table — and
    // the re-entrant trigger is refused and counted, .uzi's rule.
    bool acted = false;
    resultOut.onAny = [&] {
      if (acted) return;
      acted = true;
      array.GetInlet(0)->SetList("delete 2", YSE::T_GUI);
      array.GetInlet(0)->SetList("append 9", YSE::T_GUI);
      each.GetInlet(0)->SetBang(YSE::T_GUI);
    };

    const std::uint64_t before = each.Dropped();
    each.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(events.size() == 4);
    CHECK(events[0] == "i:10");
    CHECK(events[1] == "i:20");
    CHECK(events[2] == "i:30");
    CHECK(events[3] == "<done>");
    CHECK(each.Dropped() == before + 1);
    // The store itself moved — the proof no guard was held across the sends.
    CHECK(array.Count() == 3);
    CHECK(array.ElementAt(2) == "9");
  }

  // ─── the reference gesture, and the binding ─────────────────────────────────

  TEST_CASE("array.expr family: the reference triggers on the trigger inlet, acknowledges on "
            "its own, and anything else is refused (#799)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ag799a");
    MultiSink out;
    gArray array;
    gArrayFilter filter;
    array.SetParent(&p);
    array.SetParams("a799q");
    array.GetInlet(0)->SetList("append 1 10", YSE::T_GUI);

    filter.SetParent(&p);
    filter.SetParams("a799q $f1 >= 5");
    Wire(filter, 0, out);

    // The array's own reference — the message its .array emits on a bang —
    // runs the operation, exactly as a bang does.
    const std::uint64_t before = filter.Dropped();
    filter.GetInlet(0)->SetList("array a799q", YSE::T_GUI);
    REQUIRE(out.gotList);
    CHECK(out.listValue == "array a799q");
    CHECK(array.Count() == 1);
    CHECK(array.ElementAt(0) == "10");
    CHECK(filter.Dropped() == before);

    // A reference to an array this object is not bound to, and any other
    // message, are refused and counted, never resolved.
    out.reset();
    filter.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    filter.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK_FALSE(out.gotList);
    CHECK(filter.Dropped() == before + 2);

    // The reference inlet acknowledges the bound array silently and refuses
    // anything else — gDictSlice's inlet rule. Neither triggers.
    filter.GetInlet(1)->SetList("array a799q", YSE::T_GUI);
    CHECK(filter.Dropped() == before + 2);
    filter.GetInlet(1)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK(filter.Dropped() == before + 3);
    CHECK_FALSE(out.gotList);
  }

  TEST_CASE("array.expr family: patcherImplementation::SetName re-anchors the binding (#799)") {
    // The rename dispatch itself: an object created inside a patcher must be
    // re-anchored by the patcher, without anybody calling RefreshBinding by
    // hand. The keeper holds the old-address store; before the rename a bang
    // maps its element, after it the map acts on a fresh empty array under
    // the new prefix and the keeper stays as the first bang left it.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ag799b_before");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a799r");
    keeper.GetInlet(0)->SetList("append 4", YSE::T_GUI);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_MAP, "a799r $i1 + 1");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &sinkHandle, 0);

    h->SetBang(0);
    CHECK(keeper.ElementAt(0) == "5");

    p.SetName("ag799b_after");
    h->SetBang(0);
    CHECK(keeper.ElementAt(0) == "5"); // not mapped again: the binding moved
  }

  TEST_CASE("array.expr family: wired from the array's reference outlet, banging the array "
            "runs the operation (#799)") {
    // The flow a patch actually wires, end to end through the public patcher
    // API: the .array's reference outlet into the trigger, a bang on the
    // .array, and the verdict at the far end — the family's gesture.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::patcher p;
    p.create(2);
    p.name("ag799c");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a799s");
    YSE::pHandle* every = p.CreateObject(YSE::OBJ::G_ARRAY_EVERY, "a799s $f1 > 50");
    REQUIRE(array != nullptr);
    REQUIRE(every != nullptr);
    p.Connect(array, 1, every, 0);
    p.Connect(every, 0, &sinkHandle, 0);

    array->SetListData(0, "append 60 64 67");
    array->SetBang(0);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 1);

    sink.reset();
    array->SetListData(0, "append 40");
    array->SetBang(0);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 0);
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.expr family: an operation asked for over in-patcher delivery lands on "
            "T_DSP (#799)") {
    // A .r feeding the trigger dispatches on T_DSP when the block drains it
    // (issue #225) — "the audio thread runs the map" is the ordinary case.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ad799a");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a799t");
    keeper.GetInlet(0)->SetList("append 60 64", YSE::T_GUI);

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go799t");
    YSE::pHandle* map = p.CreateObject(YSE::OBJ::G_ARRAY_MAP, "a799t $i1 + 12");
    REQUIRE(recv != nullptr);
    REQUIRE(map != nullptr);
    p.Connect(recv, 0, map, 0);
    p.Connect(map, 0, &sinkHandle, 0);

    p.PassData(std::string("array a799t"), "go799t", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "array a799t");
    CHECK(keeper.ElementAt(0) == "72");
    CHECK(keeper.ElementAt(1) == "76");
  }

  TEST_CASE("array.expr family: no message path allocates (#799)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every object's trigger — the emit (int, float and symbol results and
    // the list send), the rewrite and the compaction with their reference
    // announcements, the fold, both quantifiers, the streaming walk with all
    // three send paths and the done bang, the empty answers, the refusals
    // (wrong name, unknown message, a misconfigured object's trigger) and
    // the reference-inlet acknowledgement — on T_DSP, in-patcher delivery's
    // thread.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "array probeAE799";
    const std::string wrongName = "array somewhere_else_long";
    const std::string unknown = "frobnicate something quite long indeed";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ap799");

    // One mixed source for the read-only objects — an int, a float and a
    // symbol wide enough to outgrow every small-string buffer — and separate
    // arrays for the two mutators, so their rewrites cannot strip the
    // symbol path out from under the emitters between the warm-up and the
    // probe. The filter keeps every numeric element, so its pass is
    // idempotent.
    gArray source;
    source.SetParent(&p);
    source.SetParams("probeAE799");
    {
      const std::string fill = "append 60 2.5 a_symbol_past_every_small_string_buffer";
      source.GetInlet(0)->SetList(fill, YSE::T_GUI);
    }
    gArray mapStore;
    mapStore.SetParent(&p);
    mapStore.SetParams("probeAM799");
    {
      const std::string fill = "append 1 2 3 keep_this_symbol_where_it_stands";
      mapStore.GetInlet(0)->SetList(fill, YSE::T_GUI);
    }
    gArray filterStore;
    filterStore.SetParent(&p);
    filterStore.SetParams("probeAF799");
    {
      const std::string fill = "append 10 20 30";
      filterStore.GetInlet(0)->SetList(fill, YSE::T_GUI);
    }

    MultiSink exprOut;
    BangSink exprEmpty;
    MultiSink mapOut;
    MultiSink filterOut;
    MultiSink foldOut;
    BangSink foldEmpty;
    IntSink everyOut;
    IntSink someOut;
    MultiSink eachOut;
    BangSink eachDone;

    gArrayExpr expr;
    expr.SetParent(&p);
    expr.SetParams("probeAE799 $f1 * 2");
    Wire(expr, 0, exprOut);
    Wire(expr, 1, exprEmpty);

    gArrayMap map;
    map.SetParent(&p);
    map.SetParams("probeAM799 $i1 + 1");
    Wire(map, 0, mapOut);

    gArrayFilter filter;
    filter.SetParent(&p);
    filter.SetParams("probeAF799 $f1 < 1000");
    Wire(filter, 0, filterOut);

    gArrayReduce fold;
    fold.SetParent(&p);
    fold.SetParams("probeAE799 $f1 + $f2");
    Wire(fold, 0, foldOut);
    Wire(fold, 1, foldEmpty);

    gArrayEvery every;
    every.SetParent(&p);
    every.SetParams("probeAE799 $f1 > 0");
    Wire(every, 0, everyOut);

    gArraySome some;
    some.SetParent(&p);
    some.SetParams("probeAE799 $f1 > 100");
    Wire(some, 0, someOut);

    gArrayForeach each;
    each.SetParent(&p);
    each.SetParams("probeAE799 $f1 * 2");
    Wire(each, 0, eachOut);
    Wire(each, 1, eachDone);

    // A name-only object — the misconfigured trigger's refusal path.
    gArrayMap bare;
    bare.SetParent(&p);
    bare.SetParams("probeAM799");

    // Warm every path — including the sinks' assignments — so first-call
    // machinery is not what the probe catches.
    expr.GetInlet(0)->SetBang(YSE::T_GUI);
    map.GetInlet(0)->SetBang(YSE::T_GUI);
    filter.GetInlet(0)->SetBang(YSE::T_GUI);
    fold.GetInlet(0)->SetBang(YSE::T_GUI);
    every.GetInlet(0)->SetBang(YSE::T_GUI);
    some.GetInlet(0)->SetBang(YSE::T_GUI);
    each.GetInlet(0)->SetBang(YSE::T_GUI);
    bare.GetInlet(0)->SetBang(YSE::T_GUI);
    expr.GetInlet(0)->SetList(reference, YSE::T_GUI);
    expr.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    expr.GetInlet(0)->SetList(unknown, YSE::T_GUI);
    expr.GetInlet(1)->SetList(reference, YSE::T_GUI);

    const std::uint64_t exprBefore = expr.Dropped();
    const std::uint64_t bareBefore = bare.Dropped();
    const int doneBefore = eachDone.bangCount;
    exprOut.reset();
    eachOut.reset();

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      expr.GetInlet(0)->SetBang(YSE::T_DSP);
      map.GetInlet(0)->SetBang(YSE::T_DSP);
      filter.GetInlet(0)->SetBang(YSE::T_DSP);
      fold.GetInlet(0)->SetBang(YSE::T_DSP);
      every.GetInlet(0)->SetBang(YSE::T_DSP);
      some.GetInlet(0)->SetBang(YSE::T_DSP);
      each.GetInlet(0)->SetBang(YSE::T_DSP);
      bare.GetInlet(0)->SetBang(YSE::T_DSP);
      expr.GetInlet(0)->SetList(wrongName, YSE::T_DSP);
      expr.GetInlet(0)->SetList(unknown, YSE::T_DSP);
      expr.GetInlet(1)->SetList(reference, YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing.
    CHECK(exprOut.gotList); // "120. 5. a_symbol_..." — the emitted results
    CHECK(mapOut.gotList); // the rewrite announced
    CHECK(filterOut.gotList); // the compaction announced
    CHECK(foldOut.gotFloat); // 62.5 both times
    CHECK(everyOut.received == 1);
    CHECK(someOut.received == 0);
    CHECK(eachOut.gotFloat); // the walk's numeric results
    CHECK(eachOut.gotList); // ... and its symbol pass-through
    CHECK(eachDone.bangCount == doneBefore + 1);
    CHECK(expr.Dropped() == exprBefore + 2);
    CHECK(bare.Dropped() == bareBefore + 1);
    // The mutators really ran under the probe: the map moved every numeric
    // element up one more time, the symbol staying put.
    CHECK(mapStore.ElementAt(0) == "3");
    CHECK(mapStore.ElementAt(3) == "keep_this_symbol_where_it_stands");
    CHECK(filterStore.Count() == 3);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.expr family: params survive a DumpJSON / ParseJSON round trip (#799)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_ARRAY_MAP, "notes799 $i1 + 1");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".array.map"));
    CHECK(copy->GetParams() == std::string("notes799 $i1 + 1"));
    CHECK(copy->GetInputs() == 2);
    CHECK(copy->GetOutputs() == 1);
  }

  TEST_CASE("array.expr family: all seven carry complete documentation metadata (#799)") {
    gArrayExpr expr;
    gArrayMap map;
    gArrayFilter filter;
    gArrayReduce reduce;
    gArrayEvery every;
    gArraySome some;
    gArrayForeach each;
    YSE::PATCHER::pObject* objects[] = {&expr, &map, &filter, &reduce, &every, &some, &each};
    for (auto* g : objects) {
      CAPTURE(g->Type());
      CHECK_FALSE(g->GetDescription().empty());
      CHECK(g->GetCategory() == YSE::PATCHER::pCategory::GENERIC);
      const auto& docs = g->GetParamDocs();
      REQUIRE(docs.size() == 2);
      CHECK(docs[0].name == "name");
      CHECK(docs[1].name == "expression");
    }
  }
}
