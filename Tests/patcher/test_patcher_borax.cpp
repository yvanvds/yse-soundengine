// Tests for `.borax` — Max's borax, his "swiss army knife for music analysis",
// which reports note-on and note-off statistics over a note stream (issue #543).
//
// The behaviours worth testing hardest are the ones a wrong analyser gets wrong
// silently, because every one of them still produces a plausible-looking number:
//
//   - **the note serial goes back down.** Max's leftmost outlet is the *note's*
//     number, not a running total: the number a note-on was given comes out
//     again at that note's release. An implementation that re-sent the running
//     count would pass "a number came out" while making it impossible for
//     anything downstream to pair an attack with its release — which is the
//     whole reason a duration is reported beside it.
//   - **which outlets stay silent.** The duration pair fires only on a note-off
//     and the delta pair only on an attack that had a predecessor. An object
//     that refreshed all nine every time would repeat stale figures, and a patch
//     reading "the same delta again" as a new attack would hear a tempo that
//     never happened.
//   - **the durations are real.** They are measured against the engine's
//     monotonic clock, so the cases that claim a duration sleep for a known time
//     and check the figure against it. A test that only asserted "a float
//     arrived" would pass against an object that always sent 0.
//
// Layers, and they are not interchangeable:
//
//   - **standalone cases** pin the grammar and the shape: what each inlet
//     accepts, that a list is Max's inlet distribution, that the middle inlet
//     stores without emitting, and the right-to-left outlet order.
//
//   - **the voice table and the counters**, which are the object: allocation,
//     serials, polyphony, repeats, the full table, durations, deltas and reset.
//
//   - **end-to-end cases** run a real chain inside a real patcher — a real
//     `.midiparse` decoding real MIDI bytes into a registry-built `.borax` —
//     and ask the question a patch asks: what did that passage do?
//
//   - **a teardown case** (issue #758) pins the deliberate *absence* of a
//     teardown release, which is a decision rather than an omission.
//
// No audio device and no MIDI hardware required.

#include <doctest/doctest.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "patcher/inlet.h"
#include "patcher/midi/mBorax.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using YSE::PATCHER::mBorax;
using YSE::PATCHER::patcherImplementation;
using YSE::PATCHER::Register;

using namespace std::chrono_literals;

namespace {

  // Records all nine outlets into one ordered log, which is the only way to see
  // the two halves of this object that live in *which* outlets fire and in what
  // order: the note serial is the value a patch keys on, so it has to arrive
  // last, and the duration and delta pairs are news precisely because they are
  // not sent every time.
  //
  // Events are a fixed-size struct in a reserved vector rather than strings, so
  // the allocation probe measures the object under test and not this sink.
  struct Notes : YSE::PATCHER::pObject {
    struct Event {
      // 's' serial, 'n' voice, 'y' poly, 'p' pitch, 'v' velocity,
      // 'c' note-off count, 'd' duration, 'k' delta count, 't' delta.
      char kind = 's';
      int value = 0;
      float ms = 0.f;
      bool isMs = false;
    };

    std::vector<Event> events;

    Notes() : pObject(false) {
      events.reserve(8192);

      Add(0, true, 's');
      Add(1, false, 'n');
      Add(2, false, 'y');
      Add(3, false, 'p');
      Add(4, false, 'v');
      Add(5, false, 'c');
      AddMs(6, 'd');
      Add(7, false, 'k');
      AddMs(8, 't');
    }
    const char* Type() const override {
      return "borax_notes";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    void Add(int index, bool hot, char kind) {
      inputs.emplace_back(this, hot, index);
      inputs.back().RegisterInt([this, kind](int v, int, YSE::THREAD) { Push(kind, v); });
    }
    void AddMs(int index, char kind) {
      inputs.emplace_back(this, false, index);
      inputs.back().RegisterFloat([this, kind](float v, int, YSE::THREAD) {
        Event e;
        e.kind = kind;
        e.ms = v;
        e.isMs = true;
        events.push_back(e);
      });
    }

    void Push(char kind, int value) {
      Event e;
      e.kind = kind;
      e.value = value;
      events.push_back(e);
    }

    std::size_t n() const {
      return events.size();
    }
    void Clear() {
      events.clear();
    }

    // "v100 p60 y1 n1 s1" — the whole exchange in the order it happened. A
    // millisecond outlet contributes its letter alone: the *value* is a real
    // measurement and is asserted by the cases that care, but whether and when
    // it fired belongs in the order.
    std::string trace() const {
      std::string out;
      for (const Event& e : events) {
        if (!out.empty()) out.push_back(' ');
        out.push_back(e.kind);
        if (!e.isMs) out += std::to_string(e.value);
      }
      return out;
    }

    // Every millisecond figure that came out of one outlet, in order.
    std::vector<float> msOn(char kind) const {
      std::vector<float> out;
      for (const Event& e : events)
        if (e.isMs && e.kind == kind) out.push_back(e.ms);
      return out;
    }
  };

