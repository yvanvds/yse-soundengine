// Tests for .delay (issue #503) — the patcher's bang delay.
//
// The object is tiny, which makes it easy to get subtly wrong in ways nothing
// crashes on. Two layers, and as with .qlist / .mtr / .seq they are not
// interchangeable:
//
//   - **standalone cases** pin the grammar and the shape: what each inlet
//     accepts, that a number in the left inlet both sets the time and starts
//     the wait while one in the right inlet only sets it, that `stop` is a
//     command, that a negative time counts as 0, and what a save carries. A
//     standalone object has no patcher and so no clock at all, which is why it
//     bangs immediately — perfect for testing everything *except* the timing.
//
//   - **patcher cases** pin the clock, which cannot exist standalone: that a
//     bang really does come back a wait later rather than now, that a second
//     bang really does forget the first, that `stop` really cancels, and that a
//     retime really does leave a bang already in flight alone. Deadlines are
//     asserted through messageScheduler::BlocksForMillis at the live SAMPLERATE
//     rather than through hard-coded block counts, so the suite holds at any
//     negotiated rate.
//
// Four rules carry the file, each of them something a plausible implementation
// gets backwards without ever failing loudly:
//
//   - **`delay 0` defers; it does not fire now.** This is the decision the
//     object turns on. .qlist treats the scheduler's one-block deadline floor
//     as a feature and .seq deliberately overrides it; here the floor *is* the
//     semantics. In Max, `delay 0` is how a patch breaks out of the current
//     message chain, and a synchronous one would be .bangbang — plus, wired
//     outlet-back-to-inlet as delay chains routinely are, it would recurse
//     until the audio thread's stack ran out.
//   - **a second bang forgets the first.** Max: "only one bang at a time can be
//     delayed." Two bangs in must not produce two bangs out.
//   - **the right inlet does not retime a bang in flight.** Max says so
//     explicitly, and the naive implementation — recompute the deadline on
//     every time change — breaks it.
//   - **a number in the left inlet also *starts* the delay.** Max: "it then
//     automatically sends a bang message to itself to start the delay." Easy to
//     read as a setter and stop there.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "headers/constants.hpp"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "patcher/time/gDelay.h"
#include "patcher/time/messageScheduler.h"

using TestHelpers::Wire;
using YSE::PATCHER::gDelay;
using YSE::PATCHER::messageScheduler;
using YSE::PATCHER::patcherImplementation;

namespace {

  // Counts the bangs it receives. That is the entire observable surface of this
  // object: one outlet, one kind of message.
  struct Counter : YSE::PATCHER::pObject {
    int bangs = 0;

