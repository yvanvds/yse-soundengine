// Tests for .prob (issue #456) — a weighted transition table, walked.
//
// "A number came out" is not coverage for this object. A .random with the right
// range satisfies that, and so does an implementation that ignores the weights
// entirely, or one that quietly walks to a transition whose weight is zero.
// What actually characterises a first-order Markov walk is
//
//   * reachability — a transition is taken only where its weight is above
//     zero, and every transition with a positive weight is reachable;
//   * proportion — over many bangs the outcomes match the *declared* weights,
//     not merely "all of them happened";
//   * the dead end — a state with nothing (or nothing positive) to go to emits
//     nothing on outlet 0, bangs outlet 1, and reverts to the reset fallback so
//     the walk can recover on the next bang;
//   * table state — `clear` really empties it, a re-sent pair replaces rather
//     than accumulates, and the fixed capacity is a limit rather than an
//     overrun;
//   * reproducibility — a seeded object replays the same walk exactly, and one
//     draw is taken per bang whether or not that bang found a transition.
//
// ### Why the statistical checks cannot flake
//
// Every test below seeds the object explicitly, and RandomSource is pure
// integer arithmetic over a per-object counter — no clock, no thread identity,
// no floating point. Each sequence is therefore a fixed, platform-independent
// list of numbers and every count below is deterministic. The tolerance is not
// what keeps CI green; it is what keeps the assertion *meaningful* if the
// generator or the reduction is ever legitimately changed.
//
// ### Where N comes from
//
// Over N bangs the count of a particular outcome is Binomial(N, p), with
// standard deviation sqrt(N*p*(1-p)). Every band below is +/- 6 sigma, a
// two-sided tail probability of about 2e-9 — wide enough that an honest
// replacement generator passes.
//
// N follows from the smallest error worth catching. The precedent is issue
// #455, where a 6-sigma band at N = 100000 was measured to let a deliberately
// mutated 51/49 coin *pass*: one percent of absolute probability error is only
// 6.3 sigma there, close enough to the edge that sampling noise carried it back
// inside. So one percent of absolute error is the bar this file has to clear
// comfortably, not marginally.
//
// At N = 1000000 the 6-sigma half-widths are 2905 (p = 0.375), 3000 (p = 0.5)
// and 1985 (p = 0.125), i.e. 0.29% / 0.30% / 0.20%. An error of one percentage
// point is 10000 counts, which misses those bands by 21, 20 and 30 sigma
// respectively; even half a percentage point misses by 10 sigma or more. That
// is the margin #455 lacked.
//
// The bands were mutation-verified rather than assumed. Three deliberate
// defects were built into TransitionTable::Pick and measured against this file:
//
//   1. compressing the draw by one percent before the reduction — which moves
//      the 37.5% bucket by only 0.36 percentage points, the 50% bucket by 0.51
//      and the 12.5% bucket by 0.87 — fails all three proportion tests (7.4,
//      10.2 and 26.2 sigma from centre against a 6-sigma band) and also
//      squeezes out the 1-in-1001 tail that `every positive-weight transition
//      is reachable` watches;
//   2. an off-by-one at the prefix boundary (`target <= running`) fails the
//      same four plus both "a different seed / stream gives a different walk"
//      cases;
//   3. flooring a zero weight at one — a plausible piece of defensive coding —
//      fails `a zero-weight transition is never taken`, `a state whose
//      transitions all weigh zero is a dead end`, and three more.
//
// Each of the three leaves most of the file passing, so the failures name the
// defect rather than smearing across every case.
//
// Defect 1 is the one that justifies the sample size. Ten times fewer bangs and
// the same defect lands at 2.3 sigma on the 37.5% bucket and 3.2 sigma on the
// 50% one — comfortably inside a 6-sigma band, with only the smallest bucket
// still catching it. That is the #455 failure mode exactly: a real distortion
// surviving because the sample was an order of magnitude too small to resolve
// it. At a million it is caught three ways over.
//
// Nothing here needs an audio device.

#include <doctest/doctest.h>
#include <atomic>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/gProb.h"
#include "patcher/math/gTransitionTable.h"
#include "patcher/sinks.hpp"

using TestHelpers::BangSink;
using TestHelpers::IntSink;
using TestHelpers::ListSink;

namespace {

  // A .prob with all three outlets wired: the states on outlet 0, the dead-end
  // report on outlet 1, the dump on outlet 2.
  struct ProbRig {
    YSE::PATCHER::gProb prob;
    IntSink values;
    BangSink stuck;
    ListSink dumped;

    explicit ProbRig(const char* params = nullptr) {
      prob.ConnectOutlet(values.GetInlet(0), 0);
      values.ConnectInlet(prob.GetOutlet(0), 0);
      prob.ConnectOutlet(stuck.GetInlet(0), 1);
      stuck.ConnectInlet(prob.GetOutlet(1), 0);
      prob.ConnectOutlet(dumped.GetInlet(0), 2);
      dumped.ConnectInlet(prob.GetOutlet(2), 0);
      if (params != nullptr) prob.SetParams(params);
    }

    // Bang the hot inlet. Returns true when a state came out (and leaves it in
    // `values.received`), false when the object reported a dead end instead.
    bool Bang() {
      values.gotInt = false;
      stuck.gotBang = false;
      prob.GetInlet(0)->SetBang(YSE::T_GUI);
      return values.gotInt;
    }

