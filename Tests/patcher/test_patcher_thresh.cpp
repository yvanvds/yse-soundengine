// Tests for `.thresh` and `.quickthresh` — Max's two "gather what arrived close
// together into a list" objects (issue #509).
//
// The two objects are one accumulation buffer with two answers to "when is a
// group over", and the answers are structurally different: `.thresh` closes a
// group on a **gap** in the input, so every value restarts its clock and a
// stream that never pauses never emits; `.quickthresh` closes it on a **window**
// that started with the group's first value, so the list comes out at a
// predictable moment however much keeps arriving. An implementation that gave
// both objects the same policy passes every "does it group?" assertion in this
// file except the ones that feed the *same* stream to both and compare — those
// cases are the point of the file, and the rest hold down the shared half.
//
// Three rigs, because the two objects need different amounts of patcher:
//
//   - **standalone** pins the grammar and the shape: what each inlet accepts,
//     that a group of one number leaves as that number, that a beat time in a
//     threshold inlet is refused rather than read as milliseconds, and what a
//     save carries. A standalone object has no patcher and so no clock at all,
//     which is why every value comes straight back out on its own — perfect for
//     everything that is not timing.
//
//   - **clocked** is a standalone object with `SetParent` pointing at a real
//     `patcherImplementation`, which gives it the block clock (and so a group)
//     while leaving the test holding the object itself. That is the only way to
//     read `Dropped()`, `Items()` and `IsCollecting()` at all: an object created
//     through `CreateObject` is reachable only as a `pHandle`, which exposes no
//     route to the concrete type. It is used for the *refusal* paths, which
//     never need a deadline to be delivered — a deferred message only reaches an
//     object present in the patcher's GraphState snapshot, and an object bolted
//     on with SetParent is not in it.
//
//   - **patcher** is the real thing — objects created by the patcher, wired by
//     the patcher, driven by `Calculate` — and it is where every timing claim
//     lives, along with the two-objects-one-stream comparison and the
//     end-to-end case. Assertions there are behavioural (what came out of the
//     outlet, what the scheduler still holds), which is the level a patch
//     actually observes.
//
// Deadlines are asserted through `messageScheduler::BlocksForMillis` /
// `MillisForBlocks` at the live SAMPLERATE rather than through hard-coded block
// counts, so the suite holds at any negotiated rate. Nothing here ever sleeps:
// the block counter is the only clock either object measures on, and it moves
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
#include "patcher/time/gPipe.h"
#include "patcher/time/gThresh.h"
#include "patcher/time/messageScheduler.h"
#include "support/alloc_probe.hpp"

using YSE::PATCHER::gQuickthresh;
using YSE::PATCHER::gThresh;
using YSE::PATCHER::messageScheduler;
using YSE::PATCHER::patcherImplementation;

namespace {

  // Records every message this object receives, in order and *with its type*.
  // The type is half of what a grouping object promises — Max's list of one atom
  // *is* an int message, and a `.thresh` that always emitted list text would
  // stop a collected single value from reaching the `.i` a patch wired it to —
  // so a sink that only recorded text would be blind to the more interesting way
  // of getting this wrong.
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
      return "thresh_recorder";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    std::size_t n() const {
      return events.size();
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

  // A standalone object with a recorder on its outlet. Standalone means no
  // patcher, so no scheduler and no clock: nothing groups and every value comes
  // straight back out on its own, which is exactly what makes this rig the right
  // place to test everything that is not timing.
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
  // has a group to collect into — and still reachable as the concrete type,
  // which is what the counter assertions need. See the file header for the one
  // thing this rig cannot do.
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

    void Int(int value, int inlet = 0) {
      obj.GetInlet(inlet)->SetInt(value, YSE::T_GUI);
    }
    void List(const std::string& message, int inlet = 0) {
      obj.GetInlet(inlet)->SetList(message, YSE::T_GUI);
    }
  };

