// Tests for .anal (issue #457) — a transition histogram over an input stream.
//
// The object has one job that matters and one that is easy to fake. The easy
// one is "a list came out with three numbers in it"; a .counter chained into a
// message box does that. What actually characterises `anal` is
//
//   * pairing — the report is about *this* number and the one immediately
//     before it, and the very first number has no predecessor at all;
//   * counting — the third number is the running total for that particular
//     succession, per pair rather than per object, and it keeps climbing;
//   * the two asymmetric erasures Max documents — `clear` drops the counts but
//     keeps the last number, `reset` drops the last number but keeps the
//     counts. Getting these the same way round is most of the object;
//   * clipping — inputs are pinned into [0, limit], which is what makes the
//     default limit of 128 mean "a MIDI note number arrives unchanged";
//   * overflow — the table is a fixed 1024 pairs, so the object has to stop
//     learning and say so rather than evict, grow, or lie about a count;
//   * **interop** — the emitted list is exactly what .prob stores a transition
//     from. That is the entire point of the object, so it is asserted end to
//     end, with a real .anal wired into a real .prob by a real patch cord,
//     rather than by eyeballing the string format.
//
// ### Why the interop assertion is the strong one
//
// A format test ("the list reads '60 62 1'") pins the text but not the
// meaning. Two ways the pair can still be broken with the text intact: .anal
// could emit the increment instead of the running total, which a .prob would
// silently store as a weight of 1 forever because .prob *replaces*; or the two
// could disagree about argument order and learn the chain backwards. So the
// tests below compare the two objects' `dump` output entry for entry, and then
// walk the .prob to check the *distribution* it ended up with is the one the
// input stream actually had.
//
// The statistical check cannot flake: .prob is seeded explicitly and
// RandomSource is pure integer arithmetic over a per-object counter, so the
// sequence is a fixed, platform-independent list of numbers. The band is 6
// sigma of the binomial (two-sided tail about 2e-9) — wide enough that an
// honest replacement generator passes, narrow enough that a chain learned
// backwards or with flat weights misses it by hundreds of sigma.
//
// Nothing here needs an audio device.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/gAnal.h"
#include "patcher/math/gProb.h"
#include "patcher/math/gTransitionTable.h"
#include "patcher/sinks.hpp"

using TestHelpers::BangSink;
using TestHelpers::IntSink;
using TestHelpers::ListSink;

namespace {