    Counter() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { bangs++; });
    }
    const char* Type() const override {
      return "delay_counter";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A standalone .delay with a counter on its outlet. Standalone means no
  // patcher, so no scheduler and no clock: every bang comes straight back out,
  // which is exactly what makes this rig the right place to test everything
  // that is not timing.
  struct Rig {
    gDelay obj;
    Counter out;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) obj.SetParams(args);
      Wire(obj, 0, out);
    }

    void Bang() {
      obj.GetInlet(0)->SetBang(YSE::T_GUI);
    }
    void Int(int inlet, int value) {
      obj.GetInlet(inlet)->SetInt(value, YSE::T_GUI);
    }
    void Float(int inlet, float value) {
      obj.GetInlet(inlet)->SetFloat(value, YSE::T_GUI);
    }
    void List(int inlet, const std::string& message) {
      obj.GetInlet(inlet)->SetList(message, YSE::T_GUI);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── the object exists ──────────────────────────────────────────────────────

  TEST_CASE("delay: creatable through the registry (#503)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DELAY);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".delay");
  }

  TEST_CASE("delay: listed by pRegistry::AllNames (#503)") {
    auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".delay")) != names.end());
  }

  TEST_CASE("delay: Max's shape is two inlets and one outlet (#503)") {
    gDelay obj;
    CHECK(obj.NumInputs() == 2);
    CHECK(obj.NumOutputs() == 1);
    CHECK(obj.GetCategory() == YSE::PATCHER::pCategory::TIME);
  }

  TEST_CASE("delay: a fresh object waits Max's documented 5 ms and holds nothing (#503)") {
    // Max's Arguments section: "if there is no argument, the initial time
    // interval is 5 milliseconds." Its Attributes table says delaytime defaults
    // to 0 ms; the two cannot both describe a freshly typed delay, and the
    // argument prose is the one about this object's constructor.
    gDelay obj;
    CHECK(obj.DelayTime() == gDelay::DEFAULT_DELAY);
    CHECK(obj.DelayTime() == 5);
    CHECK_FALSE(obj.IsPending());
  }

  // ─── the grammar, which needs no clock ──────────────────────────────────────

  TEST_CASE("delay: the creation argument is Max's time in milliseconds (#503)") {
    Rig rig("250");
    CHECK(rig.obj.DelayTime() == 250);
  }

  TEST_CASE("delay: a number in the left inlet sets the time *and* starts the wait (#503)") {
    // Max: the number "is stored as the number of milliseconds to delay a bang
    // received in the left inlet. It then automatically sends a bang message to
    // itself to start the delay." Reading that as a plain setter is the easy
    // mistake; standalone there is no clock, so the started wait resolves at
    // once and the bang is visible here.
    Rig rig;
    rig.Int(0, 120);
    CHECK(rig.obj.DelayTime() == 120);
    CHECK(rig.out.bangs == 1);

    rig.Float(0, 60.9f);
    CHECK(rig.obj.DelayTime() == 60);
    CHECK(rig.out.bangs == 2);
  }

  TEST_CASE("delay: a number in the right inlet only sets the time (#503)") {
    // Max: "a number received in the right inlet changes the delay time of the
    // next bang received." No bang of its own.
    Rig rig;
    rig.Int(1, 300);
    CHECK(rig.obj.DelayTime() == 300);
    CHECK(rig.out.bangs == 0);

    rig.Float(1, 45.5f);
    CHECK(rig.obj.DelayTime() == 45);
    CHECK(rig.out.bangs == 0);
  }

  TEST_CASE("delay: the right inlet has no bang method, as Max's has none (#503)") {
    Rig rig;
    rig.obj.GetInlet(1)->SetBang(YSE::T_GUI);
    CHECK(rig.out.bangs == 0);
  }

  TEST_CASE("delay: a negative or NaN time counts as 0 (#503)") {
    // 0 is the shortest wait there is, which the scheduler's one-block floor
    // then turns into "next block". A negative deadline is not a thing a clock
    // can wait for.
    Rig rig;
    rig.Int(1, -50);
    CHECK(rig.obj.DelayTime() == 0);
    rig.Float(1, -12.5f);
    CHECK(rig.obj.DelayTime() == 0);
  }

  TEST_CASE("delay: 'stop' is a command in the left inlet only (#503)") {
    // Max: "stop: In left inlet. Stops delay from outputting the bang it is
    // currently delaying." Standalone there is nothing to cancel, so what this
    // pins is that the word is not read as data — it must not set a time and
    // must not bang.
    Rig rig("100");
    rig.List(0, "stop");
    CHECK(rig.out.bangs == 0);
    CHECK(rig.obj.DelayTime() == 100);

    // In the right inlet it is simply not understood, which is also Max.
    rig.List(1, "stop");
    CHECK(rig.out.bangs == 0);
    CHECK(rig.obj.DelayTime() == 100);
  }

  TEST_CASE("delay: a list with a leading number sets the time like a bare number (#503)") {
    // The millisecond subset of Max's list / anything method, which exists to
    // carry its time-format syntax.
    Rig rig;
    rig.List(1, "175");
    CHECK(rig.obj.DelayTime() == 175);
    CHECK(rig.out.bangs == 0);

    rig.List(0, "80");
    CHECK(rig.obj.DelayTime() == 80);
    CHECK(rig.out.bangs == 1);
  }

  TEST_CASE("delay: a time format this patcher still cannot read does nothing (#503, #705)") {
    // #705 taught this object the *tempo-relative* half of Max's time syntax —
    // note values and ticks, which are arithmetic once a beat exists. The rest
    // stays refused whole rather than read as some number it is not:
    // bars.beats.units needs a meter, and a domainClock is a bare beat
    // accumulator with none, so "1.1.0" must not become 1.
    Rig rig("100");
    rig.List(0, "1.1.0");
    rig.List(0, "00:03:25");
    rig.List(0, "4nq"); // neither dotted nor triplet: not a note value at all
    rig.List(0, "wibble");
    CHECK(rig.obj.DelayTime() == 100);
    CHECK(rig.obj.DelayBeats() == 0.0);
    CHECK(rig.out.bangs == 0);
  }

  TEST_CASE("delay: a note value or tick count sets the delay in beats (#705)") {
    // The unit travels with the value, which is Max's model: a note value says
    // "beats" and a plain number says "milliseconds" — Max's "the number is
    // stored as the number of milliseconds".
    Rig rig("100");
    rig.List(1, "4nd"); // the cold inlet sets without starting
    CHECK(rig.obj.DelayBeats() == 1.5);
    CHECK(rig.obj.DelayTime() == 100); // the millisecond time is left alone
    CHECK(rig.out.bangs == 0);

    rig.List(1, "1440 ticks"); // Max's 480 ticks to a quarter note
    CHECK(rig.obj.DelayBeats() == 3.0);
    CHECK(rig.out.bangs == 0);

    rig.Int(1, 40);
    CHECK(rig.obj.DelayBeats() == 0.0);
    CHECK(rig.obj.DelayTime() == 40);
  }

  TEST_CASE("delay: a note value in the left inlet starts the wait like a number (#705)") {
    Rig rig("100");
    rig.List(0, "8nt");
    CHECK(rig.obj.DelayBeats() == doctest::Approx(1.0 / 3.0));
    // Standalone: no patcher, so no clock at all and "later" has no referent —
    // the same now-or-never answer a millisecond time gets here.
    CHECK(rig.out.bangs == 1);
  }

  TEST_CASE("delay: 'clock' is a command in the left inlet only (#705)") {
    // Max's own method — "the word clock, followed by the name of an existing
    // setclock object" — and a standalone object has no bridge to bind through,
    // so it is inert here. It is still a *command*: it must not be read as a
    // time and it must not start a wait.
    Rig rig("100");
    rig.List(0, "clock mine");
    CHECK_FALSE(rig.obj.OnClock());
    CHECK(std::string(rig.obj.ClockName()).empty());
    CHECK(rig.obj.DelayTime() == 100);
    CHECK(rig.obj.DelayBeats() == 0.0);
    CHECK(rig.out.bangs == 0);

    // The right inlet has no clock method, as it has no stop method: there the
    // word is only a time format this object cannot read.
    rig.List(1, "clock mine");
    CHECK_FALSE(rig.obj.OnClock());
    CHECK(rig.obj.DelayTime() == 100);
    CHECK(rig.out.bangs == 0);
  }

  TEST_CASE("delay: a standalone object bangs immediately (#503)") {
    // No patcher means no scheduler and so no clock at all: "later" has no
    // referent, and the only alternatives are now or never. `.bondo`'s answer
    // to the same dead end, and the one that keeps a standalone object testable
    // rather than a black hole.
    Rig rig("500");
    rig.Bang();
    CHECK(rig.out.bangs == 1);
    CHECK_FALSE(rig.obj.IsPending());
  }

  TEST_CASE("delay: Calculate sends nothing (#503)") {
    // The object is driven by its inlets and by its own clock; one that emitted
    // would fire a bang on every DSP tick from a stimulus no patch sent.
    Rig rig("100");
    for (int i = 0; i < 8; i++)
      rig.obj.Calculate(YSE::T_DSP);
    CHECK(rig.out.bangs == 0);
  }

  // ─── what a save carries ────────────────────────────────────────────────────

  TEST_CASE("delay: the time survives a DumpJSON / ParseJSON round trip (#503)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* obj = src.CreateObject(YSE::OBJ::G_DELAY, "420");
    REQUIRE(obj != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".delay") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".delay");
    CHECK(std::string(copy->GetParams()) == "420");
  }

  // ─── the clock, which needs a real patcher ──────────────────────────────────

  TEST_CASE("delay: a bang comes back one wait later, not in the arming dispatch (#503)") {
    // The whole object in one case. The wait is asserted through
    // BlocksForMillis at the live SAMPLERATE rather than a hard-coded block
    // count, so it holds at any negotiated rate.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* del = p.CreateObject(YSE::OBJ::G_DELAY, "100");
    REQUIRE(del != nullptr);

    Counter out;
    YSE::pHandle outHandle(&out);
    p.Connect(del, 0, &outHandle, 0);

    const std::uint64_t due = messageScheduler::BlocksForMillis(100);
    REQUIRE(due > 1);

    del->SetBang(0);
    // Nothing in the dispatch that armed it — that is what "delay" means.
    CHECK(out.bangs == 0);
    CHECK(p.Scheduler()->PendingCount() == 1);

    for (std::uint64_t block = 1; block < due; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.bangs == 0);

    p.Calculate(YSE::T_DSP);
    CHECK(out.bangs == 1);
    CHECK(p.Scheduler()->PendingCount() == 0);

    // And it stays gone: one bang in, one bang out.
    for (std::uint64_t block = 0; block < due + 2; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.bangs == 1);
  }

  TEST_CASE("delay: 'delay 0' defers to the next block rather than firing now (#503)") {
    // The decision this object turns on, and the one place its reading of the
    // scheduler's one-block deadline floor had to be chosen rather than
    // inherited. .qlist documents the floor as a feature; .seq deliberately
    // overrides it so the three bytes of a note-on share a block. Here the
    // floor *is* the semantics: in Max, `delay 0` is how a patch breaks out of
    // the current message chain, and a synchronous one would be .bangbang. It
    // is also what keeps a delay wired back into its own inlet a fast metronome
    // instead of a stack overflow, which .seq could give up only because its
    // walk is bounded by its own tape.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* del = p.CreateObject(YSE::OBJ::G_DELAY, "0");
    REQUIRE(del != nullptr);

    Counter out;
    YSE::pHandle outHandle(&out);
    p.Connect(del, 0, &outHandle, 0);

    CHECK(messageScheduler::BlocksForMillis(0) == 1);

    del->SetBang(0);
    CHECK(out.bangs == 0);
    CHECK(p.Scheduler()->PendingCount() == 1);

    p.Calculate(YSE::T_DSP);
    CHECK(out.bangs == 1);
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

  TEST_CASE("delay: a second bang forgets the first (#503)") {
    // Max: "only one bang at a time can be delayed by delay. If a bang is
    // already in delay when a new bang is received in the left inlet, the first
    // bang is forgotten." Two bangs in, one bang out — and the wait is measured
    // from the newest one, so the output lands late rather than on the first
    // bang's original deadline.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* del = p.CreateObject(YSE::OBJ::G_DELAY, "100");
    REQUIRE(del != nullptr);

    Counter out;
    YSE::pHandle outHandle(&out);
    p.Connect(del, 0, &outHandle, 0);

    const std::uint64_t due = messageScheduler::BlocksForMillis(100);
    REQUIRE(due > 3);

    del->SetBang(0);
    for (std::uint64_t block = 0; block < due - 2; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.bangs == 0);

    // The restart. Still exactly one pending message rather than two.
    del->SetBang(0);
    CHECK(p.Scheduler()->PendingCount() == 1);

    // Two more blocks carry the *first* bang past the deadline it was armed
    // with. Nothing comes out, which is what "forgotten" means.
    p.Calculate(YSE::T_DSP);
    p.Calculate(YSE::T_DSP);
    CHECK(out.bangs == 0);

    // The rest of the second bang's wait, measured from the restart.
    for (std::uint64_t block = 2; block < due - 1; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.bangs == 0);
    p.Calculate(YSE::T_DSP);
    CHECK(out.bangs == 1);

    // Only one came out, ever.
    for (std::uint64_t block = 0; block < due + 2; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.bangs == 1);
  }

  TEST_CASE("delay: 'stop' cancels the bang being held (#503)") {
    // Max: "stops delay from outputting the bang it is currently delaying."
    patcherImplementation p(1, nullptr);
    YSE::pHandle* del = p.CreateObject(YSE::OBJ::G_DELAY, "100");
    REQUIRE(del != nullptr);

    Counter out;
    YSE::pHandle outHandle(&out);
    p.Connect(del, 0, &outHandle, 0);

    const std::uint64_t due = messageScheduler::BlocksForMillis(100);

    del->SetBang(0);
    REQUIRE(p.Scheduler()->PendingCount() == 1);
    del->SetListData(0, "stop");
    CHECK(p.Scheduler()->PendingCount() == 0);

    for (std::uint64_t block = 0; block <= due + 2; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.bangs == 0);
  }

  TEST_CASE("delay: the right inlet does not retime a bang already in flight (#503)") {
    // Max: "a number received in the right inlet changes the delay time of the
    // next bang received -- it does not modify the time of a bang currently
    // being delayed." The naive implementation recomputes the deadline whenever
    // the time changes and fails this; nothing about it looks wrong.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* del = p.CreateObject(YSE::OBJ::G_DELAY, "100");
    REQUIRE(del != nullptr);

    Counter out;
    YSE::pHandle outHandle(&out);
    p.Connect(del, 0, &outHandle, 0);

    const std::uint64_t shortDue = messageScheduler::BlocksForMillis(10);
    const std::uint64_t due = messageScheduler::BlocksForMillis(100);
    REQUIRE(shortDue < due);

    del->SetBang(0);
    // A much shorter time arrives while the bang waits. It must not pull the
    // deadline in.
    del->SetIntData(1, 10);
    CHECK(del->GetInputs() == 2);

    for (std::uint64_t block = 1; block < due; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.bangs == 0);
    p.Calculate(YSE::T_DSP);
    CHECK(out.bangs == 1);

    // The next bang does use the new time, which is the other half of the same
    // sentence.
    del->SetBang(0);
    for (std::uint64_t block = 1; block < shortDue; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.bangs == 1);
    p.Calculate(YSE::T_DSP);
    CHECK(out.bangs == 2);
  }

  TEST_CASE("delay: an int in the left inlet arms the wait rather than banging now (#503)") {
    // Max's "it then automatically sends a bang message to itself to start the
    // delay" — inside a patcher the started delay is a real wait, where the
    // standalone case above could only show that something started at all.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* del = p.CreateObject(YSE::OBJ::G_DELAY, "5");
    REQUIRE(del != nullptr);

    Counter out;
    YSE::pHandle outHandle(&out);
    p.Connect(del, 0, &outHandle, 0);

    const std::uint64_t due = messageScheduler::BlocksForMillis(100);
    REQUIRE(due > 1);

    del->SetIntData(0, 100);
    CHECK(out.bangs == 0);
    CHECK(p.Scheduler()->PendingCount() == 1);

    for (std::uint64_t block = 1; block < due; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.bangs == 0);
    p.Calculate(YSE::T_DSP);
    CHECK(out.bangs == 1);
  }

  TEST_CASE("delay: a paused patcher holds the bang where it stands (#503)") {
    // The clock is the patcher's block counter, so time only advances while the
    // patcher renders. That is the only meaning "100 ms from now" can have on a
    // clock that is not running, and it is what makes a deferred bang survive a
    // paused engine instead of arriving in a burst afterwards.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* del = p.CreateObject(YSE::OBJ::G_DELAY, "100");
    REQUIRE(del != nullptr);

    Counter out;
    YSE::pHandle outHandle(&out);
    p.Connect(del, 0, &outHandle, 0);

    const std::uint64_t due = messageScheduler::BlocksForMillis(100);

    del->SetBang(0);
    // Not one block rendered: the bang is still held, however long real time
    // has moved on.
    CHECK(p.Scheduler()->PendingCount() == 1);
    CHECK(out.bangs == 0);

    for (std::uint64_t block = 0; block < due; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.bangs == 1);
  }
}
