// Tests for `.when` — the patcher's on-demand reader of a named domain clock
// (issue #514).
//
// `.transport` (#513) can already report a clock's beat and tempo on bang, so
// almost every case here is about one of the two things that make `.when` a
// separate object rather than a synonym, and they are the two things a patch
// would get wrong by reaching for a `.transport` instead:
//
//   - **ownership.** A `.transport` *creates* the clock it names when it joins
//     a patcher. A `.when` never does. Asking "what beat is main on?" must not
//     be able to conjure a stopped `main` that every `.timepoint`, `.tempo` and
//     `.metro` bound to that name would then be waiting on forever. This is the
//     #507 / #512 reader contract, and the `does not create` case is the one
//     assertion in this file that would still fail if `.when` were implemented
//     by copying `gTransport` wholesale.
//
//   - **the inlet.** A `.transport`'s int and float are start and stop, which
//     makes `.metro -> .transport` a clock that toggles itself every tick. On a
//     `.when`, as in Max, *every* input is a query — bang, int, float, or any
//     list, including one whose first word is `clock`.
//
// The rest covers what the object shares with `.transport` and therefore has to
// re-prove for itself: the right-to-left outlet pair against a real clock, the
// silence (not a zero) when there is no clock to read, the audio-callback read
// route that a direct unit call structurally cannot reach, and the JSON round
// trip.
//
// Registered in the `clock` suite, not `patcher`: the cases drive
// CLOCK::Manager().update() directly on the test thread to advance beats
// deterministically, which would race a live audio thread from another suite in
// the shared monolithic process. No audio device required.

#include <doctest/doctest.h>
#include <cstdint>
#include <string>
#include <vector>

#include "clock/clockManager.h"
#include "patcher/inlet.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/time/clockBridge.h"
#include "patcher/time/gWhen.h"
#include "patcher/time/messageScheduler.h"
#include "support/alloc_probe.hpp"

namespace {

  using YSE::PATCHER::patcherImplementation;

  // 0.25 s per tick at 120 BPM is exactly half a beat, and every number in that
  // sentence is exact in binary, so the cases below assert on boundaries rather
  // than around them. `.transport`'s constants, so the two files' numbers line
  // up when read side by side.
  constexpr float kTickSeconds = 0.25f;
  constexpr float kTempo = 120.f;

  // One block of the whole world, in the engine's own order: advance every
  // domain clock, then let the patcher render.
  void Tick(patcherImplementation& p) {
    YSE::CLOCK::Manager().update(kTickSeconds);
    p.Calculate(YSE::T_DSP);
  }

  // Records every float it is sent, in order. Both of `.when`'s outlets are
  // floats, so wiring them to one recorder is what makes the right-to-left send
  // order observable.
  struct FloatRecorder : YSE::PATCHER::pObject {
    std::vector<float> seen;

    FloatRecorder() : pObject(false) {
      // Reserved up front so the allocation-probe case measures the *object's*
      // handlers rather than this vector growing under them.
      seen.reserve(64);
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) { seen.push_back(v); });
    }
    const char* Type() const override {
      return "when_recorder";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A `.when` living in a real patcher, with one recorder taking both outlets
  // so the pair's order can be read off `seen`.
  struct WhenRig {
    patcherImplementation patcher{1, nullptr};
    FloatRecorder out;
    YSE::pHandle outHandle{&out};
    YSE::pHandle* when = nullptr;

    explicit WhenRig(const std::string& args) {
      when = patcher.CreateObject(YSE::OBJ::G_WHEN, args);
      REQUIRE(when != nullptr);
      patcher.Connect(when, 0, &outHandle, 0);
      patcher.Connect(when, 1, &outHandle, 0);
      // Makes the resolve attempt deterministic. It cannot conjure a clock that
      // was never created — which is the whole point of several cases below.
      patcher.Clocks()->WaitIdle();
    }

    void Bang() {
      when->SetBang(0);
    }
    void Send(const std::string& message) {
      when->SetListData(0, message);
    }
    void Tick() {
      ::Tick(patcher);
    }
  };

  // Drop a clock and let the manager retire it, so a case's name is free again
  // for the next run of the process.
  void DropClock(const std::string& name) {
    YSE::CLOCK::Manager().destroyClock(name);
    YSE::CLOCK::Manager().update(0.01f);
  }

  // A clock at `bpm` that this case owns and the object under test must not.
  void MakeClock(const std::string& name, float bpm) {
    REQUIRE(YSE::CLOCK::Manager().createClock(name, bpm));
  }

} // namespace

