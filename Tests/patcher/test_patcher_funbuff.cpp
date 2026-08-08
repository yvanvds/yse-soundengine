// Tests for .funbuff (issue #497) — the patcher's first store ordered by its own
// key.
//
// Five things are worth pinning here, and they are the ones an implementation can
// get wrong while still looking like it works:
//
//   - **the same inlet writes and reads, and the armed y is one-shot.** A y that
//     stayed behind would turn every later lookup into a store, so the object
//     could never be read at all — and nothing would crash.
//   - **the plain lookup is a *floor* lookup, not an interpolation.** Issue #497
//     calls it interpolated; Max does not. A patch that wanted a stepped zone map
//     and got a ramp is silently wrong in a way nothing downstream can detect, so
//     the step is asserted directly and `interp` is asserted to be the message
//     that ramps.
//   - **the table is sorted, whatever order the pairs arrived in.** Every other
//     answer — the floor lookup, next, dump, interp — is a question about where
//     an x sits among the others, so an unsorted table gives wrong answers rather
//     than ugly ones.
//   - **interp brackets the query correctly on both sides of zero.** Truncating a
//     fractional query toward zero picks the neighbour on the wrong side for a
//     negative x and extrapolates instead of interpolating.
//   - **the contents survive a save only when `embed` says so.** Max's funbuff
//     persistence is opt-in, which is neither .coll's always nor .bag's never.
//
// No audio device and no engine of its own, except where a real patcher graph is
// the point.

#include <doctest/doctest.h>
#include <string>
#include <vector>

#include "patcher/genericObjects/gFunbuff.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::gFunbuff;

namespace {

  // Records everything it receives, in order, tagged by which outlet it came
  // from — so a multi-outlet sequence (x then y, pair after pair) reads back as
  // the exact list of sends. MultiSink only keeps the last of each kind, which
  // cannot tell a dump of three pairs from a dump of one.
  struct Recorder : YSE::PATCHER::pObject {
    std::vector<std::string>* log = nullptr;
    std::string tag;