    // `count` bangs, failing the test rather than short-changing the caller if
    // the walk gets stuck.
    std::vector<int> Walk(int count) {
      std::vector<int> out;
      out.reserve(static_cast<size_t>(count));
      for (int i = 0; i < count; ++i) {
        REQUIRE(Bang());
        out.push_back(values.received);
      }
      return out;
    }

    // `count` bangs, each departing from `state`. Setting the state costs no
    // random draw, so this is `count` independent draws from one state's
    // distribution rather than a chain whose sample is diluted by the return
    // transitions — which is what the proportion tests need.
    std::vector<int> WalkFrom(int state, int count) {
      std::vector<int> out;
      out.reserve(static_cast<size_t>(count));
      for (int i = 0; i < count; ++i) {
        SetState(state);
        REQUIRE(Bang());
        out.push_back(values.received);
      }
      return out;
    }

    void Transition(int from, int to, int weight) {
      prob.GetInlet(0)->SetList(std::to_string(from) + " " + std::to_string(to) + " " +
                                    std::to_string(weight),
                                YSE::T_GUI);
    }
    void SetState(int state) {
      prob.GetInlet(0)->SetInt(state, YSE::T_GUI);
    }
    void Clear() {
      prob.GetInlet(0)->SetList("clear", YSE::T_GUI);
    }
    void Dump() {
      prob.GetInlet(0)->SetList("dump", YSE::T_GUI);
    }
    void Reset(int state) {
      prob.GetInlet(0)->SetList("reset " + std::to_string(state), YSE::T_GUI);
    }
    void Reseed(int s) {
      prob.GetInlet(0)->SetList("seed " + std::to_string(s), YSE::T_GUI);
    }
  };