TEST_SUITE("clock") {

  // ─── the object exists ──────────────────────────────────────────────────────

  TEST_CASE("when: the object is registered and creatable (#514)") {
    bool found = false;
    for (const std::string& name : YSE::PATCHER::Register().AllNames()) {
      if (name == YSE::OBJ::G_WHEN) found = true;
    }
    CHECK(found);

    patcherImplementation patcher(1, nullptr);
    YSE::pHandle* obj = patcher.CreateObject(YSE::OBJ::G_WHEN, "");
    REQUIRE(obj != nullptr);
    // One inlet — every input is the same query — and two outlets, beat and
    // tempo, `.transport`'s own layout so the two objects agree.
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 2);
  }

  // ─── ownership: the reader contract (#507 / #512 / #513) ────────────────────

  TEST_CASE("when: joining a patcher does not create the named clock (#514)") {
    // The case that separates this object from `.transport`, and the one that
    // would fail if `.when` were `gTransport` with the writes deleted. A patch
    // that merely *asks* about `main` must not bring a stopped `main` into
    // being — every `.timepoint`, `.tempo` and `.metro` bound to that name
    // would then be waiting on a clock nobody is ever going to start.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE_FALSE(mgr.clockExists("wh.never"));

    WhenRig rig("wh.never");
    CHECK_FALSE(mgr.clockExists("wh.never"));

    // It bound the name all the same — binding is not creating — so it is
    // watching for the clock to appear rather than ignoring it.
    CHECK(rig.patcher.Clocks()->BoundCount() == 1);
    CHECK(std::string(rig.patcher.Clocks()->NameOf(1)) == "wh.never");

    // And it reports nothing at all rather than a zero, which would be
    // indistinguishable from a real clock sitting stopped at beat 0.
    rig.Bang();
    CHECK(rig.out.seen.empty());
    CHECK_FALSE(mgr.clockExists("wh.never"));
  }

  TEST_CASE("when: it starts answering when someone else creates the clock (#514)") {
    // The other half of the reader contract: a name nothing has claimed is not
    // an error, it is a clock that has not appeared yet.
    WhenRig rig("wh.late");
    rig.Bang();
    REQUIRE(rig.out.seen.empty());

    MakeClock("wh.late", kTempo);
    rig.Tick();
    rig.Bang();
    REQUIRE(rig.out.seen.size() == 2);
    CHECK(rig.out.seen[0] == doctest::Approx(kTempo));
    CHECK(rig.out.seen[1] == doctest::Approx(0.5f));

    DropClock("wh.late");
  }

  TEST_CASE("when: deleting the object leaves its clock alone (#514)") {
    // A reader that unplugged the studio clock on the way out would be worse
    // than the writer that does not (#513), since nothing about asking a
    // question suggests it.
    auto& mgr = YSE::CLOCK::Manager();
    MakeClock("wh.keep", kTempo);

    WhenRig rig("wh.keep");
    rig.Tick();
    REQUIRE(mgr.beatPosition("wh.keep") == doctest::Approx(0.5));

    rig.patcher.DeleteObject(rig.when);
    rig.when = nullptr;

    CHECK(mgr.clockExists("wh.keep"));
    CHECK(mgr.currentTempo("wh.keep") == doctest::Approx(kTempo));
    rig.Tick();
    CHECK(mgr.beatPosition("wh.keep") == doctest::Approx(1.0));

    DropClock("wh.keep");
  }

  TEST_CASE("when: with no name it reads nothing, and with no patcher it binds nothing (#514)") {
    WhenRig rig("");
    rig.Bang();
    rig.Send("anything");
    CHECK(rig.out.seen.empty());
    CHECK(rig.patcher.Clocks()->BoundCount() == 0);

    // No patcher: SetParent is where binding happens, so a standalone object
    // never takes a slot — and still creates nothing.
    auto& mgr = YSE::CLOCK::Manager();
    YSE::PATCHER::gWhen orphan;
    orphan.SetParams("wh.orphan");
    orphan.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(mgr.clockExists("wh.orphan"));
    CHECK_FALSE(orphan.Bound());
    CHECK(std::string(orphan.ClockName()) == "wh.orphan");
  }

  // ─── reporting ──────────────────────────────────────────────────────────────

  TEST_CASE("when: bang reports the tempo then the beat, right to left (#514)") {
    auto& mgr = YSE::CLOCK::Manager();
    MakeClock("wh.report", kTempo);
    WhenRig rig("wh.report");

    rig.Tick();
    rig.Tick();
    REQUIRE(mgr.beatPosition("wh.report") == doctest::Approx(1.0));

    rig.Bang();
    REQUIRE(rig.out.seen.size() == 2);
    // Both outlets feed the one recorder, so the order in `seen` is the send
    // order: Max's right-to-left, tempo before beat.
    CHECK(rig.out.seen[0] == doctest::Approx(kTempo));
    CHECK(rig.out.seen[1] == doctest::Approx(1.0f));

    // The clock keeps moving and the object keeps up: this is a live read, not
    // a value cached when the object was built.
    rig.out.seen.clear();
    rig.Tick();
    rig.Bang();
    REQUIRE(rig.out.seen.size() == 2);
    CHECK(rig.out.seen[1] == doctest::Approx(1.5f));

    // A paused clock still has a position, and reports it — with tempo 0, which
    // is how the engine spells "stopped".
    rig.out.seen.clear();
    mgr.setTempo("wh.report", 0.f, 0.f);
    rig.Tick();
    rig.Bang();
    REQUIRE(rig.out.seen.size() == 2);
    CHECK(rig.out.seen[0] == doctest::Approx(0.f));
    CHECK(rig.out.seen[1] == doctest::Approx(1.5f));

    DropClock("wh.report");
  }

  TEST_CASE("when: every input is a query — int, float and any list (#514)") {
    // Max: "int / float / list / anything — equivalent to anything", and "any
    // list or message causes the current time to be sent out". That is what
    // makes the object safe on the end of something already firing, where a
    // `.transport` would read the same numbers as start and stop.
    MakeClock("wh.any", kTempo);
    WhenRig rig("wh.any");
    rig.Tick();

    rig.when->SetIntData(0, 0); // a 0 that would *stop* a `.transport`
    REQUIRE(rig.out.seen.size() == 2);
    CHECK(rig.out.seen[1] == doctest::Approx(0.5f));

    rig.out.seen.clear();
    rig.when->SetFloatData(0, -17.5f);
    REQUIRE(rig.out.seen.size() == 2);
    CHECK(rig.out.seen[1] == doctest::Approx(0.5f));

    rig.out.seen.clear();
    rig.Send("1 2 3");
    REQUIRE(rig.out.seen.size() == 2);
    CHECK(rig.out.seen[1] == doctest::Approx(0.5f));

    // Including a message whose first word is one of the family's reserved
    // ones. `.metro`, `.timepoint` and `.tempo` take a `clock <name>`; Max's
    // `when` has no such method, so this reports rather than re-points — and
    // the clock it reports on is still the one it was created with.
    rig.out.seen.clear();
    MakeClock("wh.any.other", 240.f);
    rig.Send("clock wh.any.other");
    REQUIRE(rig.out.seen.size() == 2);
    CHECK(rig.out.seen[0] == doctest::Approx(kTempo));
    CHECK(rig.out.seen[1] == doctest::Approx(0.5f));
    CHECK(std::string(rig.patcher.Clocks()->NameOf(1)) == "wh.any");

    DropClock("wh.any.other");
    DropClock("wh.any");
  }

  TEST_CASE("when: two objects on one clock share a binding and agree (#514)") {
    // Bindings are idempotent by name, so a patch full of readers costs one
    // bridge slot — and both objects are reading the engine's clock rather than
    // a copy of it.
    MakeClock("wh.share", kTempo);
    WhenRig rig("wh.share");

    FloatRecorder second;
    YSE::pHandle secondHandle{&second};
    YSE::pHandle* other = rig.patcher.CreateObject(YSE::OBJ::G_WHEN, "wh.share");
    REQUIRE(other != nullptr);
    rig.patcher.Connect(other, 0, &secondHandle, 0);
    rig.patcher.Clocks()->WaitIdle();

    CHECK(rig.patcher.Clocks()->BoundCount() == 1);

    rig.Tick();
    rig.Bang();
    other->SetBang(0);
    REQUIRE(rig.out.seen.size() == 2);
    REQUIRE(second.seen.size() == 1); // only the beat outlet is wired here
    CHECK(second.seen[0] == doctest::Approx(rig.out.seen[1]));
    CHECK(second.seen[0] == doctest::Approx(0.5f));

    DropClock("wh.share");
  }

  // ─── the audio-callback route ───────────────────────────────────────────────

  TEST_CASE("when: a query delivered from inside Calculate still answers (#514)") {
    // The property a direct unit call structurally cannot show, and the whole
    // reason `ReadClock` has two routes. A patcher message handler runs on
    // whichever thread dispatched it, and the deferred drain at the top of
    // `Calculate` dispatches *from the audio callback* — so this chain
    // (`.delay` -> `.when` -> recorder) reads the clock on the audio thread,
    // where `CLOCK::Manager()`'s mutex is out of bounds and the object has to
    // take the bridge's wait-free read instead. If that route were missing, the
    // bang would arrive and nothing would come out.
    auto& mgr = YSE::CLOCK::Manager();
    MakeClock("wh.rt", kTempo);
    WhenRig rig("wh.rt");

    YSE::pHandle* del = rig.patcher.CreateObject(YSE::OBJ::G_DELAY, "");
    REQUIRE(del != nullptr);
    rig.patcher.Connect(del, 0, rig.when, 0);

    // The wait-free read needs the binding resolved; the clock was created
    // before the bind, so this join is all it takes.
    rig.patcher.Clocks()->WaitIdle();
    REQUIRE(rig.patcher.Clocks()->Resolved(1));

    const std::uint64_t due = YSE::PATCHER::messageScheduler::BlocksForMillis(5);
    del->SetBang(0);
    for (std::uint64_t block = 0; block <= due; ++block)
      rig.Tick();

    REQUIRE(rig.out.seen.size() == 2);
    CHECK(rig.out.seen[0] == doctest::Approx(kTempo));
    // A real, advancing position — and never ahead of where the clock actually
    // stands, since it was read off that clock rather than reconstructed.
    CHECK(rig.out.seen[1] > 0.f);
    CHECK(rig.out.seen[1] <= doctest::Approx((float)mgr.beatPosition("wh.rt")));

    // Which route it took, pinned exactly. The two answers part company on a
    // clock the host has *destroyed*: the manager no longer has the name, so
    // the by-name route goes quiet, while a resolved binding keeps the clock
    // alive and frozen and goes on reading it (#707).
    // Where the clock stands as it is taken away — which is where a binding
    // that is still holding it will read forever after.
    const float frozen = (float)mgr.beatPosition("wh.rt");
    REQUIRE(frozen > 0.f);
    mgr.destroyClock("wh.rt");
    mgr.update(0.01f);
    REQUIRE_FALSE(mgr.clockExists("wh.rt"));

    // Off the callback: silent, because the manager has nothing to answer with.
    rig.out.seen.clear();
    rig.Bang();
    CHECK(rig.out.seen.empty());

    // From inside Calculate: still answers, off the frozen clock. Only the
    // bridge can do that, so this is the assertion the first half could not
    // make — a report that came back here cannot have gone through
    // CLOCK::Manager().
    del->SetBang(0);
    for (std::uint64_t block = 0; block <= due; ++block)
      rig.Tick();
    REQUIRE(rig.out.seen.size() == 2);
    CHECK(rig.out.seen[0] == doctest::Approx(kTempo));
    CHECK(rig.out.seen[1] == doctest::Approx(frozen));
  }

  TEST_CASE("when: its query allocates nothing (#514)") {
    // The handler may be the audio callback — there is no predicate an object
    // can ask up front — so nothing may be read out of the message into a
    // string. The probe sees std::string allocations since issue #697, so this
    // assertion is not vacuous over the paths that carry text.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    MakeClock("wh.noalloc", kTempo);
    WhenRig rig("wh.noalloc");
    rig.Tick();

    // Built outside the probe: it is the *handler* that must not allocate, not
    // the test's own construction of the message.
    const std::string list = "clock some other patcher message 1 2 3";
    {
      TestHelpers::ProbeScope probe;
      rig.when->SetBang(0);
      rig.when->SetIntData(0, 7);
      rig.when->SetFloatData(0, 7.5f);
      rig.when->SetListData(0, list);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    // And it really did something — an assertion that only proves nothing
    // happened proves nothing. Four queries, two floats each.
    CHECK(rig.out.seen.size() == 8);

    DropClock("wh.noalloc");
  }

  // ─── persistence ────────────────────────────────────────────────────────────

  TEST_CASE("when: parameters survive a DumpJSON / ParseJSON round trip (#514)") {
    // Checked by *driving* the restored object rather than by reading the JSON
    // back: a name that survived the file but not the rebuild would pass a
    // string comparison and fail here.
    MakeClock("wh.save", kTempo);

    YSE::patcher src;
    src.create(2);
    YSE::pHandle* obj = src.CreateObject(YSE::OBJ::G_WHEN, "wh.save");
    REQUIRE(obj != nullptr);

    const std::string json = src.DumpJSON();
    CHECK(json.find("wh.save") != std::string::npos);

    patcherImplementation restored(1, nullptr);
    restored.ParseJSON(json);
    REQUIRE(restored.Objects() == 1);
    restored.Clocks()->WaitIdle();
    CHECK(restored.Clocks()->BoundCount() == 1);
    CHECK(std::string(restored.Clocks()->NameOf(1)) == "wh.save");

    FloatRecorder out;
    YSE::pHandle outHandle{&out};
    YSE::pHandle* back = restored.GetHandleFromList(0);
    REQUIRE(back != nullptr);
    restored.Connect(back, 0, &outHandle, 0);
    restored.Connect(back, 1, &outHandle, 0);

    Tick(restored);
    back->SetBang(0);
    REQUIRE(out.seen.size() == 2);
    CHECK(out.seen[0] == doctest::Approx(kTempo));
    CHECK(out.seen[1] == doctest::Approx(0.5f));

    DropClock("wh.save");
  }

} // TEST_SUITE
