// Tests for `.pipe` — Max's pipe, "delay numbers, lists, or symbols" (issue
// #504).
//
// The object is `.delay` for data, and the one difference that matters is the
// queue. `.delay` holds a single bang and Max's "the first bang is forgotten"
// throws the previous one away; `.pipe` holds many values at once, each with
// its own deadline. Almost every case below exists to hold one half of that
// sentence down, because an implementation that quietly reused `.delay`'s
// one-pending-message shape would pass a naive "does a value come back later"
// test and silently swallow nine values out of ten.
//
// Two layers, and as with `.delay` they are not interchangeable:
//
//   - **standalone cases** pin the grammar and the shape: what each inlet
//     accepts, that the value comes back out *as the kind of message it went
//     in as*, that `clear` and `flush` are commands rather than delayable
//     text, that a beat time in the time inlet is refused rather than read as
//     milliseconds, and what a save carries. A standalone object has no
//     patcher and so no clock at all, which is why it passes values straight
//     through — perfect for everything that is not timing.
//
//   - **patcher cases** pin the queue and the clock, neither of which can
//     exist standalone: that five values really are five values out, that they
//     each keep their own deadline when the delay time changes underneath
//     them, that `clear` and `flush` reach every one of them, that a full
//     pending set drops and *counts* rather than sending early, and that a
//     paused patcher holds the lot where it stands. Deadlines are asserted
//     through `messageScheduler::BlocksForMillis` at the live SAMPLERATE
//     rather than through hard-coded block counts, so the suite holds at any
//     negotiated rate.
//
// Plus the usual per-object obligations: the registry entry, an allocation
// probe over the message paths, a DumpJSON / ParseJSON round trip, and an
// integration case that runs a real two-stage delay line inside a real
// patcher, which is the only way to exercise a `.pipe` armed from inside
// `Calculate` — the audio callback.
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
#include "patcher/time/messageScheduler.h"
#include "support/alloc_probe.hpp"

using YSE::PATCHER::gPipe;
using YSE::PATCHER::messageScheduler;
using YSE::PATCHER::patcherImplementation;

namespace {

  // Records every message this object receives, in order and *with its type*.
  // The type is half of what this object promises — a `.pipe` that turned an
  // int into a list would not reach the int inlet the undelayed value would
  // have reached — so a sink that only recorded numbers would be blind to the
  // more interesting way of getting this wrong.
  struct Recorder : YSE::PATCHER::pObject {
    struct Event {
      char kind = 'i'; // 'i' int, 'f' float, 'l' list
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
      return "pipe_recorder";
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
  // that still get this wrong; this is not a 23rd.)
  void Wire(YSE::PATCHER::pObject& from, int outlet, YSE::PATCHER::pObject& to, int inlet = 0) {
    REQUIRE(to.ConnectInlet(from.GetOutlet(outlet), inlet));
    from.ConnectOutlet(to.GetInlet(inlet), outlet);
  }