  // Collects every list a dump produces, in order.
  struct DumpCollector : YSE::PATCHER::pObject {
    std::vector<std::string> lines;
    DumpCollector() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { lines.push_back(v); });
    }
    const char* Type() const override {
      return "dump_collector";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // Thread-safe tally for the concurrency case. A plain IntSink would *be* the
  // race rather than observe it.
  struct ProbTallySink : YSE::PATCHER::pObject {
    std::atomic<int>& total;
    std::atomic<int>& strays;

    ProbTallySink(std::atomic<int>& totalCount, std::atomic<int>& strayCount)
      : pObject(false), total(totalCount), strays(strayCount) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        total.fetch_add(1);
        if (v != 1 && v != 2 && v != 3) strays.fetch_add(1);
      });
    }
    const char* Type() const override {
      return "prob_tally_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  int CountOf(const std::vector<int>& walk, int value) {
    int n = 0;
    for (int v : walk) {
      if (v == value) n++;
    }
    return n;
  }

  // 6 sigma of Binomial(n, p), rounded up. See the header comment for why every
  // band in this file is derived rather than guessed.
  int SixSigma(int n, double p) {
    return static_cast<int>(std::ceil(6.0 * std::sqrt(static_cast<double>(n) * p * (1.0 - p))));
  }

  // A seed unrelated to anything else in this file, so nothing can pass by
  // coincidence.
  constexpr int SEED = 918273;

  // The large-sample size. See the header comment: one percentage point of
  // error misses every band below by at least 20 sigma here.
  constexpr int BIG = 1000000;

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("prob: creatable through the registry with the documented shape (#456)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_PROB);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".prob"));
    CHECK(std::string(YSE::OBJ::G_PROB) == std::string(".prob"));
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 3);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::INT);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::BANG);
    CHECK(h->OutputDataType(2) == YSE::OUT_TYPE::LIST);
  }

  TEST_CASE("prob: is listed by the registry (#456)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& n : names) {
      if (n == std::string(YSE::OBJ::G_PROB)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("prob: names both parameters in order (#456)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_PROB));
    REQUIRE(obj != nullptr);
    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 2);
    CHECK(docs[0].name == std::string("reset"));
    CHECK(docs[1].name == std::string("seed"));
  }

  TEST_CASE("prob: params survive a DumpJSON / ParseJSON round trip (#456)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_PROB, "7 4242") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".prob") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".prob"));
    CHECK(h->GetParams() == std::string("7 4242"));
    // The reset parameter is also where the walk starts, so it has to have
    // reached the object and not just the parameter string.
    CHECK(h->GetGuiValue() == std::string("7"));
  }

  // ─── building the table ─────────────────────────────────────────────────────

  TEST_CASE("prob: a single transition is taken every time (#456)") {
    ProbRig rig(("0 " + std::to_string(SEED)).c_str());
    rig.Transition(0, 5, 1);
    rig.Transition(5, 0, 1); // so the walk can come back and repeat

    const std::vector<int> walk = rig.Walk(20);
    for (size_t i = 0; i < walk.size(); ++i) {
      CAPTURE(i);
      CHECK(walk[i] == ((i % 2 == 0) ? 5 : 0));
    }
  }

  TEST_CASE("prob: a state can transition to itself (#456)") {
    // Max documents this explicitly: `state state weight` is legal.
    ProbRig rig(("3 " + std::to_string(SEED)).c_str());
    rig.Transition(3, 3, 1);
    const std::vector<int> walk = rig.Walk(50);
    for (int v : walk)
      CHECK(v == 3);
  }

  TEST_CASE("prob: re-sending a pair replaces its weight rather than adding to it (#456)") {
    // Otherwise a patch that re-states its table on every load would drift the
    // probabilities further every time.
    ProbRig rig(("1 " + std::to_string(SEED)).c_str());
    rig.Transition(1, 2, 1);
    rig.Transition(1, 3, 1);
    rig.Transition(1, 2, 0); // switch 1->2 off again

    const std::vector<int> walk = rig.WalkFrom(1, 2000);
    CHECK(CountOf(walk, 2) == 0);
    CHECK(CountOf(walk, 3) == 2000);
    // Two entries out of state 1, not three: the pair was replaced, not
    // duplicated.
    CHECK(rig.prob.Entries() == 2);
  }

  TEST_CASE("prob: negative states and destinations are legal (#456)") {
    ProbRig rig(("-4 " + std::to_string(SEED)).c_str());
    rig.Transition(-4, -9, 1);
    rig.Transition(-9, -4, 1);
    const std::vector<int> walk = rig.Walk(10);
    for (size_t i = 0; i < walk.size(); ++i) {
      CAPTURE(i);
      CHECK(walk[i] == ((i % 2 == 0) ? -9 : -4));
    }
  }

  // ─── weight > 0 is the only way through ─────────────────────────────────────

  TEST_CASE("prob: a zero-weight transition is never taken (#456)") {
    // The claim the proportion test cannot make on its own: 0 does not mean
    // "rarely", it means "never". 200000 bangs, and an implementation that
    // treated a zero weight as one-in-a-few-thousand would show up long before
    // the end.
    constexpr int N = 200000;
    ProbRig rig(("1 " + std::to_string(SEED)).c_str());
    rig.Transition(1, 2, 5);
    rig.Transition(1, 3, 0); // never
    rig.Transition(1, 4, 5);

    const std::vector<int> walk = rig.WalkFrom(1, N);
    CHECK(CountOf(walk, 3) == 0);
    // ...and the two positive ones are both actually reachable, so the zero is
    // being skipped rather than the whole state being broken.
    CHECK(CountOf(walk, 2) > 0);
    CHECK(CountOf(walk, 4) > 0);
  }

  TEST_CASE("prob: a negative weight is clamped to zero, not to a large one (#456)") {
    // Signed arithmetic in the prefix sum is where a negative weight does real
    // damage: it makes the total smaller than a prefix of itself, so the scan
    // falls off the end of the table. Clamping is asserted from the outside —
    // the transition simply never fires.
    ProbRig rig(("1 " + std::to_string(SEED)).c_str());
    rig.Transition(1, 2, 4);
    rig.Transition(1, 3, -1000);

    const std::vector<int> walk = rig.WalkFrom(1, 20000);
    CHECK(CountOf(walk, 3) == 0);
    CHECK(CountOf(walk, 2) == 20000);
  }

  TEST_CASE("prob: a state whose transitions all weigh zero is a dead end (#456)") {
    ProbRig rig(("1 " + std::to_string(SEED)).c_str());
    rig.Transition(1, 2, 0);
    rig.Transition(1, 3, 0);

    CHECK_FALSE(rig.Bang());
    CHECK(rig.stuck.bangCount == 1);
  }

  TEST_CASE("prob: every positive-weight transition is reachable (#456)") {
    // The mirror of the zero-weight test: a tiny weight next to a huge one must
    // still be reachable. 1 in 1001, over 200000 bangs, is expected 200 times;
    // seeing zero would mean the small tail of the prefix scan is unreachable.
    constexpr int N = 200000;
    ProbRig rig(("1 " + std::to_string(SEED)).c_str());
    rig.Transition(1, 2, 1000);
    rig.Transition(1, 3, 1);

    const std::vector<int> walk = rig.WalkFrom(1, N);
    CHECK(CountOf(walk, 3) > 0);
  }

  // ─── the declared weights are the observed weights ──────────────────────────

  TEST_CASE("prob: the outcome split matches the declared weights (#456)") {
    // Max's own worked example: weights 3, 4 and 1 out of one state give
    // 37.5%, 50% and 12.5%.
    //
    // Bands are 6 sigma of Binomial(BIG, p); see the header comment for why BIG
    // is a million and not a hundred thousand.
    ProbRig rig(("1 " + std::to_string(SEED)).c_str());
    rig.Transition(1, 2, 3);
    rig.Transition(1, 3, 4);
    rig.Transition(1, 4, 1);

    const std::vector<int> walk = rig.WalkFrom(1, BIG);

    const struct {
      int value;
      double p;
    } expected[] = {{2, 0.375}, {3, 0.5}, {4, 0.125}};

    int accounted = 0;
    for (const auto& e : expected) {
      CAPTURE(e.value);
      const int seen = CountOf(walk, e.value);
      accounted += seen;
      const int centre = static_cast<int>(e.p * BIG);
      const int band = SixSigma(BIG, e.p);
      CAPTURE(seen);
      CAPTURE(centre);
      CAPTURE(band);
      CHECK(seen > centre - band);
      CHECK(seen < centre + band);
    }
    // Nothing else came out at all.
    CHECK(accounted == BIG);
  }

  TEST_CASE("prob: weights scale, so 30/40/10 is the same chain as 3/4/1 (#456)") {
    // Weights are relative, not absolute probabilities. Multiplying them all by
    // ten must leave the distribution alone — which an implementation that
    // treated the weight as a percentage, or normalised against a fixed total,
    // would not.
    ProbRig rig(("1 " + std::to_string(SEED)).c_str());
    rig.Transition(1, 2, 30);
    rig.Transition(1, 3, 40);
    rig.Transition(1, 4, 10);

    const std::vector<int> walk = rig.WalkFrom(1, BIG);
    const struct {
      int value;
      double p;
    } expected[] = {{2, 0.375}, {3, 0.5}, {4, 0.125}};
    for (const auto& e : expected) {
      CAPTURE(e.value);
      const int seen = CountOf(walk, e.value);
      const int centre = static_cast<int>(e.p * BIG);
      const int band = SixSigma(BIG, e.p);
      CAPTURE(seen);
      CHECK(seen > centre - band);
      CHECK(seen < centre + band);
    }
  }

  TEST_CASE("prob: each state keeps its own weights (#456)") {
    // A first-order chain is per-state, so summing weights across the whole
    // table rather than per departure state is the classic implementation
    // error. State 1 leans 7:1 toward 2 and state 2 leans 1:7 the other way; a
    // table-wide sum would blur both into the same 50/50 split, which is 300
    // sigma away from either band below.
    constexpr int N = 400000;
    ProbRig rig(("1 " + std::to_string(SEED)).c_str());
    rig.Transition(1, 2, 7);
    rig.Transition(1, 3, 1);
    rig.Transition(2, 2, 1);
    rig.Transition(2, 3, 7);

    const int band = SixSigma(N, 0.125);

    const std::vector<int> fromOne = rig.WalkFrom(1, N);
    const int threesFromOne = CountOf(fromOne, 3);
    CAPTURE(threesFromOne);
    CAPTURE(band);
    CHECK(threesFromOne > (N / 8) - band);
    CHECK(threesFromOne < (N / 8) + band);

    const std::vector<int> fromTwo = rig.WalkFrom(2, N);
    const int twosFromTwo = CountOf(fromTwo, 2);
    CAPTURE(twosFromTwo);
    CHECK(twosFromTwo > (N / 8) - band);
    CHECK(twosFromTwo < (N / 8) + band);
  }

  // ─── dead ends ──────────────────────────────────────────────────────────────

  TEST_CASE("prob: an empty table bangs outlet 1 and emits nothing (#456)") {
    // Max: "no output will be produced if no input has been received or if the
    // contents have been cleared".
    ProbRig rig(std::to_string(SEED).c_str());
    for (int i = 1; i <= 20; ++i) {
      CAPTURE(i);
      CHECK_FALSE(rig.Bang());
      CHECK(rig.stuck.bangCount == i);
      CHECK_FALSE(rig.values.gotInt);
    }
  }

  TEST_CASE("prob: a state with no outgoing transitions is a dead end (#456)") {
    ProbRig rig(("1 " + std::to_string(SEED)).c_str());
    rig.Transition(1, 2, 1); // 2 goes nowhere

    REQUIRE(rig.Bang());
    CHECK(rig.values.received == 2);
    CHECK_FALSE(rig.Bang()); // stuck on 2
    CHECK(rig.stuck.bangCount == 1);
  }

  TEST_CASE("prob: a dead end reverts to the reset state and the walk recovers (#456)") {
    // Max's `reset`: "what number to revert to in the event that it gets stuck".
    // The recovery is on the *next* bang — one bang is one attempt, never a
    // search for a state with an exit.
    ProbRig rig(("1 " + std::to_string(SEED)).c_str());
    rig.Transition(1, 2, 1); // 2 is a dead end

    REQUIRE(rig.Bang());
    CHECK(rig.values.received == 2);
    CHECK_FALSE(rig.Bang()); // stuck, and now back on state 1
    CHECK(rig.prob.GetGuiValue() == std::string("1"));
    REQUIRE(rig.Bang()); // recovered
    CHECK(rig.values.received == 2);
  }

  TEST_CASE("prob: `reset <n>` chooses where a dead end lands (#456)") {
    ProbRig rig(("0 " + std::to_string(SEED)).c_str());
    rig.Transition(9, 9, 1); // only state 9 has an exit
    rig.Reset(9);

    CHECK_FALSE(rig.Bang()); // state 0 is a dead end -> revert to 9
    CHECK(rig.stuck.bangCount == 1);
    REQUIRE(rig.Bang());
    CHECK(rig.values.received == 9);
  }

  TEST_CASE("prob: a dead-end reset state simply keeps reporting (#456)") {
    // The degenerate case has to terminate rather than spin: if the fallback is
    // itself stuck, every bang is one bang out of outlet 1 and no more.
    ProbRig rig(("1 " + std::to_string(SEED)).c_str());
    rig.Transition(5, 6, 1); // nothing leaves state 1, and reset is 1
    for (int i = 1; i <= 100; ++i) {
      CAPTURE(i);
      CHECK_FALSE(rig.Bang());
      CHECK(rig.stuck.bangCount == i);
    }
  }

  // ─── clear ──────────────────────────────────────────────────────────────────

  TEST_CASE("prob: `clear` empties the table (#456)") {
    ProbRig rig(("1 " + std::to_string(SEED)).c_str());
    rig.Transition(1, 1, 1);
    REQUIRE(rig.Bang());
    REQUIRE(rig.prob.Entries() == 1);

    rig.Clear();
    CHECK(rig.prob.Entries() == 0);
    CHECK_FALSE(rig.Bang());
    CHECK(rig.stuck.bangCount == 1);
  }

  TEST_CASE("prob: a cleared table can be rebuilt from scratch (#456)") {
    ProbRig rig(("1 " + std::to_string(SEED)).c_str());
    rig.Transition(1, 2, 1);
    rig.Transition(2, 1, 1);
    REQUIRE(rig.Walk(4).size() == 4u);

    rig.Clear();
    rig.SetState(1);
    rig.Transition(1, 7, 1);
    rig.Transition(7, 1, 1);
    CHECK(rig.prob.Entries() == 2); // not 4 — the old pairs are gone
    const std::vector<int> walk = rig.Walk(6);
    CHECK(CountOf(walk, 2) == 0);
    CHECK(CountOf(walk, 7) == 3);
  }

  // ─── setting the current state ──────────────────────────────────────────────

  TEST_CASE("prob: an int sets the departure state without emitting (#456)") {
    // Max: "int — sets (but does not send out) the current number value."
    ProbRig rig(("0 " + std::to_string(SEED)).c_str());
    rig.Transition(4, 8, 1);

    rig.values.gotInt = false;
    rig.SetState(4);
    CHECK_FALSE(rig.values.gotInt);
    CHECK(rig.stuck.bangCount == 0);
    CHECK(rig.prob.GetGuiValue() == std::string("4"));

    REQUIRE(rig.Bang());
    CHECK(rig.values.received == 8);
  }

  TEST_CASE("prob: a float sets the departure state truncated (#456)") {
    ProbRig rig(("0 " + std::to_string(SEED)).c_str());
    rig.Transition(4, 8, 1);
    rig.prob.GetInlet(0)->SetFloat(4.9f, YSE::T_GUI);
    REQUIRE(rig.Bang());
    CHECK(rig.values.received == 8);
  }

  // ─── dump ───────────────────────────────────────────────────────────────────

  TEST_CASE("prob: `dump` reports every entry in insertion order (#456)") {
    YSE::PATCHER::gProb prob;
    prob.SetParams("0 1");
    DumpCollector collector;
    prob.ConnectOutlet(collector.GetInlet(0), 2);
    collector.ConnectInlet(prob.GetOutlet(2), 0);

    prob.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    prob.GetInlet(0)->SetList("2 5 7", YSE::T_GUI);
    prob.GetInlet(0)->SetList("1 4 0", YSE::T_GUI);
    prob.GetInlet(0)->SetList("dump", YSE::T_GUI);

    REQUIRE(collector.lines.size() == 3u);
    CHECK(collector.lines[0] == std::string("1 2 3"));
    CHECK(collector.lines[1] == std::string("2 5 7"));
    CHECK(collector.lines[2] == std::string("1 4 0"));
  }

  TEST_CASE("prob: `dump` reports the replaced weight, not the original (#456)") {
    YSE::PATCHER::gProb prob;
    prob.SetParams("0 1");
    DumpCollector collector;
    prob.ConnectOutlet(collector.GetInlet(0), 2);
    collector.ConnectInlet(prob.GetOutlet(2), 0);

    prob.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    prob.GetInlet(0)->SetList("1 2 9", YSE::T_GUI);
    prob.GetInlet(0)->SetList("dump", YSE::T_GUI);

    REQUIRE(collector.lines.size() == 1u);
    CHECK(collector.lines[0] == std::string("1 2 9"));
  }

  TEST_CASE("prob: `dump` on an empty table says nothing (#456)") {
    ProbRig rig(("0 " + std::to_string(SEED)).c_str());
    rig.dumped.gotList = false;
    rig.Dump();
    CHECK_FALSE(rig.dumped.gotList);
  }

  // ─── the message grammar ────────────────────────────────────────────────────

  TEST_CASE("prob: a list that is not three numbers is ignored (#456)") {
    // Two numbers is an incomplete entry and four is a different message; both
    // are safer dropped than half-read. A silently truncated four-number list
    // would install a transition the patch never asked for.
    ProbRig rig(("1 " + std::to_string(SEED)).c_str());
    for (const char* junk :
         {"1 2", "1", "", "1 2 3 4", "banana", "1 2 three", "clear 3", "resetting 4", " ", "-"}) {
      CAPTURE(junk);
      rig.prob.GetInlet(0)->SetList(junk, YSE::T_GUI);
    }
    CHECK(rig.prob.Entries() == 0);
    CHECK_FALSE(rig.Bang());
  }

  TEST_CASE("prob: extra whitespace between the three numbers is fine (#456)") {
    ProbRig rig(("1 " + std::to_string(SEED)).c_str());
    rig.prob.GetInlet(0)->SetList("  1   2    5 ", YSE::T_GUI);
    REQUIRE(rig.prob.Entries() == 1);
    REQUIRE(rig.Bang());
    CHECK(rig.values.received == 2);
  }

  TEST_CASE("prob: a malformed `reset` or `seed` argument is ignored (#456)") {
    ProbRig a(("1 " + std::to_string(SEED)).c_str());
    ProbRig b(("1 " + std::to_string(SEED)).c_str());
    a.Transition(1, 2, 1);
    a.Transition(1, 3, 1);
    b.Transition(1, 2, 1);
    b.Transition(1, 3, 1);
    b.Transition(2, 1, 1);
    a.Transition(2, 1, 1);
    b.Transition(3, 1, 1);
    a.Transition(3, 1, 1);

    for (const char* junk : {"reset x", "reset", "seed x", "seed", "resets 4", "seeded 4"}) {
      CAPTURE(junk);
      b.prob.GetInlet(0)->SetList(junk, YSE::T_GUI);
    }
    // Neither the fallback nor the stream moved, and no entry was created.
    CHECK(b.prob.Entries() == 4);
    CHECK(a.Walk(200) == b.Walk(200));
  }

  // ─── capacity ───────────────────────────────────────────────────────────────

  TEST_CASE("prob: the table holds its documented capacity and no more (#456)") {
    // The array is fixed, so the overflow path has to drop the entry rather
    // than write past the end. Fill it exactly, then push one more.
    constexpr int CAP = YSE::PATCHER::TransitionTable::CAPACITY;
    ProbRig rig(("0 " + std::to_string(SEED)).c_str());
    for (int i = 0; i < CAP; ++i)
      rig.Transition(0, i, 1);
    CHECK(rig.prob.Entries() == CAP);

    rig.Transition(0, 999999, 1);
    CHECK(rig.prob.Entries() == CAP);

    // The full table still walks, and never to the transition that was dropped.
    const std::vector<int> walk = rig.WalkFrom(0, 5000);
    CHECK(CountOf(walk, 999999) == 0);
    for (int v : walk) {
      REQUIRE(v >= 0);
      REQUIRE(v < CAP);
    }
    // Every slot is genuinely reachable rather than the scan stopping short:
    // 5000 uniform draws over 1024 destinations leave a particular one unseen
    // with probability (1023/1024)^5000, about 1 in 130.
    CHECK(std::set<int>(walk.begin(), walk.end()).size() > 900u);

    // ...and an existing pair can still be updated once the table is full.
    rig.Transition(0, 5, 7);
    CHECK(rig.prob.Entries() == CAP);
  }

  // ─── reproducibility ────────────────────────────────────────────────────────

  TEST_CASE("prob: the same seed replays the same walk (#456)") {
    auto build = [](const char* params) {
      auto rig = std::make_unique<ProbRig>(params);
      rig->Transition(1, 2, 3);
      rig->Transition(1, 3, 4);
      rig->Transition(1, 4, 1);
      rig->Transition(2, 1, 1);
      rig->Transition(3, 1, 1);
      rig->Transition(4, 1, 1);
      return rig;
    };
    auto a = build("1 4242");
    auto b = build("1 4242");
    CHECK(a->Walk(2000) == b->Walk(2000));
  }

  TEST_CASE("prob: a different seed gives a different walk (#456)") {
    // Adjacent seeds specifically: they are what an off-by-one in the seed
    // normalisation collapses onto the same stream.
    auto build = [](const char* params) {
      auto rig = std::make_unique<ProbRig>(params);
      rig->Transition(1, 2, 1);
      rig->Transition(1, 3, 1);
      rig->Transition(2, 1, 1);
      rig->Transition(3, 1, 1);
      return rig;
    };
    auto a = build("1 4242");
    auto b = build("1 4243");
    CHECK(a->Walk(500) != b->Walk(500));
  }

  TEST_CASE("prob: seed 0 leaves neighbouring objects on separate streams (#456)") {
    auto build = []() {
      auto rig = std::make_unique<ProbRig>("1 0");
      rig->Transition(1, 2, 1);
      rig->Transition(1, 3, 1);
      rig->Transition(2, 1, 1);
      rig->Transition(3, 1, 1);
      return rig;
    };
    auto a = build();
    auto b = build();
    CHECK(a->Walk(500) != b->Walk(500));
  }

  TEST_CASE("prob: `seed <n>` and the right inlet restart the same sequence (#456)") {
    auto build = []() {
      auto rig = std::make_unique<ProbRig>("1 4242");
      rig->Transition(1, 2, 1);
      rig->Transition(1, 3, 1);
      rig->Transition(2, 1, 1);
      rig->Transition(3, 1, 1);
      return rig;
    };
    auto viaList = build();
    const std::vector<int> first = viaList->Walk(300);
    viaList->Reseed(4242);
    viaList->SetState(1);
    CHECK(viaList->Walk(300) == first);

    auto viaInlet = build();
    REQUIRE(viaInlet->Walk(300) == first);
    viaInlet->prob.GetInlet(1)->SetInt(4242, YSE::T_GUI);
    viaInlet->SetState(1);
    CHECK(viaInlet->Walk(300) == first);
  }

  TEST_CASE("prob: a float on the right inlet is truncated to a seed (#456)") {
    auto build = [](int seed) {
      auto rig = std::make_unique<ProbRig>(("1 " + std::to_string(seed)).c_str());
      rig->Transition(1, 2, 1);
      rig->Transition(1, 3, 1);
      rig->Transition(2, 1, 1);
      rig->Transition(3, 1, 1);
      return rig;
    };
    auto a = build(1);
    auto b = build(1);
    a->prob.GetInlet(1)->SetInt(4242, YSE::T_GUI);
    b->prob.GetInlet(1)->SetFloat(4242.9f, YSE::T_GUI);
    CHECK(a->Walk(300) == b->Walk(300));
  }

  // ─── one draw per bang ──────────────────────────────────────────────────────

  TEST_CASE("prob: exactly one draw per bang, dead end or not (#456)") {
    // A bang that finds nowhere to go still advances the stream. That is what
    // keeps a seeded walk replayable while the table is being edited: adding a
    // transition must not shift the position of every bang after it.
    //
    // Both objects take ten bangs first — one from a live table, one from an
    // empty one — and are then put on the same departure state by hand. If the
    // dead-end path drew a different number of times, the two streams would be
    // out of step and the following 300 states would diverge.
    auto load = [](ProbRig& rig) {
      rig.Transition(1, 2, 3);
      rig.Transition(1, 3, 4);
      rig.Transition(1, 4, 1);
      rig.Transition(2, 1, 1);
      rig.Transition(3, 1, 1);
      rig.Transition(4, 1, 1);
    };

    ProbRig live("1 4242");
    load(live);
    (void)live.Walk(10);
    live.SetState(1);

    ProbRig stuck("1 4242");
    for (int i = 0; i < 10; ++i)
      REQUIRE_FALSE(stuck.Bang()); // dead ends: the table is still empty
    load(stuck);
    stuck.SetState(1);

    const std::vector<int> fromLive = live.Walk(300);
    CHECK(stuck.Walk(300) == fromLive);

    // ...and the control: without the ten leading bangs the stream is at a
    // different position, so this must *not* match.
    ProbRig fresh("1 4242");
    load(fresh);
    fresh.SetState(1);
    CHECK(fresh.Walk(300) != fromLive);
  }

  TEST_CASE("prob: nothing but a bang consumes a draw (#456)") {
    // Table edits, dumps, state changes and messages the object ignores must
    // not advance the stream, or a seeded patch would replay differently the
    // moment an unrelated message passed through.
    auto build = []() {
      auto rig = std::make_unique<ProbRig>("1 4242");
      rig->Transition(1, 2, 1);
      rig->Transition(1, 3, 1);
      rig->Transition(2, 1, 1);
      rig->Transition(3, 1, 1);
      return rig;
    };
    auto quiet = build();
    auto noisy = build();

    noisy->Dump();
    noisy->Reset(1);
    noisy->SetState(1);
    noisy->Transition(1, 9, 0); // stored, but never taken
    noisy->Transition(9, 1, 1);
    for (const char* junk : {"", "banana", "1 2", "clear 3", "seed x"}) {
      CAPTURE(junk);
      noisy->prob.GetInlet(0)->SetList(junk, YSE::T_GUI);
    }
    noisy->SetState(1);
    quiet->SetState(1);

    CHECK(quiet->Walk(300) == noisy->Walk(300));
  }

  // ─── GUI value ──────────────────────────────────────────────────────────────

  TEST_CASE("prob: the GUI value follows the current state (#456)") {
    ProbRig rig(("1 " + std::to_string(SEED)).c_str());
    CHECK(rig.prob.GetGuiValue() == std::string("1")); // the reset state
    rig.Transition(1, 2, 1);
    rig.Transition(2, 1, 1);
    for (int i = 0; i < 20; ++i) {
      CAPTURE(i);
      REQUIRE(rig.Bang());
      CHECK(rig.prob.GetGuiValue() == std::to_string(rig.values.received));
    }
  }

  // ─── the shared transition table ────────────────────────────────────────────

  TEST_CASE("transition table: Add accumulates where Set replaces (#456)") {
    // Add is what .anal (#457) counts pairs with; .prob only ever calls Set.
    // The two have to differ in exactly this way or a learned table and a
    // stated one would not mean the same thing.
    YSE::PATCHER::TransitionTable table;
    CHECK(table.Size() == 0);

    CHECK(table.Add(1, 2, 1));
    CHECK(table.Add(1, 2, 1));
    CHECK(table.Add(1, 2, 3));
    CHECK(table.WeightOf(1, 2) == 5);
    CHECK(table.Size() == 1);

    CHECK(table.Set(1, 2, 2));
    CHECK(table.WeightOf(1, 2) == 2);
    CHECK(table.Size() == 1);
  }

  TEST_CASE("transition table: weights clamp at both ends (#456)") {
    YSE::PATCHER::TransitionTable table;
    constexpr int MAX = YSE::PATCHER::TransitionTable::MAX_WEIGHT;

    CHECK(table.Set(1, 2, -5));
    CHECK(table.WeightOf(1, 2) == 0);
    CHECK(table.Set(1, 3, MAX + 1000));
    CHECK(table.WeightOf(1, 3) == MAX);
    // Accumulating past the ceiling saturates rather than wrapping negative,
    // which is what would poison the prefix sum.
    CHECK(table.Add(1, 3, MAX));
    CHECK(table.WeightOf(1, 3) == MAX);
    // ...and subtracting past zero floors instead of going negative.
    CHECK(table.Add(1, 3, -(MAX * 2)));
    CHECK(table.WeightOf(1, 3) == 0);
  }

  TEST_CASE("transition table: an unknown pair weighs nothing and picks nothing (#456)") {
    YSE::PATCHER::TransitionTable table;
    CHECK(table.WeightOf(4, 5) == 0);
    CHECK(table.TotalWeightFrom(4) == 0);
    int out = -1;
    CHECK_FALSE(table.Pick(4, 0u, out));
    CHECK(out == -1); // untouched
  }

  TEST_CASE("transition table: the total is per departure state (#456)") {
    YSE::PATCHER::TransitionTable table;
    table.Set(1, 2, 3);
    table.Set(1, 3, 4);
    table.Set(2, 9, 100);
    CHECK(table.TotalWeightFrom(1) == 7);
    CHECK(table.TotalWeightFrom(2) == 100);
    CHECK(table.TotalWeightFrom(3) == 0);
  }

  TEST_CASE("transition table: Pick spans the whole weight range (#456)") {
    // The prefix scan has to reach the first entry at draw 0 and the last one
    // just below 2^32; an off-by-one at either end of the reduction shows up
    // here and nowhere else.
    YSE::PATCHER::TransitionTable table;
    table.Set(1, 10, 1);
    table.Set(1, 20, 1);
    table.Set(1, 30, 2);

    int out = 0;
    REQUIRE(table.Pick(1, 0u, out));
    CHECK(out == 10);
    REQUIRE(table.Pick(1, 0xFFFFFFFFu, out));
    CHECK(out == 30);
    // Exactly on the 1/4 boundary: the second entry starts there.
    REQUIRE(table.Pick(1, 0x40000000u, out));
    CHECK(out == 20);
  }

  TEST_CASE("transition table: Entry walks the stored entries and stops (#456)") {
    YSE::PATCHER::TransitionTable table;
    table.Set(1, 2, 3);
    table.Set(4, 5, 6);

    int from = 0;
    int to = 0;
    int weight = 0;
    REQUIRE(table.Entry(0, from, to, weight));
    CHECK(from == 1);
    CHECK(to == 2);
    CHECK(weight == 3);
    REQUIRE(table.Entry(1, from, to, weight));
    CHECK(from == 4);
    CHECK(weight == 6);
    CHECK_FALSE(table.Entry(2, from, to, weight));
    CHECK_FALSE(table.Entry(-1, from, to, weight));

    table.Clear();
    CHECK_FALSE(table.Entry(0, from, to, weight));
  }

  // ─── concurrency ────────────────────────────────────────────────────────────

  TEST_CASE("prob: a table edited while it is being walked stays coherent (#456)") {
    // Inlet handlers run synchronously on whichever thread sent the message, so
    // a control-thread table edit and an audio-thread bang can meet inside one
    // object. The append publishes its slot with a release store and every read
    // takes the count with an acquire load, so a reader can never land on a
    // half-written entry: whatever it emits is a destination somebody actually
    // stated. Doubles as an AddressSanitizer / ThreadSanitizer gate.
    //
    // A shared plain-int sink would itself be the race, so the tally is atomic
    // and doctest's (not thread-safe) macros only run after the join.
    constexpr int BANGS = 200000;

    YSE::PATCHER::gProb prob;
    prob.SetParams("1 4242");
    prob.GetInlet(0)->SetList("1 2 1", YSE::T_GUI);
    prob.GetInlet(0)->SetList("2 1 1", YSE::T_GUI);

    std::atomic<int> total{0};
    std::atomic<int> strays{0};
    ProbTallySink tally(total, strays);
    prob.ConnectOutlet(tally.GetInlet(0), 0);
    tally.ConnectInlet(prob.GetOutlet(0), 0);

    std::atomic<bool> go{false};
    std::atomic<bool> stop{false};

    std::thread banger([&] {
      while (!go.load(std::memory_order_acquire)) {}
      for (int i = 0; i < BANGS; ++i)
        prob.GetInlet(0)->SetBang(YSE::T_GUI);
      stop.store(true, std::memory_order_release);
    });

    std::thread editor([&] {
      while (!go.load(std::memory_order_acquire)) {}
      int weight = 1;
      while (!stop.load(std::memory_order_acquire)) {
        // Keeps 1 -> {2, 3} and 2 -> 1 live while the weights move underneath
        // the walker. State 3 has no exit, so the walker also meets dead ends.
        prob.GetInlet(0)->SetList("1 3 " + std::to_string(weight % 5), YSE::T_GUI);
        prob.GetInlet(0)->SetList("3 1 1", YSE::T_GUI);
        prob.GetInlet(0)->SetList("1 2 " + std::to_string(1 + (weight % 7)), YSE::T_GUI);
        weight++;
      }
    });

    go.store(true, std::memory_order_release);
    banger.join();
    editor.join();

    // Every value emitted was a destination that had been stated — never a
    // half-written slot, never an uninitialised one.
    CHECK(strays.load() == 0);
    // ...and the walk actually ran rather than sitting on dead ends throughout.
    CHECK(total.load() > BANGS / 2);
  }

} // TEST_SUITE
