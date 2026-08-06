// Tests for the running-comparison pair (issue #462): .maximum and .minimum
//
// Two inlets and one float outlet each. Inlet 0 compares an incoming number
// against a stored comparand and sends the winner out; inlet 1 replaces the
// comparand silently; a bang re-sends the most recent output; a list is reduced
// against itself and leaves the runner-up behind as the new comparand.
//
// The pair is a mirror image, so everything here is asserted for *both*
// objects rather than for one with the other assumed. Where the assertion is
// direction-independent (nothing emits from inlet 1; a bang replays; a
// non-finite number reads as 0) the test loops over the two through the shared
// gExtremumBase; where the numbers differ the two cases are written out, since
// a test that computed its own expectation from the same comparator it is
// testing would pass against a comparator that is simply wrong.
//
// The three places this is a design decision rather than a comparison:
//
//   - **the comparand never moves on the hot inlet.** .maximum is a two-operand
//     max with a sticky right operand, not a running peak — Max's `peak` /
//     `trough` (#463) is the running reading, and confusing the two silently
//     turns a clamp into a latch that can never come back down.
//   - **what a bang reports before anything has been sent.** Max documents the
//     bang as a replay and leaves the empty case open; the answer here is the
//     creation argument, and a comparand set silently on inlet 1 must not
//     change it, or the replay would emit a number the object never sent.
//   - **the list leaves the runner-up, not the winner.** Storing the winner
//     would make every later scalar comparison a tie with the value just
//     emitted, so the object would answer the same number forever.
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
#include "patcher/math/gExtremum.h"

namespace {

  using YSE::PATCHER::ExtremumBest;
  using YSE::PATCHER::ExtremumGreater;
  using YSE::PATCHER::ExtremumLess;
  using YSE::PATCHER::ExtremumSanitize;
  using YSE::PATCHER::ExtremumScan;
  using YSE::PATCHER::gExtremumBase;
  using YSE::PATCHER::gMaximum;
  using YSE::PATCHER::gMinimum;

  constexpr float INF = std::numeric_limits<float>::infinity();
  const float NOT_A_NUMBER = std::numeric_limits<float>::quiet_NaN();

  // Counts what came out as well as recording it, so "this message is silent"
  // can be asserted rather than assumed.
  struct Sink : YSE::PATCHER::pObject {
    float last = 0.f;
    int hits = 0;