    Recorder() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { log->push_back(tag + ":bang"); });
      inputs.back().RegisterInt(
          [this](int v, int, YSE::THREAD) { log->push_back(tag + ":i " + std::to_string(v)); });
      inputs.back().RegisterFloat(
          [this](float v, int, YSE::THREAD) { log->push_back(tag + ":f " + std::to_string(v)); });
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { log->push_back(tag + ":l " + v); });
    }
    const char* Type() const override {
      return "recorder";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A standalone .funbuff with a sink on each outlet. Standalone on purpose where
  // the patcher is not the point: a test that needed one could not tell a refused
  // message from a message the patcher never delivered.
  struct Rig {
    MultiSink y;
    MultiSink x;
    MultiSink end;
    gFunbuff obj;

    Rig() {
      obj.ConnectOutlet(y.GetInlet(0), 0);
      obj.ConnectOutlet(x.GetInlet(0), 1);
      obj.ConnectOutlet(end.GetInlet(0), 2);
    }

    void reset() {
      y.reset();
      x.reset();
      end.reset();
    }

    void List(const std::string& message) {
      obj.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void X(int value) {
      obj.GetInlet(0)->SetInt(value, YSE::T_GUI);
    }
    void XFloat(float value) {
      obj.GetInlet(0)->SetFloat(value, YSE::T_GUI);
    }
    void Y(int value) {
      obj.GetInlet(1)->SetInt(value, YSE::T_GUI);
    }
    void YFloat(float value) {
      obj.GetInlet(1)->SetFloat(value, YSE::T_GUI);
    }
    // Store one pair the way a patch does: y on the cold inlet, then x on the
    // hot one.
    void Store(int xv, int yv) {
      Y(yv);
      X(xv);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("funbuff: registered, two inlets and three outlets (#497)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* fb = p.CreateObject(YSE::OBJ::G_FUNBUFF);
    REQUIRE(fb != nullptr);
    CHECK(std::string(fb->Type()) == ".funbuff");
    CHECK(fb->GetInputs() == 2);
    CHECK(fb->GetOutputs() == 3);
  }

  TEST_CASE("funbuff: appears in the registry's name list (#497)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_FUNBUFF)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("funbuff: inlet 0 takes int, float and list; inlet 1 takes numbers (#497)") {
    gFunbuff obj;
    const unsigned int hot = obj.GetInlet(0)->GetAcceptedTypes();
    CHECK((hot & YSE::PATCHER::IT_INT) != 0);
    CHECK((hot & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((hot & YSE::PATCHER::IT_LIST) != 0);

    const unsigned int cold = obj.GetInlet(1)->GetAcceptedTypes();
    CHECK((cold & YSE::PATCHER::IT_INT) != 0);
    CHECK((cold & YSE::PATCHER::IT_FLOAT) != 0);
  }

  TEST_CASE("funbuff: Calculate() sends nothing (#497)") {
    // The object is driven by its inlets; one that emitted from Calculate() would
    // re-send on every DSP tick after a message arrived.
    Rig rig;
    rig.Store(1, 10);
    rig.reset();

    rig.obj.Calculate(YSE::T_DSP);
    CHECK_FALSE(rig.y.gotInt);
    CHECK_FALSE(rig.y.gotFloat);
    CHECK_FALSE(rig.x.gotInt);
    CHECK_FALSE(rig.end.gotBang);
  }

  TEST_CASE("funbuff: a fresh store is empty and answers nothing (#497)") {
    Rig rig;
    CHECK(rig.obj.Count() == 0);

    rig.X(5);
    CHECK_FALSE(rig.y.gotInt);
    CHECK_FALSE(rig.y.gotFloat);
  }

  // ─── storing ────────────────────────────────────────────────────────────────

  TEST_CASE("funbuff: a y on inlet 1 then an x on inlet 0 stores the pair (#497)") {
    // Max: "if a y value was previously received in the right inlet, the pair is
    // stored".
    Rig rig;
    rig.Y(100);
    rig.X(60);
    CHECK(rig.obj.Count() == 1);
    CHECK(rig.obj.XAt(0) == 60);
    CHECK(rig.obj.YAt(0) == 100);

    // Storing sends nothing at all.
    CHECK_FALSE(rig.y.gotInt);
    CHECK_FALSE(rig.x.gotInt);
  }

  TEST_CASE("funbuff: the armed y is consumed by exactly one x (#497)") {
    // Max: "paired with the *next* x value received in the left inlet". A y that
    // stayed behind would turn every later lookup into a store, and the object
    // could never be read at all — which nothing else in this file would notice.
    Rig rig;
    rig.Y(100);
    rig.X(60);
    CHECK(rig.obj.Count() == 1);

    // The second x is a lookup, not a second store.
    rig.reset();
    rig.X(60);
    CHECK(rig.obj.Count() == 1);
    CHECK(rig.y.gotInt);
    CHECK(rig.y.intValue == 100);
  }

  TEST_CASE("funbuff: a two-number list stores a pair (#497)") {
    // Max's ordinary right-to-left list distribution: the second item behaves as
    // though it went to inlet 1 and the first as though it went to inlet 0.
    Rig rig;
    rig.List("60 100");
    CHECK(rig.obj.Count() == 1);
    CHECK(rig.obj.XAt(0) == 60);
    CHECK(rig.obj.YAt(0) == 100);

    // And it leaves nothing armed behind it.
    rig.reset();
    rig.X(60);
    CHECK(rig.y.gotInt);
    CHECK(rig.y.intValue == 100);
    CHECK(rig.obj.Count() == 1);
  }

  TEST_CASE("funbuff: pairs are kept sorted by x whatever order they arrive in (#497)") {
    // The sort is the object: every other answer is a question about where an x
    // sits among the others, so an unsorted table gives wrong answers.
    Rig rig;
    rig.Store(50, 5);
    rig.Store(10, 1);
    rig.Store(90, 9);
    rig.Store(30, 3);

    REQUIRE(rig.obj.Count() == 4);
    CHECK(rig.obj.XAt(0) == 10);
    CHECK(rig.obj.XAt(1) == 30);
    CHECK(rig.obj.XAt(2) == 50);
    CHECK(rig.obj.XAt(3) == 90);
    CHECK(rig.obj.YAt(0) == 1);
    CHECK(rig.obj.YAt(3) == 9);
  }

  TEST_CASE("funbuff: storing at an x that exists replaces its y (#497)") {
    // One x has exactly one y: this is a function, not a multiset.
    Rig rig;
    rig.Store(60, 100);
    rig.Store(60, 20);
    CHECK(rig.obj.Count() == 1);
    CHECK(rig.obj.YAt(0) == 20);
  }

  TEST_CASE("funbuff: a float is converted to an int on either inlet (#497)") {
    Rig rig;
    rig.YFloat(100.7f);
    rig.XFloat(60.9f);
    REQUIRE(rig.obj.Count() == 1);
    CHECK(rig.obj.XAt(0) == 60);
    CHECK(rig.obj.YAt(0) == 100);
  }

  // ─── the lookup is a floor lookup ───────────────────────────────────────────

  TEST_CASE("funbuff: an exact x sends its y (#497)") {
    Rig rig;
    rig.Store(10, 1);
    rig.Store(20, 2);

    rig.reset();
    rig.X(20);
    CHECK(rig.y.gotInt);
    CHECK(rig.y.intValue == 2);
  }

  TEST_CASE("funbuff: a missing x falls back to the closest lesser x (#497)") {
    // Max: "if there is no stored x value which matches the number received,
    // funbuff uses the closest x value which is less than the number received,
    // and sends out the corresponding y value". A step function — and this is
    // where issue #497's "interpolated lookup" shorthand and Max part company.
    Rig rig;
    rig.Store(0, 0);
    rig.Store(100, 1000);

    rig.reset();
    rig.X(50);
    CHECK(rig.y.gotInt);
    // Held, not ramped: an interpolating lookup would have answered 500.
    CHECK(rig.y.intValue == 0);
    CHECK_FALSE(rig.y.gotFloat);

    rig.reset();
    rig.X(99);
    CHECK(rig.y.intValue == 0);

    rig.reset();
    rig.X(100);
    CHECK(rig.y.intValue == 1000);

    // And above the top it holds the last value rather than stopping.
    rig.reset();
    rig.X(500);
    CHECK(rig.y.intValue == 1000);
  }

  TEST_CASE("funbuff: an x below every stored x sends nothing (#497)") {
    // There is no lesser neighbour to fall back to, and the family's rule is
    // that an object with no answer stays quiet rather than inventing one.
    Rig rig;
    rig.Store(10, 1);

    rig.reset();
    rig.X(5);
    CHECK_FALSE(rig.y.gotInt);
    CHECK_FALSE(rig.y.gotFloat);
  }

  // ─── set, delete, clear ─────────────────────────────────────────────────────

  TEST_CASE("funbuff: set stores each space-separated pair (#497)") {
    // Max: "the word set, followed by one or more space-separated pairs of
    // numbers, stores each pair as x,y pair".
    Rig rig;
    rig.List("set 10 1 20 2 30 3");
    REQUIRE(rig.obj.Count() == 3);
    CHECK(rig.obj.XAt(0) == 10);
    CHECK(rig.obj.XAt(2) == 30);
    CHECK(rig.obj.YAt(1) == 2);
  }

  TEST_CASE("funbuff: a trailing odd number in set is dropped, not half-stored (#497)") {
    // Half a pair is not a pair, and pairing it with a zero it was never given
    // would put a point on the curve the patch did not draw.
    Rig rig;
    rig.List("set 10 1 20");
    REQUIRE(rig.obj.Count() == 1);
    CHECK(rig.obj.XAt(0) == 10);
    bool found = false;
    rig.obj.Lookup(20, found);
    CHECK_FALSE(found);
  }

  TEST_CASE("funbuff: delete with one number removes the pair at that x (#497)") {
    // Max: "if delete is followed by only one number, only the x value is sought,
    // and deleted if it is present".
    Rig rig;
    rig.List("set 10 1 20 2 30 3");
    rig.List("delete 20");
    REQUIRE(rig.obj.Count() == 2);
    CHECK(rig.obj.XAt(0) == 10);
    CHECK(rig.obj.XAt(1) == 30);
  }

  TEST_CASE("funbuff: delete with two numbers only removes an exact pair (#497)") {
    // Max: "the word delete, followed by two numbers, looks for such an x,y pair
    // in funbuff, and deletes it if it exists".
    Rig rig;
    rig.List("set 10 1 20 2");

    // The x is there but the y does not match, so nothing goes.
    rig.List("delete 20 99");
    CHECK(rig.obj.Count() == 2);

    rig.List("delete 20 2");
    CHECK(rig.obj.Count() == 1);
    CHECK(rig.obj.XAt(0) == 10);
  }

  TEST_CASE("funbuff: clear empties the store and it stays usable (#497)") {
    Rig rig;
    rig.List("set 10 1 20 2");
    rig.List("clear");
    CHECK(rig.obj.Count() == 0);

    rig.reset();
    rig.X(10);
    CHECK_FALSE(rig.y.gotInt);

    // The table is reusable after it, rather than merely empty.
    rig.Store(5, 50);
    CHECK(rig.obj.Count() == 1);
    CHECK(rig.obj.YAt(0) == 50);
  }

  // ─── dump ───────────────────────────────────────────────────────────────────

  TEST_CASE("funbuff: dump sends every pair in ascending x, x before y (#497)") {
    // Max: "sends all the stored pairs out the middle and left outlets ... in
    // ascending order based on the x value". The x fires first — Max's
    // right-to-left outlet order, the order .coll and .trigger already fire in.
    std::vector<std::string> log;
    Recorder ySink;
    Recorder xSink;
    ySink.log = &log;
    ySink.tag = "y";
    xSink.log = &log;
    xSink.tag = "x";

    gFunbuff obj;
    obj.ConnectOutlet(ySink.GetInlet(0), 0);
    obj.ConnectOutlet(xSink.GetInlet(0), 1);

    // Stored out of order on purpose: dump has to report the sort, not arrival.
    obj.GetInlet(0)->SetList("set 30 3 10 1 20 2", YSE::T_GUI);
    log.clear();

    obj.GetInlet(0)->SetList("dump", YSE::T_GUI);
    REQUIRE(log.size() == 6);
    CHECK(log[0] == "x:i 10");
    CHECK(log[1] == "y:i 1");
    CHECK(log[2] == "x:i 20");
    CHECK(log[3] == "y:i 2");
    CHECK(log[4] == "x:i 30");
    CHECK(log[5] == "y:i 3");
  }

  TEST_CASE("funbuff: dump on an empty store sends nothing (#497)") {
    Rig rig;
    rig.List("dump");
    CHECK_FALSE(rig.y.gotInt);
    CHECK_FALSE(rig.x.gotInt);
  }

  // ─── the pointer ────────────────────────────────────────────────────────────

  TEST_CASE("funbuff: next walks the pairs, sending the gap then the y (#497)") {
    // Max: "calculates the difference between that x value and the value
    // previously pointed to" — outlet 1 is the inter-onset interval, which is
    // what makes a time-stamped .funbuff drivable by a .metro.
    std::vector<std::string> log;
    Recorder ySink;
    Recorder xSink;
    Recorder endSink;
    ySink.log = &log;
    ySink.tag = "y";
    xSink.log = &log;
    xSink.tag = "d";
    endSink.log = &log;
    endSink.tag = "end";

    gFunbuff obj;
    obj.ConnectOutlet(ySink.GetInlet(0), 0);
    obj.ConnectOutlet(xSink.GetInlet(0), 1);
    obj.ConnectOutlet(endSink.GetInlet(0), 2);

    obj.GetInlet(0)->SetList("set 10 1 25 2 30 3", YSE::T_GUI);
    log.clear();

    obj.GetInlet(0)->SetList("next", YSE::T_GUI);
    obj.GetInlet(0)->SetList("next", YSE::T_GUI);
    obj.GetInlet(0)->SetList("next", YSE::T_GUI);
    REQUIRE(log.size() == 6);
    // The first next reports the first x itself — its distance from the origin,
    // which is the delta a sequencer wants before its first event.
    CHECK(log[0] == "d:i 10");
    CHECK(log[1] == "y:i 1");
    CHECK(log[2] == "d:i 15");
    CHECK(log[3] == "y:i 2");
    CHECK(log[4] == "d:i 5");
    CHECK(log[5] == "y:i 3");

    // Off the end: outlet 2 bangs and nothing else is sent. The pointer is left
    // there rather than wrapped, so a repeat keeps banging — deliberately unlike
    // .coll's next, which wraps.
    log.clear();
    obj.GetInlet(0)->SetList("next", YSE::T_GUI);
    obj.GetInlet(0)->SetList("next", YSE::T_GUI);
    REQUIRE(log.size() == 2);
    CHECK(log[0] == "end:bang");
    CHECK(log[1] == "end:bang");
  }

  TEST_CASE("funbuff: goto repositions the pointer and the gap measures from it (#497)") {
    std::vector<std::string> log;
    Recorder ySink;
    Recorder xSink;
    ySink.log = &log;
    ySink.tag = "y";
    xSink.log = &log;
    xSink.tag = "d";

    gFunbuff obj;
    obj.ConnectOutlet(ySink.GetInlet(0), 0);
    obj.ConnectOutlet(xSink.GetInlet(0), 1);

    obj.GetInlet(0)->SetList("set 10 1 25 2 30 3", YSE::T_GUI);
    log.clear();

    // Max: "sets a pointer to the x value (index) specified by the number". It
    // triggers no output of its own.
    obj.GetInlet(0)->SetList("goto 25", YSE::T_GUI);
    CHECK(log.empty());

    obj.GetInlet(0)->SetList("next", YSE::T_GUI);
    REQUIRE(log.size() == 2);
    // Measured from where the patch jumped to, not from wherever the playhead
    // had wandered: the pair at 25 is 0 away from a goto of 25.
    CHECK(log[0] == "d:i 0");
    CHECK(log[1] == "y:i 2");

    // A goto that lands between pairs finds the next greater x — Max's "if the
    // pointer points to a number not yet stored as an x value, to the next
    // greater x value".
    log.clear();
    obj.GetInlet(0)->SetList("goto 26", YSE::T_GUI);
    obj.GetInlet(0)->SetList("next", YSE::T_GUI);
    REQUIRE(log.size() == 2);
    CHECK(log[0] == "d:i 4");
    CHECK(log[1] == "y:i 3");
  }

  // ─── min, max, find ─────────────────────────────────────────────────────────

  TEST_CASE("funbuff: min and max send the smallest and largest y (#497)") {
    // Max: "sends the minimum / maximum y value currently stored in the funbuff
    // out the left outlet" — the y column, not the x, and no argument.
    Rig rig;
    rig.List("set 10 7 20 -3 30 41");

    rig.reset();
    rig.List("min");
    CHECK(rig.y.gotInt);
    CHECK(rig.y.intValue == -3);

    rig.reset();
    rig.List("max");
    CHECK(rig.y.gotInt);
    CHECK(rig.y.intValue == 41);
  }

  TEST_CASE("funbuff: min and max on an empty store send nothing (#497)") {
    // An empty store has no minimum, and a 0 would read downstream as a stored
    // value.
    Rig rig;
    rig.List("min");
    CHECK_FALSE(rig.y.gotInt);
    rig.List("max");
    CHECK_FALSE(rig.y.gotInt);
  }

  TEST_CASE("funbuff: find sends every x holding the given y (#497)") {
    // Max: "will output (out the left outlet) all x values (indexes) whose y
    // value is equal to the number indicated" — the one message that sends x
    // values out outlet 0.
    std::vector<std::string> log;
    Recorder ySink;
    ySink.log = &log;
    ySink.tag = "y";

    gFunbuff obj;
    obj.ConnectOutlet(ySink.GetInlet(0), 0);
    obj.GetInlet(0)->SetList("set 10 5 20 9 30 5 40 5", YSE::T_GUI);
    log.clear();

    obj.GetInlet(0)->SetList("find 5", YSE::T_GUI);
    REQUIRE(log.size() == 3);
    CHECK(log[0] == "y:i 10");
    CHECK(log[1] == "y:i 30");
    CHECK(log[2] == "y:i 40");

    // A y nothing holds sends nothing at all.
    log.clear();
    obj.GetInlet(0)->SetList("find 123", YSE::T_GUI);
    CHECK(log.empty());
  }

  // ─── interp ─────────────────────────────────────────────────────────────────

  TEST_CASE("funbuff: interp ramps between the bracketing pair (#497)") {
    // Max: "measures its position between its two neighboring x values ... and
    // sends the y value that holds a corresponding position between the two
    // neighboring y values". This is the message that distinguishes .funbuff
    // from a plain table, and it is what the plain lookup deliberately is not.
    Rig rig;
    rig.List("set 0 0 100 1000");

    rig.reset();
    rig.List("interp 50");
    CHECK(rig.y.gotFloat);
    CHECK(rig.y.floatValue == doctest::Approx(500.f));

    rig.reset();
    rig.List("interp 25");
    CHECK(rig.y.floatValue == doctest::Approx(250.f));

    // A fractional query is used at full precision rather than truncated: an
    // `interp 5.5` is a different question from an `interp 5`.
    rig.reset();
    rig.List("interp 0.5");
    CHECK(rig.y.floatValue == doctest::Approx(5.f));
  }

  TEST_CASE("funbuff: interp keeps the fraction the lookup throws away (#497)") {
    // The two messages answering differently for the same x is the whole point,
    // and a float is the type that keeps it. An int result would quantise every
    // curve this object exists to draw.
    Rig rig;
    rig.List("set 0 0 4 1");

    rig.reset();
    rig.X(3);
    CHECK(rig.y.gotInt);
    CHECK(rig.y.intValue == 0); // held

    rig.reset();
    rig.List("interp 3");
    CHECK(rig.y.gotFloat);
    CHECK(rig.y.floatValue == doctest::Approx(0.75f)); // ramped, and fractional
  }

  TEST_CASE("funbuff: interp brackets a negative fractional x correctly (#497)") {
    // Regression: casting the query to an int before searching truncates toward
    // zero, and for a negative x that rounds *up*, so the search lands past the
    // neighbour it should have stopped at. With pairs at -10, -2 and 0, an
    // `interp -2.5` then brackets [-2, 0] and extrapolates backwards off the
    // left end of that segment instead of interpolating along [-10, -2].
    //
    // The two segments deliberately have different slopes — 5 per unit on the
    // left, 30 on the right. With equal slopes both readings agree by accident
    // and the test proves nothing.
    Rig rig;
    rig.List("set -10 0 -2 40 0 100");

    rig.reset();
    rig.List("interp -2.5");
    CHECK(rig.y.gotFloat);
    // Along [-10, -2]: (-2.5 - -10) / 8 = 0.9375 of the way from 0 to 40.
    CHECK(rig.y.floatValue == doctest::Approx(37.5f));
    // The wrong bracket produces 25 — a backwards extrapolation off [-2, 0],
    // below the left neighbour's own y.
    CHECK(rig.y.floatValue > 25.f);

    // A negative x that needs no truncation at all still lands in [-2, 0].
    rig.reset();
    rig.List("interp -1");
    CHECK(rig.y.floatValue == doctest::Approx(70.f));
  }

  TEST_CASE("funbuff: interp clamps outside the stored range (#497)") {
    // Max describes only the bracketed case; clamping is what every breakpoint
    // curve does at its ends and the only reading that keeps interp total.
    Rig rig;
    rig.List("set 10 100 20 200");

    rig.reset();
    rig.List("interp 5");
    CHECK(rig.y.gotFloat);
    CHECK(rig.y.floatValue == doctest::Approx(100.f));

    rig.reset();
    rig.List("interp 50");
    CHECK(rig.y.floatValue == doctest::Approx(200.f));
  }

  TEST_CASE("funbuff: interp on one pair or none (#497)") {
    Rig rig;
    rig.List("interp 5");
    CHECK_FALSE(rig.y.gotFloat);
    CHECK_FALSE(rig.y.gotInt);

    rig.List("set 10 42");
    rig.reset();
    rig.List("interp 5");
    CHECK(rig.y.gotFloat);
    CHECK(rig.y.floatValue == doctest::Approx(42.f));
  }

  // ─── refusals ───────────────────────────────────────────────────────────────

  TEST_CASE("funbuff: a store past the capacity is refused, not grown (#497)") {
    // Growing the table would allocate on whichever thread the message arrived
    // on, which may be the audio thread — so the bound is the object's contract
    // rather than an implementation detail.
    Rig rig;
    for (std::size_t i = 0; i < gFunbuff::MAX_PAIRS; i++) {
      rig.Store((int)i, (int)i * 2);
    }
    CHECK(rig.obj.Count() == gFunbuff::MAX_PAIRS);

    rig.Store((int)gFunbuff::MAX_PAIRS, 999);
    CHECK(rig.obj.Count() == gFunbuff::MAX_PAIRS);
    bool found = false;
    rig.obj.Lookup((int)gFunbuff::MAX_PAIRS, found);
    CHECK_FALSE(found);

    // A replacement still works when the table is full: it writes into a pair
    // that already exists rather than needing a new one.
    rig.Store(0, 77);
    CHECK(rig.obj.Count() == gFunbuff::MAX_PAIRS);
    const int y = rig.obj.Lookup(0, found);
    CHECK(found);
    CHECK(y == 77);
  }

  TEST_CASE("funbuff: print and bang are consumed rather than read as data (#497)") {
    // Max's Console diagnostics. Neither sends anything out any outlet in Max
    // either, so consuming them reproduces Max's observable behaviour — and a
    // funbuff in Max cannot read `print` as data, because Max dispatches on the
    // selector.
    Rig rig;
    rig.List("set 10 1");
    rig.reset();

    rig.List("print");
    rig.List("bang");
    CHECK(rig.obj.Count() == 1);
    CHECK_FALSE(rig.y.gotInt);
    CHECK_FALSE(rig.x.gotInt);
    CHECK_FALSE(rig.end.gotBang);
  }

  // ─── persistence ────────────────────────────────────────────────────────────

  TEST_CASE("funbuff: without embed nothing is written to the patch (#497)") {
    // Max's funbuff persistence is opt-in, which is neither .coll's always nor
    // .bag's never — and the state hook's contract is that an object with
    // nothing to save serialises byte for byte the way it always did.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* fb = p.CreateObject(YSE::OBJ::G_FUNBUFF);
    REQUIRE(fb != nullptr);
    fb->SetListData(0, "set 10 1 20 2");

    CHECK(p.DumpJSON().find("\"state\"") == std::string::npos);
  }

  TEST_CASE("funbuff: with embed on the pairs survive a JSON round trip (#497)") {
    // Max: "the word embed, followed by a non-zero number, causes the funbuff
    // data to be stored inside the patcher".
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher src;
    src.create(2);
    YSE::pHandle* fb = src.CreateObject(YSE::OBJ::G_FUNBUFF);
    REQUIRE(fb != nullptr);
    fb->SetListData(0, "embed 1");
    fb->SetListData(0, "set 10 100 20 200 30 300");

    const std::string json = src.DumpJSON();
    CHECK(json.find(".funbuff") != std::string::npos);
    CHECK(json.find("\"state\"") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    // Read back through the outlet rather than through an accessor: what has to
    // survive is the function a patch can use, not a member variable.
    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".funbuff");
    loaded.Connect(copy, 0, &sinkHandle, 0);

    copy->SetIntData(0, 20);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 200);

    // The sort survived, so the floor lookup still answers.
    sink.reset();
    copy->SetIntData(0, 25);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 200);

    // And the embed flag came back with them, so the reloaded object still knows
    // to save itself the next time the patch is written.
    CHECK(loaded.DumpJSON().find("\"state\"") != std::string::npos);
  }

  TEST_CASE("funbuff: the filename argument survives a round trip unchanged (#497)") {
    // Accepted and ignored — there is no file I/O here — but held so a patch
    // brought across from Max still builds and the argument the author typed is
    // not quietly rewritten.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* fb = src.CreateObject(YSE::OBJ::G_FUNBUFF, "mydata");
    REQUIRE(fb != nullptr);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(src.DumpJSON());
    REQUIRE(loaded.Objects() == 1);
    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(copy->GetParams() == "mydata");
  }

  // ─── end to end, through a real patcher graph ───────────────────────────────

  TEST_CASE("funbuff: a looked-up y arrives downstream as a number (#497)") {
    // The property the object exists for, and one no unit test on the object
    // alone can show: a tuning table is only usable if what comes out of it can
    // be added to. A one-element list here would silently do nothing at the .+.
    std::vector<std::string> log;
    Recorder raw;
    raw.log = &log;
    raw.tag = "raw";
    MultiSink sink;
    YSE::pHandle rawHandle(&raw);
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* fb = p.CreateObject(YSE::OBJ::G_FUNBUFF);
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::G_ADD, "12");
    REQUIRE(fb != nullptr);
    REQUIRE(add != nullptr);
    p.Connect(fb, 0, &rawHandle, 0);
    p.Connect(fb, 0, add, 0);
    p.Connect(add, 0, &sinkHandle, 0);

    fb->SetListData(0, "set 1 60");
    fb->SetIntData(0, 1);

    REQUIRE(log.size() == 1);
    CHECK(log[0] == "raw:i 60");
    // `.+` answers in floats whatever it was given, so what this pins is that
    // the y reached its numeric inlet at all: a list would have been ignored
    // there and the sink never touched.
    CHECK(sink.gotFloat);
    CHECK(sink.floatValue == doctest::Approx(72.f));
  }

  TEST_CASE("funbuff: a dump drives a downstream chain pair by pair (#497)") {
    // The breakpoint-table use case run through the real thing: a curve dumped
    // into a transposer, with the x column landing on its own branch.
    std::vector<std::string> log;
    Recorder notes;
    Recorder times;
    notes.log = &log;
    notes.tag = "note";
    times.log = &log;
    times.tag = "at";
    YSE::pHandle noteHandle(&notes);
    YSE::pHandle timeHandle(&times);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* fb = p.CreateObject(YSE::OBJ::G_FUNBUFF);
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::G_ADD, "12");
    REQUIRE(fb != nullptr);
    REQUIRE(add != nullptr);
    p.Connect(fb, 0, add, 0);
    p.Connect(add, 0, &noteHandle, 0);
    p.Connect(fb, 1, &timeHandle, 0);

    fb->SetListData(0, "set 0 60 1 62 2 64");
    log.clear();

    fb->SetListData(0, "dump");
    REQUIRE(log.size() == 6);
    CHECK(log[0] == "at:i 0");
    CHECK(log[1] == "note:f 72.000000");
    CHECK(log[2] == "at:i 1");
    CHECK(log[3] == "note:f 74.000000");
    CHECK(log[4] == "at:i 2");
    CHECK(log[5] == "note:f 76.000000");
  }

  TEST_CASE("funbuff: a .trigger steps the traversal from inside the patch (#497)") {
    // How a patch actually drives one: an upstream object fires `next`, which is
    // a command the object has to read out of another outlet's list exactly as
    // it reads one handed in by the host. A command that only worked from the
    // host would make the object undrivable from inside a patch.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* step = p.CreateObject(YSE::OBJ::G_TRIGGER, "next");
    YSE::pHandle* fb = p.CreateObject(YSE::OBJ::G_FUNBUFF);
    REQUIRE(step != nullptr);
    REQUIRE(fb != nullptr);
    p.Connect(step, 0, fb, 0);
    p.Connect(fb, 0, &sinkHandle, 0);

    fb->SetListData(0, "set 5 111 9 222");

    sink.reset();
    step->SetBang(0);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 111);

    sink.reset();
    step->SetBang(0);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 222);
  }

  TEST_CASE("funbuff: a real graph writes pairs through the two inlets (#497)") {
    // Writing is the half a standalone rig cannot prove: the cold inlet has to
    // be reachable through a real connection, and the hot inlet has to still see
    // the y that arrived on it. This is the note-table-building idiom.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* xs = p.CreateObject(YSE::OBJ::G_INT);
    YSE::pHandle* ys = p.CreateObject(YSE::OBJ::G_INT);
    YSE::pHandle* fb = p.CreateObject(YSE::OBJ::G_FUNBUFF);
    REQUIRE(xs != nullptr);
    REQUIRE(ys != nullptr);
    REQUIRE(fb != nullptr);
    p.Connect(ys, 0, fb, 1);
    p.Connect(xs, 0, fb, 0);
    p.Connect(fb, 0, &sinkHandle, 0);

    // y first, then x — the order a patch writes a pair in.
    ys->SetIntData(0, 440);
    xs->SetIntData(0, 69);
    ys->SetIntData(0, 880);
    xs->SetIntData(0, 81);

    sink.reset();
    xs->SetIntData(0, 69);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 440);

    // And the floor lookup works through the graph too: a note between the two
    // entries holds the lower one.
    sink.reset();
    xs->SetIntData(0, 75);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 440);
  }
}
