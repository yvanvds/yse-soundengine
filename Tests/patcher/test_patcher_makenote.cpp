// Tests for `.makenote` — Max's makenote, "outputs a MIDI note-on message
// paired with a velocity value followed by a note-off message after a specified
// amount of time" (issue #538).
//
// The object's whole promise is that the release cannot be forgotten, so most
// of what is pinned below is about the release rather than about the attack. An
// implementation that emitted the pair and scheduled nothing would pass a naive
// "does a note come out" test and strand every note it ever played.
//
// Three layers, and they are not interchangeable:
//
//   - **standalone cases** pin the grammar and the shape: what each inlet
//     accepts, that a list is Max's inlet distribution, that `stop` and `clear`
//     are commands rather than pitches, that a beat time in the duration inlet
//     is refused rather than read as milliseconds, that the velocity outlet
//     fires before the pitch outlet, and what a save carries. A standalone
//     object has no patcher and so no clock at all, which is why it sends the
//     release straight after the attack — perfect for everything that is not
//     timing.
//
//   - **patcher cases** pin the clock and the pending set, neither of which can
//     exist standalone: that the release really does wait, that notes are
//     polyphonic and each keeps its own deadline, that `stop` and `clear` reach
//     every one of them, that a full pending set refuses the *whole note* while
//     a full patcher-wide budget releases immediately, and that a paused
//     patcher holds everything where it stands. Deadlines are asserted through
//     `messageScheduler::BlocksForMillis` at the live SAMPLERATE rather than
//     through hard-coded block counts, so the suite holds at any negotiated
//     rate.
//
//   - **end-to-end cases** run a real `.makenote` -> `.noteon` -> `.midiflush`
//     chain inside a real patcher and ask the question a user asks: after the
//     notes have played, is anything still sounding? `.midiflush` (#537) is the
//     honest judge of that — it decodes the byte stream and remembers exactly
//     what was left hanging — and the control case proves the rig can see a
//     hanging note before the case that proves there isn't one.
//
// No audio device and no MIDI hardware required.

#include <doctest/doctest.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "patcher/inlet.h"
#include "patcher/midi/mMakeNote.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "patcher/time/messageScheduler.h"
#include "support/alloc_probe.hpp"

using YSE::PATCHER::messageScheduler;
using YSE::PATCHER::mMakeNote;
using YSE::PATCHER::patcherImplementation;

namespace {

  // Records both outlets into one ordered log, which is the only way to see the
  // half of this object that lives in the *order* the two outlets fire in.
  // Velocity has to reach a sender's cold inlet before the pitch reaches its hot
  // one, so a sink that watched the pitch outlet alone would be blind to the
  // most consequential way of getting this object wrong.
  //
  // Events are a fixed-size struct in a reserved vector rather than strings, so
  // the allocation probe measures the object under test and not this sink.
  struct Notes : YSE::PATCHER::pObject {
    struct Event {
      char kind = 'p'; // 'p' pitch outlet, 'v' velocity outlet
      int value = 0;
    };

    std::vector<Event> events;

    Notes() : pObject(false) {
      events.reserve(4096);

      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        Event e;
        e.kind = 'p';
        e.value = v;
        events.push_back(e);
      });

