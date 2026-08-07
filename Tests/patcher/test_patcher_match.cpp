// Tests for .match (issue #472) — detect a sequence of values as it arrives.
//
// Max's `match` "watches an incoming stream of ints, floats, symbols, lists, or
// messages, and outputs the stream after it has met the specification of its
// arguments", with `nn` as "a wild card that will match any number".
//
// The reference states the happy path and leaves the hard rule unstated, so
// this file starts from that rule and works outwards. **A mismatch must not
// throw away the values that caused it.** Against `.match 1 2 3` the stream
//
//     1  2  1  2  3
//
// matches: the third value ends one candidate and begins another at the same
// moment. An implementation that walks a cursor forward and resets it to 0 on a
// mismatch reports nothing at all there — it eats the second 1 as a failed
// third element, then the second 2 as a failed first element, and by the time
// the 3 arrives it is back at the beginning. That is not an exotic input, it is
// what any repeated gesture looks like, and it is the first thing asserted
// below (`match: a partial match that restarts is not lost`). Several other
// cases fail the same naive implementation from other directions, and the
// wildcard is tested *through* the restart rather than only on a clean hit.
//
// The file is in seven parts:
//
//   - **the sequence**, clean hits and clean misses, so the rest has a floor.
//   - **restarting and overlapping**, which is the object.
//   - **the wildcard**, including the two things it does *not* match.
//   - **what is not a number**, the rule that lets a symbol or a NaN break a
//     candidate without matching anything.
//   - **the two messages**, `clear` and `set`, and the words that look like
//     them and are not.
//   - **the pattern**, its ceiling, its dead elements and its round trip.
//   - **the invariants**, asserted over whole streams: matches never overlap,
//     `Progress()` is always short of the pattern length, and the window is
//     already empty when the outlet fires.
//
// The rigs wire objects directly, as the sibling suites do; two tests at the
// end run the whole thing through a real patcher so the registry, the wiring
// API and the object agree. OrderSink's counters are cumulative, so every
// sequencing assertion reads the shared order log or the recorded list values
// instead.
//
// No audio device required.

#include <doctest/doctest.h>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/genericObjects/gMatch.h"
#include "sinks.hpp"

namespace {

  using YSE::PATCHER::gMatch;
  using YSE::PATCHER::kMatchMaxPattern;

  // Records every list the object sends, in order, so a test can assert both
  // *how many* sequences were reported and *what was in them* — the wildcard
  // positions carry the values that filled them, so the payload is half the
  // object and a hit counter alone would miss it.
  struct SeqSink : YSE::PATCHER::pObject {
    std::vector<std::string> lists;

    SeqSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { lists.push_back(v); });
    }
    const char* Type() const override {
      return "seq_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    int Count() const {
      return (int)lists.size();
    }
    std::string Last() const {
      return lists.empty() ? std::string() : lists.back();
    }
    // Every reported sequence joined by '|', so one CHECK pins the whole
    // history rather than one entry of it.
    std::string All() const {
      std::string joined;
      for (std::size_t i = 0; i < lists.size(); i++) {
        if (i != 0) joined += '|';
        joined += lists[i];
      }
      return joined;
    }
  };

  struct Rig {
    std::unique_ptr<gMatch> op;
    SeqSink sink;

    explicit Rig(const std::string& pattern = "") : op(new gMatch()) {
      if (!pattern.empty()) op->SetParams(pattern);
      op->ConnectOutlet(sink.GetInlet(0), 0);
      sink.ConnectInlet(op->GetOutlet(0), 0);
    }

    void SendInt(int v) {
      op->GetInlet(0)->SetInt(v, YSE::T_GUI);
    }
    void Send(float v) {
      op->GetInlet(0)->SetFloat(v, YSE::T_GUI);
    }
    void List(const std::string& text) {
      op->GetInlet(0)->SetList(text, YSE::T_GUI);
    }

    // A whole stream of ints, one message each — the common shape of a test
    // here, and one message per value rather than one list so that the object
    // is exercised on the path a patch actually drives it from.
    void Stream(const std::vector<int>& values) {
      for (int v : values)
        SendInt(v);
    }

    int Count() const {
      return sink.Count();
    }
    std::string All() const {
      return sink.All();
    }
    std::string Last() const {
      return sink.Last();
    }
  };

  // A sink that inspects the object from *inside* the send, so "the window is
  // emptied before the outlet fires" is asserted at the only moment it can be
  // observed rather than inferred from the state left behind afterwards.
  struct InspectingSink : YSE::PATCHER::pObject {
    const gMatch* target = nullptr;
    int heldDuringSend = -1;
    int progressDuringSend = -1;
    int hits = 0;

    InspectingSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList([this](const std::string&, int, YSE::THREAD) {
        hits++;
        if (target != nullptr) {
          heldDuringSend = target->Held();
          progressDuringSend = target->Progress();
        }
      });
    }
    const char* Type() const override {
      return "inspecting_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── the sequence ───────────────────────────────────────────────────────────

  TEST_CASE("match: reports a clean hit as the list it received (#472)") {
    Rig rig("1 2 3");
    REQUIRE(rig.op->PatternLength() == 3);

    rig.Stream({1, 2, 3});
    CHECK(rig.Count() == 1);
    CHECK(rig.Last() == "1 2 3");
  }

  TEST_CASE("match: says nothing about a clean miss (#472)") {
    Rig rig("1 2 3");
    rig.Stream({1, 2, 4});
    CHECK(rig.Count() == 0);

    // Nor about a sequence in the wrong order, which is the whole of "in the
    // proper order".
    rig.Stream({3, 2, 1});
    CHECK(rig.Count() == 0);
  }

  TEST_CASE("match: nothing comes out until the sequence is complete (#472)") {
    Rig rig("1 2 3");
    rig.SendInt(1);
    CHECK(rig.Count() == 0);
    rig.SendInt(2);
    CHECK(rig.Count() == 0);
    rig.SendInt(3);
    CHECK(rig.Count() == 1);
  }

  TEST_CASE("match: a whole sequence in one list message matches (#472)") {
    // Max: `anything` "performs the same as list", and a list contributes its
    // items left to right — so the same values delivered as one message and as
    // three are the same stream.
    Rig rig("1 2 3");
    rig.List("1 2 3");
    CHECK(rig.Count() == 1);
    CHECK(rig.Last() == "1 2 3");

    // And split across messages however you like.
    rig.List("1 2");
    CHECK(rig.Count() == 1);
    rig.List("3");
    CHECK(rig.Count() == 2);
  }

  TEST_CASE("match: a one-element pattern still reports a list (#472)") {
    // Max: "they are sent out as a list". Uniform length is what makes the
    // outlet readable — a downstream object always gets pattern-length values.
    Rig rig("5");
    rig.Stream({4, 5, 6});
    CHECK(rig.Count() == 1);
    CHECK(rig.Last() == "5");
  }

  TEST_CASE("match: values are echoed in the spelling they arrived in (#472)") {
    Rig rig("1 2 3");
    rig.Send(1.f);
    rig.Send(2.f);
    rig.Send(3.f);
    REQUIRE(rig.Count() == 1);
    // ExprFormatValue keeps a float visibly a float, and an int an int, so a
    // downstream object sees what the patch sent rather than a retyped copy.
    CHECK(rig.Last() == "1. 2. 3.");

    rig.Stream({1, 2, 3});
    REQUIRE(rig.Count() == 2);
    CHECK(rig.Last() == "1 2 3");
  }

  // ─── restarting and overlapping ─────────────────────────────────────────────

  TEST_CASE("match: a partial match that restarts is not lost (#472)") {
    // THE case. `1 2` gets two elements in, the third value breaks the
    // candidate *and* begins the next one, and the sequence completes two
    // values later. A cursor that resets to 0 on a mismatch reports nothing.
    Rig rig("1 2 3");
    rig.Stream({1, 2, 1, 2, 3});
    CHECK(rig.Count() == 1);
    CHECK(rig.Last() == "1 2 3");
  }

  TEST_CASE("match: the restart is visible in Progress() as it happens (#472)") {
    // The same stream, watched value by value. The third value takes the
    // object back to 1 rather than to 0, which is exactly the information a
    // reset-on-mismatch implementation destroys.
    Rig rig("1 2 3");
    CHECK(rig.op->Progress() == 0);
    rig.SendInt(1);
    CHECK(rig.op->Progress() == 1);
    rig.SendInt(2);
    CHECK(rig.op->Progress() == 2);
    rig.SendInt(1);
    CHECK(rig.op->Progress() == 1);
    rig.SendInt(2);
    CHECK(rig.op->Progress() == 2);
    rig.SendInt(3);
    CHECK(rig.Count() == 1);
    // A match empties the window, so the object is back to knowing nothing.
    CHECK(rig.op->Progress() == 0);
    CHECK(rig.op->Held() == 0);
  }

  TEST_CASE("match: a repeated leading element restarts within itself (#472)") {
    // `1 1 2` fed 1, 1, 1, 2: the third 1 fails as the third element but the
    // *last two* 1s are a fresh two-element prefix, so the 2 completes it.
    Rig rig("1 1 2");
    rig.Stream({1, 1, 1, 2});
    CHECK(rig.Count() == 1);
    CHECK(rig.Last() == "1 1 2");
  }

  TEST_CASE("match: an interruption does not stop the next attempt (#472)") {
    Rig rig("1 2 3");
    rig.Stream({1, 2, 9, 1, 2, 3});
    CHECK(rig.Count() == 1);
    CHECK(rig.Last() == "1 2 3");
  }

  TEST_CASE("match: consecutive complete sequences each report (#472)") {
    Rig rig("1 2");
    rig.Stream({1, 2, 1, 2, 1, 2});
    CHECK(rig.Count() == 3);
    CHECK(rig.All() == "1 2|1 2|1 2");
  }

  TEST_CASE("match: matches never overlap (#472)") {
    // A match forgets everything it consumed, which is Max's `clear` applied
    // automatically. Five 1s against `1 1 1` is one match, not three: the
    // sliding window would otherwise report a new sequence on every value from
    // the third onwards.
    Rig rig("1 1 1");
    rig.Stream({1, 1, 1, 1, 1});
    CHECK(rig.Count() == 1);

    // A sixth value completes a second, disjoint sequence.
    rig.SendInt(1);
    CHECK(rig.Count() == 2);
  }

  TEST_CASE("match: an all-wildcard pattern chops the stream into blocks (#472)") {
    // The clearest statement of non-overlap: with `nn nn` every pair of
    // consecutive values would match if matches could overlap.
    Rig rig("nn nn");
    rig.Stream({1, 2, 3, 4, 5});
    CHECK(rig.Count() == 2);
    CHECK(rig.All() == "1 2|3 4");
    CHECK(rig.op->Held() == 1);
  }

  // ─── the wildcard ───────────────────────────────────────────────────────────

  TEST_CASE("match: nn matches any number and reports what filled it (#472)") {
    Rig rig("1 nn 3");
    REQUIRE(rig.op->PatternLength() == 3);
    CHECK(rig.op->ElementIsWildcard(1));
    CHECK_FALSE(rig.op->ElementIsLiteral(1));
    CHECK(rig.op->ElementIsLiteral(0));
    CHECK(rig.op->ElementValue(0) == 1.f);

    rig.SendInt(1);
    rig.Send(2.5f);
    rig.SendInt(3);
    REQUIRE(rig.Count() == 1);
    // The point of the list outlet: the pattern says nothing about the middle
    // value, so the object has to hand it over.
    CHECK(rig.Last() == "1 2.5 3");

    // Negative, zero and large all count as numbers.
    rig.SendInt(1);
    rig.Send(-7.25f);
    rig.SendInt(3);
    CHECK(rig.Last() == "1 -7.25 3");

    rig.SendInt(1);
    rig.SendInt(0);
    rig.SendInt(3);
    CHECK(rig.Last() == "1 0 3");
  }

  TEST_CASE("match: a wildcard restarts like anything else (#472)") {
    // The wildcard interacts with the restart rule rather than sidestepping
    // it: `1 nn 1` fed 1, 9, 2, 1, 9, 1 must find the sequence that ends on the
    // last value, having had two failed candidates before it.
    Rig rig("1 nn 1");
    rig.Stream({1, 9, 2, 1, 9, 1});
    CHECK(rig.Count() == 1);
    CHECK(rig.Last() == "1 9 1");
  }

  TEST_CASE("match: a wildcard-led pattern still needs its literals (#472)") {
    Rig rig("nn 5");
    rig.Stream({1, 2, 3});
    CHECK(rig.Count() == 0);
    rig.SendInt(5);
    CHECK(rig.Count() == 1);
    // The wildcard took the value before the 5, whichever that was.
    CHECK(rig.Last() == "3 5");
  }

  TEST_CASE("match: nn does not match a symbol (#472)") {
    // Max: "a wild card that will match any number" — a number, and so not a
    // word. A symbol in the middle of the stream breaks the candidate.
    Rig rig("1 nn 3");
    rig.List("1 foo 3");
    CHECK(rig.Count() == 0);

    rig.List("1 2 3");
    CHECK(rig.Count() == 1);
  }

  TEST_CASE("match: nn does not match a non-finite value (#472)") {
    // The patcher's usual "read a non-finite as 0" substitution is deliberately
    // not applied. A NaN could never match a literal in any case — this pins
    // that it does not slip through the wildcard either.
    Rig rig("1 nn 3");
    rig.SendInt(1);
    rig.Send(std::numeric_limits<float>::quiet_NaN());
    rig.SendInt(3);
    CHECK(rig.Count() == 0);

    rig.SendInt(1);
    rig.Send(std::numeric_limits<float>::infinity());
    rig.SendInt(3);
    CHECK(rig.Count() == 0);

    // Same from the text side, where `inf` and `nan` are spellings the strict
    // reader refuses.
    rig.List("1 inf 3");
    CHECK(rig.Count() == 0);
    rig.List("1 nan 3");
    CHECK(rig.Count() == 0);

    // And the object is not wedged: a real number still completes it.
    rig.List("1 2 3");
    CHECK(rig.Count() == 1);
  }

  TEST_CASE("match: a non-finite value does not stand in for a literal zero (#472)") {
    // The trap the substitution would spring: `.match 0 0` would fire on a pair
    // of NaNs no patch ever sent.
    Rig rig("0 0");
    rig.Send(std::numeric_limits<float>::quiet_NaN());
    rig.Send(std::numeric_limits<float>::quiet_NaN());
    CHECK(rig.Count() == 0);

    rig.Send(0.f);
    rig.Send(0.f);
    CHECK(rig.Count() == 1);
  }

  // ─── what is not a number ───────────────────────────────────────────────────

  TEST_CASE("match: a symbol takes its position and breaks the candidate (#472)") {
    Rig rig("1 2 3");
    // "1 2 foo" is three values, the third of which matches nothing — so the
    // 1 2 that follow are a fresh candidate and the 3 completes it.
    rig.List("1 2 foo 1 2 3");
    CHECK(rig.Count() == 1);
    CHECK(rig.Last() == "1 2 3");
  }

  TEST_CASE("match: a partly-numeric token is a symbol, not its numeric prefix (#472)") {
    // The strict reader's whole reason for existing: ExprParseFloatList would
    // turn `5abc` into the number 5 and complete a sequence the patch did not
    // send.
    Rig rig("1 5 3");
    rig.List("1 5abc 3");
    CHECK(rig.Count() == 0);

    rig.List("1 5 3");
    CHECK(rig.Count() == 1);
  }

  TEST_CASE("match: literal comparison is exact (#472)") {
    // As in .sel and .change: a computed float may miss a literal it looks
    // equal to, and a .round upstream is the fix. One ulp is enough to say so
    // without depending on how any particular sum rounds.
    Rig rig("2");
    rig.Send(std::nextafter(2.f, 3.f));
    CHECK(rig.Count() == 0);
    rig.Send(2.f);
    CHECK(rig.Count() == 1);
  }

  TEST_CASE("match: an int and a float of the same value both match a literal (#472)") {
    // One numeric type: `.match 5` and `.match 5.0` are the same parameter
    // string here, so the object cannot and does not distinguish the two.
    Rig a("5");
    a.SendInt(5);
    CHECK(a.Count() == 1);
    a.Send(5.f);
    CHECK(a.Count() == 2);

    Rig b("5.0");
    b.SendInt(5);
    CHECK(b.Count() == 1);
    b.Send(5.f);
    CHECK(b.Count() == 2);
  }

  // ─── the two messages ───────────────────────────────────────────────────────

  TEST_CASE("match: clear forgets every value received (#472)") {
    // Max: "Causes match to forget all numbers it has received up to that
    // time."
    Rig rig("1 2 3");
    rig.Stream({1, 2});
    REQUIRE(rig.op->Held() == 2);
    REQUIRE(rig.op->Progress() == 2);

    rig.List("clear");
    CHECK(rig.op->Held() == 0);
    CHECK(rig.op->Progress() == 0);
    CHECK(rig.Count() == 0);

    // The 3 that would have completed the sequence now starts nothing.
    rig.SendInt(3);
    CHECK(rig.Count() == 0);
  }

  TEST_CASE("match: clear leaves the pattern alone (#472)") {
    Rig rig("1 2 3");
    rig.Stream({1, 2});
    rig.List("clear");
    CHECK(rig.op->PatternLength() == 3);

    rig.Stream({1, 2, 3});
    CHECK(rig.Count() == 1);
  }

  TEST_CASE("match: set replaces the pattern (#472)") {
    // Max: "The word set, followed by a list of numbers, specifies a new series
    // of numbers match will look for."
    Rig rig("1 2 3");
    rig.List("set 4 5");
    CHECK(rig.op->PatternLength() == 2);
    CHECK(rig.op->ElementValue(0) == 4.f);
    CHECK(rig.op->ElementValue(1) == 5.f);

    rig.Stream({1, 2, 3});
    CHECK(rig.Count() == 0);
    rig.Stream({4, 5});
    CHECK(rig.Count() == 1);
    CHECK(rig.Last() == "4 5");
  }

  TEST_CASE("match: set takes the wildcard too (#472)") {
    Rig rig;
    rig.List("set 1 nn 3");
    REQUIRE(rig.op->PatternLength() == 3);
    CHECK(rig.op->ElementIsWildcard(1));

    rig.Stream({1, 7, 3});
    CHECK(rig.Count() == 1);
    CHECK(rig.Last() == "1 7 3");
  }

  TEST_CASE("match: set forgets the values received so far (#472)") {
    // A candidate against the old pattern means nothing against the new one,
    // and carrying it over is how a `set` mid-stream would report a sequence
    // that was never sent.
    Rig rig("1 2 3");
    rig.Stream({1, 2});
    rig.List("set 1 2 3");
    CHECK(rig.op->Held() == 0);

    rig.SendInt(3);
    CHECK(rig.Count() == 0);
  }

  TEST_CASE("match: a bare set leaves no sequence to detect (#472)") {
    Rig rig("1 2 3");
    rig.List("set");
    CHECK(rig.op->PatternLength() == 0);

    rig.Stream({1, 2, 3, 1, 2, 3});
    CHECK(rig.Count() == 0);

    // And another `set` brings it back.
    rig.List("set 1 2");
    rig.Stream({1, 2});
    CHECK(rig.Count() == 1);
  }

  TEST_CASE("match: a word that merely starts with set or clear is a value (#472)") {
    // MatchWord requires the separator, so `settle 1 2` is not `set tle 1 2`,
    // and the exact compare on `clear` keeps `clearance` out. Both fall through
    // to the stream as an unmatchable symbol followed by their numbers.
    Rig rig("1 2");
    rig.List("settle 1 2");
    CHECK(rig.op->PatternLength() == 2);
    CHECK(rig.Count() == 1);
    CHECK(rig.Last() == "1 2");

    rig.List("clearance 1 2");
    CHECK(rig.Count() == 2);
    CHECK(rig.op->Held() == 0);
  }

  TEST_CASE("match: set does not rewrite the creation argument (#472)") {
    // The same split .peak has between its `set` message and its `initial`
    // argument: a message is a live edit, and what gets saved is what the
    // object was created with.
    Rig rig("1 2 3");
    rig.List("set 7 8");
    CHECK(rig.op->GetParams() == "1 2 3");
  }

  // ─── the pattern ────────────────────────────────────────────────────────────

  TEST_CASE("match: a bare .match never matches (#472)") {
    // Not "matches everything immediately", which is the only other reading of
    // an empty pattern and would put an endless stream of empty lists on the
    // outlet.
    Rig rig;
    CHECK(rig.op->PatternLength() == 0);
    rig.Stream({1, 2, 3, 0, -1});
    rig.List("hello 1 2");
    CHECK(rig.Count() == 0);
    CHECK(rig.op->Held() == 0);
    CHECK(rig.op->Progress() == 0);
  }

  TEST_CASE("match: an argument that is neither a number nor nn matches nothing (#472)") {
    // Kept rather than dropped: the pattern's length is half of what the object
    // means, so an uppercase typo makes the box silent rather than making it
    // fire on a sequence the patch never asked for.
    Rig rig("1 NN 3");
    CHECK(rig.op->PatternLength() == 3);
    CHECK_FALSE(rig.op->ElementIsWildcard(1));
    CHECK_FALSE(rig.op->ElementIsLiteral(1));

    rig.Stream({1, 2, 3});
    CHECK(rig.Count() == 0);
    rig.List("1 NN 3");
    CHECK(rig.Count() == 0);
  }

  TEST_CASE("match: a non-finite argument is an element nothing can match (#472)") {
    // ReadNumericToken refuses `inf` and `nan`, so they are not numbers to look
    // for — and folding them to 0 would collide with every real zero a patch
    // sends.
    Rig rig("inf");
    CHECK(rig.op->PatternLength() == 1);
    CHECK_FALSE(rig.op->ElementIsLiteral(0));
    rig.Send(std::numeric_limits<float>::infinity());
    rig.Send(0.f);
    CHECK(rig.Count() == 0);
  }

  TEST_CASE("match: the pattern is capped at 256 elements (#472)") {
    std::string args;
    for (int i = 0; i < 300; i++) {
      if (i != 0) args += ' ';
      args += "nn";
    }
    Rig rig(args);
    CHECK(rig.op->PatternLength() == kMatchMaxPattern);
    CHECK(rig.op->ElementIsWildcard(kMatchMaxPattern - 1));
    CHECK_FALSE(rig.op->ElementIsWildcard(kMatchMaxPattern));

    // And it still works at the ceiling: 256 values complete it, 255 do not.
    for (int i = 0; i < kMatchMaxPattern - 1; i++)
      rig.SendInt(i);
    CHECK(rig.Count() == 0);
    rig.SendInt(0);
    CHECK(rig.Count() == 1);
  }

  TEST_CASE("match: SetParams empties the pattern and re-reads it (#472)") {
    Rig rig("1 2 3");
    rig.op->SetParams("");
    CHECK(rig.op->PatternLength() == 0);
    rig.Stream({1, 2, 3});
    CHECK(rig.Count() == 0);

    rig.op->SetParams("4 5");
    CHECK(rig.op->PatternLength() == 2);
    rig.Stream({4, 5});
    CHECK(rig.Count() == 1);

    // A shorter pattern must not leave the window carrying values from the
    // longer one.
    rig.op->SetParams("9");
    CHECK(rig.op->Held() == 0);
  }

  TEST_CASE("match: accessors refuse an index outside the pattern (#472)") {
    Rig rig("1 nn");
    CHECK_FALSE(rig.op->ElementIsLiteral(-1));
    CHECK_FALSE(rig.op->ElementIsWildcard(-1));
    CHECK(rig.op->ElementValue(-1) == 0.f);
    CHECK_FALSE(rig.op->ElementIsLiteral(2));
    CHECK_FALSE(rig.op->ElementIsWildcard(2));
    CHECK(rig.op->ElementValue(2) == 0.f);
    // A wildcard has no value of its own.
    CHECK(rig.op->ElementValue(1) == 0.f);
  }

  // ─── the invariants ─────────────────────────────────────────────────────────

  TEST_CASE("match: Progress() never reaches the pattern length (#472)") {
    // A full-length prefix match would have emitted and emptied the window, so
    // a Progress() equal to the pattern length would mean a sequence had been
    // completed and not reported.
    Rig rig("1 2 1 2");
    const int values[] = {1, 2, 1, 2, 1, 1, 2, 1, 2, 2, 1, 2, 1, 2};
    for (int v : values) {
      rig.SendInt(v);
      CHECK(rig.op->Progress() < rig.op->PatternLength());
      CHECK(rig.op->Held() <= rig.op->PatternLength());
    }
    // The stream does contain the sequence, three times over and disjointly —
    // which is also a restart test, since two of the three begin inside the
    // wreckage of a failed candidate.
    CHECK(rig.Count() == 3);
  }

  TEST_CASE("match: every reported list has exactly pattern-length values (#472)") {
    Rig rig("nn nn nn");
    for (int i = 0; i < 20; i++)
      rig.SendInt(i);
    REQUIRE(rig.Count() == 6);
    for (const std::string& list : rig.sink.lists) {
      CAPTURE(list);
      int values = 1;
      for (char c : list)
        if (c == ' ') values++;
      CHECK(values == 3);
    }
    CHECK(rig.All() == "0 1 2|3 4 5|6 7 8|9 10 11|12 13 14|15 16 17");
  }

  TEST_CASE("match: the window is already empty when the outlet fires (#472)") {
    // Settled before the send, as .onebang's and .next's state is: the send
    // path is synchronous with no queue in between, so anything reached from
    // the outlet must find the object describing the sequence being reported
    // rather than the one before it.
    std::unique_ptr<gMatch> op(new gMatch());
    op->SetParams("1 2");
    InspectingSink sink;
    sink.target = op.get();
    op->ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op->GetOutlet(0), 0);

    op->GetInlet(0)->SetInt(1, YSE::T_GUI);
    op->GetInlet(0)->SetInt(2, YSE::T_GUI);
    REQUIRE(sink.hits == 1);
    CHECK(sink.heldDuringSend == 0);
    CHECK(sink.progressDuringSend == 0);
  }

  TEST_CASE("match: Calculate does nothing (#472)") {
    // The object is driven by its inlet. An emitting Calculate() would report a
    // sequence once per DSP block from a stimulus no patch sent.
    Rig rig("1 2");
    rig.Stream({1, 2});
    REQUIRE(rig.Count() == 1);

    for (int i = 0; i < 10; i++)
      rig.op->Calculate(YSE::T_DSP);
    CHECK(rig.Count() == 1);

    // Nor does it advance a partial sequence.
    rig.SendInt(1);
    for (int i = 0; i < 10; i++)
      rig.op->Calculate(YSE::T_DSP);
    CHECK(rig.Count() == 1);
    CHECK(rig.op->Progress() == 1);
  }

  TEST_CASE("match: the GUI value reports how far into the pattern the stream is (#472)") {
    Rig rig("1 2 3");
    CHECK(rig.op->GetGuiValue() == "0");
    rig.SendInt(1);
    CHECK(rig.op->GetGuiValue() == "1");
    rig.SendInt(2);
    CHECK(rig.op->GetGuiValue() == "2");
    rig.SendInt(3);
    CHECK(rig.op->GetGuiValue() == "0");
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("match: survives a DumpJSON / ParseJSON round trip (#472)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_MATCH, "1 nn 3") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".match") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".match"));
    CHECK(copy->GetInputs() == 1);
    CHECK(copy->GetOutputs() == 1);

    // The pattern came back, wildcard and all — a round trip that only kept
    // the port count would pass everything above this line.
    TestHelpers::ListSink sink;
    YSE::pHandle sinkHandle(&sink);
    loaded.Connect(copy, 0, &sinkHandle, 0);
    copy->SetIntData(0, 1);
    copy->SetIntData(0, 42);
    copy->SetIntData(0, 3);
    CHECK(sink.gotList);
    CHECK(sink.received == "1 42 3");
  }

  // ─── documentation ──────────────────────────────────────────────────────────

  TEST_CASE("match: documents itself as GENERIC with a labelled port set (#472)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_MATCH));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    CHECK_FALSE(obj->GetDescription().empty());

    REQUIRE(obj->NumInputs() == 1);
    CHECK(obj->GetInlet(0)->GetDocLabel() == "in");

    REQUIRE(obj->NumOutputs() == 1);
    CHECK(obj->GetOutlet(0)->GetDocLabel() == "match");

    REQUIRE(obj->GetParamDocs().size() == 1);
    CHECK(obj->GetParamDocs()[0].name == "pattern");
  }

  TEST_CASE("match: the inlet takes int, float and list but not bang (#472)") {
    // Max routes bang through `anything`, which "performs the same as list",
    // and a bang is a list of no values: a message carrying no number cannot
    // advance a sequence.
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_MATCH));
    REQUIRE(obj != nullptr);
    const unsigned int in = obj->GetInlet(0)->GetAcceptedTypes();
    CHECK((in & YSE::PATCHER::IT_INT) != 0);
    CHECK((in & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((in & YSE::PATCHER::IT_LIST) != 0);
    CHECK((in & YSE::PATCHER::IT_BANG) == 0);
    CHECK((in & YSE::PATCHER::IT_BUFFER) == 0);
  }

  // ─── end to end ─────────────────────────────────────────────────────────────

  TEST_CASE("match: recognises a motif in a real patcher (#472)") {
    // Registry, wiring API and object together, driven through the handle API
    // the way a host drives it.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* m = p.CreateObject(YSE::OBJ::G_MATCH, "60 62 nn 60");
    REQUIRE(m != nullptr);

    TestHelpers::ListSink sink;
    YSE::pHandle sinkHandle(&sink);
    p.Connect(m, 0, &sinkHandle, 0);

    // A false start, then the motif — the restart rule under the wiring API.
    const int notes[] = {60, 62, 65, 67, 60, 62, 64, 60};
    for (int n : notes)
      m->SetIntData(0, n);

    CHECK(sink.gotList);
    CHECK(sink.received == "60 62 64 60");
  }

  TEST_CASE("match: a live SetParams through the patcher replaces the pattern (#472)") {
    // The pattern is a LIST parameter, so a re-parse takes the structural
    // rebuild route of #234 rather than patching the live object. The handle
    // follows the replacement, so the patch cords drawn afterwards land on the
    // object that is actually in the graph.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* m = p.CreateObject(YSE::OBJ::G_MATCH, "1 2");
    REQUIRE(m != nullptr);
    m->SetParams("3 4");
    CHECK(m->GetParams() == std::string("3 4"));

    TestHelpers::ListSink sink;
    YSE::pHandle sinkHandle(&sink);
    p.Connect(m, 0, &sinkHandle, 0);

    m->SetIntData(0, 1);
    m->SetIntData(0, 2);
    CHECK_FALSE(sink.gotList);

    m->SetIntData(0, 3);
    m->SetIntData(0, 4);
    CHECK(sink.gotList);
    CHECK(sink.received == "3 4");
  }

} // TEST_SUITE
