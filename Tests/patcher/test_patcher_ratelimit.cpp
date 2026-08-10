// Tests for `.speedlim` and `.qlim` — Max's two message-throughput limiters
// (issues #508 and #728).
//
// #728 added the four things #508 left out: Max's `usurp 0` queue, his
// `threshold` and `quantize` attributes, and the tempo-relative half of his
// time syntax. Everything here that needs a *running domain clock* — a beat
// interval that actually limits, and the quantize grid — lives in
// `test_patcher_clock_bridge.cpp` instead, in the isolated `clock` suite,
// because those cases drive `CLOCK::Manager().update()` on the test thread.
// What stays here is everything a block clock can decide: the grammar of the
// new words, the threshold window, the queue, and the two objects' policies.
//
// The two objects are one window test with two answers to "a message arrived
// too soon", and the answers are opposites: `.speedlim` **drops** it and
// `.qlim` **holds** it. Getting that pair backwards is the classic mistake the
// issue warns about, and it is a mistake no single-object case can catch — an
// implementation that held on both, or dropped on both, passes every "is it
// rate limited?" assertion in this file except the ones that feed the same
// burst to both objects and compare what came out. Those cases are the point of
// the file; the rest hold down the shared half.
//
// Three rigs, because the two objects need different amounts of patcher:
//
//   - **standalone** pins the grammar and the shape: what each inlet accepts,
//     that a message comes back out as the kind of message it went in as, that
//     a beat time in the interval inlet is refused rather than read as
//     milliseconds, and what a save carries. A standalone object has no patcher
//     and so no clock at all, which is why it passes everything — perfect for
//     everything that is not timing.
//
//   - **clocked** is a standalone object with `SetParent` pointing at a real
//     `patcherImplementation`, which gives it the block clock (and so a window)
//     while leaving the test holding the object itself. That is the only way to
//     read `Dropped()`, `HasOutput()` and `SinceLastOutput()` at all: an object
//     created through `CreateObject` is reachable only as a `pHandle`, which
//     exposes no route to the concrete type. The rig is used for `.speedlim`,
//     which never defers anything, and for `.qlim`'s *refusal* paths, which
//     arm nothing either. It deliberately is **not** used for a `.qlim` that
//     succeeds in holding: a deferred message is delivered only to an object
//     present in the patcher's GraphState snapshot, and an object bolted on
//     with SetParent is not in it.
//
//   - **patcher** is the real thing — objects created by the patcher, wired by
//     the patcher, driven by `Calculate` — and it is where every timing claim
//     about `.qlim` lives, along with the two-objects-one-burst comparison and
//     the end-to-end case. Assertions there are behavioural (what came out of
//     the outlet, what the scheduler still holds), which is the level a patch
//     actually observes.
//
// Deadlines are asserted through `messageScheduler::BlocksForMillis` /
// `MillisForBlocks` at the live SAMPLERATE rather than through hard-coded block
// counts, so the suite holds at any negotiated rate.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "patcher/time/gPipe.h"
#include "patcher/time/gRateLimit.h"
#include "patcher/time/messageScheduler.h"
#include "support/alloc_probe.hpp"

using YSE::PATCHER::gPipe;
using YSE::PATCHER::gQlim;
using YSE::PATCHER::gSpeedlim;
using YSE::PATCHER::messageScheduler;
using YSE::PATCHER::patcherImplementation;

namespace {

  // Records every message this object receives, in order and *with its type*.
  // The type is half of what a limiter promises — one that turned an int into a
  // list would not reach the int inlet the unlimited message would have reached
  // — so a sink that only recorded numbers would be blind to the more
  // interesting way of getting this wrong.
  struct Recorder : YSE::PATCHER::pObject {
    struct Event {
      char kind = 'i'; // 'b' bang, 'i' int, 'f' float, 'l' list
      int intValue = 0;
      float floatValue = 0.f;
      std::string text;
    };

    std::vector<Event> events;