    Sink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) {
        last = v;
        hits++;
      });
    }
    const char* Type() const override {
      return "extremum_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // One object wired to its outlet, plus the ways a patch drives it. Holds the
  // object through the shared base so a single rig serves both directions.
  struct Rig {
    std::unique_ptr<gExtremumBase> op;
    Sink sink;

    explicit Rig(gExtremumBase* object) : op(object) {
      op->ConnectOutlet(sink.GetInlet(0), 0);
      sink.ConnectInlet(op->GetOutlet(0), 0);
    }

    // Creation argument, applied the way a saved patch applies it.
    Rig(gExtremumBase* object, const std::string& args) : Rig(object) {
      op->SetParams(args);
    }

    float Send(float value) {
      op->GetInlet(0)->SetFloat(value, YSE::T_GUI);
      return sink.last;
    }

    float SendInt(int value) {
      op->GetInlet(0)->SetInt(value, YSE::T_GUI);
      return sink.last;
    }

    float Bang() {
      op->GetInlet(0)->SetBang(YSE::T_GUI);
      return sink.last;
    }

    float List(const std::string& text) {
      op->GetInlet(0)->SetList(text, YSE::T_GUI);
      return sink.last;
    }

    void SetCompare(float value) {
      op->GetInlet(1)->SetFloat(value, YSE::T_GUI);
    }

    void SetCompareInt(int value) {
      op->GetInlet(1)->SetInt(value, YSE::T_GUI);
    }

    int Emitted() const {
      return sink.hits;
    }
  };

  // A rig per direction, for the assertions that hold whichever way the
  // comparison runs.
  std::vector<const char*> BothNames() {
    return {YSE::OBJ::G_MAXIMUM, YSE::OBJ::G_MINIMUM};
  }

  gExtremumBase* Make(const char* name) {
    if (std::string(name) == YSE::OBJ::G_MAXIMUM) return new gMaximum();
    return new gMinimum();
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("extremum: both objects are creatable through the registry (#462)") {
    YSE::patcher p;
    p.create(2);
    for (const char* name : BothNames()) {
      CAPTURE(name);
      YSE::pHandle* h = p.CreateObject(name);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == std::string(name));
      CHECK(h->GetInputs() == 2);
      CHECK(h->GetOutputs() == 1);
      CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
    }
  }

  TEST_CASE("extremum: both objects are listed by pRegistry::AllNames (#462)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".maximum")) != names.end());
    CHECK(std::find(names.begin(), names.end(), std::string(".minimum")) != names.end());
  }

  // ─── the shared comparator pieces ───────────────────────────────────────────
  // Free functions in gExtremum.h, which is where .peak / .trough (#463) will
  // pick them up. Tested directly as well as through the objects, since a bug
  // in the scan is much easier to read here than through four inlets.

  TEST_CASE("extremum: the orderings are strict, so a tie keeps the incumbent (#462)") {
    CHECK(ExtremumGreater(2.f, 1.f));
    CHECK_FALSE(ExtremumGreater(1.f, 2.f));
    CHECK_FALSE(ExtremumGreater(1.f, 1.f));

    CHECK(ExtremumLess(1.f, 2.f));
    CHECK_FALSE(ExtremumLess(2.f, 1.f));
    CHECK_FALSE(ExtremumLess(1.f, 1.f));

    // ExtremumBest is Max's scalar rule: the candidate only displaces the
    // incumbent when it strictly beats it.
    CHECK(ExtremumBest(3.f, 5.f, ExtremumGreater) == 5.f);
    CHECK(ExtremumBest(7.f, 5.f, ExtremumGreater) == 7.f);
    CHECK(ExtremumBest(3.f, 5.f, ExtremumLess) == 3.f);
    CHECK(ExtremumBest(7.f, 5.f, ExtremumLess) == 5.f);
  }

  TEST_CASE("extremum: ExtremumSanitize replaces every non-finite value with 0 (#462)") {
    CHECK(ExtremumSanitize(1.5f) == 1.5f);
    CHECK(ExtremumSanitize(-1.5f) == -1.5f);
    CHECK(ExtremumSanitize(0.f) == 0.f);
    CHECK(ExtremumSanitize(INF) == 0.f);
    CHECK(ExtremumSanitize(-INF) == 0.f);
    CHECK(ExtremumSanitize(NOT_A_NUMBER) == 0.f);
  }

  TEST_CASE("extremum: ExtremumScan reports the winner and the runner-up (#462)") {
    float best = -1.f;
    float second = -1.f;

    const float ascending[3] = {1.f, 2.f, 3.f};
    ExtremumScan(ascending, 3, ExtremumGreater, best, second);
    CHECK(best == 3.f);
    CHECK(second == 2.f);
    ExtremumScan(ascending, 3, ExtremumLess, best, second);
    CHECK(best == 1.f);
    CHECK(second == 2.f);

    // Descending — the case a runner-up seeded from items[0] twice gets wrong,
    // which is why the scan seeds from the first *pair*.
    const float descending[2] = {5.f, 3.f};
    ExtremumScan(descending, 2, ExtremumGreater, best, second);
    CHECK(best == 5.f);
    CHECK(second == 3.f);
    ExtremumScan(descending, 2, ExtremumLess, best, second);
    CHECK(best == 3.f);
    CHECK(second == 5.f);

    // The winner arriving last, and arriving in the middle.
    const float scattered[5] = {2.f, 9.f, -4.f, 7.f, 0.f};
    ExtremumScan(scattered, 5, ExtremumGreater, best, second);
    CHECK(best == 9.f);
    CHECK(second == 7.f);
    ExtremumScan(scattered, 5, ExtremumLess, best, second);
    CHECK(best == -4.f);
    CHECK(second == 0.f);
  }

  TEST_CASE("extremum: ExtremumScan counts duplicates as separate places (#462)") {
    float best = -1.f;
    float second = -1.f;

    // "The next greatest value in the list" is the second element in sorted
    // order, not the second *distinct* one.
    const float tied[3] = {5.f, 5.f, 1.f};
    ExtremumScan(tied, 3, ExtremumGreater, best, second);
    CHECK(best == 5.f);
    CHECK(second == 5.f);

    const float flat[4] = {2.f, 2.f, 2.f, 2.f};
    ExtremumScan(flat, 4, ExtremumLess, best, second);
    CHECK(best == 2.f);
    CHECK(second == 2.f);
  }

  TEST_CASE("extremum: ExtremumScan writes both outputs on a degenerate call (#462)") {
    float best = -1.f;
    float second = -1.f;

    const float one[1] = {42.f};
    ExtremumScan(one, 1, ExtremumGreater, best, second);
    CHECK(best == 42.f);
    CHECK(second == 42.f);

    // No caller can read an uninitialised value out of it, whatever it passes.
    ExtremumScan(one, 0, ExtremumGreater, best, second);
    CHECK(best == 0.f);
    CHECK(second == 0.f);
    ExtremumScan(nullptr, 4, ExtremumLess, best, second);
    CHECK(best == 0.f);
    CHECK(second == 0.f);
  }

  // ─── inlet 0: compare and emit ──────────────────────────────────────────────

  TEST_CASE("maximum: a number on inlet 0 emits the larger of it and the comparand (#462)") {
    Rig rig(new gMaximum());
    rig.SetCompare(5.f);
    // Max: "If the number is greater than the value currently stored in
    // maximum, it is sent out the outlet. Otherwise, the stored value is sent
    // out."
    CHECK(rig.Send(10.f) == doctest::Approx(10.f));
    CHECK(rig.Send(3.f) == doctest::Approx(5.f));
    CHECK(rig.Send(5.f) == doctest::Approx(5.f)); // a tie keeps the incumbent
    CHECK(rig.Emitted() == 3); // every number is answered
  }

  TEST_CASE("minimum: a number on inlet 0 emits the smaller of it and the comparand (#462)") {
    Rig rig(new gMinimum());
    rig.SetCompare(5.f);
    CHECK(rig.Send(10.f) == doctest::Approx(5.f));
    CHECK(rig.Send(3.f) == doctest::Approx(3.f));
    CHECK(rig.Send(5.f) == doctest::Approx(5.f));
    CHECK(rig.Emitted() == 3);
  }

  TEST_CASE("extremum: an int on inlet 0 behaves exactly like the float (#462)") {
    Rig hi(new gMaximum());
    hi.SetCompareInt(4);
    CHECK(hi.SendInt(9) == doctest::Approx(9.f));
    CHECK(hi.SendInt(1) == doctest::Approx(4.f));

    Rig lo(new gMinimum());
    lo.SetCompareInt(4);
    CHECK(lo.SendInt(9) == doctest::Approx(4.f));
    CHECK(lo.SendInt(1) == doctest::Approx(1.f));
  }

  TEST_CASE("extremum: negative and fractional numbers compare as numbers (#462)") {
    Rig hi(new gMaximum());
    hi.SetCompare(-2.5f);
    CHECK(hi.Send(-7.25f) == doctest::Approx(-2.5f));
    CHECK(hi.Send(-1.25f) == doctest::Approx(-1.25f));

    Rig lo(new gMinimum());
    lo.SetCompare(-2.5f);
    CHECK(lo.Send(-7.25f) == doctest::Approx(-7.25f));
    CHECK(lo.Send(-1.25f) == doctest::Approx(-2.5f));
  }

  // The distinction that decides what the object is: it is a two-operand max
  // with a sticky right operand, not a running peak. If the input were folded
  // into the comparand, the second CHECK in each half would report the first
  // input forever — a clamp that has turned into a latch.
  TEST_CASE("extremum: a number on inlet 0 never becomes the comparand (#462)") {
    Rig hi(new gMaximum());
    hi.SetCompare(5.f);
    CHECK(hi.Send(100.f) == doctest::Approx(100.f));
    CHECK(hi.op->Comparand() == doctest::Approx(5.f));
    CHECK(hi.Send(3.f) == doctest::Approx(5.f)); // not 100
    CHECK(hi.op->Comparand() == doctest::Approx(5.f));

    Rig lo(new gMinimum());
    lo.SetCompare(5.f);
    CHECK(lo.Send(-100.f) == doctest::Approx(-100.f));
    CHECK(lo.op->Comparand() == doctest::Approx(5.f));
    CHECK(lo.Send(7.f) == doctest::Approx(5.f)); // not -100
    CHECK(lo.op->Comparand() == doctest::Approx(5.f));
  }

  // The everyday use: a bound that is itself a signal.
  TEST_CASE("extremum: the comparand can move between inputs (#462)") {
    Rig rig(new gMaximum());
    rig.SetCompare(0.f);
    CHECK(rig.Send(-5.f) == doctest::Approx(0.f)); // a half-wave rectifier
    rig.SetCompare(-10.f);
    CHECK(rig.Send(-5.f) == doctest::Approx(-5.f)); // the floor moved
  }

  // ─── inlet 1: silent ────────────────────────────────────────────────────────

  TEST_CASE("extremum: inlet 1 stores the comparand without emitting (#462)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name));
      const int before = rig.Emitted();

      // Max: "The number is stored for comparison with subsequent numbers
      // received in the left inlet."
      rig.SetCompare(3.f);
      rig.SetCompare(-8.f);
      rig.SetCompareInt(12);
      CHECK(rig.Emitted() == before);
      CHECK(rig.op->Comparand() == doctest::Approx(12.f));
    }
  }

  TEST_CASE("extremum: inlet 1 ignores bangs and lists (#462)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name));
      rig.SetCompare(4.f);
      const int before = rig.Emitted();

      // Only inlet 0 registers them, so this asserts the registration as much
      // as the handler guard; either way nothing may change or come out.
      rig.op->GetInlet(1)->SetBang(YSE::T_GUI);
      rig.op->GetInlet(1)->SetList("1 2 3", YSE::T_GUI);
      CHECK(rig.Emitted() == before);
      CHECK(rig.op->Comparand() == doctest::Approx(4.f));
    }
  }

  // ─── bang ───────────────────────────────────────────────────────────────────

  TEST_CASE("extremum: a bang re-sends the most recent output (#462)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name));
      rig.SetCompare(5.f);
      const float emitted = rig.Send(1.f);

      for (int i = 0; i < 3; i++) {
        CAPTURE(i);
        CHECK(rig.Bang() == doctest::Approx(emitted));
      }
      // A replay, not a recomputation: the comparand is untouched by it.
      CHECK(rig.op->Comparand() == doctest::Approx(5.f));
    }
  }

  TEST_CASE("extremum: a bang before any input reports the initial argument (#462)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name), "7");
      REQUIRE(rig.op->Initial() == doctest::Approx(7.f));
      // Not silence, and not the constructor's 0: the argument reaches both the
      // comparand and the bang's stand-in through the parse callback.
      CHECK(rig.Bang() == doctest::Approx(7.f));
      CHECK(rig.Emitted() == 1);
    }
  }

  TEST_CASE("extremum: a bang before any input reports 0 without an argument (#462)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name));
      CHECK(rig.Bang() == 0.f);
      CHECK(std::isfinite(rig.sink.last));
      CHECK(rig.Emitted() == 1);
    }
  }

  // A bang is documented as a replay, so it must never report a number the
  // object has not sent — and a comparand set silently on inlet 1 has not been
  // sent.
  TEST_CASE("extremum: a silent comparand change does not move what a bang reports (#462)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name), "2");
      rig.SetCompare(99.f);
      CHECK(rig.Bang() == doctest::Approx(2.f));

      // Once something has actually come out, that is what the bang replays.
      const float emitted = rig.Send(50.f);
      CHECK(rig.Bang() == doctest::Approx(emitted));
    }
  }

  // ─── the creation argument ──────────────────────────────────────────────────

  TEST_CASE("maximum: the initial argument is the first comparand (#462)") {
    Rig rig(new gMaximum(), "10");
    CHECK(rig.op->Comparand() == doctest::Approx(10.f));
    CHECK(rig.Send(4.f) == doctest::Approx(10.f));
    CHECK(rig.Send(40.f) == doctest::Approx(40.f));
  }

  TEST_CASE("minimum: the initial argument is the first comparand (#462)") {
    Rig rig(new gMinimum(), "10");
    CHECK(rig.op->Comparand() == doctest::Approx(10.f));
    CHECK(rig.Send(40.f) == doctest::Approx(10.f));
    CHECK(rig.Send(4.f) == doctest::Approx(4.f));
  }

  TEST_CASE("extremum: a fractional argument is kept as a float (#462)") {
    Rig rig(new gMaximum(), "2.5");
    CHECK(rig.op->Comparand() == doctest::Approx(2.5f));
    CHECK(rig.Send(2.25f) == doctest::Approx(2.5f));
  }

  // Max's int variant would round every comparison to an integer; this port
  // deliberately does not, since `.maximum 5` and `.maximum 5.0` are the same
  // parameter string here.
  TEST_CASE("extremum: an integral argument does not make the object integral (#462)") {
    Rig hi(new gMaximum(), "5");
    CHECK(hi.Send(5.5f) == doctest::Approx(5.5f)); // not 5, and not 6
    CHECK(hi.Send(4.75f) == doctest::Approx(5.f));

    Rig lo(new gMinimum(), "5");
    CHECK(lo.Send(4.5f) == doctest::Approx(4.5f));
  }

  TEST_CASE("extremum: a non-finite argument reads as 0 (#462)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name), "inf");
      // An infinite comparand would win (or lose) every comparison for the rest
      // of the object's life.
      CHECK(std::isfinite(rig.op->Comparand()));
      CHECK(rig.op->Comparand() == 0.f);
      CHECK(rig.Bang() == 0.f);
    }
  }

  // ─── lists ──────────────────────────────────────────────────────────────────

  TEST_CASE("maximum: a list is reduced to its greatest value (#462)") {
    Rig rig(new gMaximum());
    // Max: "The numbers in the list are all compared to each other, and the
    // greatest value is sent out the outlet."
    CHECK(rig.List("3 9 4") == doctest::Approx(9.f));
    CHECK(rig.List("-5 -1 -3") == doctest::Approx(-1.f));
    CHECK(rig.List("2.5 2.75") == doctest::Approx(2.75f));
  }

  TEST_CASE("minimum: a list is reduced to its smallest value (#462)") {
    Rig rig(new gMinimum());
    CHECK(rig.List("3 9 4") == doctest::Approx(3.f));
    CHECK(rig.List("-5 -1 -3") == doctest::Approx(-5.f));
    CHECK(rig.List("2.5 2.75") == doctest::Approx(2.5f));
  }

  // The comparand takes no part in the reduction — the list is compared against
  // itself alone.
  TEST_CASE("extremum: the stored comparand does not join the list comparison (#462)") {
    Rig hi(new gMaximum());
    hi.SetCompare(1000.f);
    CHECK(hi.List("3 9 4") == doctest::Approx(9.f)); // not 1000

    Rig lo(new gMinimum());
    lo.SetCompare(-1000.f);
    CHECK(lo.List("3 9 4") == doctest::Approx(3.f)); // not -1000
  }

  TEST_CASE("maximum: a list leaves the next greatest value as the comparand (#462)") {
    Rig rig(new gMaximum());
    // Max: "The value stored in maximum is replaced by the next greatest value
    // in the list."
    CHECK(rig.List("3 9 4") == doctest::Approx(9.f));
    CHECK(rig.op->Comparand() == doctest::Approx(4.f));
    // Which is what the next scalar compares against.
    CHECK(rig.Send(1.f) == doctest::Approx(4.f));
    CHECK(rig.Send(6.f) == doctest::Approx(6.f));
  }

  TEST_CASE("minimum: a list leaves the next smallest value as the comparand (#462)") {
    Rig rig(new gMinimum());
    CHECK(rig.List("3 9 4") == doctest::Approx(3.f));
    CHECK(rig.op->Comparand() == doctest::Approx(4.f));
    CHECK(rig.Send(8.f) == doctest::Approx(4.f));
    CHECK(rig.Send(2.f) == doctest::Approx(2.f));
  }

  // Why the runner-up rather than the winner: storing the winner would make
  // every later comparison a tie with the value just emitted, and the object
  // would answer the same number forever.
  TEST_CASE("extremum: after a list the object still responds to smaller inputs (#462)") {
    Rig rig(new gMaximum());
    rig.List("1 100");
    CHECK(rig.op->Comparand() == doctest::Approx(1.f)); // not 100
    CHECK(rig.Send(50.f) == doctest::Approx(50.f));
  }

  TEST_CASE("extremum: a one-element list is a plain number (#462)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name), "5");
      // Max routes it to the int / float method: compared against the
      // comparand, and the comparand is left alone.
      const float scalar = rig.Send(8.f);
      Rig other(Make(name), "5");
      CHECK(other.List("8") == doctest::Approx(scalar));
      CHECK(other.op->Comparand() == doctest::Approx(5.f));
    }
  }

  TEST_CASE("extremum: a list of equal numbers reports that number (#462)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name));
      CHECK(rig.List("4 4 4") == doctest::Approx(4.f));
      CHECK(rig.op->Comparand() == doctest::Approx(4.f));
    }
  }

  TEST_CASE("extremum: a long list is truncated at 256 items (#462)") {
    // Max: "accepts lists of up to 256 elements". The 257th item is the biggest
    // and the smallest, and neither object may see it.
    std::string text;
    for (int i = 0; i < 256; i++) {
      text += std::to_string(i + 1);
      text += ' ';
    }

    Rig hi(new gMaximum());
    CHECK(hi.List(text + "9999") == doctest::Approx(256.f));

    Rig lo(new gMinimum());
    CHECK(lo.List(text + "-9999") == doctest::Approx(1.f));
  }

  TEST_CASE("extremum: a message with no numbers in it is ignored (#462)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name), "6");
      const int before = rig.Emitted();

      rig.List("wobble");
      rig.List("");
      rig.List("   ");
      rig.List("clear");
      CHECK(rig.Emitted() == before);
      CHECK(rig.op->Comparand() == doctest::Approx(6.f));
    }
  }

  // The corollary .mean and .accum also carry: the shared reader steps over
  // tokens it cannot parse, so a word followed by numbers is a list of those
  // numbers. Neither Max object defines a message word, so nothing is shadowed.
  TEST_CASE("extremum: a word followed by numbers reads as the numbers (#462)") {
    Rig rig(new gMaximum(), "5");
    CHECK(rig.List("set 8") == doctest::Approx(8.f)); // the number 8
    CHECK(rig.op->Comparand() == doctest::Approx(5.f)); // still a one-item list
    CHECK(rig.List("go 1 9") == doctest::Approx(9.f));
  }

  // ─── non-finite input ───────────────────────────────────────────────────────
  // A NaN compares false against everything, so an unfiltered one would lose
  // every comparison and *still* be the value sent out; an infinity would win
  // or lose every comparison for good. Both read as 0 instead — the convention
  // ./ , .sqrt, .zmap, .clip, .slide and .mean already use.

  TEST_CASE("extremum: a non-finite number on inlet 0 reads as 0 (#462)") {
    Rig hi(new gMaximum());
    hi.SetCompare(-5.f);
    CHECK(hi.Send(NOT_A_NUMBER) == doctest::Approx(0.f));
    CHECK(hi.Send(INF) == doctest::Approx(0.f));
    CHECK(hi.Send(-INF) == doctest::Approx(0.f));
    CHECK(std::isfinite(hi.sink.last));

    Rig lo(new gMinimum());
    lo.SetCompare(5.f);
    CHECK(lo.Send(NOT_A_NUMBER) == doctest::Approx(0.f));
    CHECK(lo.Send(INF) == doctest::Approx(0.f));
    CHECK(lo.Send(-INF) == doctest::Approx(0.f));
    CHECK(std::isfinite(lo.sink.last));
  }

  TEST_CASE("extremum: a non-finite number on inlet 1 reads as 0 (#462)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name));
      rig.SetCompare(INF);
      CHECK(rig.op->Comparand() == 0.f);
      rig.SetCompare(NOT_A_NUMBER);
      CHECK(rig.op->Comparand() == 0.f);
      // And the object still compares, which an infinite comparand would have
      // permanently prevented.
      CHECK(std::isfinite(rig.Send(3.f)));
    }
  }

  TEST_CASE("extremum: a non-finite list item reads as 0 (#462)") {
    Rig hi(new gMaximum());
    CHECK(hi.List("1 inf 2") == doctest::Approx(2.f));
    CHECK(std::isfinite(hi.op->Comparand()));

    Rig lo(new gMinimum());
    CHECK(lo.List("1 -inf 2") == doctest::Approx(0.f));
    CHECK(std::isfinite(lo.op->Comparand()));
  }

  TEST_CASE("extremum: nothing the outlet ever carries is non-finite (#462)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name), "nan");
      rig.SetCompare(INF);
      rig.Send(-INF);
      rig.List("inf nan -inf");
      rig.Bang();
      CHECK(std::isfinite(rig.sink.last));
      CHECK(std::isfinite(rig.op->LastOutput()));
      CHECK(std::isfinite(rig.op->Comparand()));
    }
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("extremum: survives a DumpJSON / ParseJSON round trip (#462)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      YSE::patcher src;
      src.create(2);
      YSE::pHandle* h = src.CreateObject(name);
      REQUIRE(h != nullptr);
      h->SetParams("12.5");
      const std::string json = src.DumpJSON();
      CHECK(json.find(name) != std::string::npos);

      YSE::patcher loaded;
      loaded.create(2);
      loaded.ParseJSON(json);
      REQUIRE(loaded.Objects() == 1);

      YSE::pHandle* copy = loaded.GetHandleFromList(0);
      REQUIRE(copy != nullptr);
      CHECK(std::string(copy->Type()) == std::string(name));
      CHECK(copy->GetInputs() == 2);
      CHECK(copy->GetOutputs() == 1);
      CHECK(copy->GetParams() == std::string("12.5"));
    }
  }

  TEST_CASE("extremum: the GUI value reports what the outlet last carried (#462)") {
    Rig rig(new gMaximum(), "3");
    CHECK(std::stod(rig.op->GetGuiValue()) == doctest::Approx(3.0));
    rig.Send(9.f);
    CHECK(std::stod(rig.op->GetGuiValue()) == doctest::Approx(9.0));
    rig.Send(1.f);
    CHECK(std::stod(rig.op->GetGuiValue()) == doctest::Approx(3.0));
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the port shape, which is what a binding
  // generator keys on.

  TEST_CASE("extremum: both document themselves as MATH with two inlets and one outlet (#462)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(name));
      REQUIRE(obj != nullptr);
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
      CHECK_FALSE(obj->GetDescription().empty());
      REQUIRE(obj->NumInputs() == 2);
      CHECK(obj->GetInlet(0)->GetDocLabel() == "in");
      CHECK(obj->GetInlet(1)->GetDocLabel() == "compare");
      REQUIRE(obj->NumOutputs() == 1);
      CHECK(obj->GetOutlet(0)->GetDocLabel() == "out");
      CHECK(obj->GetOutputType(0) == YSE::OUT_TYPE::FLOAT);
      REQUIRE(obj->GetParamDocs().size() == 1);
      CHECK(obj->GetParamDocs()[0].name == "initial");
      CHECK(obj->GetParamDocs()[0].defaultValue == "0");
    }
  }

  TEST_CASE("extremum: the two descriptions name their own direction (#462)") {
    // The pair is built from one body, so a copy-paste in the doc strings is
    // the one mistake the shared implementation makes easy.
    std::unique_ptr<YSE::PATCHER::pObject> hi(YSE::PATCHER::Register().Get(YSE::OBJ::G_MAXIMUM));
    std::unique_ptr<YSE::PATCHER::pObject> lo(YSE::PATCHER::Register().Get(YSE::OBJ::G_MINIMUM));
    REQUIRE(hi != nullptr);
    REQUIRE(lo != nullptr);
    CHECK(hi->GetDescription() != lo->GetDescription());
    CHECK(hi->GetDescription().find("larger") != std::string::npos);
    CHECK(lo->GetDescription().find("smaller") != std::string::npos);
    CHECK(hi->GetOutlet(0)->GetDocDescription() != lo->GetOutlet(0)->GetDocDescription());
  }

  TEST_CASE("extremum: inlet 0 accepts float, int, bang and list (#462)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(name));
      REQUIRE(obj != nullptr);
      const unsigned int hot = obj->GetInlet(0)->GetAcceptedTypes();
      CHECK((hot & YSE::PATCHER::IT_FLOAT) != 0);
      CHECK((hot & YSE::PATCHER::IT_INT) != 0);
      CHECK((hot & YSE::PATCHER::IT_BANG) != 0);
      CHECK((hot & YSE::PATCHER::IT_LIST) != 0);

      // The cold inlet takes numbers only — it has one job.
      const unsigned int cold = obj->GetInlet(1)->GetAcceptedTypes();
      CHECK((cold & YSE::PATCHER::IT_FLOAT) != 0);
      CHECK((cold & YSE::PATCHER::IT_INT) != 0);
      CHECK((cold & YSE::PATCHER::IT_BANG) == 0);
      CHECK((cold & YSE::PATCHER::IT_LIST) == 0);
    }
  }

} // TEST_SUITE("patcher")
