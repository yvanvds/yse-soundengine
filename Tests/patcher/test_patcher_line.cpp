// Tests for `.line` — Max's line, "generate timed ramp" (issue #510).
//
// The object is a cursor walking a list of breakpoint segments, and everything
// worth asserting falls into one of four claims:
//
//   - **the grammar**: what each inlet accepts, which list shapes mean what,
//     that the ramp time is consumed by the target that uses it while the grain
//     persists, and that Max's four words (stop / pause / resume / set) do what
//     his page says. None of this needs a clock, so it runs on a standalone
//     object — which has no scheduler and therefore arrives at every target
//     immediately, the documented answer to "later" with no clock to measure it
//     against.
//
//   - **the ramp itself**: that values come out at the grain, that they climb,
//     that the last one is the target *exactly*, that the arrival bang comes
//     with it, and that the whole thing takes the time it was given rather than
//     one grain per delivery however late the deliveries are. These need the
//     real block clock, so they run inside a real `patcherImplementation`
//     driven by `Calculate`.
//
//   - **the output typing**: Max's rule that the creation argument's *spelling*
//     makes an int object or a float one, plus his `floatoutput` auto default
//     that lets an int object emit floats for a segment too small for an int to
//     show. An implementation that always emitted floats passes every timing
//     case in this file and fails these.
//
//   - **the refusals**: a list longer than the object holds, a scheduler with
//     no room for the next step, and the allocation probe over every message
//     path.
//
// Deadlines are asserted through `messageScheduler::BlocksForMillis` /
// `MillisForBlocks` at the live SAMPLERATE rather than through hard-coded block
// counts, so the suite holds at any negotiated rate. Nothing here ever sleeps:
// the block counter is the only clock the object measures on, and it moves
// when — and only when — the patcher renders.
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
#include "patcher/time/gLine.h"
#include "patcher/time/gPipe.h"
#include "patcher/time/messageScheduler.h"
#include "support/alloc_probe.hpp"

using YSE::PATCHER::gLine;
using YSE::PATCHER::messageScheduler;
using YSE::PATCHER::patcherImplementation;

namespace {