  // Records whatever arrives, for the end-to-end chains where the point is what
  // reached a patch rather than the exact outlet order. Deliberately not owned
  // by the patcher.
  struct Tap : YSE::PATCHER::pObject {
    std::vector<std::string> log;

    Tap() : pObject(false) {
      log.reserve(64);
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt(
          [this](int v, int, YSE::THREAD) { log.push_back("i" + std::to_string(v)); });
      inputs.back().RegisterFloat([this](float, int, YSE::THREAD) { log.push_back("f"); });
    }
    const char* Type() const override {
      return "borax_tap";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    std::string trace() const {
      std::string out;
      for (const std::string& entry : log) {
        if (!out.empty()) out.push_back(' ');
        out += entry;
      }
      return out;
    }
  };

  using TestHelpers::Wire;

  // A standalone `.borax` with all nine outlets watched. The object needs no
  // clock binding and no patcher, so standalone is the whole of its message
  // behaviour.
  //
  // The sink is declared **before** the object so it is destroyed after it —
  // see Wire on why that matters even for a symmetric edge.
  struct Rig {
    Notes out;
    mBorax obj;

    Rig() {
      for (int i = 0; i < 9; i++)
        Wire(obj, i, out, i);
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
    void Bang(int inlet = 2) {
      obj.GetInlet(inlet)->SetBang(YSE::T_GUI);
    }
    // Play a note the way a patch does: velocity first, then pitch.
    void Note(int pitch, int noteVelocity) {
      Int(noteVelocity, 1);
      Int(pitch, 0);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── the object exists ──────────────────────────────────────────────────────

  TEST_CASE("borax: creatable through the registry (#543)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::M_BORAX);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".borax");
  }

  TEST_CASE("borax: listed by pRegistry::AllNames (#543)") {
    auto names = Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".borax")) != names.end());
  }

  TEST_CASE("borax: the shape is three inlets and Max's nine outlets (#543)") {
    // Seven counts and two measurements. The two millisecond outlets are floats
    // rather than Max's ints: a grace note and the next note of a rolled chord
    // can be less than a millisecond apart, and truncating would throw that away.
    mBorax obj;
    CHECK(obj.NumInputs() == 3);
    CHECK(obj.NumOutputs() == 9);
    CHECK(obj.GetOutputType(0) == YSE::OUT_TYPE::INT); // serial
    CHECK(obj.GetOutputType(1) == YSE::OUT_TYPE::INT); // voice
    CHECK(obj.GetOutputType(2) == YSE::OUT_TYPE::INT); // polyphony
    CHECK(obj.GetOutputType(3) == YSE::OUT_TYPE::INT); // pitch
    CHECK(obj.GetOutputType(4) == YSE::OUT_TYPE::INT); // velocity
    CHECK(obj.GetOutputType(5) == YSE::OUT_TYPE::INT); // note-off count
    CHECK(obj.GetOutputType(6) == YSE::OUT_TYPE::FLOAT); // duration
    CHECK(obj.GetOutputType(7) == YSE::OUT_TYPE::INT); // delta count
    CHECK(obj.GetOutputType(8) == YSE::OUT_TYPE::FLOAT); // delta
    CHECK(obj.GetCategory() == YSE::PATCHER::pCategory::MIDI);
    CHECK_FALSE(obj.IsDSPObject());
    CHECK_FALSE(obj.WantsBlockPoll());
  }

  TEST_CASE("borax: documents itself, and has no creation arguments (#543)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::M_BORAX));
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == std::string(YSE::OBJ::M_BORAX));
    CHECK_FALSE(obj->GetDescription().empty());
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MIDI);
    for (int i = 0; i < obj->NumInputs(); i++)
      CHECK_FALSE(obj->GetInlet(i)->GetDocLabel().empty());
    for (int i = 0; i < obj->NumOutputs(); i++)
      CHECK_FALSE(obj->GetOutlet(i)->GetDocLabel().empty());
    // Max's borax takes none, and there is nothing here to configure: the table
    // is the MIDI note range and the clock is the engine's.
    CHECK(obj->GetParamDocs().empty());
  }