    Recorder() : pObject(false) {
      // Reserved up front so the allocation probe measures the *object* rather
      // than this sink's own vector growing under it.
      events.reserve(1024);
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) {
        Event e;
        e.kind = 'b';
        events.push_back(e);
      });
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        Event e;
        e.kind = 'i';
        e.intValue = v;
        events.push_back(e);
      });
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) {
        Event e;
        e.kind = 'f';
        e.floatValue = v;
        events.push_back(e);
      });
      inputs.back().RegisterList([this](const std::string& v, int, YSE::THREAD) {
        Event e;
        e.kind = 'l';
        e.text = v;
        events.push_back(e);
      });
    }
    const char* Type() const override {
      return "ratelimit_recorder";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    std::size_t n() const {
      return events.size();
    }
    std::vector<int> ints() const {
      std::vector<int> out;
      for (const Event& e : events)
        if (e.kind == 'i') out.push_back(e.intValue);
      return out;
    }
  };

  using TestHelpers::Wire;

  // A standalone limiter with a recorder on its outlet. Standalone means no
  // patcher, so no scheduler and no clock: every message comes straight back
  // out, which is exactly what makes this rig the right place to test
  // everything that is not timing.
  //
  // The recorder is declared **before** the object so it is destroyed after it
  // — see Wire on why that matters even for a symmetric edge.
  template <typename Obj> struct Rig {
    Recorder out;
    Obj obj;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) obj.SetParams(args);
      Wire(obj, 0, out);
    }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    Rig(Rig&&) = delete;
    Rig& operator=(Rig&&) = delete;

    void Bang(int inlet = 0) {
      obj.GetInlet(inlet)->SetBang(YSE::T_GUI);
    }
    void Int(int value, int inlet = 0) {
      obj.GetInlet(inlet)->SetInt(value, YSE::T_GUI);
    }
    void Float(float value, int inlet = 0) {
      obj.GetInlet(inlet)->SetFloat(value, YSE::T_GUI);
    }
    void List(const std::string& message, int inlet = 0) {
      obj.GetInlet(inlet)->SetList(message, YSE::T_GUI);
    }
  };

  // The same object, given a real patcher's block clock through SetParent so it
  // has a window to measure — and still reachable as the concrete type, which
  // is what the counter assertions need. See the file header for the two things
  // this rig cannot do.
  //
  // Declaration order is load-bearing twice over: the patcher outlives the
  // object that points at it, and the recorder outlives the outlet that feeds
  // it.
  template <typename Obj> struct ClockedRig {
    patcherImplementation patcher{1, nullptr};
    Recorder out;
    Obj obj;

    explicit ClockedRig(const std::string& args = "") {
      if (!args.empty()) obj.SetParams(args);
      obj.SetParent(&patcher);
      Wire(obj, 0, out);
    }
    ClockedRig(const ClockedRig&) = delete;
    ClockedRig& operator=(const ClockedRig&) = delete;
    ClockedRig(ClockedRig&&) = delete;
    ClockedRig& operator=(ClockedRig&&) = delete;

    void Bang(int inlet = 0) {
      obj.GetInlet(inlet)->SetBang(YSE::T_GUI);
    }
    void Int(int value, int inlet = 0) {
      obj.GetInlet(inlet)->SetInt(value, YSE::T_GUI);
    }
    void Float(float value, int inlet = 0) {
      obj.GetInlet(inlet)->SetFloat(value, YSE::T_GUI);
    }
    void List(const std::string& message, int inlet = 0) {
      obj.GetInlet(inlet)->SetList(message, YSE::T_GUI);
    }
    // Advance the block clock, which is the only clock either object measures
    // on: time passes when the patcher renders and at no other moment.
    void Step(std::uint64_t blocks) {
      for (std::uint64_t i = 0; i < blocks; i++)
        patcher.Calculate(YSE::T_DSP);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── the objects exist ──────────────────────────────────────────────────────

  TEST_CASE("ratelimit: both objects are creatable through the registry (#508)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* speedlim = p.CreateObject(YSE::OBJ::G_SPEEDLIM);
    YSE::pHandle* qlim = p.CreateObject(YSE::OBJ::G_QLIM);
    REQUIRE(speedlim != nullptr);
    REQUIRE(qlim != nullptr);
    CHECK(std::string(speedlim->Type()) == ".speedlim");
    CHECK(std::string(qlim->Type()) == ".qlim");
  }

  TEST_CASE("ratelimit: both are listed by pRegistry::AllNames (#508)") {
    auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".speedlim")) != names.end());
    CHECK(std::find(names.begin(), names.end(), std::string(".qlim")) != names.end());
  }

  TEST_CASE("ratelimit: the shape is two inlets and one outlet (#508)") {
    gSpeedlim speedlim;
    gQlim qlim;
    YSE::PATCHER::pObject* both[] = {&speedlim, &qlim};
    for (YSE::PATCHER::pObject* obj : both) {
      CAPTURE(obj->Type());
      CHECK(obj->NumInputs() == 2);
      CHECK(obj->NumOutputs() == 1);
      CHECK(obj->GetOutputType(0) == YSE::OUT_TYPE::ANY);
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::TIME);
    }
  }

  TEST_CASE("ratelimit: a fresh object limits nothing and holds nothing (#508)") {
    // Max's argument prose for both objects: "if there is no argument, the
    // minimum time is 0 milliseconds", which is no limiting at all.
    gSpeedlim speedlim;
    gQlim qlim;
    CHECK(speedlim.Interval() == gSpeedlim::DEFAULT_INTERVAL);
    CHECK(speedlim.Interval() == 0);
    CHECK(qlim.Interval() == 0);
    CHECK_FALSE(speedlim.HasOutput());
    CHECK_FALSE(qlim.HasOutput());
    CHECK_FALSE(qlim.IsHolding());
    CHECK(speedlim.Dropped() == 0);
    CHECK(qlim.Dropped() == 0);
    CHECK(speedlim.SinceLastOutput() == -1);

    // And every attribute issue #728 added is off, so a fresh object is #508's
    // object exactly.
    YSE::PATCHER::gRateLimitBase* both[] = {&speedlim, &qlim};
    for (YSE::PATCHER::gRateLimitBase* obj : both) {
      CAPTURE(obj->Type());
      CHECK(obj->IntervalBeats() == 0.0);
      CHECK(obj->Threshold() == 0);
      CHECK(obj->ThresholdBeats() == 0.0);
      CHECK(obj->Quantize() == 0.0);
      CHECK_FALSE(obj->OnClock());
    }
    // Max's usurp default: "the most recently received message replaces any
    // currently queued message."
    CHECK(qlim.Usurp());
  }

  TEST_CASE("ratelimit: the new times are creation arguments too (#728)") {
    // Positional, after the interval Max gives as the object's one argument:
    // interval, intervalbeats, threshold, thresholdbeats, quantize — and usurp
    // after those on .qlim, which is the only object that has one.
    Rig<gSpeedlim> speedlim("100 0 25 0 0");
    CHECK(speedlim.obj.Interval() == 100);
    CHECK(speedlim.obj.Threshold() == 25);

    Rig<gQlim> qlim("0 1.5 0 0.25 0.5 0");
    CHECK(qlim.obj.IntervalBeats() == 1.5);
    CHECK(qlim.obj.ThresholdBeats() == 0.25);
    CHECK(qlim.obj.Quantize() == 0.5);
    CHECK_FALSE(qlim.obj.Usurp());
  }

  // ─── the grammar, which needs no clock ──────────────────────────────────────

  TEST_CASE("ratelimit: the creation argument is Max's minimum time in milliseconds (#508)") {
    Rig<gSpeedlim> speedlim("250");
    Rig<gQlim> qlim("40");
    CHECK(speedlim.obj.Interval() == 250);
    CHECK(qlim.obj.Interval() == 40);
  }

  TEST_CASE("ratelimit: a message comes back as the kind of message it went in as (#508)") {
    // Standalone there is no clock, so nothing is limited and every message is
    // visible here. What this pins is the *type*: a bang out for a bang, an int
    // for an int, a float for a float, text for text. This patcher does no
    // coercion at an inlet, so a limiter that retyped its payload would silently
    // stop reaching whatever the unlimited message reached.
    Rig<gSpeedlim> rig("100");
    rig.Bang();
    rig.Int(7);
    rig.Float(2.5f);
    rig.List("some words here");

    REQUIRE(rig.out.n() == 4);
    CHECK(rig.out.events[0].kind == 'b');
    CHECK(rig.out.events[1].kind == 'i');
    CHECK(rig.out.events[1].intValue == 7);
    CHECK(rig.out.events[2].kind == 'f');
    CHECK(rig.out.events[2].floatValue == doctest::Approx(2.5f));
    CHECK(rig.out.events[3].kind == 'l');
    CHECK(rig.out.events[3].text == "some words here");
  }

  TEST_CASE("ratelimit: a one-token numeric message passes as the number it spells (#508)") {
    // A `.m 5` reaches this inlet as a list carrying "5", and re-emitting it as
    // a list would not reach an `.i` downstream. The leading-token test
    // `.bondo`, `.trigger` and `.pipe` already use decides it.
    Rig<gQlim> rig;
    rig.List("5");
    rig.List(" 12.5 ");
    rig.List("1 2 3"); // more than one token: text, and stays text

    REQUIRE(rig.out.n() == 3);
    CHECK(rig.out.events[0].kind == 'i');
    CHECK(rig.out.events[0].intValue == 5);
    CHECK(rig.out.events[1].kind == 'f');
    CHECK(rig.out.events[1].floatValue == doctest::Approx(12.5f));
    CHECK(rig.out.events[2].kind == 'l');
    CHECK(rig.out.events[2].text == "1 2 3");
  }

  TEST_CASE("ratelimit: the right inlet only sets the interval (#508)") {
    Rig<gSpeedlim> rig;
    rig.Int(300, 1);
    CHECK(rig.obj.Interval() == 300);
    rig.Float(45.5f, 1);
    CHECK(rig.obj.Interval() == 45);
    rig.List("175", 1);
    CHECK(rig.obj.Interval() == 175);
    // And it has no bang method, as Max's does not.
    rig.Bang(1);
    CHECK(rig.obj.Interval() == 175);
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("ratelimit: a negative or NaN interval counts as 0 (#508)") {
    Rig<gQlim> rig("100");
    rig.Int(-50, 1);
    CHECK(rig.obj.Interval() == 0);
    rig.Int(100, 1);
    rig.Float(-12.5f, 1);
    CHECK(rig.obj.Interval() == 0);
  }

  TEST_CASE("ratelimit: a tempo-relative interval is read as beats, not milliseconds (#728)") {
    // Max's speedlim: "the time can be specified in milliseconds or using a
    // tempo-relative interval." What must *not* happen is the trap `.clocker`
    // fell into before #725 — `1440 ticks` taken for its leading token becomes
    // 1440 ms, which is not an interval anybody asked for. Three quarter notes
    // is what it is, and the millisecond value is left alone underneath so the
    // unit can be switched back.
    Rig<gSpeedlim> rig("100");
    rig.List("1440 ticks", 1);
    CHECK(rig.obj.IntervalBeats() == 3.0);
    CHECK(rig.obj.Interval() == 100);
    rig.List("4nd", 1);
    CHECK(rig.obj.IntervalBeats() == 1.5);
    rig.List("8nt", 1);
    CHECK(rig.obj.IntervalBeats() == doctest::Approx(1.0 / 3.0));

    // "The number is stored as the minimum amount of time, in milliseconds" is
    // a statement about the unit, so any plain number puts the object back on
    // milliseconds — Max's model, and `.delay`'s (#705).
    rig.Int(250, 1);
    CHECK(rig.obj.IntervalBeats() == 0.0);
    CHECK(rig.obj.Interval() == 250);
    rig.List("8nt", 1);
    rig.List("175", 1);
    CHECK(rig.obj.IntervalBeats() == 0.0);
    CHECK(rig.obj.Interval() == 175);

    // And what it still cannot read leaves the interval where it was.
    // bars.beats.units needs a meter no domain clock has.
    rig.List("1.1.0", 1);
    rig.List("wibble", 1);
    CHECK(rig.obj.Interval() == 175);
    CHECK(rig.obj.IntervalBeats() == 0.0);
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("ratelimit: a beat interval with no clock bound limits nothing (#728)") {
    // `.delay` answers this dead end by arming nothing, and that is right for
    // an object holding one bang: the loss is bounded. Here it would be a black
    // hole — a window that never opens swallows every message from then on —
    // so a beat time nothing can measure simply stops limiting. This is the
    // case that forbids the other answer.
    ClockedRig<gSpeedlim> speedlim("1000");
    ClockedRig<gQlim> qlim("1000");
    speedlim.List("4n", 1);
    qlim.List("4n", 1);
    for (int i = 1; i <= 4; i++) {
      speedlim.Int(i);
      qlim.Int(i);
    }
    CHECK(speedlim.out.ints() == std::vector<int>{1, 2, 3, 4});
    CHECK(qlim.out.ints() == std::vector<int>{1, 2, 3, 4});
    CHECK(speedlim.obj.Dropped() == 0);
    CHECK(qlim.obj.Dropped() == 0);
    CHECK(qlim.patcher.Scheduler()->PendingCount() == 0);
  }

  TEST_CASE("ratelimit: the attribute words are commands in the left inlet (#728)") {
    // Max carries `threshold`, `quantize` and `usurp` as attribute messages, so
    // they collide with data there in Max exactly as they do here. That is the
    // price of putting them where Max puts them, and this case is the price
    // written down: they configure the object and do not travel through it.
    Rig<gQlim> rig;
    rig.List("threshold 25");
    CHECK(rig.obj.Threshold() == 25);
    CHECK(rig.obj.ThresholdBeats() == 0.0);
    rig.List("threshold 4nd");
    CHECK(rig.obj.ThresholdBeats() == 1.5);
    rig.List("threshold 1440 ticks");
    CHECK(rig.obj.ThresholdBeats() == 3.0);
    rig.List("threshold 60");
    CHECK(rig.obj.ThresholdBeats() == 0.0);
    CHECK(rig.obj.Threshold() == 60);
    // A `threshold` followed by something that is not a time leaves it alone
    // rather than clearing it by accident.
    rig.List("threshold wibble");
    CHECK(rig.obj.Threshold() == 60);

    // The grid is a musical grid: a note value or a tick count, and 0 to clear
    // it. A millisecond number is refused rather than read as some musical time
    // it is not.
    rig.List("quantize 8n");
    CHECK(rig.obj.Quantize() == 0.5);
    rig.List("quantize 100");
    CHECK(rig.obj.Quantize() == 0.5);
    rig.List("quantize 0");
    CHECK(rig.obj.Quantize() == 0.0);

    rig.List("usurp 0");
    CHECK_FALSE(rig.obj.Usurp());
    rig.List("usurp 1");
    CHECK(rig.obj.Usurp());
    rig.List("usurp");
    CHECK(rig.obj.Usurp());

    // Not one of them reached the outlet.
    CHECK(rig.out.n() == 0);
    // And a word that is not a command is still data, which is the half that
    // stops this from being a silent black hole for text.
    rig.List("wibble 3");
    REQUIRE(rig.out.n() == 1);
    CHECK(rig.out.events[0].text == "wibble 3");
  }

  TEST_CASE("ratelimit: 'usurp' is .qlim's word and .speedlim's data (#728)") {
    // Max lists usurp on both objects, but it says what happens to a message
    // that is *waiting* and .speedlim never has one — issue #508 gave it the
    // drop policy. So the attribute lives on .qlim alone, and on .speedlim the
    // word is just text passing through.
    Rig<gSpeedlim> rig;
    rig.List("usurp 0");
    REQUIRE(rig.out.n() == 1);
    CHECK(rig.out.events[0].kind == 'l');
    CHECK(rig.out.events[0].text == "usurp 0");
  }

  TEST_CASE("ratelimit: 'clock' binds a domain clock and a bare 'clock' takes it away (#728)") {
    // Max's setclock method, the shape `.delay` already has (#705). The name is
    // bound wait-free through the patcher's bridge; whether the clock exists is
    // discovered on the background pool, so a binding to a name nothing has
    // created still reports itself here.
    ClockedRig<gQlim> rig("100");
    CHECK_FALSE(rig.obj.OnClock());
    CHECK(std::string(rig.obj.ClockName()).empty());

    rig.List("clock ratelimit.nowhere");
    CHECK(rig.obj.OnClock());
    CHECK(std::string(rig.obj.ClockName()) == "ratelimit.nowhere");

    rig.List("clock");
    CHECK_FALSE(rig.obj.OnClock());
    CHECK(std::string(rig.obj.ClockName()).empty());

    // A standalone object has no bridge to bind through, and says so silently
    // rather than logging on what may be the audio thread.
    Rig<gQlim> loose;
    loose.List("clock ratelimit.nowhere");
    CHECK_FALSE(loose.obj.OnClock());
    CHECK(loose.out.n() == 0);
  }

  TEST_CASE("ratelimit: a standalone object passes everything (#508)") {
    // No patcher means no scheduler and so no clock at all: "since the previous
    // output" has no referent. `.pipe`'s and `.mtr`'s answer to the same dead
    // end, and the one that keeps a standalone object testable rather than a
    // black hole that eats every message after the first.
    Rig<gSpeedlim> speedlim("500");
    Rig<gQlim> qlim("500");
    for (int i = 1; i <= 4; i++) {
      speedlim.Int(i);
      qlim.Int(i);
    }
    CHECK(speedlim.out.ints() == std::vector<int>{1, 2, 3, 4});
    CHECK(qlim.out.ints() == std::vector<int>{1, 2, 3, 4});
    CHECK(speedlim.obj.Dropped() == 0);
    CHECK(qlim.obj.Dropped() == 0);
    CHECK_FALSE(qlim.obj.IsHolding());
  }

  TEST_CASE("ratelimit: Calculate sends nothing (#508)") {
    // Both objects are driven by their inlets and by the scheduler; one that
    // emitted would send a message on every DSP tick from a stimulus no patch
    // sent.
    Rig<gSpeedlim> speedlim("100");
    Rig<gQlim> qlim("100");
    for (int i = 0; i < 8; i++) {
      speedlim.obj.Calculate(YSE::T_DSP);
      qlim.obj.Calculate(YSE::T_DSP);
    }
    CHECK(speedlim.out.n() == 0);
    CHECK(qlim.out.n() == 0);
  }

  // ─── what a save carries ────────────────────────────────────────────────────

  TEST_CASE("ratelimit: the interval survives a DumpJSON / ParseJSON round trip (#508)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_SPEEDLIM, "420") != nullptr);
    REQUIRE(src.CreateObject(YSE::OBJ::G_QLIM, "75 0 20 0 1 0") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".speedlim") != std::string::npos);
    CHECK(json.find(".qlim") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 2);

    int seen = 0;
    for (unsigned int i = 0; i < 2; i++) {
      YSE::pHandle* copy = loaded.GetHandleFromList(i);
      REQUIRE(copy != nullptr);
      const std::string type = copy->Type();
      const std::string params = copy->GetParams();
      CAPTURE(type);
      if (type == ".speedlim") {
        CHECK(params == "420");
        seen++;
      } else {
        CHECK(type == ".qlim");
        // Verbatim, attributes and all: the arguments a patch was saved with
        // are the arguments it loads with (issue #728 added five more of them).
        CHECK(params == "75 0 20 0 1 0");
        seen++;
      }
    }
    CHECK(seen == 2);
  }

  // ─── the window, and .speedlim's whole behaviour ────────────────────────────

  TEST_CASE("ratelimit: the first message always passes, there being no previous output (#508)") {
    ClockedRig<gSpeedlim> rig("1000");
    CHECK_FALSE(rig.obj.HasOutput());
    CHECK(rig.obj.SinceLastOutput() == -1);

    rig.Int(1);
    CHECK(rig.out.ints() == std::vector<int>{1});
    CHECK(rig.obj.HasOutput());
    CHECK(rig.obj.SinceLastOutput() == 0);
    CHECK(rig.obj.Dropped() == 0);
  }

  TEST_CASE("ratelimit: .speedlim drops everything that arrives inside the window (#508)") {
    // The drop policy. Everything after the first is discarded and counted, and
    // — the half that matters — nothing arrives late either: an implementation
    // that quietly held the surplus would pass the count assertion and fail the
    // one at the end of this case.
    ClockedRig<gSpeedlim> rig("100");
    for (int i = 1; i <= 5; i++)
      rig.Int(i);

    CHECK(rig.out.ints() == std::vector<int>{1});
    CHECK(rig.obj.Dropped() == 4);
    // Nothing was deferred: this object never touches the scheduler at all.
    CHECK(rig.patcher.Scheduler()->PendingCount() == 0);

    rig.Step(messageScheduler::BlocksForMillis(100) + 2);
    CHECK(rig.out.ints() == std::vector<int>{1});
  }

  TEST_CASE("ratelimit: the window opens at the interval and not a block earlier (#508)") {
    // The shared half of both objects, asserted through the scheduler's own
    // conversion rather than a hard-coded block count so it holds at any
    // negotiated sample rate. One block before the interval is up the window is
    // still shut; on the block it is up, it is open.
    ClockedRig<gSpeedlim> rig("100");
    const std::uint64_t window = messageScheduler::BlocksForMillis(100);
    REQUIRE(window > 1);

    rig.Int(1);
    REQUIRE(rig.out.ints() == std::vector<int>{1});

    rig.Step(window - 1);
    CHECK(rig.obj.SinceLastOutput() < 100);
    rig.Int(2);
    CHECK(rig.out.ints() == std::vector<int>{1});
    CHECK(rig.obj.Dropped() == 1);

    // The block the interval is up on. The refused message did not move the
    // window — only an output does — so the elapsed time is still measured from
    // the 1.
    rig.Step(1);
    CHECK(rig.obj.SinceLastOutput() >= 100);
    rig.Int(3);
    CHECK(rig.out.ints() == std::vector<int>{1, 3});
    CHECK(rig.obj.Dropped() == 1);
    CHECK(rig.obj.SinceLastOutput() == 0);
  }

  TEST_CASE("ratelimit: an interval of 0 limits nothing (#508)") {
    // Max's default. Deliberately *not* the reading the scheduler gives a delay
    // of 0 (which defers one block): a rate limiter set to no limit is a wire,
    // and a wire does not defer.
    ClockedRig<gSpeedlim> speedlim;
    ClockedRig<gQlim> qlim;
    for (int i = 1; i <= 5; i++) {
      speedlim.Int(i);
      qlim.Int(i);
    }
    CHECK(speedlim.out.ints() == std::vector<int>{1, 2, 3, 4, 5});
    CHECK(qlim.out.ints() == std::vector<int>{1, 2, 3, 4, 5});
    CHECK(speedlim.obj.Dropped() == 0);
    CHECK(qlim.obj.Dropped() == 0);
    CHECK_FALSE(qlim.obj.IsHolding());
    CHECK(qlim.patcher.Scheduler()->PendingCount() == 0);
  }

  // ─── .qlim's refusals, which arm nothing and so fit the clocked rig ─────────

  TEST_CASE("ratelimit: text too long to hold is refused and counted (#508)") {
    // A slot carries TEXT_CAPACITY characters, reserved with the object.
    // Anything longer cannot be held without allocating on whichever thread the
    // message arrived on — routinely the audio callback — so it is refused
    // rather than truncated into a different message. The pass path has no such
    // limit, which the first half of this case pins.
    ClockedRig<gQlim> rig("100");
    const std::string huge(gQlim::TEXT_CAPACITY + 1, 'x');

    rig.List(huge); // the first message: passes, however long it is
    REQUIRE(rig.out.n() == 1);
    CHECK(rig.out.events[0].text == huge);

    rig.List(huge); // the window is shut now, and this cannot be held
    CHECK(rig.out.n() == 1);
    CHECK(rig.obj.Dropped() == 1);
    CHECK_FALSE(rig.obj.IsHolding());
    CHECK(rig.patcher.Scheduler()->PendingCount() == 0);
  }

  TEST_CASE("ratelimit: a full patcher-wide scheduler refuses the hold and counts it (#508)") {
    // Beyond the object's own single slot sits the patcher's: the scheduler
    // holds messageScheduler::CAPACITY pending messages for *every* object
    // together, so a .qlim competes with .pipe, .delay, .qlist and .seq for that
    // budget and can be refused by it. The message is dropped and counted rather
    // than sent early — sending it early would break the object's guarantee at
    // exactly the moment the patch is at its resource limit.
    ClockedRig<gQlim> rig("1000");

    std::vector<YSE::pHandle*> pipes;
    const std::size_t needed = messageScheduler::CAPACITY / gPipe::CAPACITY;
    for (std::size_t i = 0; i < needed; i++) {
      YSE::pHandle* pipe = rig.patcher.CreateObject(YSE::OBJ::G_PIPE, "100000");
      REQUIRE(pipe != nullptr);
      pipes.push_back(pipe);
      for (std::size_t v = 0; v < gPipe::CAPACITY; v++)
        pipe->SetIntData(0, (int)v);
    }
    REQUIRE(rig.patcher.Scheduler()->PendingCount() == messageScheduler::CAPACITY);

    rig.Int(1); // the first message passes without needing the scheduler
    REQUIRE(rig.out.ints() == std::vector<int>{1});
    rig.Int(2); // this one needs a slot, and there are none
    CHECK(rig.out.ints() == std::vector<int>{1});
    CHECK(rig.obj.Dropped() == 1);
    CHECK_FALSE(rig.obj.IsHolding());
  }

  // ─── .qlim's hold, which needs a real patcher object ────────────────────────

  TEST_CASE("ratelimit: .qlim holds a message and sends it when the window opens (#508)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlim = p.CreateObject(YSE::OBJ::G_QLIM, "100");
    REQUIRE(qlim != nullptr);

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(qlim, 0, &outHandle, 0);

    const std::uint64_t window = messageScheduler::BlocksForMillis(100);
    REQUIRE(window > 1);

    qlim->SetIntData(0, 1); // the first message: straight through
    qlim->SetIntData(0, 2); // inside the window: held
    CHECK(out.ints() == std::vector<int>{1});
    CHECK(p.Scheduler()->PendingCount() == 1);

    for (std::uint64_t block = 1; block < window; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.ints() == std::vector<int>{1});

    p.Calculate(YSE::T_DSP);
    CHECK(out.ints() == std::vector<int>{1, 2});
    CHECK(p.Scheduler()->PendingCount() == 0);

    // And it stays out: one hold, one release.
    for (std::uint64_t block = 0; block < window + 2; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.ints() == std::vector<int>{1, 2});
  }

  TEST_CASE("ratelimit: a burst leaves its LAST message through .qlim (#508)") {
    // Max's usurp: "the most recently received message replaces any currently
    // queued message." Exactly one message waits however long the burst is —
    // this is not `.pipe`, which would queue all five — and it arrives one
    // interval after the *previous output*, not one interval after the last
    // thing the flood sent.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlim = p.CreateObject(YSE::OBJ::G_QLIM, "100");
    REQUIRE(qlim != nullptr);

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(qlim, 0, &outHandle, 0);

    const std::uint64_t window = messageScheduler::BlocksForMillis(100);

    for (int i = 1; i <= 5; i++)
      qlim->SetIntData(0, i);

    CHECK(out.ints() == std::vector<int>{1});
    CHECK(p.Scheduler()->PendingCount() == 1);

    for (std::uint64_t block = 1; block < window; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.ints() == std::vector<int>{1});

    p.Calculate(YSE::T_DSP);
    CHECK(out.ints() == std::vector<int>{1, 5});
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

  TEST_CASE("ratelimit: .qlim holds a message as the kind it went in as (#508)") {
    // The held path is a different code path from the pass path, and it is the
    // one that has to carry the type across a deferral. A .qlim that flattened
    // a held bang into an int, or a held float into text, would still look
    // rate-limited.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlim = p.CreateObject(YSE::OBJ::G_QLIM, "100");
    REQUIRE(qlim != nullptr);

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(qlim, 0, &outHandle, 0);

    const std::uint64_t window = messageScheduler::BlocksForMillis(100);

    qlim->SetIntData(0, 1); // passes: the first message
    qlim->SetListData(0, "held words here"); // held
    for (std::uint64_t block = 0; block < window; ++block)
      p.Calculate(YSE::T_DSP);
    REQUIRE(out.n() == 2);
    CHECK(out.events[1].kind == 'l');
    CHECK(out.events[1].text == "held words here");

    qlim->SetFloatData(0, 2.5f); // held
    for (std::uint64_t block = 0; block < window; ++block)
      p.Calculate(YSE::T_DSP);
    REQUIRE(out.n() == 3);
    CHECK(out.events[2].kind == 'f');
    CHECK(out.events[2].floatValue == doctest::Approx(2.5f));

    qlim->SetBang(0); // held
    for (std::uint64_t block = 0; block < window; ++block)
      p.Calculate(YSE::T_DSP);
    REQUIRE(out.n() == 4);
    CHECK(out.events[3].kind == 'b');
  }

  TEST_CASE("ratelimit: a paused patcher holds .qlim's message where it stands (#508)") {
    // The clock is the patcher's block counter, so time only advances while the
    // patcher renders. That is the only meaning "100 ms from now" can have on a
    // clock that is not running, and it is what stops a paused engine from
    // releasing a burst the moment it resumes.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlim = p.CreateObject(YSE::OBJ::G_QLIM, "100");
    REQUIRE(qlim != nullptr);

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(qlim, 0, &outHandle, 0);

    qlim->SetIntData(0, 1);
    qlim->SetIntData(0, 2);
    REQUIRE(out.ints() == std::vector<int>{1});
    REQUIRE(p.Scheduler()->PendingCount() == 1);

    // No Calculate at all: no time passes, and the message stays put however
    // long the test stands here.
    CHECK(p.Scheduler()->PendingCount() == 1);
    CHECK(out.ints() == std::vector<int>{1});

    for (std::uint64_t block = 0; block < messageScheduler::BlocksForMillis(100); ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.ints() == std::vector<int>{1, 2});
  }

  // ─── Max's threshold: "only one message may pass" (#728) ────────────────────

  TEST_CASE("ratelimit: a threshold caps the rate even when the interval does not (#728)") {
    // Max: "time threshold under which only one message may pass." It is a
    // second window measured from the same output, so it composes with the
    // interval rather than replacing it — and with the interval at Max's
    // default of 0, which limits nothing at all, the threshold is the only
    // thing holding the rate down. An implementation that folded threshold into
    // interval, or ignored it while the interval was 0, fails here.
    ClockedRig<gSpeedlim> rig;
    rig.List("threshold 100");
    REQUIRE(rig.obj.Interval() == 0);
    REQUIRE(rig.obj.Threshold() == 100);

    rig.Int(1);
    rig.Int(2);
    rig.Int(3);
    CHECK(rig.out.ints() == std::vector<int>{1});
    CHECK(rig.obj.Dropped() == 2);

    rig.Step(messageScheduler::BlocksForMillis(100));
    rig.Int(4);
    CHECK(rig.out.ints() == std::vector<int>{1, 4});
  }

  TEST_CASE("ratelimit: inside the threshold .qlim drops rather than holds (#728)") {
    // The half that separates a threshold from a shorter interval: a message
    // that lands inside it is not held either, on the object whose whole
    // purpose is holding. Between the threshold and the interval the object is
    // itself again.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlim = p.CreateObject(YSE::OBJ::G_QLIM, "1000");
    REQUIRE(qlim != nullptr);
    qlim->SetListData(0, "threshold 200");

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(qlim, 0, &outHandle, 0);

    qlim->SetIntData(0, 1); // the first message: straight through
    qlim->SetIntData(0, 2); // inside the threshold: dropped, and not held
    CHECK(out.ints() == std::vector<int>{1});
    CHECK(p.Scheduler()->PendingCount() == 0);

    for (std::uint64_t block = 0; block < messageScheduler::BlocksForMillis(200); ++block)
      p.Calculate(YSE::T_DSP);

    qlim->SetIntData(0, 3); // past the threshold, inside the interval: held
    CHECK(out.ints() == std::vector<int>{1});
    CHECK(p.Scheduler()->PendingCount() == 1);

    for (std::uint64_t block = 0; block <= messageScheduler::BlocksForMillis(1000); ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.ints() == std::vector<int>{1, 3});
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

  // ─── Max's usurp 0: the queue (#728) ────────────────────────────────────────

  TEST_CASE("ratelimit: 'usurp 0' sends every message, one per window (#728)") {
    // Max: "when usurp is disabled, all messages received will be sent out."
    // The object stops being a replace-the-pending-value limiter and becomes a
    // queue — a different and useful shape, and the one where the *order*
    // matters. Note the pending count: however long the queue is it costs one
    // slot of the patcher-wide scheduler, because the release re-arms itself
    // rather than arming a deadline per message.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlim = p.CreateObject(YSE::OBJ::G_QLIM, "100");
    REQUIRE(qlim != nullptr);
    qlim->SetListData(0, "usurp 0");

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(qlim, 0, &outHandle, 0);

    const std::uint64_t window = messageScheduler::BlocksForMillis(100);
    REQUIRE(window > 1);

    for (int i = 1; i <= 4; i++)
      qlim->SetIntData(0, i);
    CHECK(out.ints() == std::vector<int>{1});
    CHECK(p.Scheduler()->PendingCount() == 1);

    for (int expected = 2; expected <= 4; expected++) {
      for (std::uint64_t block = 0; block < window; ++block)
        p.Calculate(YSE::T_DSP);
      CAPTURE(expected);
      CHECK((int)out.ints().size() == expected);
      CHECK(out.ints().back() == expected);
      // Still one slot, whatever is left in the queue.
      CHECK(p.Scheduler()->PendingCount() == (expected < 4 ? 1u : 0u));
    }

    CHECK(out.ints() == std::vector<int>{1, 2, 3, 4});

    // And it stays drained: the release stops re-arming once the queue is
    // empty, rather than ticking on forever.
    for (std::uint64_t block = 0; block < window * 3; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.ints() == std::vector<int>{1, 2, 3, 4});
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

  TEST_CASE("ratelimit: usurp on and usurp off answer the same burst differently (#728)") {
    // **The usurp pair**, and the case that says which is which. Everything
    // else about the queue passes for an implementation that quietly kept
    // usurping; only feeding one burst to both settings and comparing what came
    // out can tell them apart.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* replacing = p.CreateObject(YSE::OBJ::G_QLIM, "100");
    YSE::pHandle* queueing = p.CreateObject(YSE::OBJ::G_QLIM, "100");
    REQUIRE(replacing != nullptr);
    REQUIRE(queueing != nullptr);
    queueing->SetListData(0, "usurp 0");

    Recorder newest;
    Recorder every;
    YSE::pHandle newestHandle(&newest);
    YSE::pHandle everyHandle(&every);
    p.Connect(replacing, 0, &newestHandle, 0);
    p.Connect(queueing, 0, &everyHandle, 0);

    for (int i = 1; i <= 4; i++) {
      replacing->SetIntData(0, i);
      queueing->SetIntData(0, i);
    }

    for (std::uint64_t block = 0; block < messageScheduler::BlocksForMillis(100) * 4 + 4; ++block)
      p.Calculate(YSE::T_DSP);

    // Usurp on: the first and the last, the middle two overwritten.
    CHECK(newest.ints() == std::vector<int>{1, 4});
    // Usurp off: all of them, in order.
    CHECK(every.ints() == std::vector<int>{1, 2, 3, 4});
  }

  TEST_CASE("ratelimit: a usurp 0 queue holds its messages as the kind they went in as (#728)") {
    // The queued path carries the type across a deferral just as the single
    // held slot does, and it has to keep the order while doing it.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlim = p.CreateObject(YSE::OBJ::G_QLIM, "100");
    REQUIRE(qlim != nullptr);
    qlim->SetListData(0, "usurp 0");

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(qlim, 0, &outHandle, 0);

    const std::uint64_t window = messageScheduler::BlocksForMillis(100);

    qlim->SetIntData(0, 1); // passes: the first message
    qlim->SetListData(0, "queued words here");
    qlim->SetFloatData(0, 2.5f);
    qlim->SetBang(0);

    for (std::uint64_t block = 0; block < window * 3 + 3; ++block)
      p.Calculate(YSE::T_DSP);

    REQUIRE(out.n() == 4);
    CHECK(out.events[0].kind == 'i');
    CHECK(out.events[1].kind == 'l');
    CHECK(out.events[1].text == "queued words here");
    CHECK(out.events[2].kind == 'f');
    CHECK(out.events[2].floatValue == doctest::Approx(2.5f));
    CHECK(out.events[3].kind == 'b');
  }

  TEST_CASE("ratelimit: a full usurp 0 queue drops the newest and counts it (#728)") {
    // The queue is bounded and pre-allocated, like every other store in the
    // patcher. What it drops is the message that would not fit — the history
    // already accepted is kept, because a queue that discarded its head would
    // be usurp with extra steps.
    ClockedRig<gQlim> rig("100000");
    rig.List("usurp 0");
    REQUIRE_FALSE(rig.obj.Usurp());

    rig.Int(0); // the first message passes without needing a slot
    REQUIRE(rig.out.ints() == std::vector<int>{0});

    for (std::size_t i = 0; i < gQlim::CAPACITY; i++)
      rig.Int((int)i + 1);
    CHECK(rig.obj.Waiting() == gQlim::CAPACITY);
    CHECK(rig.obj.Dropped() == 0);
    // The whole queue rides on one pending scheduler message.
    CHECK(rig.patcher.Scheduler()->PendingCount() == 1);

    rig.Int(999);
    CHECK(rig.obj.Waiting() == gQlim::CAPACITY);
    CHECK(rig.obj.Dropped() == 1);
  }

  TEST_CASE("ratelimit: usurp on never queues more than one message (#728)") {
    // The other half of the same claim: with usurp in its default state the
    // extra slots are never touched, however long the burst.
    ClockedRig<gQlim> rig("100000");
    rig.Int(0);
    for (int i = 1; i <= 20; i++)
      rig.Int(i);
    CHECK(rig.obj.Waiting() == 1);
    CHECK(rig.obj.IsHolding());
    // A replaced message is usurp working, not a refusal, so nothing is counted.
    CHECK(rig.obj.Dropped() == 0);
    CHECK(rig.patcher.Scheduler()->PendingCount() == 1);
  }

  // ─── the pair ───────────────────────────────────────────────────────────────

  TEST_CASE("ratelimit: .speedlim and .qlim answer the same burst differently (#508)") {
    // **The pair.** Everything else in this file passes for an implementation
    // that gave both objects the same policy; this case is what says which is
    // which. The issue's warning — "document clearly which one drops and which
    // one holds; getting that backwards is a classic Max bug" — is exactly this
    // assertion, written down.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* speedlim = p.CreateObject(YSE::OBJ::G_SPEEDLIM, "100");
    YSE::pHandle* qlim = p.CreateObject(YSE::OBJ::G_QLIM, "100");
    REQUIRE(speedlim != nullptr);
    REQUIRE(qlim != nullptr);

    Recorder thinned;
    Recorder held;
    YSE::pHandle thinnedHandle(&thinned);
    YSE::pHandle heldHandle(&held);
    p.Connect(speedlim, 0, &thinnedHandle, 0);
    p.Connect(qlim, 0, &heldHandle, 0);

    for (int i = 1; i <= 4; i++) {
      speedlim->SetIntData(0, i);
      qlim->SetIntData(0, i);
    }
    // .speedlim deferred nothing; .qlim is holding exactly one message.
    CHECK(p.Scheduler()->PendingCount() == 1);

    for (std::uint64_t block = 0; block <= messageScheduler::BlocksForMillis(100) + 2; ++block)
      p.Calculate(YSE::T_DSP);

    // The first message of the burst, and nothing else: .speedlim thins.
    CHECK(thinned.ints() == std::vector<int>{1});
    // The first *and the last*: .qlim loses nothing that mattered.
    CHECK(held.ints() == std::vector<int>{1, 4});
  }

  // ─── the audio-thread path, end to end ──────────────────────────────────────

  TEST_CASE("ratelimit: a real chain limits values arriving from inside Calculate (#508)") {
    // The integration case: a `.pipe` feeding both limiters inside a real
    // patcher. The pipe releases its values from the scheduler at the top of
    // `Calculate` — on the audio callback — so every arrival here reaches the
    // limiters on the audio thread, and .qlim's hold is armed there too. Nothing
    // else in this file exercises that, and the arming path is precisely the
    // code that must not allocate or lock.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* pipe = p.CreateObject(YSE::OBJ::G_PIPE, "");
    YSE::pHandle* speedlim = p.CreateObject(YSE::OBJ::G_SPEEDLIM, "100");
    YSE::pHandle* qlim = p.CreateObject(YSE::OBJ::G_QLIM, "100");
    REQUIRE(pipe != nullptr);
    REQUIRE(speedlim != nullptr);
    REQUIRE(qlim != nullptr);

    Recorder thinned;
    Recorder held;
    YSE::pHandle thinnedHandle(&thinned);
    YSE::pHandle heldHandle(&held);
    p.Connect(pipe, 0, speedlim, 0);
    p.Connect(pipe, 0, qlim, 0);
    p.Connect(speedlim, 0, &thinnedHandle, 0);
    p.Connect(qlim, 0, &heldHandle, 0);

    // Three values staggered inside one 100 ms window, each queued with its own
    // delay so they arrive on three different audio blocks.
    const int gaps[3] = {5, 25, 45};
    REQUIRE(messageScheduler::MillisForBlocks(messageScheduler::BlocksForMillis(gaps[2])) < 100);
    for (int i = 0; i < 3; i++) {
      pipe->SetIntData(1, gaps[i]);
      pipe->SetIntData(0, i + 1);
    }
    CHECK(thinned.n() == 0);
    CHECK(held.n() == 0);

    const std::uint64_t enough =
        messageScheduler::BlocksForMillis(gaps[2]) + messageScheduler::BlocksForMillis(100) + 4;
    for (std::uint64_t block = 0; block < enough; ++block)
      p.Calculate(YSE::T_DSP);

    // The first value of the burst reached both — neither had output before it.
    // After that the two part company: .speedlim dropped 2 and 3, while .qlim
    // held 2, let 3 usurp it, and released 3 when the window opened.
    CHECK(thinned.ints() == std::vector<int>{1});
    CHECK(held.ints() == std::vector<int>{1, 3});
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

  // ─── real-time discipline ───────────────────────────────────────────────────

  TEST_CASE("ratelimit: the message paths allocate nothing (#508)") {
    // Everything a message can do on the way in, measured on the thread that
    // does it. Holding text is the interesting one: the slot's string was
    // reserved by the constructor, so an assign of up to TEXT_CAPACITY
    // characters is a copy rather than an allocation.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    patcherImplementation p(1, nullptr);
    YSE::pHandle* speedlim = p.CreateObject(YSE::OBJ::G_SPEEDLIM, "100000");
    YSE::pHandle* qlim = p.CreateObject(YSE::OBJ::G_QLIM, "100000");
    REQUIRE(speedlim != nullptr);
    REQUIRE(qlim != nullptr);

    Recorder thinned;
    Recorder held;
    YSE::pHandle thinnedHandle(&thinned);
    YSE::pHandle heldHandle(&held);
    p.Connect(speedlim, 0, &thinnedHandle, 0);
    p.Connect(qlim, 0, &heldHandle, 0);

    // The first message of each object goes out, and an emit runs the recorder,
    // which allocates — so it happens outside the probe. Everything after it is
    // refused or held, which is the path under test.
    speedlim->SetIntData(0, 0);
    qlim->SetIntData(0, 0);

    // Built outside the probe: it is the *handler* that must not allocate, not
    // the test's own construction of the message.
    const std::string text = "a b c d e";
    const std::string number = "17";
    const std::string ticks = "1440 ticks";
    const std::string time = "100000";
    YSE::pHandle* both[] = {speedlim, qlim};
    {
      TestHelpers::ProbeScope probe;
      for (YSE::pHandle* obj : both) {
        obj->SetBang(0);
        obj->SetIntData(0, 1);
        obj->SetFloatData(0, 2.f);
        obj->SetListData(0, text);
        obj->SetListData(0, number);
        obj->SetIntData(1, 100000);
        obj->SetFloatData(1, 100000.f);
        obj->SetListData(1, ticks);
        obj->SetListData(1, time);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    // The probed messages really did something — an assertion that only proves
    // nothing happened proves nothing. Five messages each: .speedlim dropped
    // all five, .qlim held the first and let the other four usurp it.
    CHECK(thinned.n() == 1);
    CHECK(held.n() == 1);
    CHECK(p.Scheduler()->PendingCount() == 1);
  }
}