  // Advance the block clock, which is the only clock either object measures on:
  // time passes when the patcher renders and at no other moment.
  void Step(patcherImplementation& p, std::uint64_t blocks) {
    for (std::uint64_t i = 0; i < blocks; i++)
      p.Calculate(YSE::T_DSP);
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── the objects exist ──────────────────────────────────────────────────────

  TEST_CASE("thresh: both objects are creatable through the registry (#509)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* thresh = p.CreateObject(YSE::OBJ::G_THRESH);
    YSE::pHandle* quick = p.CreateObject(YSE::OBJ::G_QUICKTHRESH);
    REQUIRE(thresh != nullptr);
    REQUIRE(quick != nullptr);
    CHECK(std::string(thresh->Type()) == ".thresh");
    CHECK(std::string(quick->Type()) == ".quickthresh");
  }

  TEST_CASE("thresh: both are listed by pRegistry::AllNames (#509)") {
    auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".thresh")) != names.end());
    CHECK(std::find(names.begin(), names.end(), std::string(".quickthresh")) != names.end());
  }

  TEST_CASE("thresh: the shapes are Max's, and both send any kind of message (#509)") {
    gThresh thresh;
    gQuickthresh quick;
    // Max's thresh has one value inlet and one threshold inlet; quickthresh adds
    // the fudge and extension inlets.
    CHECK(thresh.NumInputs() == 2);
    CHECK(quick.NumInputs() == 4);
    YSE::PATCHER::pObject* both[] = {&thresh, &quick};
    for (YSE::PATCHER::pObject* obj : both) {
      CAPTURE(obj->Type());
      CHECK(obj->NumOutputs() == 1);
      CHECK(obj->GetOutputType(0) == YSE::OUT_TYPE::ANY);
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::TIME);
    }
  }

  TEST_CASE("thresh: a fresh object has Max's default times and collects nothing (#509)") {
    gThresh thresh;
    gQuickthresh quick;
    CHECK(thresh.Threshold() == gThresh::DEFAULT_THRESHOLD);
    CHECK(thresh.Threshold() == 10); // "if no argument is present ... 10 milliseconds"
    CHECK(quick.Threshold() == 40); // "the default value for the base threshold is 40 ms"
    CHECK(quick.Fudge() == 10); // "if not provided, the default value is 10 ms"
    CHECK(quick.Extension() == 20); // "the default value is 20 ms"
    for (YSE::PATCHER::gThreshBase* obj : {static_cast<YSE::PATCHER::gThreshBase*>(&thresh),
                                           static_cast<YSE::PATCHER::gThreshBase*>(&quick)}) {
      CHECK_FALSE(obj->IsCollecting());
      CHECK(obj->Items() == 0u);
      CHECK(obj->Dropped() == 0u);
    }
  }

  // ─── the grammar, which needs no clock ──────────────────────────────────────

  TEST_CASE("thresh: the creation arguments are Max's times in milliseconds (#509)") {
    Rig<gThresh> thresh("250");
    Rig<gQuickthresh> quick("100 25 50");
    CHECK(thresh.obj.Threshold() == 250);
    CHECK(quick.obj.Threshold() == 100);
    CHECK(quick.obj.Fudge() == 25);
    CHECK(quick.obj.Extension() == 50);
  }

  TEST_CASE("thresh: a standalone object groups nothing and passes each value on (#509)") {
    // No patcher means no scheduler and so no clock at all: "close together" has
    // no referent. `.pipe`'s and `.qlim`'s answer to the same dead end, and the
    // one that keeps a standalone object testable rather than a black hole that
    // swallows everything it is sent.
    Rig<gThresh> thresh("500");
    Rig<gQuickthresh> quick("500");
    for (int i = 1; i <= 3; i++) {
      thresh.Int(i);
      quick.Int(i);
    }
    REQUIRE(thresh.out.n() == 3);
    REQUIRE(quick.out.n() == 3);
    CHECK(thresh.out.events[2].kind == 'i');
    CHECK(thresh.out.events[2].intValue == 3);
    CHECK(thresh.obj.Dropped() == 0u);
    CHECK_FALSE(thresh.obj.IsCollecting());
  }

  TEST_CASE("thresh: a group of one numeric token leaves as the number it spells (#509)") {
    // Max's list of one atom *is* an int or a float message, so his thresh
    // sending a single collected number reaches an int inlet downstream. This
    // patcher does no coercion at an inlet, so the leading-token test `.bondo`,
    // `.trigger`, `.pipe` and `.qlim` already use decides it here too.
    Rig<gThresh> rig;
    rig.Int(5);
    rig.Float(2.5f);
    rig.List("7");
    rig.List("1 2 3"); // more than one atom: list text, and stays list text
    rig.List("some words");

    REQUIRE(rig.out.n() == 5);
    CHECK(rig.out.events[0].kind == 'i');
    CHECK(rig.out.events[0].intValue == 5);
    CHECK(rig.out.events[1].kind == 'f');
    CHECK(rig.out.events[1].floatValue == doctest::Approx(2.5f));
    CHECK(rig.out.events[2].kind == 'i');
    CHECK(rig.out.events[2].intValue == 7);
    CHECK(rig.out.events[3].kind == 'l');
    CHECK(rig.out.events[3].text == "1 2 3");
    CHECK(rig.out.events[4].kind == 'l');
    CHECK(rig.out.events[4].text == "some words");
  }

  TEST_CASE("thresh: the threshold inlet only sets the threshold (#509)") {
    Rig<gThresh> rig("100");
    rig.Int(300, 1);
    CHECK(rig.obj.Threshold() == 300);
    rig.Float(45.5f, 1);
    CHECK(rig.obj.Threshold() == 45);
    rig.List("175", 1);
    CHECK(rig.obj.Threshold() == 175);
    // And it has no bang method, as Max's thresh has none anywhere.
    rig.Bang(1);
    rig.Bang(0);
    CHECK(rig.obj.Threshold() == 175);
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("thresh: .quickthresh's three time inlets set three different times (#509)") {
    Rig<gQuickthresh> rig("100 25 50");
    rig.Int(60, 1);
    rig.Int(15, 2);
    rig.Float(30.5f, 3);
    CHECK(rig.obj.Threshold() == 60);
    CHECK(rig.obj.Fudge() == 15);
    CHECK(rig.obj.Extension() == 30);
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("thresh: a negative or NaN time counts as 0 (#509)") {
    Rig<gQuickthresh> rig("100 25 50");
    rig.Int(-50, 1);
    rig.Float(-12.5f, 2);
    rig.Int(-1, 3);
    CHECK(rig.obj.Threshold() == 0);
    CHECK(rig.obj.Fudge() == 0);
    CHECK(rig.obj.Extension() == 0);
  }

  TEST_CASE("thresh: a tempo-relative time is refused, not read as milliseconds (#509)") {
    // The trap `.clocker` fell into before #725: `1440 ticks` taken for its
    // leading token becomes 1440 ms, which is not a threshold anybody asked for.
    // Neither object has a clock to measure a beat against, so the honest answer
    // is to leave the time where it was.
    Rig<gQuickthresh> rig("100 25 50");
    for (int inlet = 1; inlet <= 3; inlet++) {
      CAPTURE(inlet);
      rig.List("1440 ticks", inlet);
      rig.List("4nd", inlet);
      rig.List("8nt", inlet);
      rig.List("1.1.0", inlet);
      rig.List("wibble", inlet);
    }
    CHECK(rig.obj.Threshold() == 100);
    CHECK(rig.obj.Fudge() == 25);
    CHECK(rig.obj.Extension() == 50);
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("thresh: .quickthresh's 'set' writes all three times and is not collected (#509)") {
    // Max: "the word set, followed by three millisecond values, can be used to
    // set the three threshold parameter values." A command word rather than
    // data, as it is in Max, so it must not end up in the list.
    Rig<gQuickthresh> rig("100 25 50");
    rig.List("set 200 30 60");
    CHECK(rig.obj.Threshold() == 200);
    CHECK(rig.obj.Fudge() == 30);
    CHECK(rig.obj.Extension() == 60);
    CHECK(rig.out.n() == 0);

    // Fewer values write only what was given.
    rig.List("set 90");
    CHECK(rig.obj.Threshold() == 90);
    CHECK(rig.obj.Fudge() == 30);
    CHECK(rig.obj.Extension() == 60);
    CHECK(rig.out.n() == 0);

    // And `.thresh`, which has no command words, collects the same text as data.
    Rig<gThresh> plain;
    plain.List("set 200 30 60");
    REQUIRE(plain.out.n() == 1);
    CHECK(plain.out.events[0].kind == 'l');
    CHECK(plain.out.events[0].text == "set 200 30 60");
  }

  TEST_CASE("thresh: Calculate sends nothing (#509)") {
    // Both objects are driven by their inlets and by the scheduler; one that
    // emitted would send a list on every DSP tick from a stimulus no patch sent.
    Rig<gThresh> thresh("100");
    Rig<gQuickthresh> quick("100");
    for (int i = 0; i < 8; i++) {
      thresh.obj.Calculate(YSE::T_DSP);
      quick.obj.Calculate(YSE::T_DSP);
    }
    CHECK(thresh.out.n() == 0);
    CHECK(quick.out.n() == 0);
  }

  // ─── what a save carries ────────────────────────────────────────────────────

  TEST_CASE("thresh: the times survive a DumpJSON / ParseJSON round trip (#509)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_THRESH, "420") != nullptr);
    REQUIRE(src.CreateObject(YSE::OBJ::G_QUICKTHRESH, "75 12 34") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".thresh") != std::string::npos);
    CHECK(json.find(".quickthresh") != std::string::npos);

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
      if (type == ".thresh") {
        CHECK(params == "420");
        seen++;
      } else {
        CHECK(type == ".quickthresh");
        CHECK(params == "75 12 34");
        seen++;
      }
    }
    CHECK(seen == 2);
  }

  // ─── .thresh: the gap ───────────────────────────────────────────────────────

  TEST_CASE("thresh: values arriving together come out as one list after the gap (#509)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* thresh = p.CreateObject(YSE::OBJ::G_THRESH, "100");
    REQUIRE(thresh != nullptr);

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(thresh, 0, &outHandle, 0);

    const std::uint64_t window = messageScheduler::BlocksForMillis(100);
    REQUIRE(window > 1);

    thresh->SetIntData(0, 60);
    thresh->SetIntData(0, 64);
    thresh->SetIntData(0, 67);
    // Nothing yet: the group is still open, and one deferred message is holding
    // its deadline.
    CHECK(out.n() == 0);
    CHECK(p.Scheduler()->PendingCount() == 1);

    Step(p, window - 1);
    CHECK(out.n() == 0);

    Step(p, 1);
    REQUIRE(out.n() == 1);
    CHECK(out.events[0].kind == 'l');
    CHECK(out.events[0].text == "60 64 67");
    CHECK(p.Scheduler()->PendingCount() == 0);

    // And the group is gone: one gap, one list.
    Step(p, window + 2);
    CHECK(out.n() == 1);
  }

  TEST_CASE("thresh: every value restarts the gap, so a busy stream stays collected (#509)") {
    // Max: "each time an item arrives, the time is reset." This is the assertion
    // an implementation that armed a fixed window from the first value would
    // fail — the list must *not* be out one threshold after the first value when
    // a second arrived in between.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* thresh = p.CreateObject(YSE::OBJ::G_THRESH, "100");
    REQUIRE(thresh != nullptr);

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(thresh, 0, &outHandle, 0);

    const std::uint64_t window = messageScheduler::BlocksForMillis(100);
    const std::uint64_t half = window / 2;
    REQUIRE(half >= 1);
    // The second value has to land inside the window for this case to say
    // anything at all.
    REQUIRE(messageScheduler::MillisForBlocks(window - half) < 100);

    thresh->SetIntData(0, 1);
    Step(p, half);
    thresh->SetIntData(0, 2);

    // One threshold after the *first* value. A fixed window would have emitted
    // here; a reset-on-arrival gap has not.
    Step(p, window - half);
    CHECK(out.n() == 0);

    // One threshold after the *second* value, with a block of slack for the
    // scheduler's ceiling.
    Step(p, window + 2);
    REQUIRE(out.n() == 1);
    CHECK(out.events[0].text == "1 2");
  }

  TEST_CASE("thresh: a collected group of one number comes out as that number (#509)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* thresh = p.CreateObject(YSE::OBJ::G_THRESH, "100");
    REQUIRE(thresh != nullptr);

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(thresh, 0, &outHandle, 0);

    thresh->SetIntData(0, 42);
    Step(p, messageScheduler::BlocksForMillis(100) + 2);
    REQUIRE(out.n() == 1);
    CHECK(out.events[0].kind == 'i');
    CHECK(out.events[0].intValue == 42);

    thresh->SetFloatData(0, 2.5f);
    Step(p, messageScheduler::BlocksForMillis(100) + 2);
    REQUIRE(out.n() == 2);
    CHECK(out.events[1].kind == 'f');
    CHECK(out.events[1].floatValue == doctest::Approx(2.5f));
  }

  TEST_CASE("thresh: a whole list joins the group whole (#509)") {
    // Max: "the entire list is appended to the list stored in thresh." A list is
    // one value here and travels whole, so all of it joins the group rather than
    // only its leading token.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* thresh = p.CreateObject(YSE::OBJ::G_THRESH, "100");
    REQUIRE(thresh != nullptr);

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(thresh, 0, &outHandle, 0);

    thresh->SetListData(0, "60 64");
    thresh->SetIntData(0, 67);
    thresh->SetListData(0, "hello there");
    Step(p, messageScheduler::BlocksForMillis(100) + 2);
    REQUIRE(out.n() == 1);
    CHECK(out.events[0].text == "60 64 67 hello there");
  }

  TEST_CASE("thresh: a paused patcher holds an open group where it stands (#509)") {
    // The clock is the patcher's block counter, so time only advances while the
    // patcher renders. That is the only meaning "100 ms from now" can have on a
    // clock that is not running, and it is what stops a paused engine from
    // flushing every open group the moment it resumes.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* thresh = p.CreateObject(YSE::OBJ::G_THRESH, "100");
    REQUIRE(thresh != nullptr);

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(thresh, 0, &outHandle, 0);

    thresh->SetIntData(0, 1);
    thresh->SetIntData(0, 2);
    REQUIRE(p.Scheduler()->PendingCount() == 1);

    // No Calculate at all: no time passes, and the group stays open however
    // long the test stands here.
    CHECK(out.n() == 0);
    CHECK(p.Scheduler()->PendingCount() == 1);

    Step(p, messageScheduler::BlocksForMillis(100) + 2);
    REQUIRE(out.n() == 1);
    CHECK(out.events[0].text == "1 2");
  }

  // ─── .quickthresh: the window ───────────────────────────────────────────────

  TEST_CASE("thresh: .quickthresh closes its window on time and starts a new one (#509)") {
    // Fudge 0 disables the extension, which leaves the base window on its own —
    // the thing this case is about.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* quick = p.CreateObject(YSE::OBJ::G_QUICKTHRESH, "60 0 0");
    REQUIRE(quick != nullptr);

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(quick, 0, &outHandle, 0);

    const std::uint64_t window = messageScheduler::BlocksForMillis(60);
    REQUIRE(window > 2);

    quick->SetIntData(0, 60);
    Step(p, 1);
    quick->SetIntData(0, 64);
    Step(p, window - 2);
    CHECK(out.n() == 0);

    Step(p, 1);
    REQUIRE(out.n() == 1);
    CHECK(out.events[0].text == "60 64");
    CHECK(p.Scheduler()->PendingCount() == 0);

    // The next value opens a fresh window rather than joining the sent group.
    quick->SetIntData(0, 67);
    Step(p, window + 2);
    REQUIRE(out.n() == 2);
    CHECK(out.events[1].kind == 'i');
    CHECK(out.events[1].intValue == 67);
  }

  TEST_CASE("thresh: a bang closes .quickthresh's group and sends it now (#509)") {
    // Max: "bang will reset quickthresh and output the notes in its buffer."
    // The pending deadline must go with it — a group sent twice would be worse
    // than one sent late.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* quick = p.CreateObject(YSE::OBJ::G_QUICKTHRESH, "100000 0 0");
    REQUIRE(quick != nullptr);

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(quick, 0, &outHandle, 0);

    quick->SetIntData(0, 60);
    quick->SetIntData(0, 64);
    CHECK(out.n() == 0);
    REQUIRE(p.Scheduler()->PendingCount() == 1);

    quick->SetBang(0);
    REQUIRE(out.n() == 1);
    CHECK(out.events[0].text == "60 64");
    // The deadline was handed back rather than left to fire into an empty
    // buffer.
    CHECK(p.Scheduler()->PendingCount() == 0);

    // And a bang with nothing collected sends nothing.
    quick->SetBang(0);
    CHECK(out.n() == 1);

    Step(p, 8);
    CHECK(out.n() == 1);
  }

  TEST_CASE("thresh: a value in the fudge zone extends .quickthresh's window once (#509)") {
    // Max: "if any notes are played within this amount of time at the end of the
    // base thresh time, the threshold is extended." And exactly once — "an
    // additional time frame added to the first argument" — which is the bound
    // that keeps a group to threshold + extension however long the stream runs.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* quick = p.CreateObject(YSE::OBJ::G_QUICKTHRESH, "60 30 60");
    REQUIRE(quick != nullptr);

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(quick, 0, &outHandle, 0);

    const std::uint64_t window = messageScheduler::BlocksForMillis(60);
    const std::uint64_t extension = messageScheduler::BlocksForMillis(60);
    // A value one block before the deadline has to read as inside the 30 ms
    // fudge zone for this case to say anything.
    REQUIRE(window > 1);
    REQUIRE(messageScheduler::MillisForBlocks(1) < 30);

    quick->SetIntData(0, 60);
    Step(p, window - 1);
    quick->SetIntData(0, 64); // inside the fudge zone at the end of the window

    // The base window is up — and the group is still here, extended.
    Step(p, 1);
    CHECK(out.n() == 0);
    CHECK(p.Scheduler()->PendingCount() == 1);

    // A value in the fudge zone of the *extended* window buys nothing more: one
    // extension per group.
    Step(p, extension - 1);
    quick->SetIntData(0, 67);
    CHECK(out.n() == 0);

    Step(p, 1);
    REQUIRE(out.n() == 1);
    CHECK(out.events[0].text == "60 64 67");
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

  // ─── the pair ───────────────────────────────────────────────────────────────

  TEST_CASE("thresh: .thresh and .quickthresh answer the same stream differently (#509)") {
    // **The pair.** Everything else in this file passes for an implementation
    // that gave both objects the same policy; this case is what says which is
    // which. A steady stream with no gap in it never closes a `.thresh` group
    // and closes a `.quickthresh` one right on schedule, which is the whole
    // reason a patch would reach for one rather than the other.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* thresh = p.CreateObject(YSE::OBJ::G_THRESH, "60");
    YSE::pHandle* quick = p.CreateObject(YSE::OBJ::G_QUICKTHRESH, "60 0 0");
    REQUIRE(thresh != nullptr);
    REQUIRE(quick != nullptr);

    Recorder gapped;
    Recorder windowed;
    YSE::pHandle gappedHandle(&gapped);
    YSE::pHandle windowedHandle(&windowed);
    p.Connect(thresh, 0, &gappedHandle, 0);
    p.Connect(quick, 0, &windowedHandle, 0);

    const std::uint64_t window = messageScheduler::BlocksForMillis(60);
    const std::uint64_t gap = window / 4;
    REQUIRE(gap >= 1);
    // Every value lands well inside the threshold, so the gap never opens.
    REQUIRE(messageScheduler::MillisForBlocks(gap) < 60);

    for (int i = 1; i <= 8; i++) {
      thresh->SetIntData(0, i);
      quick->SetIntData(0, i);
      Step(p, gap);
    }

    // Two windows' worth of a stream that never pauses.
    CHECK(8 * gap > window);
    // `.thresh` has had no gap to close on and is still collecting.
    CHECK(gapped.n() == 0);
    // `.quickthresh` closed its window on schedule, whatever kept arriving.
    REQUIRE(windowed.n() >= 1);
    CHECK(windowed.events[0].text.rfind("1 2", 0) == 0u);

    // And when the stream stops, `.thresh` finally sends the lot as one list.
    Step(p, window + 2);
    REQUIRE(gapped.n() == 1);
    CHECK(gapped.events[0].text == "1 2 3 4 5 6 7 8");
  }

  // ─── the audio-thread path, end to end ──────────────────────────────────────

  TEST_CASE("thresh: a real chain groups values arriving from inside Calculate (#509)") {
    // The integration case: a `.pipe` feeding both objects inside a real
    // patcher. The pipe releases its values from the scheduler at the top of
    // `Calculate` — on the audio callback — so every arrival here reaches the
    // collectors on the audio thread, and the group's deadline is armed there
    // too. Nothing else in this file exercises that, and the arming path is
    // precisely the code that must not allocate or lock.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* pipe = p.CreateObject(YSE::OBJ::G_PIPE, "");
    YSE::pHandle* thresh = p.CreateObject(YSE::OBJ::G_THRESH, "100");
    YSE::pHandle* quick = p.CreateObject(YSE::OBJ::G_QUICKTHRESH, "100 0 0");
    REQUIRE(pipe != nullptr);
    REQUIRE(thresh != nullptr);
    REQUIRE(quick != nullptr);

    Recorder gapped;
    Recorder windowed;
    YSE::pHandle gappedHandle(&gapped);
    YSE::pHandle windowedHandle(&windowed);
    p.Connect(pipe, 0, thresh, 0);
    p.Connect(pipe, 0, quick, 0);
    p.Connect(thresh, 0, &gappedHandle, 0);
    p.Connect(quick, 0, &windowedHandle, 0);

    // Three values staggered well inside one 100 ms window, each queued with its
    // own delay so they arrive on three different audio blocks.
    const int gaps[3] = {5, 25, 45};
    REQUIRE(messageScheduler::MillisForBlocks(messageScheduler::BlocksForMillis(gaps[2])) < 100);
    for (int i = 0; i < 3; i++) {
      pipe->SetIntData(1, gaps[i]);
      pipe->SetIntData(0, 60 + i);
    }
    CHECK(gapped.n() == 0);
    CHECK(windowed.n() == 0);

    const std::uint64_t enough =
        messageScheduler::BlocksForMillis(gaps[2]) + messageScheduler::BlocksForMillis(100) + 4;
    Step(p, enough);

    // Every value arrived inside one threshold of the last, so both objects
    // gathered all three into one chord.
    REQUIRE(gapped.n() == 1);
    CHECK(gapped.events[0].text == "60 61 62");
    REQUIRE(windowed.n() == 1);
    CHECK(windowed.events[0].text == "60 61 62");
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

  // ─── the bounded buffer, and what a full one does ───────────────────────────

  TEST_CASE("thresh: a value that will not fit the group is refused and counted (#509)") {
    // The buffer is TEXT_CAPACITY characters, reserved with the object. Anything
    // longer cannot be collected without allocating on whichever thread the
    // value arrived on — routinely the audio callback — so it is refused rather
    // than truncated into a different list, and the group keeps what it already
    // has: an over-long group loses its tail, not its head.
    ClockedRig<gThresh> rig("100000");
    const std::string huge(gThresh::TEXT_CAPACITY + 1, 'x');

    rig.List(huge); // cannot even open a group
    CHECK_FALSE(rig.obj.IsCollecting());
    CHECK(rig.obj.Items() == 0u);
    CHECK(rig.obj.Dropped() == 1u);

    rig.Int(1); // opens one
    CHECK(rig.obj.IsCollecting());
    CHECK(rig.obj.Items() == 1u);

    rig.List(huge); // will not fit into it either
    CHECK(rig.obj.IsCollecting());
    CHECK(rig.obj.Items() == 1u);
    CHECK(rig.obj.Dropped() == 2u);

    // Nothing was emitted along the way: a refusal is silent, not a flush.
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("thresh: a group that cannot arm its deadline is refused, not left open (#509)") {
    // Beyond the object's own buffer sits the patcher's: the scheduler holds
    // messageScheduler::CAPACITY pending messages for *every* object together,
    // so a group competes with .pipe, .delay, .qlist and .seq for that budget.
    // A group with no way to ever close would be a black hole, and emitting the
    // value immediately instead would let an object wired back into its own
    // inlet recurse on the audio thread — so it is refused and counted.
    ClockedRig<gThresh> rig("1000");

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

    rig.Int(1);
    CHECK_FALSE(rig.obj.IsCollecting());
    CHECK(rig.obj.Items() == 0u);
    CHECK(rig.obj.Dropped() == 1u);
    CHECK(rig.out.n() == 0);
  }

  // ─── real-time discipline ───────────────────────────────────────────────────

  TEST_CASE("thresh: the message paths allocate nothing (#509)") {
    // Everything a message can do on the way in, measured on the thread that
    // does it. Collecting is the interesting one: the buffer was reserved by the
    // constructor, so appending up to TEXT_CAPACITY characters is a copy rather
    // than an allocation, and a number is rendered into a stack buffer by
    // ExprFormatValue rather than through std::to_string.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    patcherImplementation p(1, nullptr);
    // Long enough that nothing flushes inside the probe — an emit runs the
    // recorder, which allocates.
    YSE::pHandle* thresh = p.CreateObject(YSE::OBJ::G_THRESH, "100000");
    YSE::pHandle* quick = p.CreateObject(YSE::OBJ::G_QUICKTHRESH, "100000 0 0");
    REQUIRE(thresh != nullptr);
    REQUIRE(quick != nullptr);

    Recorder gapped;
    Recorder windowed;
    YSE::pHandle gappedHandle(&gapped);
    YSE::pHandle windowedHandle(&windowed);
    p.Connect(thresh, 0, &gappedHandle, 0);
    p.Connect(quick, 0, &windowedHandle, 0);

    // Built outside the probe: it is the *handler* that must not allocate, not
    // the test's own construction of the message.
    const std::string text = "a b c";
    const std::string number = "17";
    const std::string ticks = "1440 ticks";
    const std::string time = "100000";
    const std::string setAll = "set 100000 0 0";
    {
      TestHelpers::ProbeScope probe;
      YSE::pHandle* both[] = {thresh, quick};
      for (YSE::pHandle* obj : both) {
        obj->SetIntData(0, 1); // opens a group: assign + arm
        obj->SetFloatData(0, 2.f); // appends
        obj->SetListData(0, text);
        obj->SetListData(0, number);
        obj->SetIntData(1, 100000);
        obj->SetFloatData(1, 100000.f);
        obj->SetListData(1, ticks);
        obj->SetListData(1, time);
      }
      quick->SetIntData(2, 0);
      quick->SetIntData(3, 0);
      quick->SetListData(0, setAll);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    // The probed messages really did something — an assertion that only proves
    // nothing happened proves nothing. Both objects are holding an open group
    // and its deadline, and neither has sent anything.
    CHECK(gapped.n() == 0);
    CHECK(windowed.n() == 0);
    CHECK(p.Scheduler()->PendingCount() == 2);
  }
}