  // A standalone `.pipe` with a recorder on its outlet. Standalone means no
  // patcher, so no scheduler and no clock: every value comes straight back out,
  // which is exactly what makes this rig the right place to test everything
  // that is not timing.
  //
  // The recorder is declared **before** the pipe so it is destroyed after it —
  // see Wire on why that matters even for a symmetric edge.
  struct Rig {
    Recorder out;
    gPipe obj;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) obj.SetParams(args);
      Wire(obj, 0, out);
    }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    Rig(Rig&&) = delete;
    Rig& operator=(Rig&&) = delete;

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

} // namespace

TEST_SUITE("patcher") {

  // ─── the object exists ──────────────────────────────────────────────────────

  TEST_CASE("pipe: creatable through the registry (#504)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_PIPE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".pipe");
  }

  TEST_CASE("pipe: listed by pRegistry::AllNames (#504)") {
    auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".pipe")) != names.end());
  }

  TEST_CASE("pipe: the shape is two inlets and one outlet (#504)") {
    gPipe obj;
    CHECK(obj.NumInputs() == 2);
    CHECK(obj.NumOutputs() == 1);
    CHECK(obj.GetOutputType(0) == YSE::OUT_TYPE::ANY);
    CHECK(obj.GetCategory() == YSE::PATCHER::pCategory::TIME);
  }

  TEST_CASE("pipe: a fresh object waits Max's documented 0 ms and holds nothing (#504)") {
    // Max's delaytime attribute: "default 0". A delay of 0 is still a delay
    // inside a patcher — the scheduler's floor is one block — which is what the
    // `pipe 0` case further down pins.
    gPipe obj;
    CHECK(obj.DelayTime() == gPipe::DEFAULT_DELAY);
    CHECK(obj.DelayTime() == 0);
    CHECK(obj.PendingCount() == 0);
    CHECK(obj.Dropped() == 0);
  }

  // ─── the grammar, which needs no clock ──────────────────────────────────────

  TEST_CASE("pipe: the creation argument is Max's delay time in milliseconds (#504)") {
    Rig rig("250");
    CHECK(rig.obj.DelayTime() == 250);
  }

  TEST_CASE("pipe: a value comes back as the kind of message it went in as (#504)") {
    // Standalone there is no clock, so "later" resolves at once and the value
    // is visible here. What this pins is the *type*: an int out for an int in,
    // a float for a float, text for text. This patcher does no coercion at an
    // inlet, so a `.pipe` that retyped its payload would silently stop reaching
    // whatever the undelayed value reached.
    Rig rig("100");
    rig.Int(7);
    rig.Float(2.5f);
    rig.List("some words here");

    REQUIRE(rig.out.n() == 3);
    CHECK(rig.out.events[0].kind == 'i');
    CHECK(rig.out.events[0].intValue == 7);
    CHECK(rig.out.events[1].kind == 'f');
    CHECK(rig.out.events[1].floatValue == doctest::Approx(2.5f));
    CHECK(rig.out.events[2].kind == 'l');
    CHECK(rig.out.events[2].text == "some words here");
  }

  TEST_CASE("pipe: a one-token numeric message is queued as the number it spells (#504)") {
    // A `.m 5` reaches this inlet as a list carrying "5", and re-emitting it as
    // a list would not reach an `.i` downstream. The leading-token test
    // `.bondo` and `.trigger` already use decides it, so the value that comes
    // out is the value that went in.
    Rig rig;
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

  TEST_CASE("pipe: the right inlet only sets the time (#504)") {
    Rig rig;
    rig.Int(300, 1);
    CHECK(rig.obj.DelayTime() == 300);
    rig.Float(45.5f, 1);
    CHECK(rig.obj.DelayTime() == 45);
    rig.List("175", 1);
    CHECK(rig.obj.DelayTime() == 175);
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("pipe: a negative or NaN time counts as 0 (#504)") {
    Rig rig("100");
    rig.Int(-50, 1);
    CHECK(rig.obj.DelayTime() == 0);
    rig.Int(100, 1);
    rig.Float(-12.5f, 1);
    CHECK(rig.obj.DelayTime() == 0);
  }

  TEST_CASE("pipe: a tempo-relative time is refused rather than read as milliseconds (#504)") {
    // The trap `.clocker` fell into before #725: `1440 ticks` taken for its
    // leading token becomes 1440 ms, which is not a wait anybody asked for.
    // This object has no clock to measure a beat against, so the honest answer
    // is to leave the delay where it was.
    Rig rig("100");
    rig.List("1440 ticks", 1);
    CHECK(rig.obj.DelayTime() == 100);
    rig.List("4nd", 1);
    CHECK(rig.obj.DelayTime() == 100);
    rig.List("8nt", 1);
    CHECK(rig.obj.DelayTime() == 100);
    // And neither is anything else it cannot read.
    rig.List("1.1.0", 1);
    rig.List("wibble", 1);
    CHECK(rig.obj.DelayTime() == 100);
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("pipe: 'clear' and 'flush' are commands in the left inlet only (#504)") {
    // Standalone there is nothing pending to clear or flush, so what this pins
    // is that the two words are not data: they must not come out the outlet.
    Rig rig("100");
    rig.List("clear");
    rig.List("flush");
    CHECK(rig.out.n() == 0);

    // In the time inlet they are neither commands nor data — only a time this
    // object cannot read, which leaves the delay alone.
    rig.List("clear", 1);
    rig.List("flush", 1);
    CHECK(rig.obj.DelayTime() == 100);
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("pipe: there is no bang method, as Max's pipe has none (#504)") {
    Rig rig("100");
    rig.obj.GetInlet(0)->SetBang(YSE::T_GUI);
    rig.obj.GetInlet(1)->SetBang(YSE::T_GUI);
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("pipe: a standalone object passes values straight through (#504)") {
    // No patcher means no scheduler and so no clock at all: "later" has no
    // referent, and the only alternatives are now or never. `.delay`'s answer
    // to the same dead end, and the one that keeps a standalone object testable
    // rather than a black hole.
    Rig rig("500");
    rig.Int(1);
    rig.Int(2);
    CHECK(rig.out.ints() == std::vector<int>{1, 2});
    CHECK(rig.obj.PendingCount() == 0);
    CHECK(rig.obj.Dropped() == 0);
  }

  TEST_CASE("pipe: Calculate sends nothing (#504)") {
    // The object is driven by its inlets and by the scheduler; one that emitted
    // would send a value on every DSP tick from a stimulus no patch sent.
    Rig rig("100");
    for (int i = 0; i < 8; i++)
      rig.obj.Calculate(YSE::T_DSP);
    CHECK(rig.out.n() == 0);
  }

  // ─── what a save carries ────────────────────────────────────────────────────

  TEST_CASE("pipe: the delay time survives a DumpJSON / ParseJSON round trip (#504)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* obj = src.CreateObject(YSE::OBJ::G_PIPE, "420");
    REQUIRE(obj != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".pipe") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".pipe");
    CHECK(std::string(copy->GetParams()) == "420");
  }

  // ─── the queue and the clock, which need a real patcher ─────────────────────

  TEST_CASE("pipe: a value comes back one wait later, not in the arming dispatch (#504)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* pipe = p.CreateObject(YSE::OBJ::G_PIPE, "100");
    REQUIRE(pipe != nullptr);

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(pipe, 0, &outHandle, 0);

    const std::uint64_t due = messageScheduler::BlocksForMillis(100);
    REQUIRE(due > 1);

    pipe->SetIntData(0, 42);
    CHECK(out.n() == 0);
    CHECK(p.Scheduler()->PendingCount() == 1);

    for (std::uint64_t block = 1; block < due; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.n() == 0);

    p.Calculate(YSE::T_DSP);
    REQUIRE(out.n() == 1);
    CHECK(out.events[0].kind == 'i');
    CHECK(out.events[0].intValue == 42);
    CHECK(p.Scheduler()->PendingCount() == 0);

    // And it stays gone: one value in, one value out.
    for (std::uint64_t block = 0; block < due + 2; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.n() == 1);
  }

  TEST_CASE("pipe: values queue rather than replacing each other (#504)") {
    // **The object.** This is the whole difference from `.delay`, whose Max
    // page says "if a bang is already in delay when a new bang is received in
    // the left inlet, the first bang is forgotten". Five values in, five values
    // out, in order — an implementation that reused `.delay`'s single pending
    // handle would deliver exactly one and look perfectly healthy doing it.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* pipe = p.CreateObject(YSE::OBJ::G_PIPE, "100");
    REQUIRE(pipe != nullptr);

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(pipe, 0, &outHandle, 0);

    const std::uint64_t due = messageScheduler::BlocksForMillis(100);

    for (int i = 1; i <= 5; i++)
      pipe->SetIntData(0, i);
    CHECK(p.Scheduler()->PendingCount() == 5);
    CHECK(out.n() == 0);

    for (std::uint64_t block = 1; block < due; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.n() == 0);

    // All five were armed in the same dispatch, so all five come due in the
    // same block — and the scheduler delivers a block's messages in arm order.
    p.Calculate(YSE::T_DSP);
    CHECK(out.ints() == std::vector<int>{1, 2, 3, 4, 5});
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

  TEST_CASE("pipe: each value keeps the delay it was queued with (#504)") {
    // Max's right inlet sets the time for values received *after* it; a value
    // already in the pipe is not retimed. The naive implementation recomputes
    // every deadline when the time changes, and nothing about it looks wrong.
    // Here the second value is queued with a *shorter* delay than the first,
    // so it overtakes it — which is only possible because the two deadlines
    // are independent.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* pipe = p.CreateObject(YSE::OBJ::G_PIPE, "100");
    REQUIRE(pipe != nullptr);

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(pipe, 0, &outHandle, 0);

    const std::uint64_t slow = messageScheduler::BlocksForMillis(100);
    const std::uint64_t quick = messageScheduler::BlocksForMillis(10);
    REQUIRE(quick < slow);

    pipe->SetIntData(0, 1); // queued at 100 ms
    pipe->SetIntData(1, 10); // the time inlet, after the fact
    pipe->SetIntData(0, 2); // queued at 10 ms

    for (std::uint64_t block = 1; block < quick; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.n() == 0);

    p.Calculate(YSE::T_DSP);
    CHECK(out.ints() == std::vector<int>{2});

    for (std::uint64_t block = quick + 1; block < slow; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.ints() == std::vector<int>{2});

    p.Calculate(YSE::T_DSP);
    CHECK(out.ints() == std::vector<int>{2, 1});
  }

  TEST_CASE("pipe: 'clear' drops every pending value (#504)") {
    // Max: "clear: removes all delayed items from pipe's memory, so they will
    // not be output." Every one of them, not just the newest.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* pipe = p.CreateObject(YSE::OBJ::G_PIPE, "100");
    REQUIRE(pipe != nullptr);

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(pipe, 0, &outHandle, 0);

    const std::uint64_t due = messageScheduler::BlocksForMillis(100);

    for (int i = 1; i <= 4; i++)
      pipe->SetIntData(0, i);
    REQUIRE(p.Scheduler()->PendingCount() == 4);

    pipe->SetListData(0, "clear");
    CHECK(out.n() == 0);
    // The object's own set is empty at once, and the patcher-wide budget is
    // handed back with it.
    CHECK(p.Scheduler()->PendingCount() == 0);

    for (std::uint64_t block = 0; block <= due + 2; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.n() == 0);
  }

  TEST_CASE("pipe: 'flush' sends every pending value now, in arrival order (#504)") {
    // Max: "flush: causes all items currently stored in pipe to be output
    // immediately." Immediately means inside this dispatch, not on the next
    // block — a flush that merely rescheduled everything for 0 ms would pass a
    // count assertion and fail this one.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* pipe = p.CreateObject(YSE::OBJ::G_PIPE, "100");
    REQUIRE(pipe != nullptr);

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(pipe, 0, &outHandle, 0);

    pipe->SetIntData(0, 1);
    pipe->SetFloatData(0, 2.5f);
    pipe->SetListData(0, "three words here");
    pipe->SetIntData(0, 4);
    REQUIRE(p.Scheduler()->PendingCount() == 4);

    pipe->SetListData(0, "flush");
    REQUIRE(out.n() == 4);
    CHECK(out.events[0].kind == 'i');
    CHECK(out.events[0].intValue == 1);
    CHECK(out.events[1].kind == 'f');
    CHECK(out.events[1].floatValue == doctest::Approx(2.5f));
    CHECK(out.events[2].kind == 'l');
    CHECK(out.events[2].text == "three words here");
    CHECK(out.events[3].kind == 'i');
    CHECK(out.events[3].intValue == 4);
    CHECK(p.Scheduler()->PendingCount() == 0);

    // Nothing comes out a second time when the deadlines they were armed with
    // would have passed.
    for (std::uint64_t block = 0; block <= messageScheduler::BlocksForMillis(100) + 2; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.n() == 4);
  }

  TEST_CASE("pipe: a full pending set drops and counts rather than sending early (#504)") {
    // The bound the issue asks for, and the answer to over-capacity: the value
    // is dropped, `Dropped()` reports it, and — the part that matters — nothing
    // comes out *early*. Sending the surplus immediately would break the
    // object's one guarantee at exactly the moment the patch is at its resource
    // limit, and would let a `.pipe` fed from its own outlet recurse on the
    // audio thread.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* pipe = p.CreateObject(YSE::OBJ::G_PIPE, "1000");
    REQUIRE(pipe != nullptr);

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(pipe, 0, &outHandle, 0);

    const int offered = (int)gPipe::CAPACITY + 20;
    for (int i = 0; i < offered; i++)
      pipe->SetIntData(0, i);

    CHECK(out.n() == 0); // nothing early
    CHECK(p.Scheduler()->PendingCount() == gPipe::CAPACITY);

    const std::uint64_t due = messageScheduler::BlocksForMillis(1000);
    for (std::uint64_t block = 0; block <= due; ++block)
      p.Calculate(YSE::T_DSP);

    // Exactly the capacity came out, and they are the first ones offered.
    CHECK(out.n() == gPipe::CAPACITY);
    const std::vector<int> got = out.ints();
    REQUIRE(got.size() == gPipe::CAPACITY);
    CHECK(got.front() == 0);
    CHECK(got.back() == (int)gPipe::CAPACITY - 1);
  }

  TEST_CASE("pipe: 'pipe 0' defers to the next block rather than firing now (#504)") {
    // Max's default delay, and the scheduler's one-block floor read as
    // semantics rather than as rounding: `pipe 0` is how a patch breaks out of
    // the current message chain, and it is what keeps a pipe wired back into
    // its own inlet a fast delay line instead of a stack overflow.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* pipe = p.CreateObject(YSE::OBJ::G_PIPE, "");
    REQUIRE(pipe != nullptr);

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(pipe, 0, &outHandle, 0);

    CHECK(messageScheduler::BlocksForMillis(0) == 1);

    pipe->SetIntData(0, 9);
    CHECK(out.n() == 0);
    CHECK(p.Scheduler()->PendingCount() == 1);

    p.Calculate(YSE::T_DSP);
    CHECK(out.ints() == std::vector<int>{9});
  }

  TEST_CASE("pipe: a paused patcher holds every pending value where it stands (#504)") {
    // The clock is the patcher's block counter, so time only advances while the
    // patcher renders. That is the only meaning "100 ms from now" can have on a
    // clock that is not running, and it is what makes a queued value survive a
    // paused engine instead of arriving in a burst afterwards.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* pipe = p.CreateObject(YSE::OBJ::G_PIPE, "100");
    REQUIRE(pipe != nullptr);

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(pipe, 0, &outHandle, 0);

    for (int i = 1; i <= 3; i++)
      pipe->SetIntData(0, i);
    CHECK(p.Scheduler()->PendingCount() == 3);
    CHECK(out.n() == 0);

    for (std::uint64_t block = 0; block < messageScheduler::BlocksForMillis(100); ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.ints() == std::vector<int>{1, 2, 3});
  }

  // ─── the audio-thread path, end to end ──────────────────────────────────────

  TEST_CASE("pipe: a two-stage delay line queues from inside Calculate (#504)") {
    // The integration case: two `.pipe` objects in series inside a real
    // patcher. The first one's value is released by the scheduler at the top of
    // `Calculate` — on the audio callback — and that release is what queues the
    // second one. Nothing else in this file exercises an arrival on the audio
    // thread, and the arm path is precisely the code that must not allocate or
    // lock there.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* first = p.CreateObject(YSE::OBJ::G_PIPE, "50");
    YSE::pHandle* second = p.CreateObject(YSE::OBJ::G_PIPE, "50");
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(first, 0, second, 0);
    p.Connect(second, 0, &outHandle, 0);

    const std::uint64_t leg = messageScheduler::BlocksForMillis(50);
    REQUIRE(leg > 1);

    first->SetIntData(0, 3);
    first->SetIntData(0, 4);

    // One leg: the first pipe has released into the second, which has queued
    // both values from inside the audio callback and sent nothing on.
    for (std::uint64_t block = 0; block < leg; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.n() == 0);
    CHECK(p.Scheduler()->PendingCount() == 2);

    // Two legs: both values are out the far end, still in order.
    for (std::uint64_t block = 0; block < leg; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.ints() == std::vector<int>{3, 4});
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

  // ─── real-time discipline ───────────────────────────────────────────────────

  TEST_CASE("pipe: the message paths allocate nothing (#504)") {
    // Everything a value can do on the way in, measured on the thread that
    // does it. Queueing text is the interesting one: the slot's string was
    // reserved by the constructor, so an assign of up to TEXT_CAPACITY
    // characters is a copy rather than an allocation.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    patcherImplementation p(1, nullptr);
    YSE::pHandle* pipe = p.CreateObject(YSE::OBJ::G_PIPE, "1000");
    REQUIRE(pipe != nullptr);

    Recorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(pipe, 0, &outHandle, 0);

    // Built outside the probe: it is the *handler* that must not allocate, not
    // the test's own construction of the message.
    const std::string text = "a b c d e";
    const std::string number = "17";
    const std::string ticks = "1440 ticks";
    const std::string time = "250";
    const std::string clear = "clear";
    {
      TestHelpers::ProbeScope probe;
      pipe->SetIntData(0, 1);
      pipe->SetFloatData(0, 2.f);
      pipe->SetListData(0, text);
      pipe->SetListData(0, number);
      pipe->SetIntData(1, 500);
      pipe->SetFloatData(1, 125.f);
      pipe->SetListData(1, ticks);
      pipe->SetListData(1, time);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    // The probed messages really did something — an assertion that only proves
    // nothing happened proves nothing.
    CHECK(out.n() == 0);
    REQUIRE(p.Scheduler()->PendingCount() == 4);

    // `clear` walks the whole slot table and cancels four scheduler messages,
    // which is the other path that must stay allocation-free.
    {
      TestHelpers::ProbeScope probe;
      pipe->SetListData(0, clear);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(p.Scheduler()->PendingCount() == 0);
    CHECK(out.n() == 0);
  }
}