  TEST_CASE("borax: a fresh object has counted nothing (#543)") {
    Rig rig;
    CHECK(rig.obj.NoteCount() == 0u);
    CHECK(rig.obj.NoteOffCount() == 0u);
    CHECK(rig.obj.DeltaCount() == 0u);
    CHECK(rig.obj.Poly() == 0);
    CHECK(rig.obj.Velocity() == 0);
    CHECK(rig.obj.PitchOfVoice(1) == -1);
    CHECK(rig.obj.LastDurationMs() == 0.0);
    CHECK(rig.obj.LastDeltaMs() == 0.0);
  }

  // ─── a note-on ──────────────────────────────────────────────────────────────

  TEST_CASE("borax: the first note-on reports five outlets, right to left (#543)") {
    // The serial is the value a patch keys on, so it goes to a hot inlet and has
    // to arrive last or it would carry the previous note's figures with it. And
    // the first attack reports no delta at all: there is no interval between one
    // event and no event, which is `.timer`'s rule and for `.timer`'s reason.
    Rig rig;
    rig.Note(60, 100);
    CHECK(rig.out.trace() == "v100 p60 y1 n1 s1");
    CHECK(rig.obj.NoteCount() == 1u);
    CHECK(rig.obj.Poly() == 1);
    CHECK(rig.obj.DeltaCount() == 0u);
    CHECK(rig.obj.PitchOfVoice(1) == 60);
  }

  TEST_CASE("borax: voices are the lowest free number, from 1 (#543)") {
    // Max: "each note is also assigned a unique voice number, equal to the
    // lowest available number", which is `.poly`'s free-voice rule. Numbered
    // from 1 like `.poly`'s, so the two objects watching one stream agree.
    Rig rig;
    rig.Note(60, 100);
    rig.Note(64, 100);
    rig.Note(67, 100);
    CHECK(rig.obj.Poly() == 3);
    CHECK(rig.obj.PitchOfVoice(1) == 60);
    CHECK(rig.obj.PitchOfVoice(2) == 64);
    CHECK(rig.obj.PitchOfVoice(3) == 67);

    rig.Note(64, 0); // voice 2 comes free
    REQUIRE(rig.obj.Poly() == 2);

    rig.out.Clear();
    rig.Note(72, 100);
    CHECK(rig.out.trace() == "t k3 v100 p72 y3 n2 s4"); // the lowest free voice
    CHECK(rig.obj.PitchOfVoice(2) == 72);
  }

  TEST_CASE("borax: the polyphony count rises and falls with the notes held (#543)") {
    // The figure the object mostly exists to make a patch able to ask for: 0 is
    // "the passage has ended" and a rising number is a thickening texture.
    Rig rig;
    rig.Int(100, 1);
    rig.Int(60);
    rig.Int(64);
    rig.Int(67);
    CHECK(rig.obj.Poly() == 3);

    rig.Int(0, 1);
    rig.Int(64);
    CHECK(rig.obj.Poly() == 2);
    rig.Int(60);
    rig.Int(67);
    CHECK(rig.obj.Poly() == 0);
  }

  // ─── the serial, which is the note's and not a running total ────────────────

  TEST_CASE("borax: a note-off reports the serial its note-on was given (#543)") {
    // Max: "that number is sent out when the note-on is received, and the same
    // number is sent out when the note is turned off." An implementation that
    // re-sent the running count would look right in every single-note test and
    // make it impossible to pair an attack with its release — which is the whole
    // reason the duration is worth reporting beside it.
    Rig rig;
    rig.Note(60, 100); // serial 1, voice 1
    rig.Note(64, 100); // serial 2, voice 2
    rig.Note(67, 100); // serial 3, voice 3
    REQUIRE(rig.obj.NoteCount() == 3u);

    rig.out.Clear();
    rig.Note(60, 0); // the *first* note ends
    CHECK(rig.out.trace() == "d c1 v0 p60 y2 n1 s1"); // serial 1, not 3 and not 4
    CHECK(rig.obj.NoteOffCount() == 1u);
    CHECK(rig.obj.NoteCount() == 3u); // and a release is not a new note

    rig.out.Clear();
    rig.Note(67, 0);
    CHECK(rig.out.trace() == "d c2 v0 p67 y1 n3 s3");
  }