  // Records every message this object receives, in order and *with its type*.
  // The type is half of what this object promises — Max's line is an int object
  // or a float one, and a ramp that always emitted floats would not reach the
  // `.i` a patch wired it to — so a sink that only recorded numbers would be
  // blind to the more interesting way of getting this wrong.
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
      return "line_recorder";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    std::size_t n() const {
      return events.size();
    }
    // The numeric value of an event, whichever spelling it arrived with.
    double number(std::size_t i) const {
      return events[i].kind == 'f' ? (double)events[i].floatValue : (double)events[i].intValue;
    }
    double last() const {
      return number(events.size() - 1);
    }
  };

  // Wire two standalone objects the way `patcherImplementation::ConnectUnlocked`
  // wires two objects in a real patch: **both ends, inlet first**.
  //
  // Registering only the outlet side is enough to make sends work, which is why
  // it is an easy thing to write and a hard thing to notice. It is also a bug,
  // and a documented one — `pObject::ConnectInlet` and `ConnectUnlocked` both
  // spell it out for issue #237. `~outlet` walks its `connections` and calls
  // `inlet::Disconnect` on every peer, and `~inlet` does the mirror image, so a
  // *symmetric* edge is unwired by whichever end dies first and destruction
  // order stops mattering. A one-sided one leaves the outlet holding an
  // `inlet*` the inlet never knew about, and destroying the receiver first
  // makes `~outlet` read freed memory. (Issue #727 tracks the older test files
  // that still get this wrong; this is not another one.)
  void Wire(YSE::PATCHER::pObject& from, int outlet, YSE::PATCHER::pObject& to, int inlet = 0) {
    REQUIRE(to.ConnectInlet(from.GetOutlet(outlet), inlet));
    from.ConnectOutlet(to.GetInlet(inlet), outlet);
  }

  // A standalone `.line` with a recorder on each outlet. Standalone means no
  // patcher, so no scheduler and no clock: every target is arrived at
  // immediately, which is exactly what makes this rig the right place for
  // everything that is not timing.
  //
  // Both recorders are declared **before** the object so they are destroyed
  // after it — see Wire on why that matters even for a symmetric edge.
  struct Rig {
    Recorder out;
    Recorder done;
    gLine obj;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) obj.SetParams(args);
      Wire(obj, 0, out);
      Wire(obj, 1, done);
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
  // can arm a step — and still reachable as the concrete type, which is what
  // the counter assertions need. An object bolted on with SetParent is not in
  // the patcher's GraphState, so a deferred message never reaches it; this rig
  // is therefore only for the paths that refuse *at arm time*.
  //
  // Declaration order is load-bearing twice over: the patcher outlives the
  // object that points at it, and the recorders outlive the outlets that feed
  // them.
  struct ClockedRig {
    patcherImplementation patcher{1, nullptr};
    Recorder out;
    Recorder done;
    gLine obj;

    explicit ClockedRig(const std::string& args = "") {
      if (!args.empty()) obj.SetParams(args);
      obj.SetParent(&patcher);
      Wire(obj, 0, out);
      Wire(obj, 1, done);
    }
    ClockedRig(const ClockedRig&) = delete;
    ClockedRig& operator=(const ClockedRig&) = delete;
    ClockedRig(ClockedRig&&) = delete;
    ClockedRig& operator=(ClockedRig&&) = delete;

    void List(const std::string& message, int inlet = 0) {
      obj.GetInlet(inlet)->SetList(message, YSE::T_GUI);
    }
  };

  // A `.line` created *by* a patcher, with a recorder on each outlet — the only
  // shape in which the object's timing can be observed, since a deferred step
  // is only delivered to an object present in the block's GraphState.
  struct PatchedLine {
    patcherImplementation p{1, nullptr};
    Recorder out;
    Recorder done;
    YSE::pHandle outHandle{&out};
    YSE::pHandle doneHandle{&done};
    YSE::pHandle* line = nullptr;

    explicit PatchedLine(const std::string& args) {
      line = p.CreateObject(YSE::OBJ::G_LINE, args);
      REQUIRE(line != nullptr);
      p.Connect(line, 0, &outHandle, 0);
      p.Connect(line, 1, &doneHandle, 0);
    }
    PatchedLine(const PatchedLine&) = delete;
    PatchedLine& operator=(const PatchedLine&) = delete;
    PatchedLine(PatchedLine&&) = delete;
    PatchedLine& operator=(PatchedLine&&) = delete;
  };

  // Advance the block clock, which is the only clock this object measures on:
  // time passes when the patcher renders and at no other moment.
  void Step(patcherImplementation& p, std::uint64_t blocks) {
    for (std::uint64_t i = 0; i < blocks; i++)
      p.Calculate(YSE::T_DSP);
  }

  // Render until the ramp has arrived (the done outlet banged) or `budget`
  // blocks have gone by, and report how many blocks that took.
  std::uint64_t StepUntilDone(patcherImplementation& p, const Recorder& done,
                              std::uint64_t budget) {
    for (std::uint64_t i = 0; i < budget; i++) {
      if (done.n() > 0) return i;
      p.Calculate(YSE::T_DSP);
    }
    return budget;
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── the object exists ──────────────────────────────────────────────────────

  TEST_CASE("line: the object is creatable through the registry (#510)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* line = p.CreateObject(YSE::OBJ::G_LINE);
    REQUIRE(line != nullptr);
    CHECK(std::string(line->Type()) == ".line");
  }

  TEST_CASE("line: it is listed by pRegistry::AllNames (#510)") {
    auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".line")) != names.end());
    // And it is a different object from the audio-rate ~line it is named after,
    // which writes a DSP buffer and cannot drive a control value.
    CHECK(std::find(names.begin(), names.end(), std::string("~line")) != names.end());
  }

  TEST_CASE("line: the shape is Max's — three inlets, a value outlet and a bang (#510)") {
    gLine line;
    CHECK(line.NumInputs() == 3); // target, time, grain
    CHECK(line.NumOutputs() == 2); // the ramp, and the arrival bang
    CHECK(line.GetOutputType(0) == YSE::OUT_TYPE::ANY);
    CHECK(line.GetOutputType(1) == YSE::OUT_TYPE::BANG);
    CHECK(line.GetCategory() == YSE::PATCHER::pCategory::TIME);
  }

  TEST_CASE("line: a fresh object has Max's defaults and is not ramping (#510)") {
    gLine line;
    // "If there is no argument, the initial value is 0 and the output type is
    // int", and "if the grain is not specified, line outputs a number every 20
    // milliseconds".
    CHECK(line.Value() == doctest::Approx(0.0));
    CHECK_FALSE(line.FloatObject());
    CHECK(line.Grain() == gLine::DEFAULT_GRAIN);
    CHECK(line.Grain() == 20);
    CHECK(line.RampTime() == 0);
    CHECK_FALSE(line.IsRunning());
    CHECK(line.Pending() == 0u);
    CHECK(line.Dropped() == 0u);
  }

  // ─── the grammar, which needs no clock ──────────────────────────────────────

  TEST_CASE("line: the creation arguments are Max's initial value and grain (#510)") {
    Rig rig("5 50");
    CHECK(rig.obj.Value() == doctest::Approx(5.0));
    CHECK(rig.obj.Grain() == 50);
    CHECK_FALSE(rig.obj.FloatObject());

    // "The minimum grain allowed is 1 millisecond; any number less than 1 will
    // be set to 20."
    Rig zero("0 0");
    CHECK(zero.obj.Grain() == 20);
    Rig negative("0 -5");
    CHECK(negative.obj.Grain() == 20);

    // An argument that is not a number leaves both defaults where they were.
    Rig nonsense("wibble");
    CHECK(nonsense.obj.Value() == doctest::Approx(0.0));
    CHECK_FALSE(nonsense.obj.FloatObject());
  }

  TEST_CASE("line: the spelling of the first argument decides the output type (#510)") {
    // Max: "an argument may be used to set the initial value to be stored in
    // line and the output type for the object (floating-point or integer)." The
    // *spelling* is the whole of it — `.line 0` and `.line 0.` store the same
    // number and are different objects.
    Rig asInt("0");
    Rig asFloat("0.");
    Rig alsoFloat("1.5");
    CHECK_FALSE(asInt.obj.FloatObject());
    CHECK(asFloat.obj.FloatObject());
    CHECK(alsoFloat.obj.FloatObject());
    CHECK(alsoFloat.obj.Value() == doctest::Approx(1.5));
  }

  TEST_CASE("line: a target with no time is sent out immediately, and bangs (#510)") {
    // Max: "if no time has been specified since the last target value, the time
    // is considered 0 and line immediately outputs the target value." Having
    // arrived, it bangs.
    Rig rig;
    rig.Int(5);
    REQUIRE(rig.out.n() == 1);
    CHECK(rig.out.events[0].kind == 'i');
    CHECK(rig.out.events[0].intValue == 5);
    CHECK(rig.done.n() == 1);
    CHECK(rig.done.events[0].kind == 'b');
    CHECK(rig.obj.Value() == doctest::Approx(5.0));
    CHECK_FALSE(rig.obj.IsRunning());

    rig.Float(2.5f);
    REQUIRE(rig.out.n() == 2);
    CHECK(rig.obj.Value() == doctest::Approx(2.5));
    CHECK(rig.done.n() == 2);
  }

  TEST_CASE("line: the ramp time is consumed by the target that uses it (#510)") {
    // The rule that surprises people, and Max's: the middle inlet is a one-shot.
    // A patch that wants every target ramped sends the pair as a list.
    Rig rig;
    rig.Int(500, 1);
    CHECK(rig.obj.RampTime() == 500);
    rig.Int(10);
    CHECK(rig.obj.RampTime() == 0);

    // A negative or NaN time counts as 0.
    rig.Int(-5, 1);
    CHECK(rig.obj.RampTime() == 0);
    rig.Float(-2.5f, 1);
    CHECK(rig.obj.RampTime() == 0);
    rig.Float(250.5f, 1);
    CHECK(rig.obj.RampTime() == 250);
    rig.List("125", 1);
    CHECK(rig.obj.RampTime() == 125);
  }

  TEST_CASE("line: the grain persists where the time does not (#510)") {
    // Max: "once grains are set in a list, they will override the default until
    // manually reset."
    Rig rig("0 20");
    rig.Int(100, 2);
    CHECK(rig.obj.Grain() == 100);
    rig.Int(1); // a target does not consume it
    CHECK(rig.obj.Grain() == 100);
    rig.Float(35.5f, 2);
    CHECK(rig.obj.Grain() == 35);
    rig.List("60", 2);
    CHECK(rig.obj.Grain() == 60);
    // Below Max's 1 ms minimum is his default of 20, wherever it is set.
    rig.Int(0, 2);
    CHECK(rig.obj.Grain() == 20);
  }

  TEST_CASE("line: a three-element list sets the grain, four or more are pairs (#510)") {
    // Max: "the third number, which is optional, sets the grain", and "if the
    // list has an even number of elements greater than three, each pair of
    // elements is considered a destination-ramptime pair".
    Rig rig("0 20");
    rig.List("1 1000 100");
    CHECK(rig.obj.Grain() == 100);
    // A standalone object has no clock, so the segment arrived at once — but the
    // grain it was given stayed.
    REQUIRE(rig.out.n() == 1);
    CHECK(rig.obj.Value() == doctest::Approx(1.0));

    // Four numbers are two segments, not a segment and a grain.
    Rig pairs("0 20");
    pairs.List("1 1000 0 1000");
    CHECK(pairs.obj.Grain() == 20);
    // Both segments arrived at once, there being no clock; the second is last.
    REQUIRE(pairs.out.n() == 2);
    CHECK(pairs.out.number(0) == doctest::Approx(1.0));
    CHECK(pairs.out.number(1) == doctest::Approx(0.0));
    // One arrival, at the end of the whole list rather than per segment.
    CHECK(pairs.done.n() == 1);
  }

  TEST_CASE("line: a standalone object arrives at its target immediately (#510)") {
    // No patcher means no scheduler and so no clock at all: "over 500 ms" has no
    // referent, and the only alternatives are *now* or *never*. `.pipe`'s and
    // `.thresh`'s answer to the same dead end, and the one that keeps a
    // standalone object testable rather than a black hole.
    Rig rig;
    rig.List("100 5000");
    REQUIRE(rig.out.n() == 1);
    CHECK(rig.out.number(0) == doctest::Approx(100.0));
    CHECK(rig.done.n() == 1);
    CHECK_FALSE(rig.obj.IsRunning());
  }

  TEST_CASE("line: Max's 'set' moves the stored value without emitting (#510)") {
    // Max: "the word set, followed by a number, makes that number the new
    // starting value from which to proceed to the next received target value.
    // The set message also stops line if it is in the process of sending out
    // numbers."
    Rig rig;
    rig.List("set 42");
    CHECK(rig.obj.Value() == doctest::Approx(42.0));
    CHECK(rig.out.n() == 0);
    CHECK(rig.done.n() == 0);
    CHECK_FALSE(rig.obj.IsRunning());

    // And the next target starts from there rather than from 0.
    rig.Int(43);
    REQUIRE(rig.out.n() == 1);
    CHECK(rig.out.number(0) == doctest::Approx(43.0));
  }

  TEST_CASE("line: 'stop' emits nothing at all (#510)") {
    Rig rig;
    rig.List("stop");
    CHECK(rig.out.n() == 0);
    CHECK(rig.done.n() == 0);
    // And it is a command word rather than data, so it is not read as a number
    // list either.
    CHECK(rig.obj.Value() == doctest::Approx(0.0));
  }

  TEST_CASE("line: a tempo-relative time is refused, not read as milliseconds (#510)") {
    // The trap `.clocker` fell into before #725: `1440 ticks` taken for its
    // leading token becomes 1440 ms, which is not a time anybody asked for. This
    // object has no clock to measure a beat against, so the honest answer is to
    // leave the time where it was.
    Rig rig("0 60");
    rig.Int(250, 1);
    for (int inlet = 1; inlet <= 2; inlet++) {
      CAPTURE(inlet);
      rig.List("1440 ticks", inlet);
      rig.List("4nd", inlet);
      rig.List("8nt", inlet);
      rig.List("1.1.0", inlet);
      rig.List("wibble", inlet);
    }
    CHECK(rig.obj.RampTime() == 250);
    CHECK(rig.obj.Grain() == 60);
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("line: there is no bang method, Max's line having none (#510)") {
    Rig rig;
    rig.Bang(0);
    rig.Bang(1);
    rig.Bang(2);
    CHECK(rig.out.n() == 0);
    CHECK(rig.done.n() == 0);
  }

  TEST_CASE("line: Calculate sends nothing (#510)") {
    // The object is driven by its inlets and by the scheduler; one that emitted
    // would send a value on every DSP tick from a stimulus no patch sent.
    Rig rig("0 20");
    rig.Int(1000, 1);
    for (int i = 0; i < 16; i++)
      rig.obj.Calculate(YSE::T_DSP);
    CHECK(rig.out.n() == 0);
    CHECK(rig.done.n() == 0);
  }

  // ─── what a save carries ────────────────────────────────────────────────────

  TEST_CASE("line: the creation arguments survive a DumpJSON / ParseJSON round trip (#510)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_LINE, "5 50") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".line") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".line");
    CHECK(copy->GetParams() == "5 50");
  }

  TEST_CASE("line: a float creation argument survives the round trip as a float (#510)") {
    // The spelling is the object's type, so a save that normalised "0." to "0"
    // would load back a different object. The parameter string is stored
    // verbatim, which is what makes that impossible.
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_LINE, "0. 20") != nullptr);
    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(src.DumpJSON());
    REQUIRE(loaded.Objects() == 1);
    CHECK(loaded.GetHandleFromList(0)->GetParams() == "0. 20");
  }

  // ─── the ramp, on the real block clock ──────────────────────────────────────

  TEST_CASE("line: a target with a time ramps to it and bangs on arrival (#510)") {
    PatchedLine rig("0 10");

    rig.line->SetIntData(1, 100); // the ramp time
    rig.line->SetIntData(0, 100); // the target
    // Nothing yet: the first value is one grain away, and a deferred message is
    // never delivered inside the dispatch that armed it.
    CHECK(rig.out.n() == 0);
    CHECK(rig.p.Scheduler()->PendingCount() == 1);

    const std::uint64_t took =
        StepUntilDone(rig.p, rig.done, messageScheduler::BlocksForMillis(100) * 4);
    REQUIRE(rig.done.n() == 1);
    CHECK(rig.done.events[0].kind == 'b');

    // Values came out along the way, they climbed, and the last one is the
    // target exactly rather than an interpolation that nearly got there.
    REQUIRE(rig.out.n() >= 2);
    for (std::size_t i = 1; i < rig.out.n(); i++) {
      CAPTURE(i);
      CHECK(rig.out.number(i) >= rig.out.number(i - 1));
    }
    CHECK(rig.out.number(0) < 100.0);
    CHECK(rig.out.last() == doctest::Approx(100.0));

    // The ramp is over: no step is left armed, and rendering on emits nothing.
    CHECK(rig.p.Scheduler()->PendingCount() == 0);
    const std::size_t settled = rig.out.n();
    Step(rig.p, 32);
    CHECK(rig.out.n() == settled);
    CHECK(rig.done.n() == 1);

    // And it took the time it was given, to within the block the arrival was
    // noticed on — not one grain per delivery, which is the failure mode a
    // step-counting implementation has.
    CHECK(took >= messageScheduler::BlocksForMillis(100) - 1);
    CHECK(took <= messageScheduler::BlocksForMillis(100) + 2);
  }

  TEST_CASE("line: values come out at the grain, not once per block (#510)") {
    // A coarse grain against a long ramp: an implementation that emitted on
    // every wakeup it could get would produce one value per audio block, which
    // is far more than the ramp time divided by the grain.
    PatchedLine rig("0 50");

    rig.line->SetListData(0, "1000 500");
    StepUntilDone(rig.p, rig.done, messageScheduler::BlocksForMillis(500) * 4);
    REQUIRE(rig.done.n() == 1);

    // 500 ms at a 50 ms grain is ten steps, give or take the block the last one
    // lands on.
    CHECK(rig.out.n() >= 8u);
    CHECK(rig.out.n() <= 12u);
    // And decisively fewer than one per block, which is what the object would
    // emit if it ignored the grain.
    CHECK(rig.out.n() < (std::size_t)messageScheduler::BlocksForMillis(500));
    CHECK(rig.out.last() == doctest::Approx(1000.0));
  }

  TEST_CASE("line: a paused patcher holds a ramp where it stands (#510)") {
    // The clock is the patcher's block counter, so time only advances while the
    // patcher renders. That is the only meaning "over 100 ms" can have on a
    // clock that is not running, and it is what stops a paused engine from
    // finding every ramp finished the moment it resumes.
    PatchedLine rig("0 10");
    rig.line->SetListData(0, "100 100");

    // No Calculate at all: no time passes, and the ramp stays where it is
    // however long the test stands here.
    CHECK(rig.out.n() == 0);
    CHECK(rig.p.Scheduler()->PendingCount() == 1);

    StepUntilDone(rig.p, rig.done, messageScheduler::BlocksForMillis(100) * 4);
    CHECK(rig.done.n() == 1);
    CHECK(rig.out.last() == doctest::Approx(100.0));
  }

  TEST_CASE("line: a breakpoint list travels its segments in order and bangs once (#510)") {
    // Max's example: "line would go from the starting value of 0 to 1 in one
    // second, then back down to 0 in one second. Once the first ramp has reached
    // its target value, the next one starts."
    PatchedLine rig("0 10");
    rig.line->SetListData(0, "100 60 0 60");

    StepUntilDone(rig.p, rig.done, messageScheduler::BlocksForMillis(120) * 4);
    REQUIRE(rig.done.n() == 1); // one arrival, at the end of the whole list
    REQUIRE(rig.out.n() >= 4);

    // Up, then down, and back to where it started.
    double peak = 0.0;
    std::size_t peakAt = 0;
    for (std::size_t i = 0; i < rig.out.n(); i++) {
      if (rig.out.number(i) > peak) {
        peak = rig.out.number(i);
        peakAt = i;
      }
    }
    CHECK(peak == doctest::Approx(100.0));
    CHECK(peakAt > 0u);
    CHECK(peakAt < rig.out.n() - 1);
    CHECK(rig.out.last() == doctest::Approx(0.0));
  }

  TEST_CASE("line: a new target mid-ramp starts from where the ramp stands (#510)") {
    // Max: "if a new target value and time are specified before the line is
    // completed, the new line starts from the most recent output value, in order
    // to avoid discontinuities" — and "a subsequent list, float, or int in the
    // left inlet clears all ramps yet to be generated."
    PatchedLine rig("0 10");
    rig.line->SetListData(0, "100 200 0 200"); // two segments queued

    Step(rig.p, messageScheduler::BlocksForMillis(100));
    REQUIRE(rig.out.n() >= 1);
    const double interrupted = rig.out.last();
    CHECK(interrupted > 0.0);
    CHECK(interrupted < 100.0);

    // Redirect. The queued second segment is forgotten, and the new ramp starts
    // from the value the object stands at.
    rig.line->SetListData(0, "0 60");
    const std::size_t before = rig.out.n();
    StepUntilDone(rig.p, rig.done, messageScheduler::BlocksForMillis(60) * 4);

    REQUIRE(rig.out.n() > before);
    // No jump: the first value of the new ramp is no higher than where the old
    // one had got to.
    CHECK(rig.out.number(before) <= interrupted);
    CHECK(rig.out.last() == doctest::Approx(0.0));
    // One bang, for the one ramp that arrived — the interrupted segment did not.
    CHECK(rig.done.n() == 1);
  }

  TEST_CASE("line: 'stop' freezes a running ramp and does not bang (#510)") {
    // Max: "stops line from sending out numbers, until a new target value is
    // received." The pending step must go with it — a ramp that kept stepping
    // after a stop would be worse than one that never started.
    PatchedLine rig("0 10");
    rig.line->SetListData(0, "100 200");

    Step(rig.p, messageScheduler::BlocksForMillis(60));
    REQUIRE(rig.out.n() >= 1);
    const double frozen = rig.out.last();

    rig.line->SetListData(0, "stop");
    CHECK(rig.p.Scheduler()->PendingCount() == 0);
    const std::size_t settled = rig.out.n();

    Step(rig.p, messageScheduler::BlocksForMillis(400));
    CHECK(rig.out.n() == settled);
    CHECK(rig.done.n() == 0); // it never arrived
    CHECK(rig.out.last() == doctest::Approx(frozen));

    // A new target starts from the frozen value.
    rig.line->SetIntData(0, 200);
    REQUIRE(rig.out.n() == settled + 1);
    CHECK(rig.out.last() == doctest::Approx(200.0));
    CHECK(rig.done.n() == 1);
  }

  TEST_CASE("line: 'pause' holds the ramp and 'resume' travels what was left (#510)") {
    // Max: "pauses the internal ramp but does not change the target value nor
    // clear pending target-time pairs", and resume "resumes the internal ramp
    // and subsequent pending target-time pairs".
    PatchedLine rig("0 10");
    rig.line->SetListData(0, "100 200");

    Step(rig.p, messageScheduler::BlocksForMillis(60));
    REQUIRE(rig.out.n() >= 1);
    const double held = rig.out.last();

    rig.line->SetListData(0, "pause");
    CHECK(rig.p.Scheduler()->PendingCount() == 0);
    const std::size_t settled = rig.out.n();

    // A paused ramp stands still however long the patcher renders — and does
    // not keep re-sending the value it stopped at.
    Step(rig.p, messageScheduler::BlocksForMillis(400));
    CHECK(rig.out.n() == settled);
    CHECK(rig.done.n() == 0);

    rig.line->SetListData(0, "resume");
    StepUntilDone(rig.p, rig.done, messageScheduler::BlocksForMillis(400));
    CHECK(rig.done.n() == 1);
    CHECK(rig.out.n() > settled);
    CHECK(rig.out.number(settled) >= held);
    CHECK(rig.out.last() == doctest::Approx(100.0));
  }

  // ─── the output typing ──────────────────────────────────────────────────────

  TEST_CASE("line: an int object emits ints and a float object emits floats (#510)") {
    PatchedLine asInt("0 10");
    PatchedLine asFloat("0. 10");

    asInt.line->SetListData(0, "100 60");
    asFloat.line->SetListData(0, "100 60");
    StepUntilDone(asInt.p, asInt.done, messageScheduler::BlocksForMillis(60) * 4);
    StepUntilDone(asFloat.p, asFloat.done, messageScheduler::BlocksForMillis(60) * 4);

    REQUIRE(asInt.out.n() >= 2);
    REQUIRE(asFloat.out.n() >= 2);
    for (std::size_t i = 0; i < asInt.out.n(); i++) {
      CAPTURE(i);
      CHECK(asInt.out.events[i].kind == 'i');
    }
    for (std::size_t i = 0; i < asFloat.out.n(); i++) {
      CAPTURE(i);
      CHECK(asFloat.out.events[i].kind == 'f');
    }
  }

  TEST_CASE("line: an int object emits floats for a ramp too small to show (#510)") {
    // Max's floatoutput default of 2, auto: "outputs float values if distance is
    // <= 1, line does not have a float argument, and step size is < 0.4". Without
    // it a `.line` ramping 0 to 1 emits a run of zeroes and then a one, which is
    // not a ramp.
    PatchedLine rig("0 20");
    rig.line->SetListData(0, "1 400");
    StepUntilDone(rig.p, rig.done, messageScheduler::BlocksForMillis(400) * 4);

    REQUIRE(rig.out.n() >= 4);
    bool sawIntermediate = false;
    for (std::size_t i = 0; i < rig.out.n(); i++) {
      CAPTURE(i);
      CHECK(rig.out.events[i].kind == 'f');
      const double v = rig.out.number(i);
      if (v > 0.0 && v < 1.0) sawIntermediate = true;
    }
    CHECK(sawIntermediate);
    CHECK(rig.out.last() == doctest::Approx(1.0));

    // The same object ramping further than one unit is an int object again: the
    // rule is per segment, not per object.
    PatchedLine wide("0 20");
    wide.line->SetListData(0, "50 400");
    StepUntilDone(wide.p, wide.done, messageScheduler::BlocksForMillis(400) * 4);
    REQUIRE(wide.out.n() >= 2);
    CHECK(wide.out.events[0].kind == 'i');
  }

  // ─── the audio-thread path, end to end ──────────────────────────────────────

  TEST_CASE("line: a real chain ramps from a target arriving inside Calculate (#510)") {
    // The integration case: a `.pipe` feeding the `.line`, whose arrival bang
    // then drives a `.delay`. The pipe releases its value from the scheduler at
    // the top of `Calculate` — on the audio callback — so the ramp is *armed*
    // there, which is precisely the code that must not allocate or lock, and
    // nothing else in this file exercises it. The `.delay` on the far side makes
    // the whole "ramp, then do the next thing" idiom run end to end.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* pipe = p.CreateObject(YSE::OBJ::G_PIPE, "5");
    YSE::pHandle* line = p.CreateObject(YSE::OBJ::G_LINE, "0 10");
    YSE::pHandle* after = p.CreateObject(YSE::OBJ::G_DELAY, "5");
    REQUIRE(pipe != nullptr);
    REQUIRE(line != nullptr);
    REQUIRE(after != nullptr);

    Recorder out;
    Recorder chained;
    YSE::pHandle outHandle(&out);
    YSE::pHandle chainedHandle(&chained);
    p.Connect(pipe, 0, line, 0);
    p.Connect(line, 0, &outHandle, 0);
    p.Connect(line, 1, after, 0);
    p.Connect(after, 0, &chainedHandle, 0);

    // The ramp time first, then the target through the pipe: the target lands a
    // few blocks later, from inside the audio callback.
    line->SetIntData(1, 100);
    pipe->SetIntData(0, 64);
    CHECK(out.n() == 0);

    const std::uint64_t budget =
        messageScheduler::BlocksForMillis(5) + (messageScheduler::BlocksForMillis(100) * 4);
    for (std::uint64_t i = 0; i < budget; i++)
      p.Calculate(YSE::T_DSP);

    REQUIRE(out.n() >= 2);
    CHECK(out.last() == doctest::Approx(64.0));
    // The arrival bang went through the second pipe and came out the far side,
    // so the causal chain survived both deferrals.
    CHECK(chained.n() == 1);
    CHECK(chained.events[0].kind == 'b');
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

  // ─── the bounded list, and what a full scheduler does ───────────────────────

  TEST_CASE("line: a list longer than the object holds keeps its head and counts it (#510)") {
    // 129 numbers is Max's maxpoints default, and 64 segments is what this
    // object holds. Anything longer is truncated rather than allocated for: the
    // arriving thread is routinely the audio callback.
    Rig rig;
    std::string huge;
    huge.reserve(8192);
    for (int i = 0; i < gLine::MAX_POINTS + 40; i++) {
      if (i > 0) huge.push_back(' ');
      huge += std::to_string(i + 1);
    }
    rig.List(huge);
    CHECK(rig.obj.Dropped() >= 1u);
    // A standalone object arrives at every segment at once, so the head of the
    // list is what came out — and it stopped at the object's ceiling.
    CHECK(rig.out.n() == gLine::CAPACITY);
    CHECK(rig.done.n() == 1);
  }

  TEST_CASE("line: a step the scheduler cannot hold arrives at the target instead (#510)") {
    // Beyond the object sits the patcher's ceiling: the scheduler holds
    // messageScheduler::CAPACITY pending messages for *every* object together.
    // A ramp with no way to take its next step would be stranded forever, so it
    // arrives now and counts the shortfall — the target is reached, everything
    // downstream stays consistent, and the arrival bang still fires.
    ClockedRig rig("0 10");

    std::vector<YSE::pHandle*> pipes;
    const std::size_t needed = messageScheduler::CAPACITY / YSE::PATCHER::gPipe::CAPACITY;
    for (std::size_t i = 0; i < needed; i++) {
      YSE::pHandle* pipe = rig.patcher.CreateObject(YSE::OBJ::G_PIPE, "100000");
      REQUIRE(pipe != nullptr);
      pipes.push_back(pipe);
      for (std::size_t v = 0; v < YSE::PATCHER::gPipe::CAPACITY; v++)
        pipe->SetIntData(0, (int)v);
    }
    REQUIRE(rig.patcher.Scheduler()->PendingCount() == messageScheduler::CAPACITY);

    rig.List("100 5000");
    CHECK(rig.obj.Dropped() == 1u);
    CHECK_FALSE(rig.obj.IsRunning());
    REQUIRE(rig.out.n() == 1);
    CHECK(rig.out.number(0) == doctest::Approx(100.0));
    CHECK(rig.done.n() == 1);
  }

  // ─── real-time discipline ───────────────────────────────────────────────────

  TEST_CASE("line: the message paths allocate nothing (#510)") {
    // Everything a message can do on the way in, measured on the thread that
    // does it. The list paths are the interesting ones: a breakpoint list is
    // read into a stack array by ExprParseFloatList and copied into segments the
    // object already owns, so a 64-segment ramp costs no allocation however it
    // arrives.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    patcherImplementation p(1, nullptr);
    // Times long enough that nothing is emitted inside the probe — a send runs
    // the recorder, which allocates.
    YSE::pHandle* line = p.CreateObject(YSE::OBJ::G_LINE, "0 100000");
    REQUIRE(line != nullptr);

    Recorder out;
    Recorder done;
    YSE::pHandle outHandle(&out);
    YSE::pHandle doneHandle(&done);
    p.Connect(line, 0, &outHandle, 0);
    p.Connect(line, 1, &doneHandle, 0);

    // Built outside the probe: it is the *handler* that must not allocate, not
    // the test's own construction of the message.
    const std::string ticks = "1440 ticks";
    const std::string pause = "pause";
    const std::string resume = "resume";
    const std::string set = "set 5";
    const std::string stop = "stop";
    const std::string withGrain = "1 100000 100000";
    const std::string breakpoints = "10 100000 20 100000 30 100000";
    {
      TestHelpers::ProbeScope probe;
      line->SetIntData(1, 100000); // the ramp time
      line->SetIntData(2, 100000); // the grain
      line->SetListData(1, ticks); // refused
      line->SetIntData(0, 100); // starts a ramp: one segment, one arm
      line->SetListData(0, pause);
      line->SetListData(0, resume);
      line->SetListData(0, set);
      line->SetListData(0, withGrain);
      line->SetListData(0, breakpoints);
      line->SetListData(0, stop);
      line->SetIntData(1, 100000);
      line->SetFloatData(0, 7.f);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    // The probed messages really did something — an assertion that only proves
    // nothing happened proves nothing. The object is mid-ramp with one step
    // armed, and it has emitted nothing.
    CHECK(out.n() == 0);
    CHECK(done.n() == 0);
    CHECK(p.Scheduler()->PendingCount() == 1);
  }
}