      inputs.emplace_back(this, false, 1);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        Event e;
        e.kind = 'v';
        e.value = v;
        events.push_back(e);
      });
    }
    const char* Type() const override {
      return "makenote_notes";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    std::size_t n() const {
      return events.size();
    }
    void Clear() {
      events.clear();
    }

    // "v100 p60 v0 p60" — the whole exchange in the order it happened.
    std::string trace() const {
      std::string out;
      for (const Event& e : events) {
        if (!out.empty()) out.push_back(' ');
        out.push_back(e.kind);
        out += std::to_string(e.value);
      }
      return out;
    }

    // Just the pitches, for the cases that only care which notes came back.
    std::vector<int> pitches() const {
      std::vector<int> out;
      for (const Event& e : events)
        if (e.kind == 'p') out.push_back(e.value);
      return out;
    }
  };

  using TestHelpers::Wire;

  // A standalone `.makenote` with both outlets watched. Standalone means no
  // patcher, so no scheduler and no clock: the release follows the attack at
  // once, which is exactly what makes this rig the right place to test
  // everything that is not timing.
  //
  // The sink is declared **before** the object so it is destroyed after it —
  // see Wire on why that matters even for a symmetric edge.
  struct Rig {
    Notes out;
    mMakeNote obj;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) obj.SetParams(args);
      Wire(obj, 0, out, 0);
      Wire(obj, 1, out, 1);
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

  // Records every message reaching it, for the end-to-end chain where the
  // payload is a binary MIDI string and only the *count* is being asserted.
  struct Tap : YSE::PATCHER::pObject {
    int seen = 0;

    Tap() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int, int, YSE::THREAD) { seen++; });
      inputs.back().RegisterList([this](const std::string&, int, YSE::THREAD) { seen++; });
    }
    const char* Type() const override {
      return "makenote_tap";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── the object exists ──────────────────────────────────────────────────────

  TEST_CASE("makenote: creatable through the registry (#538)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::M_MAKENOTE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".makenote");
  }

  TEST_CASE("makenote: listed by pRegistry::AllNames (#538)") {
    auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".makenote")) != names.end());
  }

  TEST_CASE("makenote: the shape is three inlets and two outlets (#538)") {
    // Always three and two, never Max's channel pair: ports are built in the
    // constructor and creation arguments are parsed afterwards, so a port count
    // that depends on the arguments is not expressible here.
    mMakeNote obj;
    CHECK(obj.NumInputs() == 3);
    CHECK(obj.NumOutputs() == 2);
    CHECK(obj.GetOutputType(0) == YSE::OUT_TYPE::INT);
    CHECK(obj.GetOutputType(1) == YSE::OUT_TYPE::INT);
    CHECK(obj.GetCategory() == YSE::PATCHER::pCategory::MIDI);
    CHECK_FALSE(obj.IsDSPObject());
    CHECK_FALSE(obj.WantsBlockPoll());
  }

  TEST_CASE("makenote: documents itself (#538)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::M_MAKENOTE));
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == std::string(YSE::OBJ::M_MAKENOTE));
    CHECK_FALSE(obj->GetDescription().empty());
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MIDI);
    for (int i = 0; i < obj->NumInputs(); i++)
      CHECK_FALSE(obj->GetInlet(i)->GetDocLabel().empty());
    for (int i = 0; i < obj->NumOutputs(); i++)
      CHECK_FALSE(obj->GetOutlet(i)->GetDocLabel().empty());
    CHECK(obj->GetParamDocs().size() == 2);
  }

  TEST_CASE("makenote: a fresh object takes Max's documented defaults (#538)") {
    // Max's creation arguments: velocity "defaults to 0", duration "defaults to
    // immediate".
    mMakeNote obj;
    CHECK(obj.Velocity() == mMakeNote::DEFAULT_VELOCITY);
    CHECK(obj.Velocity() == 0);
    CHECK(obj.Duration() == mMakeNote::DEFAULT_DURATION);
    CHECK(obj.Duration() == 0);
    CHECK(obj.Pending() == 0);
    CHECK(obj.Dropped() == 0);
  }

  // ─── the grammar, which needs no clock ──────────────────────────────────────

  TEST_CASE("makenote: the creation arguments are Max's velocity and duration (#538)") {
    Rig rig("100 250");
    CHECK(rig.obj.Velocity() == 100);
    CHECK(rig.obj.Duration() == 250);
  }

  TEST_CASE("makenote: a pitch plays the pair, velocity outlet first (#538)") {
    // Max's outlets fire right to left, and here that is load-bearing rather
    // than cosmetic: every note sender downstream takes its pitch on a hot inlet
    // and its velocity on a cold one, so a pitch sent first would carry the
    // *previous* note's velocity. Standalone there is no clock, so the release
    // follows at once and both halves are visible here.
    Rig rig("100 250");
    rig.Int(60);
    CHECK(rig.out.trace() == "v100 p60 v0 p60");
  }

  TEST_CASE("makenote: a float pitch is truncated to an int (#538)") {
    Rig rig("100 250");
    rig.Float(60.7f);
    CHECK(rig.out.trace() == "v100 p60 v0 p60");
  }

  TEST_CASE("makenote: the middle inlet sets the velocity for later notes (#538)") {
    Rig rig("100 250");
    rig.Int(64, 1);
    CHECK(rig.obj.Velocity() == 64);
    rig.Float(70.9f, 1);
    CHECK(rig.obj.Velocity() == 70);
    rig.List("55", 1);
    CHECK(rig.obj.Velocity() == 55);
    // Setting it plays nothing.
    CHECK(rig.out.n() == 0);

    rig.Int(60);
    CHECK(rig.out.trace() == "v55 p60 v0 p60");
  }

  TEST_CASE("makenote: the right inlet sets the duration for later notes (#538)") {
    Rig rig("100 250");
    rig.Int(300, 2);
    CHECK(rig.obj.Duration() == 300);
    rig.Float(45.5f, 2);
    CHECK(rig.obj.Duration() == 45);
    rig.List("175", 2);
    CHECK(rig.obj.Duration() == 175);
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("makenote: a list is Max's inlet distribution — pitch then velocity (#538)") {
    // Max's list distributes across the inlets right to left, so the velocity is
    // stored *before* the pitch plays and it stays stored for the notes after
    // it. A list that played the pitch first would sound the note at the old
    // velocity, which is a bug that only shows on the very first note.
    Rig rig("1 250");
    rig.List("60 100");
    CHECK(rig.out.trace() == "v100 p60 v0 p60");
    CHECK(rig.obj.Velocity() == 100);

    // And it persists, as a middle-inlet value would.
    rig.out.Clear();
    rig.Int(62);
    CHECK(rig.out.trace() == "v100 p62 v0 p62");

    // Further elements are ignored: Max's third is a channel and there is no
    // channel outlet here.
    rig.out.Clear();
    rig.List("64 90 7 7");
    CHECK(rig.out.trace() == "v90 p64 v0 p64");
  }

  TEST_CASE("makenote: a single-token numeric list is a pitch (#538)") {
    // A `.m 60` reaches this inlet as a list carrying "60"; one that did not
    // read it as a pitch would silently play nothing.
    Rig rig("100 250");
    rig.List("60");
    CHECK(rig.out.trace() == "v100 p60 v0 p60");

    rig.out.Clear();
    rig.List("wibble");
    rig.List("");
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("makenote: a velocity of 0 sends the pair and schedules nothing (#538)") {
    // A note-on with velocity 0 *is* a release in MIDI. Max would queue a
    // second, identical release behind it; spending a slot on that would let a
    // patch that plays releases through a `.makenote` exhaust the pending set
    // with notes that were never sounding.
    Rig rig("0 250");
    rig.Int(60);
    CHECK(rig.out.trace() == "v0 p60");
    CHECK(rig.obj.Pending() == 0);
    CHECK(rig.obj.Dropped() == 0);
  }

  TEST_CASE("makenote: the velocity is clamped to MIDI's range (#538)") {
    // Clamped where a pitch passing through is not, because this value decides
    // whether a release is scheduled at all: an out-of-range velocity that
    // silently read as 0 would change the object's behaviour and not just its
    // output.
    Rig rig;
    rig.Int(200, 1);
    CHECK(rig.obj.Velocity() == mMakeNote::MAX_VELOCITY);
    CHECK(rig.obj.Velocity() == 127);
    rig.Int(-5, 1);
    CHECK(rig.obj.Velocity() == 0);
  }

  TEST_CASE("makenote: a negative or NaN duration counts as 0 (#538)") {
    Rig rig("100 250");
    rig.Int(-50, 2);
    CHECK(rig.obj.Duration() == 0);
    rig.Int(250, 2);
    rig.Float(-12.5f, 2);
    CHECK(rig.obj.Duration() == 0);
  }

  TEST_CASE("makenote: a tempo-relative duration is refused rather than read as ms (#538)") {
    // The trap `.clocker` fell into before #725: `1440 ticks` taken for its
    // leading token becomes 1440 ms, which is not a note length anybody asked
    // for. This object has no clock to measure a beat against, so the honest
    // answer is to leave the duration where it was.
    Rig rig("100 250");
    rig.List("1440 ticks", 2);
    CHECK(rig.obj.Duration() == 250);
    rig.List("4nd", 2);
    CHECK(rig.obj.Duration() == 250);
    rig.List("8nt", 2);
    CHECK(rig.obj.Duration() == 250);
    // And neither is anything else it cannot read.
    rig.List("1.1.0", 2);
    rig.List("wibble", 2);
    CHECK(rig.obj.Duration() == 250);
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("makenote: 'stop' and 'clear' are commands in the left inlet only (#538)") {
    // Standalone there is nothing sounding to stop or clear, so what this pins
    // is that the two words are not data: they must not be played as pitches.
    Rig rig("100 250");
    rig.List("stop");
    rig.List("clear");
    CHECK(rig.out.n() == 0);

    // In the other inlets they are neither commands nor data — only a velocity
    // and a duration this object cannot read, which leave both alone.
    rig.List("stop", 1);
    rig.List("clear", 1);
    rig.List("stop", 2);
    rig.List("clear", 2);
    CHECK(rig.obj.Velocity() == 100);
    CHECK(rig.obj.Duration() == 250);
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("makenote: there is no bang method, as Max's makenote has none (#538)") {
    Rig rig("100 250");
    rig.obj.GetInlet(0)->SetBang(YSE::T_GUI);
    rig.obj.GetInlet(1)->SetBang(YSE::T_GUI);
    rig.obj.GetInlet(2)->SetBang(YSE::T_GUI);
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("makenote: a standalone object releases the note at once (#538)") {
    // No patcher means no scheduler and so no clock: "a duration later" has no
    // referent, and the alternatives are now or never. Never would leave a note
    // hanging, which is the one thing this object must not do.
    Rig rig("100 5000");
    rig.Int(60);
    rig.Int(62);
    CHECK(rig.out.trace() == "v100 p60 v0 p60 v100 p62 v0 p62");
    CHECK(rig.obj.Pending() == 0);
    CHECK(rig.obj.Dropped() == 0);
  }

  TEST_CASE("makenote: Calculate sends nothing (#538)") {
    // The object is driven by its inlets and by the scheduler; one that emitted
    // would play a note on every DSP tick from a stimulus no patch sent.
    Rig rig("100 250");
    for (int i = 0; i < 8; i++)
      rig.obj.Calculate(YSE::T_DSP);
    CHECK(rig.out.n() == 0);
  }

  // ─── what a save carries ────────────────────────────────────────────────────

  TEST_CASE("makenote: the parameters survive a DumpJSON / ParseJSON round trip (#538)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* obj = src.CreateObject(YSE::OBJ::M_MAKENOTE, "110 640");
    REQUIRE(obj != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".makenote") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".makenote");
    CHECK(std::string(copy->GetParams()) == "110 640");
  }

  // ─── the clock and the pending set, which need a real patcher ───────────────

  TEST_CASE("makenote: the release comes a duration later, not in the same dispatch (#538)") {
    // **The object.** An implementation that emitted the pair and scheduled
    // nothing would pass every standalone case above and strand every note it
    // ever played, so this is the case that has to fail on it.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* mk = p.CreateObject(YSE::OBJ::M_MAKENOTE, "100 100");
    REQUIRE(mk != nullptr);

    Notes out;
    YSE::pHandle outHandle(&out);
    p.Connect(mk, 0, &outHandle, 0);
    p.Connect(mk, 1, &outHandle, 1);

    const std::uint64_t due = messageScheduler::BlocksForMillis(100);
    REQUIRE(due > 1);

    mk->SetIntData(0, 60);
    CHECK(out.trace() == "v100 p60"); // the attack, and nothing else
    CHECK(p.Scheduler()->PendingCount() == 1);

    for (std::uint64_t block = 1; block < due; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.trace() == "v100 p60");

    p.Calculate(YSE::T_DSP);
    CHECK(out.trace() == "v100 p60 v0 p60");
    CHECK(p.Scheduler()->PendingCount() == 0);

    // And it stays released: one note in, one release out.
    for (std::uint64_t block = 0; block < due + 2; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.trace() == "v100 p60 v0 p60");
  }

  TEST_CASE("makenote: a duration of 0 releases on the next block (#538)") {
    // Max's default duration, and the scheduler's one-block floor read as
    // semantics rather than as rounding: it is what keeps a `.makenote` wired
    // back into its own inlet a fast trill instead of a stack overflow.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* mk = p.CreateObject(YSE::OBJ::M_MAKENOTE, "100");
    REQUIRE(mk != nullptr);

    Notes out;
    YSE::pHandle outHandle(&out);
    p.Connect(mk, 0, &outHandle, 0);
    p.Connect(mk, 1, &outHandle, 1);

    CHECK(messageScheduler::BlocksForMillis(0) == 1);

    mk->SetIntData(0, 60);
    CHECK(out.trace() == "v100 p60");
    CHECK(p.Scheduler()->PendingCount() == 1);

    p.Calculate(YSE::T_DSP);
    CHECK(out.trace() == "v100 p60 v0 p60");
  }

  TEST_CASE("makenote: notes are polyphonic and each keeps its own duration (#538)") {
    // Max's default repeatmode is poly: a repeated pitch is a second note with
    // its own deadline, not a retrigger of the first. And a duration set
    // underneath a sounding note does not retime it — the naive implementation
    // recomputes every deadline when the duration changes, and nothing about it
    // looks wrong until a held chord all releases together.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* mk = p.CreateObject(YSE::OBJ::M_MAKENOTE, "100 100");
    REQUIRE(mk != nullptr);

    Notes out;
    YSE::pHandle outHandle(&out);
    p.Connect(mk, 0, &outHandle, 0);
    p.Connect(mk, 1, &outHandle, 1);

    const std::uint64_t slow = messageScheduler::BlocksForMillis(100);
    const std::uint64_t quick = messageScheduler::BlocksForMillis(10);
    REQUIRE(quick < slow);

    mk->SetIntData(0, 60); // played at 100 ms
    mk->SetIntData(2, 10); // the duration inlet, after the fact
    mk->SetIntData(0, 60); // the *same* pitch, played at 10 ms
    CHECK(out.pitches() == std::vector<int>{60, 60});
    CHECK(p.Scheduler()->PendingCount() == 2);

    // The short note releases first, which is only possible because the two
    // deadlines are independent — and there are two notes at all only because
    // the repeat did not swallow the first.
    for (std::uint64_t block = 1; block < quick; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.n() == 4);

    p.Calculate(YSE::T_DSP);
    CHECK(out.pitches() == std::vector<int>{60, 60, 60});
    CHECK(p.Scheduler()->PendingCount() == 1);

    for (std::uint64_t block = quick + 1; block < slow; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.pitches() == std::vector<int>{60, 60, 60});

    p.Calculate(YSE::T_DSP);
    CHECK(out.pitches() == std::vector<int>{60, 60, 60, 60});
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

  TEST_CASE("makenote: 'stop' releases everything now, in the order played (#538)") {
    // Max: "stop: causes makenote to send out immediate note-offs for all
    // pitches it currently holds." Immediately means inside this dispatch, not
    // on the next block — a stop that merely rescheduled everything for 0 ms
    // would pass a count assertion and fail this one.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* mk = p.CreateObject(YSE::OBJ::M_MAKENOTE, "100 5000");
    REQUIRE(mk != nullptr);

    Notes out;
    YSE::pHandle outHandle(&out);
    p.Connect(mk, 0, &outHandle, 0);
    p.Connect(mk, 1, &outHandle, 1);

    mk->SetIntData(0, 60);
    mk->SetIntData(0, 64);
    mk->SetIntData(0, 67);
    REQUIRE(p.Scheduler()->PendingCount() == 3);
    out.Clear();

    mk->SetListData(0, "stop");
    CHECK(out.trace() == "v0 p60 v0 p64 v0 p67");
    CHECK(p.Scheduler()->PendingCount() == 0);

    // Nothing comes out a second time when the deadlines they were armed with
    // would have passed.
    for (std::uint64_t block = 0; block <= messageScheduler::BlocksForMillis(5000); ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.trace() == "v0 p60 v0 p64 v0 p67");
  }

  TEST_CASE("makenote: 'clear' forgets pending releases without sending them (#538)") {
    // Max: "clear: erases all notes currently held by makenote, without sending
    // note-offs." Every one of them, not just the newest — and the patcher-wide
    // budget comes back with them.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* mk = p.CreateObject(YSE::OBJ::M_MAKENOTE, "100 100");
    REQUIRE(mk != nullptr);

    Notes out;
    YSE::pHandle outHandle(&out);
    p.Connect(mk, 0, &outHandle, 0);
    p.Connect(mk, 1, &outHandle, 1);

    for (int pitch = 60; pitch < 64; pitch++)
      mk->SetIntData(0, pitch);
    REQUIRE(p.Scheduler()->PendingCount() == 4);
    out.Clear();

    mk->SetListData(0, "clear");
    CHECK(out.n() == 0);
    CHECK(p.Scheduler()->PendingCount() == 0);

    for (std::uint64_t block = 0; block <= messageScheduler::BlocksForMillis(100) + 2; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.n() == 0);
  }

  TEST_CASE("makenote: a full pending set refuses the whole note, attack included (#538)") {
    // The bound the issue asks for, and the answer to over-capacity that
    // matters: the *attack* is refused too. An implementation that played the
    // note and merely failed to schedule its release would manufacture exactly
    // the hanging note this object exists to prevent, at precisely the moment
    // the patch is at its resource limit.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* mk = p.CreateObject(YSE::OBJ::M_MAKENOTE, "100 5000");
    REQUIRE(mk != nullptr);

    Notes out;
    YSE::pHandle outHandle(&out);
    p.Connect(mk, 0, &outHandle, 0);
    p.Connect(mk, 1, &outHandle, 1);

    const int surplus = 20;
    const int offered = (int)mMakeNote::CAPACITY + surplus;
    for (int i = 0; i < offered; i++)
      mk->SetIntData(0, i);

    // Exactly the capacity was played, and they are the first ones offered.
    const std::vector<int> played = out.pitches();
    REQUIRE(played.size() == mMakeNote::CAPACITY);
    CHECK(played.front() == 0);
    CHECK(played.back() == (int)mMakeNote::CAPACITY - 1);
    CHECK(p.Scheduler()->PendingCount() == mMakeNote::CAPACITY);

    // The surplus produced no output at all — not an attack, not an early
    // release. It is counted on `Dropped()` rather than logged, the refusing
    // thread being routinely the audio callback.
    CHECK(out.n() == mMakeNote::CAPACITY * 2); // one attack pair each, no more

    // And every note that *was* played is released.
    for (std::uint64_t block = 0; block <= messageScheduler::BlocksForMillis(5000); ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.pitches().size() == mMakeNote::CAPACITY * 2);
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

  TEST_CASE("makenote: a full patcher-wide budget releases the note at once (#538)") {
    // The other overflow, and it is answered differently on purpose. This one is
    // only discovered *after* the attack has gone out — a slot has to be
    // published before its message can be armed — so the release cannot be
    // dropped with the note. It is sent immediately instead: a note shorter than
    // it was asked to be, rather than one that never ends.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* filler = p.CreateObject(YSE::OBJ::M_MAKENOTE, "100 5000");
    YSE::pHandle* filler2 = p.CreateObject(YSE::OBJ::M_MAKENOTE, "100 5000");
    YSE::pHandle* mk = p.CreateObject(YSE::OBJ::M_MAKENOTE, "100 5000");
    REQUIRE(filler != nullptr);
    REQUIRE(filler2 != nullptr);
    REQUIRE(mk != nullptr);

    Notes out;
    YSE::pHandle outHandle(&out);
    p.Connect(mk, 0, &outHandle, 0);
    p.Connect(mk, 1, &outHandle, 1);

    // Two objects at capacity fill the patcher-wide pending set, so the third
    // has free slots of its own and no budget to arm them with.
    REQUIRE(mMakeNote::CAPACITY * 2 >= messageScheduler::CAPACITY);
    for (std::size_t i = 0; i < mMakeNote::CAPACITY; i++) {
      filler->SetIntData(0, 60);
      filler2->SetIntData(0, 62);
    }
    REQUIRE(p.Scheduler()->PendingCount() == messageScheduler::CAPACITY);

    mk->SetIntData(0, 72);
    CHECK(out.trace() == "v100 p72 v0 p72"); // played, and released at once
  }

  TEST_CASE("makenote: a paused patcher holds its pending releases where they stand (#538)") {
    // The clock is the patcher's block counter, so time only advances while the
    // patcher renders. That is what makes a held note survive a paused engine
    // instead of being released in a burst afterwards.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* mk = p.CreateObject(YSE::OBJ::M_MAKENOTE, "100 100");
    REQUIRE(mk != nullptr);

    Notes out;
    YSE::pHandle outHandle(&out);
    p.Connect(mk, 0, &outHandle, 0);
    p.Connect(mk, 1, &outHandle, 1);

    for (int pitch = 60; pitch < 63; pitch++)
      mk->SetIntData(0, pitch);
    CHECK(p.Scheduler()->PendingCount() == 3);
    CHECK(out.pitches() == std::vector<int>{60, 61, 62});

    for (std::uint64_t block = 0; block < messageScheduler::BlocksForMillis(100); ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(out.pitches() == std::vector<int>{60, 61, 62, 60, 61, 62});
  }

  // ─── end to end, in a real patcher ──────────────────────────────────────────

  TEST_CASE("makenote: the rig can see a hanging note (#538)") {
    // The control for the case below, and it is not optional: a test that only
    // ever asserts "nothing is sounding" would pass just as happily against a
    // chain that never sounded anything at all. Here the notes are played and
    // the clock is *not* advanced, so the releases are still pending — and
    // `.midiflush`, which decodes the byte stream and remembers exactly what is
    // sounding, sees three hanging notes.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* mk = p.CreateObject(YSE::OBJ::M_MAKENOTE, "100 100");
    YSE::pHandle* noteon = p.CreateObject(YSE::OBJ::M_NOTEON, "");
    YSE::pHandle* flush = p.CreateObject(YSE::OBJ::M_MIDIFLUSH, "");
    REQUIRE(mk != nullptr);
    REQUIRE(noteon != nullptr);
    REQUIRE(flush != nullptr);

    p.Connect(mk, 1, noteon, 1); // velocity, the cold inlet
    p.Connect(mk, 0, noteon, 0); // pitch, the hot one — fires the message
    p.Connect(noteon, 0, flush, 0);

    Tap tap;
    YSE::pHandle tapHandle(&tap);
    p.Connect(flush, 0, &tapHandle, 0);

    mk->SetIntData(0, 60);
    mk->SetIntData(0, 64);
    mk->SetIntData(0, 67);
    CHECK(tap.seen == 3); // three note-ons through the stream

    // Nothing has released them yet, so the flush has three to send.
    flush->SetBang(0);
    CHECK(tap.seen == 6);
  }

  TEST_CASE("makenote: a played note leaves nothing hanging, end to end (#538)") {
    // The issue's claim at the level a patch makes it: a real `.makenote`
    // driving a real `.noteon` into a real `.midiflush` through real cords. The
    // notes play, the patcher renders past their duration, and the object that
    // exists to find stranded notes finds none — which is the whole point of
    // `.makenote` and the one thing no standalone case can show.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* mk = p.CreateObject(YSE::OBJ::M_MAKENOTE, "100 100");
    YSE::pHandle* noteon = p.CreateObject(YSE::OBJ::M_NOTEON, "");
    YSE::pHandle* flush = p.CreateObject(YSE::OBJ::M_MIDIFLUSH, "");
    REQUIRE(mk != nullptr);
    REQUIRE(noteon != nullptr);
    REQUIRE(flush != nullptr);

    p.Connect(mk, 1, noteon, 1);
    p.Connect(mk, 0, noteon, 0);
    p.Connect(noteon, 0, flush, 0);

    Tap tap;
    YSE::pHandle tapHandle(&tap);
    p.Connect(flush, 0, &tapHandle, 0);

    mk->SetIntData(0, 60);
    mk->SetIntData(0, 64);
    mk->SetIntData(0, 67);
    CHECK(tap.seen == 3);

    // The patcher renders past the duration. The releases are armed on its block
    // counter, so this is the only thing that makes them fall due — and they are
    // delivered on the audio thread, inside `Calculate`.
    for (std::uint64_t block = 0; block <= messageScheduler::BlocksForMillis(100); ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(tap.seen == 6); // three releases went through the stream

    // And the judge agrees: nothing is left sounding, so a flush sends nothing.
    flush->SetBang(0);
    CHECK(tap.seen == 6);
  }

  // ─── real-time discipline ───────────────────────────────────────────────────

  TEST_CASE("makenote: the message paths allocate nothing (#538)") {
    // Everything a message can do on the way in, measured on the thread that
    // does it. A note routinely arrives on the audio callback — an in-patcher
    // dispatch runs on T_DSP — and the arm path is precisely the code that must
    // not allocate or lock there.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    patcherImplementation p(1, nullptr);
    YSE::pHandle* mk = p.CreateObject(YSE::OBJ::M_MAKENOTE, "100 5000");
    REQUIRE(mk != nullptr);

    Notes out;
    YSE::pHandle outHandle(&out);
    p.Connect(mk, 0, &outHandle, 0);
    p.Connect(mk, 1, &outHandle, 1);

    // Built outside the probe: it is the *handler* that must not allocate, not
    // the test's own construction of the message.
    const std::string pair = "64 90";
    const std::string one = "67";
    const std::string ticks = "1440 ticks";
    const std::string ms = "250";
    const std::string vel = "77";
    const std::string stop = "stop";
    {
      TestHelpers::ProbeScope probe;
      mk->SetIntData(0, 60);
      mk->SetFloatData(0, 62.5f);
      mk->SetListData(0, pair);
      mk->SetListData(0, one);
      mk->SetIntData(1, 80);
      mk->SetFloatData(1, 90.f);
      mk->SetListData(1, vel);
      mk->SetIntData(2, 500);
      mk->SetFloatData(2, 125.f);
      mk->SetListData(2, ticks);
      mk->SetListData(2, ms);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    // The probed messages really did something — an assertion that only proves
    // nothing happened proves nothing.
    REQUIRE(p.Scheduler()->PendingCount() == 4);

    // `stop` walks the whole slot table, cancels four scheduler messages and
    // sends four releases, which is the other path that must stay
    // allocation-free.
    {
      TestHelpers::ProbeScope probe;
      mk->SetListData(0, stop);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(p.Scheduler()->PendingCount() == 0);
  }
}