  TEST_CASE("borax: a repeated pitch takes a second voice, released oldest first (#543)") {
    // `.poly`'s answer and Max's default: collapsing the two would leave the
    // second attack with no note of its own to be measured. The releases pair
    // oldest-first, so the first release reports the first attack's serial and
    // its duration.
    Rig rig;
    rig.Note(60, 100); // serial 1, voice 1
    rig.Note(60, 90); // serial 2, voice 2
    CHECK(rig.obj.Poly() == 2);

    rig.out.Clear();
    rig.Note(60, 0);
    CHECK(rig.out.trace() == "d c1 v0 p60 y1 n1 s1"); // the one played first

    rig.out.Clear();
    rig.Note(60, 0);
    CHECK(rig.out.trace() == "d c2 v0 p60 y0 n2 s2");
    CHECK(rig.obj.Poly() == 0);
  }

  // ─── what is ignored, and it is ignored silently ────────────────────────────

  TEST_CASE("borax: a note-off for a pitch no voice holds reports nothing (#543)") {
    // There is no attack to measure it from, and a duration from nothing would
    // be worse than none. Silence is how a patch reads "this told me nothing".
    Rig rig;
    rig.Note(60, 100);
    REQUIRE(rig.obj.Poly() == 1);

    rig.out.Clear();
    rig.Note(72, 0);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Poly() == 1); // and nothing was closed off by accident
    CHECK(rig.obj.NoteOffCount() == 0u);
  }

  TEST_CASE("borax: a note-on with the table full is ignored, not stolen (#543)") {
    // Stealing is an allocation policy — it exists so a *sounding* voice can be
    // reused — and this object sounds nothing, so a 129th note is dropped rather
    // than displacing one it is measuring.
    Rig rig;
    rig.Int(100, 1);
    for (int i = 0; i < mBorax::MAX_VOICES; i++)
      rig.Int(1000 + i);
    REQUIRE(rig.obj.Poly() == mBorax::MAX_VOICES);
    REQUIRE(rig.obj.NoteCount() == (std::uint64_t)mBorax::MAX_VOICES);

    rig.out.Clear();
    rig.Int(2000);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Poly() == mBorax::MAX_VOICES);
    CHECK(rig.obj.NoteCount() == (std::uint64_t)mBorax::MAX_VOICES); // not counted either
    CHECK(rig.obj.PitchOfVoice(mBorax::MAX_VOICES) == 1000 + mBorax::MAX_VOICES - 1);
  }

  TEST_CASE("borax: any pitch at all is measured — this is not a bitmap (#543)") {
    // The table holds pitches rather than being a bitmap over the MIDI note
    // range, `.poly`'s arrangement, so a patch driving a non-MIDI synth through
    // wider values is measured like any other.
    Rig rig;
    rig.Note(5000, 100);
    rig.Note(-42, 100);
    CHECK(rig.obj.PitchOfVoice(1) == 5000);
    CHECK(rig.obj.PitchOfVoice(2) == -42);

    rig.out.Clear();
    rig.Note(-42, 0);
    CHECK(rig.out.trace() == "d c1 v0 p-42 y1 n2 s2");
  }

  // ─── the durations, which are measured rather than counted ──────────────────

  TEST_CASE("borax: the duration is the real time the note was held (#543)") {
    // The claim the object exists to make, and the one a test that merely
    // asserted "a float arrived" would pass against an object always sending 0.
    // The clock is `.timer`'s — issue #543 asks for exactly that — so the figure
    // never disagrees with a `.timer` beside it.
    Rig rig;
    rig.Note(60, 100);
    std::this_thread::sleep_for(40ms);

    rig.out.Clear();
    rig.Note(60, 0);

    const std::vector<float> durations = rig.out.msOn('d');
    REQUIRE(durations.size() == 1);
    CHECK(durations[0] >= 30.f);
    CHECK(durations[0] < 5000.f); // a loaded runner oversleeps, but not by seconds
    CHECK(rig.obj.LastDurationMs() == doctest::Approx((double)durations[0]).epsilon(0.01));
  }

  TEST_CASE("borax: each note is measured from its own attack (#543)") {
    // Polyphony is the whole difference between this and `.timer`: two notes
    // overlapping are two independent measurements, each against its own onset,
    // and an object holding one baseline would report the same figure twice.
    Rig rig;
    rig.Note(60, 100);
    std::this_thread::sleep_for(50ms);
    rig.Note(64, 100); // starts half a beat later

    rig.out.Clear();
    rig.Note(60, 0);
    rig.Note(64, 0);

    const std::vector<float> durations = rig.out.msOn('d');
    REQUIRE(durations.size() == 2);
    CHECK(durations[0] >= 40.f);
    CHECK(durations[1] < durations[0]); // the later attack was held for less
  }

  TEST_CASE("borax: the duration outlet is silent on a note-on (#543)") {
    // Max's selectivity, and it is information rather than an optimisation: an
    // object that refreshed all nine outlets every time would repeat the
    // previous note's duration at every attack, and nothing downstream could
    // tell that apart from a note that really had just ended.
    Rig rig;
    rig.Note(60, 100);
    rig.Note(60, 0);
    REQUIRE(rig.out.msOn('d').size() == 1);

    rig.out.Clear();
    rig.Note(64, 100);
    CHECK(rig.out.msOn('d').empty());
    CHECK(rig.out.trace() == "t k1 v100 p64 y1 n1 s2");
  }

  // ─── the deltas ─────────────────────────────────────────────────────────────

  TEST_CASE("borax: the delta is the time between successive attacks (#543)") {
    // How fast the passage is being played, which is the figure a patch reads to
    // play denser when the performer does.
    Rig rig;
    rig.Note(60, 100);
    std::this_thread::sleep_for(40ms);
    rig.out.Clear();
    rig.Note(64, 100);

    const std::vector<float> deltas = rig.out.msOn('t');
    REQUIRE(deltas.size() == 1);
    CHECK(deltas[0] >= 30.f);
    CHECK(rig.obj.DeltaCount() == 1u);
    // Right to left: the delta and its count go out before everything else.
    CHECK(rig.out.trace() == "t k1 v100 p64 y2 n2 s2");
  }

  TEST_CASE("borax: the delta is measured between attacks, not from the last message (#543)") {
    // A note-off in between must not move the baseline: the delta is about how
    // fast notes *start*, and an implementation that timestamped every message
    // would report the gap since the release instead.
    Rig rig;
    rig.Note(60, 100);
    std::this_thread::sleep_for(40ms);
    rig.Note(60, 0); // a release, which is not an attack
    std::this_thread::sleep_for(40ms);

    rig.out.Clear();
    rig.Note(64, 100);
    const std::vector<float> deltas = rig.out.msOn('t');
    REQUIRE(deltas.size() == 1);
    CHECK(deltas[0] >= 65.f); // both sleeps, not just the second
  }

  TEST_CASE("borax: the 'delta' message takes a reading on demand (#543)") {
    // Max: "causes the delta time (the time elapsed since the last note-on) and
    // the delta count to be sent out." This is how a patch asks how long it has
    // been quiet, and it counts as a delta report, so the count climbs.
    Rig rig;
    rig.Note(60, 100);
    std::this_thread::sleep_for(40ms);

    rig.out.Clear();
    rig.List("delta");
    CHECK(rig.out.trace() == "t k1"); // those two outlets and nothing else
    const std::vector<float> deltas = rig.out.msOn('t');
    REQUIRE(deltas.size() == 1);
    CHECK(deltas[0] >= 30.f);
    CHECK(rig.obj.DeltaCount() == 1u);
    CHECK(rig.obj.Poly() == 1); // and it disturbed nothing

    // It really is "right now", not a frozen figure.
    std::this_thread::sleep_for(40ms);
    rig.out.Clear();
    rig.List("delta");
    const std::vector<float> again = rig.out.msOn('t');
    REQUIRE(again.size() == 1);
    CHECK(again[0] > deltas[0]);
    CHECK(rig.obj.DeltaCount() == 2u);
  }

  TEST_CASE("borax: neither an attack nor 'delta' reports a delta before the first note (#543)") {
    // There is no interval between one event and no event. A 0 here would be
    // indistinguishable downstream from two attacks that really did land
    // together — `.timer`'s rule, and this family's.
    Rig rig;
    rig.List("delta");
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.DeltaCount() == 0u);

    rig.Note(60, 100);
    CHECK(rig.out.msOn('t').empty()); // the first attack has no predecessor
    CHECK(rig.obj.DeltaCount() == 0u);
  }

  TEST_CASE("borax: 'delta' in the other inlets is not a command (#543)") {
    // Max's message is left-inlet only. The middle inlet takes numbers and the
    // right one takes a bang, so a word in either is neither.
    Rig rig;
    rig.Note(60, 100);
    rig.out.Clear();

    rig.List("delta", 1);
    rig.List("delta", 2);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.DeltaCount() == 0u);
    CHECK(rig.obj.Velocity() == 100);
  }

  // ─── the reset ──────────────────────────────────────────────────────────────

  TEST_CASE("borax: a bang reports every held note off, then zeroes the counters (#543)") {
    // Max: "resets borax by sending note-offs for all notes currently being
    // held, erasing the borax object's memory of all notes received, and setting
    // its counters and its clock to 0." The note-offs are full reports —
    // durations included, those being the one thing this object measured that
    // nothing else could reconstruct — and the polyphony count drains across
    // them exactly as it would had the releases arrived one at a time.
    Rig rig;
    rig.Note(60, 100); // serial 1, voice 1
    rig.Note(67, 90); // serial 2, voice 2
    REQUIRE(rig.obj.Poly() == 2);

    rig.out.Clear();
    rig.Bang();
    CHECK(rig.out.trace() == "d c1 v0 p60 y1 n1 s1 d c2 v0 p67 y0 n2 s2");
    CHECK(rig.out.msOn('d').size() == 2);

    CHECK(rig.obj.Poly() == 0);
    CHECK(rig.obj.NoteCount() == 0u);
    CHECK(rig.obj.NoteOffCount() == 0u);
    CHECK(rig.obj.DeltaCount() == 0u);
    CHECK(rig.obj.PitchOfVoice(1) == -1);
  }

  TEST_CASE("borax: a reset with nothing held sends nothing but still zeroes (#543)") {
    // The half of the claim that keeps a bang from being a burst of spurious
    // reports after a properly balanced phrase.
    Rig rig;
    rig.Note(60, 100);
    rig.Note(60, 0);
    REQUIRE(rig.obj.NoteCount() == 1u);
    REQUIRE(rig.obj.NoteOffCount() == 1u);

    rig.out.Clear();
    rig.Bang();
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.NoteCount() == 0u);
    CHECK(rig.obj.NoteOffCount() == 0u);
  }

  TEST_CASE("borax: a reset sets the clock to 0, so the next attack has no delta (#543)") {
    // Max's "setting its counters and its clock to 0". Reporting a delta across
    // a reset would measure an interval whose first event the object has just
    // been told to forget.
    Rig rig;
    rig.Note(60, 100);
    rig.Note(64, 100);
    REQUIRE(rig.obj.DeltaCount() == 1u);

    rig.Bang();
    rig.out.Clear();
    rig.Note(67, 100);
    CHECK(rig.out.msOn('t').empty());
    CHECK(rig.out.trace() == "v100 p67 y1 n1 s1"); // counting from 1 again
  }

  TEST_CASE("borax: notes played after a reset are measured from scratch (#543)") {
    Rig rig;
    rig.Note(60, 100);
    rig.Note(64, 100);
    rig.Bang();
    REQUIRE(rig.obj.Poly() == 0);

    rig.out.Clear();
    rig.Note(72, 80);
    CHECK(rig.out.trace() == "v80 p72 y1 n1 s1");

    rig.out.Clear();
    rig.Note(72, 0);
    CHECK(rig.out.trace() == "d c1 v0 p72 y0 n1 s1");
  }

  TEST_CASE("borax: a bang in the left or middle inlet does nothing (#543)") {
    // Max's bang is the right inlet's. One that reset from the note inlet would
    // wipe the analysis in every patch that banged a note source.
    Rig rig;
    rig.Note(60, 100);
    rig.out.Clear();

    rig.Bang(0);
    rig.Bang(1);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Poly() == 1);
    CHECK(rig.obj.NoteCount() == 1u);
  }

  TEST_CASE("borax: a number in the reset inlet is not a reset (#543)") {
    // The inlet takes a bang and nothing else, so nothing else is registered: a
    // number there is neither a pitch nor a velocity nor a reset by accident.
    Rig rig;
    rig.Note(60, 100);
    rig.out.Clear();

    rig.Int(0, 2);
    rig.Float(1.f, 2);
    rig.List("1", 2);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Poly() == 1);
    CHECK(rig.obj.NoteCount() == 1u);
  }

  // ─── the grammar ────────────────────────────────────────────────────────────

  TEST_CASE("borax: the middle inlet stores without emitting (#543)") {
    // Only the pitch half completes a note, so only the pitch half can start or
    // end a measurement. A middle inlet that emitted would turn every velocity
    // change into a phantom note.
    Rig rig;
    rig.Int(100, 1);
    rig.Float(70.9f, 1);
    rig.List("55", 1);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.NoteCount() == 0u);
    CHECK(rig.obj.Velocity() == 55);

    rig.Int(60);
    CHECK(rig.out.trace() == "v55 p60 y1 n1 s1");
  }

  TEST_CASE("borax: a list is Max's inlet distribution — pitch then velocity (#543)") {
    // The velocity is stored *before* the pitch is read against it, exactly as
    // if it had reached the middle inlet first. A list that read the pitch first
    // would measure the very first note against the wrong velocity — and with
    // velocity 0 that is not a wrong number, it is a release instead of a note.
    Rig rig;
    rig.List("60 100");
    CHECK(rig.out.trace() == "v100 p60 y1 n1 s1");

    // And it persists, as a middle-inlet value would.
    rig.out.Clear();
    rig.List("62");
    CHECK(rig.out.trace() == "t k1 v100 p62 y2 n2 s2");

    // A release spelled as a list closes off the right note.
    rig.out.Clear();
    rig.List("60 0");
    CHECK(rig.out.trace() == "d c1 v0 p60 y1 n1 s1");
    CHECK(rig.obj.Velocity() == 0);

    // Further elements are ignored.
    rig.out.Clear();
    rig.List("64 90 7 7");
    CHECK(rig.out.trace() == "t k2 v90 p64 y2 n1 s3");
  }

  TEST_CASE("borax: anything that is not a number and not 'delta' does nothing (#543)") {
    Rig rig;
    rig.Int(100, 1);
    rig.List("60");
    REQUIRE(rig.obj.Poly() == 1);

    rig.out.Clear();
    rig.List("wibble");
    rig.List("");
    rig.List("wibble", 1);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Velocity() == 100);
    CHECK(rig.obj.Poly() == 1);
  }

  TEST_CASE("borax: a float is converted to an int in both note inlets (#543)") {
    Rig rig;
    rig.Float(100.7f, 1);
    CHECK(rig.obj.Velocity() == 100);
    rig.Float(60.9f);
    CHECK(rig.out.trace() == "v100 p60 y1 n1 s1");
    CHECK(rig.obj.PitchOfVoice(1) == 60);
  }

  TEST_CASE("borax: Calculate sends nothing (#543)") {
    // The object is driven entirely by its inlets; one that emitted would report
    // a note on every DSP tick that nobody played.
    Rig rig;
    rig.Note(60, 100);
    rig.out.Clear();
    for (int i = 0; i < 8; i++)
      rig.obj.Calculate(YSE::T_DSP);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Poly() == 1);
  }

  TEST_CASE("borax: an outlet wired back into an inlet is refused, not recursed (#543)") {
    // A reset sends out the pitch outlet, and a patch may wire that outlet back
    // into the left inlet. Without the guard the report would re-enter the table
    // mid-walk; with it the re-entrant message is refused and counted.
    Rig rig;
    Wire(rig.obj, 3, rig.obj, 0);
    rig.Note(60, 100);
    REQUIRE(rig.obj.Poly() == 1);

    const std::uint64_t before = rig.obj.Dropped();
    rig.Bang();
    CHECK(rig.obj.Dropped() > before);
    CHECK(rig.obj.Poly() == 0);
  }

  // ─── what a save carries ────────────────────────────────────────────────────

  TEST_CASE("borax: survives a DumpJSON / ParseJSON round trip (#543)") {
    // The object has no persistent state of its own — no creation arguments, and
    // the counters are a reading of the stream rather than a setting — so what a
    // save has to carry is the object itself, under its own name.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* obj = src.CreateObject(YSE::OBJ::M_BORAX);
    REQUIRE(obj != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".borax") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".borax");
  }

  // ─── end to end, in a real patcher ──────────────────────────────────────────

  TEST_CASE("borax: counts the polyphony of a real MIDI passage (#543)") {
    // The question a patch actually asks, through a real chain: a real
    // `.midiparse` decodes real bytes into a registry-built `.borax`, and the
    // polyphony outlet says how thick the texture got. Three keys go down and
    // one comes up.
    Tap poly;
    YSE::pHandle polyHandle(&poly);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* parse = p.CreateObject(YSE::OBJ::M_PARSE, "");
    YSE::pHandle* borax = p.CreateObject(YSE::OBJ::M_BORAX, "");
    REQUIRE(parse != nullptr);
    REQUIRE(borax != nullptr);

    p.Connect(parse, 0, borax, 0); // the note pair, on one cord
    p.Connect(borax, 2, &polyHandle, 0);

    const int bytes[] = {0x90, 60, 100, 0x90, 64, 100, 0x90, 67, 100, 0x90, 64, 0};
    for (int b : bytes)
      parse->SetIntData(0, b);

    CHECK(poly.trace() == "i1 i2 i3 i2");
  }

  TEST_CASE("borax: pairs a note-off with its note-on end to end (#543)") {
    // The serial claim through a real chain rather than through the object's own
    // accessors: three keys down, the first one comes up, and what reaches the
    // patch is the number that key was given on the way in.
    Tap serial;
    Tap duration;
    YSE::pHandle serialHandle(&serial);
    YSE::pHandle durationHandle(&duration);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* parse = p.CreateObject(YSE::OBJ::M_PARSE, "");
    YSE::pHandle* borax = p.CreateObject(YSE::OBJ::M_BORAX, "");
    REQUIRE(parse != nullptr);
    REQUIRE(borax != nullptr);

    p.Connect(parse, 0, borax, 0);
    p.Connect(borax, 0, &serialHandle, 0);
    p.Connect(borax, 6, &durationHandle, 0);

    const int down[] = {0x90, 60, 100, 0x90, 64, 100, 0x90, 67, 100};
    for (int b : down)
      parse->SetIntData(0, b);
    REQUIRE(serial.trace() == "i1 i2 i3");
    REQUIRE(duration.log.empty()); // nothing has ended yet

    serial.log.clear();
    const int up[] = {0x90, 60, 0}; // a note-on with velocity 0 is a release
    for (int b : up)
      parse->SetIntData(0, b);

    CHECK(serial.trace() == "i1"); // the first note's number, not the third's
    CHECK(duration.trace() == "f"); // and exactly one duration
  }

  // ─── teardown (issue #758): the deliberate absence of one ───────────────────

  TEST_CASE("borax: a cleared patcher reports nothing — there is no teardown pass (#543, #758)") {
    // A decision rather than an omission. `.flush`, `.poly` and `.makenote`
    // override Teardown because each of them is the *only* thing that knows
    // about a sounding note, so a patcher torn down mid-chord would strand notes
    // nothing could ever release. This object knows about no note something else
    // does not also know about — it is wired alongside the patch, not in it — so
    // a burst of duplicate note-off reports at teardown, with durations
    // measuring how long the patcher took to be destroyed, would be a worse
    // answer than silence.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* borax = p.CreateObject(YSE::OBJ::M_BORAX, "");
    REQUIRE(borax != nullptr);
    p.Connect(borax, 0, &tapHandle, 0);

    borax->SetIntData(1, 100);
    borax->SetIntData(0, 60);
    borax->SetIntData(0, 67);
    REQUIRE(tap.trace() == "i1 i2");

    tap.log.clear();
    p.Clear();
    CHECK(tap.log.empty());
  }

  // ─── real-time discipline ───────────────────────────────────────────────────

  TEST_CASE("borax: the message paths allocate nothing (#543)") {
    // Everything a message can do, measured on the thread that does it. A note
    // routinely arrives on the audio callback — an in-patcher dispatch runs on
    // T_DSP — so every one of these paths is audio-thread code, the reset and
    // the ignored messages included. Nothing here builds text: every outlet
    // carries a number, which is why there is no reserved-buffer case beside
    // this one as `.poly` has.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    Rig rig;

    // Built outside the probe: it is the *handler* that must not allocate, not
    // the test's own construction of the message.
    const std::string pair = "64 90";
    const std::string one = "67";
    const std::string release = "67 0";
    const std::string vel = "77";
    const std::string word = "wibble";
    const std::string delta = "delta";
    {
      TestHelpers::ProbeScope probe;
      rig.Int(100, 1);
      rig.Float(90.5f, 1);
      rig.List(vel, 1);
      rig.List(word, 1);
      rig.Int(60);
      rig.Float(62.5f);
      rig.List(pair);
      rig.List(one);
      rig.List(delta);
      rig.List(release);
      rig.List(word);
      rig.Int(0, 1);
      rig.Int(60); // a release
      rig.Int(99); // and one for a pitch nothing is holding
      rig.Bang(); // the reset, with two notes still open
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    // The probed messages really did something — an assertion that only proves
    // nothing happened proves nothing.
    CHECK(rig.out.n() > 0);
    CHECK(rig.obj.Poly() == 0);
  }

  TEST_CASE("borax: a full table allocates nothing either (#543)") {
    // Its own case because it is the one path that walks all 128 entries twice —
    // the failed search for a free voice, and then the reset that empties them.
    if (!TestHelpers::probeCountsAllocations()) return;

    Rig rig;
    rig.Int(100, 1);
    for (int i = 0; i < mBorax::MAX_VOICES; i++)
      rig.Int(1000 + i);
    REQUIRE(rig.obj.Poly() == mBorax::MAX_VOICES);

    {
      TestHelpers::ProbeScope probe;
      rig.Int(2000); // refused
      rig.Int(2001); // and again
      rig.Bang(); // 128 note-off reports
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    CHECK(rig.obj.Poly() == 0);
    CHECK(rig.obj.NoteCount() == 0u);
  }
}