  // Collects every list an outlet produces, in order.
  struct LineCollector : YSE::PATCHER::pObject {
    std::vector<std::string> lines;
    LineCollector() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { lines.push_back(v); });
    }
    const char* Type() const override {
      return "line_collector";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A .anal with both outlets wired: the pair reports on outlet 0, the
  // table-full report on outlet 1.
  struct AnalRig {
    YSE::PATCHER::gAnal anal;
    ListSink pairs;
    BangSink full;

    explicit AnalRig(const char* params = nullptr) {
      anal.ConnectOutlet(pairs.GetInlet(0), 0);
      pairs.ConnectInlet(anal.GetOutlet(0), 0);
      anal.ConnectOutlet(full.GetInlet(0), 1);
      full.ConnectInlet(anal.GetOutlet(1), 0);
      if (params != nullptr) anal.SetParams(params);
    }

    // Sends one number. Returns the list it produced, or "" when it produced
    // none — which is a real outcome here, not an error.
    std::string Send(int value) {
      pairs.gotList = false;
      pairs.received.clear();
      anal.GetInlet(0)->SetInt(value, YSE::T_GUI);
      return pairs.gotList ? pairs.received : std::string();
    }

    std::string SendFloat(float value) {
      pairs.gotList = false;
      pairs.received.clear();
      anal.GetInlet(0)->SetFloat(value, YSE::T_GUI);
      return pairs.gotList ? pairs.received : std::string();
    }

    void Message(const std::string& text) {
      anal.GetInlet(0)->SetList(text, YSE::T_GUI);
    }
  };

  // A .prob with its state outlet wired, for the interop cases.
  struct ProbReader {
    YSE::PATCHER::gProb prob;
    IntSink values;

    explicit ProbReader(const char* params) {
      prob.ConnectOutlet(values.GetInlet(0), 0);
      values.ConnectInlet(prob.GetOutlet(0), 0);
      prob.SetParams(params);
    }

    // `count` bangs, each departing from `state`, so the sample is `count`
    // independent draws from that one state's distribution.
    std::vector<int> WalkFrom(int state, int count) {
      std::vector<int> out;
      out.reserve(static_cast<size_t>(count));
      for (int i = 0; i < count; ++i) {
        prob.GetInlet(0)->SetInt(state, YSE::T_GUI);
        values.gotInt = false;
        prob.GetInlet(0)->SetBang(YSE::T_GUI);
        REQUIRE(values.gotInt);
        out.push_back(values.received);
      }
      return out;
    }
  };

  int CountOf(const std::vector<int>& walk, int value) {
    int n = 0;
    for (int v : walk) {
      if (v == value) n++;
    }
    return n;
  }

  // 6 sigma of Binomial(n, p), rounded up.
  int SixSigma(int n, double p) {
    return static_cast<int>(std::ceil(6.0 * std::sqrt(static_cast<double>(n) * p * (1.0 - p))));
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("anal: creatable through the registry with the documented shape (#457)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ANAL);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".anal"));
    CHECK(std::string(YSE::OBJ::G_ANAL) == std::string(".anal"));
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 2);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::LIST);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::BANG);
  }

  TEST_CASE("anal: is listed by the registry (#457)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& n : names) {
      if (n == std::string(YSE::OBJ::G_ANAL)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("anal: names its parameter (#457)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_ANAL));
    REQUIRE(obj != nullptr);
    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 1);
    CHECK(docs[0].name == std::string("limit"));
  }

  TEST_CASE("anal: params survive a DumpJSON / ParseJSON round trip (#457)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_ANAL, "7") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".anal") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".anal"));
    CHECK(h->GetParams() == std::string("7"));

    // ...and the limit reached the object, not just the parameter string. With
    // a limit of 7 every one of these clips to 7, so the four numbers produce a
    // single pair (7 -> 7); at the default limit of 128 they would produce two
    // ((100 -> 200) and (200 -> 100)). The GUI value is the stored-pair count.
    h->SetIntData(0, 100);
    h->SetIntData(0, 200);
    h->SetIntData(0, 100);
    h->SetIntData(0, 200);
    CHECK(h->GetGuiValue() == std::string("1"));
  }

  // ─── pairing ────────────────────────────────────────────────────────────────

  TEST_CASE("anal: the first number has no predecessor and reports nothing (#457)") {
    // Max: "the first time a number is received, there has been no previous
    // number, so nothing happens."
    AnalRig rig;
    CHECK(rig.Send(60) == std::string(""));
    CHECK(rig.anal.Entries() == 0);
    CHECK(rig.full.bangCount == 0);
  }

  TEST_CASE("anal: the second number reports the pair with a count of one (#457)") {
    AnalRig rig;
    rig.Send(60);
    CHECK(rig.Send(62) == std::string("60 62 1"));
    CHECK(rig.anal.Entries() == 1);
  }

  TEST_CASE("anal: the report pairs with the immediately preceding number (#457)") {
    // Not the first number ever seen, and not the number two back: a walk
    // through a phrase reports a sliding window of two.
    AnalRig rig;
    rig.Send(1);
    CHECK(rig.Send(2) == std::string("1 2 1"));
    CHECK(rig.Send(3) == std::string("2 3 1"));
    CHECK(rig.Send(4) == std::string("3 4 1"));
    CHECK(rig.anal.Entries() == 3);
  }

  TEST_CASE("anal: a repeated number pairs with itself (#457)") {
    AnalRig rig;
    rig.Send(5);
    CHECK(rig.Send(5) == std::string("5 5 1"));
    CHECK(rig.Send(5) == std::string("5 5 2"));
    CHECK(rig.anal.Entries() == 1);
  }

  TEST_CASE("anal: a float counts as the same number, truncated (#457)") {
    AnalRig rig;
    rig.SendFloat(60.9f);
    CHECK(rig.SendFloat(62.4f) == std::string("60 62 1"));
  }

  // ─── counting ───────────────────────────────────────────────────────────────

  TEST_CASE("anal: the count climbs for the pair, once per occurrence (#457)") {
    AnalRig rig;
    rig.Send(1);
    for (int expected = 1; expected <= 25; ++expected) {
      CAPTURE(expected);
      // 1 -> 2 -> 1 -> 2 ...: each return to 2 is one more occurrence of the
      // same pair.
      CHECK(rig.Send(2) == "1 2 " + std::to_string(expected));
      CHECK(rig.Send(1) == "2 1 " + std::to_string(expected));
    }
    CHECK(rig.anal.CountOf(1, 2) == 25);
    CHECK(rig.anal.Entries() == 2);
  }

  TEST_CASE("anal: counts are per pair, not one running total (#457)") {
    // The classic implementation error: counting occurrences of the object's
    // input rather than of the succession.
    AnalRig rig;
    rig.Send(1);
    rig.Send(2); // 1->2 = 1
    rig.Send(3); // 2->3 = 1
    rig.Send(1); // 3->1 = 1
    CHECK(rig.Send(2) == std::string("1 2 2")); // 1->2 again, not 4
    CHECK(rig.anal.CountOf(1, 2) == 2);
    CHECK(rig.anal.CountOf(2, 3) == 1);
    CHECK(rig.anal.CountOf(3, 1) == 1);
    CHECK(rig.anal.CountOf(2, 1) == 0); // never seen in that direction
  }

  TEST_CASE("anal: direction matters — a -> b is not b -> a (#457)") {
    AnalRig rig;
    rig.Send(7);
    rig.Send(9); // 7->9
    rig.Send(7); // 9->7
    CHECK(rig.anal.CountOf(7, 9) == 1);
    CHECK(rig.anal.CountOf(9, 7) == 1);
    CHECK(rig.anal.Entries() == 2); // two entries, not one shared
  }

  // ─── clear / reset ──────────────────────────────────────────────────────────

  TEST_CASE("anal: `clear` drops the counts but keeps the last number (#457)") {
    // Max: "erases the memory of the anal object entirely, but retains the most
    // recently received number to use as the next 'previous' value."
    AnalRig rig;
    rig.Send(1);
    rig.Send(2);
    rig.Send(3);
    REQUIRE(rig.anal.Entries() == 2);

    rig.Message("clear");
    CHECK(rig.anal.Entries() == 0);
    // 3 is still the predecessor, so the next number pairs against it and the
    // count restarts at one.
    CHECK(rig.Send(4) == std::string("3 4 1"));
  }

  TEST_CASE("anal: `reset` drops the last number but keeps the counts (#457)") {
    // Max: "erases the most recently received number... The next number to be
    // received gets stored in its place (but nothing else happens)."
    AnalRig rig;
    rig.Send(1);
    rig.Send(2);
    REQUIRE(rig.anal.CountOf(1, 2) == 1);

    rig.Message("reset");
    CHECK(rig.anal.Entries() == 1); // the counts survived
    CHECK(rig.Send(9) == std::string("")); // no predecessor: nothing happens
    CHECK(rig.Send(1) == std::string("9 1 1"));
    // ...and the pair learned before the reset is still there.
    CHECK(rig.anal.CountOf(1, 2) == 1);
    CHECK(rig.Send(2) == std::string("1 2 2"));
  }

  TEST_CASE("anal: `clear` and `reset` together start from nothing (#457)") {
    AnalRig rig;
    rig.Send(1);
    rig.Send(2);
    rig.Message("clear");
    rig.Message("reset");
    CHECK(rig.anal.Entries() == 0);
    CHECK(rig.Send(5) == std::string(""));
    CHECK(rig.Send(6) == std::string("5 6 1"));
  }

  TEST_CASE("anal: an unknown message is ignored (#457)") {
    AnalRig rig;
    rig.Send(1);
    rig.Send(2);
    for (const char* junk : {"", " ", "banana", "clear 3", "resetting", "dumps", "1 2 3", "-"}) {
      CAPTURE(junk);
      rig.Message(junk);
    }
    // Neither the counts nor the predecessor moved.
    CHECK(rig.anal.Entries() == 1);
    CHECK(rig.Send(3) == std::string("2 3 1"));
  }

  // ─── clipping ───────────────────────────────────────────────────────────────

  TEST_CASE("anal: a value above the limit clips to the limit (#457)") {
    // Max clips rather than rejects, so an out-of-range number still advances
    // the stream — it just lands on the boundary.
    AnalRig rig("16");
    rig.Send(100);
    CHECK(rig.Send(200) == std::string("16 16 1"));
    CHECK(rig.anal.CountOf(16, 16) == 1);
  }

  TEST_CASE("anal: a negative value clips to zero (#457)") {
    AnalRig rig("16");
    rig.Send(-5);
    CHECK(rig.Send(-9000) == std::string("0 0 1"));
  }

  TEST_CASE("anal: the default limit passes a MIDI note range through (#457)") {
    // The reason the default is 128 rather than something rounder.
    AnalRig rig;
    CHECK(rig.anal.Limit() == 128);
    rig.Send(0);
    CHECK(rig.Send(127) == std::string("0 127 1"));
    CHECK(rig.Send(128) == std::string("127 128 1"));
  }

  TEST_CASE("anal: the limit is clamped to Max's documented range (#457)") {
    YSE::PATCHER::gAnal a;
    a.SetParams("99999");
    CHECK(a.Limit() == YSE::PATCHER::gAnal::MAX_LIMIT);

    YSE::PATCHER::gAnal b;
    b.SetParams("0");
    CHECK(b.Limit() == 1);

    YSE::PATCHER::gAnal c;
    c.SetParams("-40");
    CHECK(c.Limit() == 1);

    YSE::PATCHER::gAnal d;
    d.SetParams("500");
    CHECK(d.Limit() == 500);
  }

  // ─── dump ───────────────────────────────────────────────────────────────────

  TEST_CASE("anal: `dump` reports every stored pair in insertion order (#457)") {
    YSE::PATCHER::gAnal anal;
    LineCollector collector;
    anal.ConnectOutlet(collector.GetInlet(0), 0);
    collector.ConnectInlet(anal.GetOutlet(0), 0);

    anal.GetInlet(0)->SetInt(1, YSE::T_GUI);
    anal.GetInlet(0)->SetInt(2, YSE::T_GUI);
    anal.GetInlet(0)->SetInt(3, YSE::T_GUI);
    anal.GetInlet(0)->SetInt(1, YSE::T_GUI);
    anal.GetInlet(0)->SetInt(2, YSE::T_GUI);
    collector.lines.clear(); // drop the live reports; only the dump matters

    anal.GetInlet(0)->SetList("dump", YSE::T_GUI);
    REQUIRE(collector.lines.size() == 3u);
    CHECK(collector.lines[0] == std::string("1 2 2"));
    CHECK(collector.lines[1] == std::string("2 3 1"));
    CHECK(collector.lines[2] == std::string("3 1 1"));
  }

  TEST_CASE("anal: `dump` on an empty table says nothing (#457)") {
    AnalRig rig;
    rig.pairs.gotList = false;
    rig.Message("dump");
    CHECK_FALSE(rig.pairs.gotList);

    // ...and after `clear` too, which is the same emptiness by another route.
    rig.Send(1);
    rig.Send(2);
    rig.Message("clear");
    rig.pairs.gotList = false;
    rig.Message("dump");
    CHECK_FALSE(rig.pairs.gotList);
  }

  TEST_CASE("anal: `dump` does not disturb the stream (#457)") {
    AnalRig rig;
    rig.Send(1);
    rig.Send(2);
    rig.Message("dump");
    // The predecessor is still 2, and the dump did not count anything.
    CHECK(rig.Send(3) == std::string("2 3 1"));
    CHECK(rig.anal.Entries() == 2);
  }

  // ─── overflow ───────────────────────────────────────────────────────────────

  TEST_CASE("anal: a full table stops learning and bangs outlet 1 (#457)") {
    // The array is fixed, so the overflow path has to drop the pair rather than
    // write past the end — and it has to *say* so, since a histogram that
    // quietly stopped counting is worse than one that stopped.
    constexpr int CAP = YSE::PATCHER::TransitionTable::CAPACITY;
    AnalRig rig("2000");

    // `reset` before each pair so every one of them is a fresh (0 -> n) rather
    // than a chain, which is what fills the table with distinct entries.
    for (int i = 1; i <= CAP; ++i) {
      rig.Message("reset");
      rig.Send(0);
      CHECK(rig.Send(i) == "0 " + std::to_string(i) + " 1");
    }
    REQUIRE(rig.anal.Entries() == CAP);
    REQUIRE(rig.full.bangCount == 0);

    // One more distinct pair: dropped, outlet 0 silent, outlet 1 bangs.
    rig.Message("reset");
    rig.Send(0);
    CHECK(rig.Send(1500) == std::string(""));
    CHECK(rig.full.bangCount == 1);
    CHECK(rig.anal.Entries() == CAP);
    CHECK(rig.anal.CountOf(0, 1500) == 0);

    // ...and a pair already in the table keeps counting normally.
    rig.Message("reset");
    rig.Send(0);
    CHECK(rig.Send(1) == std::string("0 1 2"));
    CHECK(rig.full.bangCount == 1);
    CHECK(rig.anal.Entries() == CAP);
  }

  TEST_CASE("anal: `clear` makes room again after an overflow (#457)") {
    constexpr int CAP = YSE::PATCHER::TransitionTable::CAPACITY;
    AnalRig rig("2000");
    for (int i = 1; i <= CAP; ++i) {
      rig.Message("reset");
      rig.Send(0);
      rig.Send(i);
    }
    rig.Message("reset");
    rig.Send(0);
    REQUIRE(rig.Send(1500) == std::string(""));
    REQUIRE(rig.full.bangCount == 1);

    rig.Message("clear");
    rig.Message("reset");
    rig.Send(0);
    CHECK(rig.Send(1500) == std::string("0 1500 1"));
    CHECK(rig.full.bangCount == 1); // no further complaint
  }

  // ─── GUI value ──────────────────────────────────────────────────────────────

  TEST_CASE("anal: the GUI value follows the number of pairs learned (#457)") {
    AnalRig rig;
    CHECK(rig.anal.GetGuiValue() == std::string("0"));
    rig.Send(1);
    CHECK(rig.anal.GetGuiValue() == std::string("0"));
    rig.Send(2);
    CHECK(rig.anal.GetGuiValue() == std::string("1"));
    rig.Send(3);
    CHECK(rig.anal.GetGuiValue() == std::string("2"));
    rig.Send(2); // 3 -> 2, a third pair
    CHECK(rig.anal.GetGuiValue() == std::string("3"));
    rig.Send(3); // 2 -> 3 again, no new pair
    CHECK(rig.anal.GetGuiValue() == std::string("3"));
    rig.Message("clear");
    CHECK(rig.anal.GetGuiValue() == std::string("0"));
  }

  // ─── interop with .prob — the reason the object exists ──────────────────────

  TEST_CASE("anal: its live output builds the identical table inside a .prob (#457)") {
    // A real patch cord: .anal's list outlet straight into .prob's hot inlet,
    // nothing in between. After a phrase has been played in, the two tables have
    // to agree entry for entry — same pairs, same order, same counts.
    YSE::PATCHER::gAnal anal;
    YSE::PATCHER::gProb prob;
    prob.SetParams("0 4242");

    anal.ConnectOutlet(prob.GetInlet(0), 0);
    prob.ConnectInlet(anal.GetOutlet(0), 0);

    LineCollector analDump;
    anal.ConnectOutlet(analDump.GetInlet(0), 0);
    analDump.ConnectInlet(anal.GetOutlet(0), 0);

    LineCollector probDump;
    prob.ConnectOutlet(probDump.GetInlet(0), 2);
    probDump.ConnectInlet(prob.GetOutlet(2), 0);

    // A phrase with an uneven shape, so a table that merely has the right pairs
    // but flat weights would not match.
    const int phrase[] = {60, 62, 64, 62, 60, 62, 64, 65, 64, 62};
    for (int repeat = 0; repeat < 40; ++repeat) {
      for (int note : phrase)
        anal.GetInlet(0)->SetInt(note, YSE::T_GUI);
    }

    REQUIRE(anal.Entries() > 0);
    CHECK(prob.Entries() == anal.Entries());

    analDump.lines.clear();
    anal.GetInlet(0)->SetList("dump", YSE::T_GUI);
    probDump.lines.clear();
    prob.GetInlet(0)->SetList("dump", YSE::T_GUI);

    REQUIRE(analDump.lines.size() == probDump.lines.size());
    REQUIRE_FALSE(analDump.lines.empty());
    CHECK(analDump.lines == probDump.lines);
  }

  TEST_CASE("anal: the learned chain reproduces the input distribution (#457)") {
    // Text equality is not meaning. This drives the .prob the .anal taught and
    // checks the *proportions* it walks with are the ones the stream had — which
    // a chain learned backwards, or one whose counts never rose above 1, would
    // fail by hundreds of sigma.
    //
    // The motif {60, 62, 64, 62} repeated N times leaves state 62 with 64 as its
    // successor N times and 60 as its successor N-1 times (the trailing 62 has
    // no successor), so a walk out of 62 should be an almost exactly even split.
    constexpr int REPEATS = 1000;
    YSE::PATCHER::gAnal anal;
    ProbReader reader("0 918273");

    anal.ConnectOutlet(reader.prob.GetInlet(0), 0);
    reader.prob.ConnectInlet(anal.GetOutlet(0), 0);

    for (int i = 0; i < REPEATS; ++i) {
      for (int note : {60, 62, 64, 62})
        anal.GetInlet(0)->SetInt(note, YSE::T_GUI);
    }

    // Four pairs: 60->62, 62->64, 64->62, 62->60.
    REQUIRE(anal.Entries() == 4);
    CHECK(anal.CountOf(60, 62) == REPEATS);
    CHECK(anal.CountOf(62, 64) == REPEATS);
    CHECK(anal.CountOf(64, 62) == REPEATS);
    CHECK(anal.CountOf(62, 60) == REPEATS - 1);

    // 60 has exactly one successor in the whole stream, so the walk out of it is
    // deterministic — a chain learned in the wrong direction cannot produce it.
    const std::vector<int> from60 = reader.WalkFrom(60, 500);
    CHECK(CountOf(from60, 62) == 500);

    // ...and 62's two successors split by their counts.
    constexpr int N = 200000;
    const double p = static_cast<double>(REPEATS) / (2.0 * REPEATS - 1.0);
    const std::vector<int> from62 = reader.WalkFrom(62, N);
    const int seen64 = CountOf(from62, 64);
    const int centre = static_cast<int>(p * N);
    const int band = SixSigma(N, p);
    CAPTURE(seen64);
    CAPTURE(centre);
    CAPTURE(band);
    CHECK(seen64 > centre - band);
    CHECK(seen64 < centre + band);
    // Nothing but the two learned successors ever came out.
    CHECK(seen64 + CountOf(from62, 60) == N);
  }

  TEST_CASE("anal: a table learned earlier replays into a .prob created later (#457)") {
    // The `dump` case: the .anal did its learning before the .prob existed, and
    // one dump hands the whole table over.
    YSE::PATCHER::gAnal anal;
    for (int i = 0; i < 30; ++i) {
      for (int note : {1, 2, 3, 2})
        anal.GetInlet(0)->SetInt(note, YSE::T_GUI);
    }
    REQUIRE(anal.Entries() == 4);

    ProbReader reader("0 4242");
    anal.ConnectOutlet(reader.prob.GetInlet(0), 0);
    reader.prob.ConnectInlet(anal.GetOutlet(0), 0);
    CHECK(reader.prob.Entries() == 0); // nothing has crossed the cord yet

    anal.GetInlet(0)->SetList("dump", YSE::T_GUI);
    CHECK(reader.prob.Entries() == 4);

    // The counts came across, not just the pairs: 1 has one successor and 3 has
    // one successor, so both walks are deterministic.
    const std::vector<int> fromOne = reader.WalkFrom(1, 200);
    CHECK(CountOf(fromOne, 2) == 200);
    const std::vector<int> fromThree = reader.WalkFrom(3, 200);
    CHECK(CountOf(fromThree, 2) == 200);
  }

  TEST_CASE("anal: re-dumping a grown table replaces rather than doubles (#457)") {
    // .anal reports a running total and .prob *replaces* the weight with it, so
    // learning more and dumping again must leave .prob holding the new totals —
    // not the sum of every report it has ever received.
    YSE::PATCHER::gAnal anal;
    YSE::PATCHER::gProb prob;
    prob.SetParams("0 4242");
    anal.ConnectOutlet(prob.GetInlet(0), 0);
    prob.ConnectInlet(anal.GetOutlet(0), 0);

    LineCollector probDump;
    prob.ConnectOutlet(probDump.GetInlet(0), 2);
    probDump.ConnectInlet(prob.GetOutlet(2), 0);

    for (int i = 0; i < 10; ++i) {
      anal.GetInlet(0)->SetInt(1, YSE::T_GUI);
      anal.GetInlet(0)->SetInt(2, YSE::T_GUI);
    }
    // Ten numbers of each, so 1 -> 2 ten times and 2 -> 1 nine times: an uneven
    // pair of counts, which a doubling bug would not preserve the ratio of.
    anal.GetInlet(0)->SetList("dump", YSE::T_GUI);
    anal.GetInlet(0)->SetList("dump", YSE::T_GUI);
    anal.GetInlet(0)->SetList("dump", YSE::T_GUI);

    probDump.lines.clear();
    prob.GetInlet(0)->SetList("dump", YSE::T_GUI);
    REQUIRE(probDump.lines.size() == 2u);
    CHECK(probDump.lines[0] == "1 2 " + std::to_string(anal.CountOf(1, 2)));
    CHECK(probDump.lines[1] == "2 1 " + std::to_string(anal.CountOf(2, 1)));
  }

} // TEST_SUITE
