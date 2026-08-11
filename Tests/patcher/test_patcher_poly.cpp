// Tests for `.poly` — Max's poly, which "provides polyphonic voice-allocation
// by allocating data to different individual voices" (issue #542).
//
// The two behaviours worth testing hardest are the two a wrong allocator gets
// wrong silently:
//
//   - **note-off routing.** A release must come out on the voice number that is
//     playing that pitch, not on the last one allocated and not on voice 1. An
//     implementation that got this wrong would still pass "a release came out",
//     while downstream one chain hangs for ever and another is cut short.
//   - **voice stealing.** When the pool is full the note held the *longest*
//     gives way — not the lowest-numbered voice, and not the newest — and the
//     stolen note's release goes out *before* the new note's attack, on the same
//     voice number. An implementation that stole by index would pass every test
//     where the two happen to agree, so the stealing cases are built so they do
//     not agree.
//
// Layers, and they are not interchangeable:
//
//   - **standalone cases** pin the grammar and the shape: what each inlet
//     accepts, that a list is Max's inlet distribution, that the right inlet
//     stores without emitting, and that the outlets fire velocity, pitch, voice.
//
//   - **the voice table**, which is the object: allocation order, note-off
//     routing, repeats, overflow, stealing and `stop`.
//
//   - **end-to-end cases** run real chains inside a real patcher — a
//     `.midiparse` feeding a registry-built `.poly` feeding a real `.pack` — and
//     ask the question a patch asks: which voice was told to play what? The
//     control case proves the rig can see a hanging note before the case that
//     proves `stop` clears it.
//
//   - **teardown cases** (issue #758) take the three routes a patch really dies
//     by: Clear(), the destructor, and DeleteObject().
//
// No audio device and no MIDI hardware required.

#include <doctest/doctest.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "patcher/inlet.h"
#include "patcher/midi/mPoly.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using YSE::PATCHER::mPoly;
using YSE::PATCHER::patcherImplementation;
using YSE::PATCHER::Register;

namespace {

  // Records all four outlets into one ordered log, which is the only way to see
  // the half of this object that lives in the *order* the outlets fire in: the
  // voice number is the value that routes a note, so it has to reach a hot inlet
  // last or it would carry the previous note's pair with it.
  //
  // Events are a fixed-size struct in a reserved vector rather than strings, and
  // the overflow list is parsed with strtol rather than split, so the allocation
  // probe measures the object under test and not this sink.
  struct Notes : YSE::PATCHER::pObject {
    struct Event {
      char kind = 'p'; // 'n' voice, 'p' pitch, 'v' velocity, 'o'/'w' overflow
      int value = 0;
    };

    std::vector<Event> events;

    Notes() : pObject(false) {
      events.reserve(4096);

      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) { Push('n', v); });

      inputs.emplace_back(this, false, 1);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) { Push('p', v); });

      inputs.emplace_back(this, false, 2);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) { Push('v', v); });

      inputs.emplace_back(this, false, 3);
      inputs.back().RegisterList([this](const std::string& v, int, YSE::THREAD) {
        char* rest = nullptr;
        const long pitch = std::strtol(v.c_str(), &rest, 10);
        const long velocity = std::strtol(rest, nullptr, 10);
        Push('o', (int)pitch);
        Push('w', (int)velocity);
      });
    }
    const char* Type() const override {
      return "poly_notes";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

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

    // "v100 p60 n1" — the whole exchange in the order it happened.
    std::string trace() const {
      std::string out;
      for (const Event& e : events) {
        if (!out.empty()) out.push_back(' ');
        out.push_back(e.kind);
        out += std::to_string(e.value);
      }
      return out;
    }
  };

  // Records whatever arrives, in the shape it arrives in, for the end-to-end
  // chains where the message is a list assembled by a real `.pack`. Deliberately
  // not owned by the patcher: it stands in for everything downstream that a
  // release has to reach.
  struct Tap : YSE::PATCHER::pObject {
    std::vector<std::string> log;

    Tap() : pObject(false) {
      log.reserve(64);
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt(
          [this](int v, int, YSE::THREAD) { log.push_back("i" + std::to_string(v)); });
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { log.push_back("l" + v); });

      inputs.emplace_back(this, false, 1);
      inputs.back().RegisterInt(
          [this](int v, int, YSE::THREAD) { log.push_back("p" + std::to_string(v)); });

      inputs.emplace_back(this, false, 2);
      inputs.back().RegisterInt(
          [this](int v, int, YSE::THREAD) { log.push_back("v" + std::to_string(v)); });
    }
    const char* Type() const override {
      return "poly_tap";
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

  // A standalone `.poly` with all four outlets watched. The object needs no
  // clock and no patcher, so standalone is the whole of its message behaviour —
  // the one thing it cannot show is the teardown hook, which has its own cases.
  //
  // The sink is declared **before** the object so it is destroyed after it —
  // see Wire on why that matters even for a symmetric edge.
  struct Rig {
    Notes out;
    mPoly obj;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) obj.SetParams(args);
      Wire(obj, 0, out, 0);
      Wire(obj, 1, out, 1);
      Wire(obj, 2, out, 2);
      Wire(obj, 3, out, 3);
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
    void Bang(int inlet = 0) {
      obj.GetInlet(inlet)->SetBang(YSE::T_GUI);
    }
    // Play a note the way a patch does: velocity first, then pitch.
    void Note(int pitch, int noteVelocity) {
      Int(noteVelocity, 1);
      Int(pitch, 0);
    }
    // The voice a note-on came out on, which is what most cases here are about.
    int VoiceOf(int pitch, int noteVelocity) {
      out.Clear();
      Note(pitch, noteVelocity);
      for (const Notes::Event& e : out.events)
        if (e.kind == 'n') return e.value;
      return -1;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── the object exists ──────────────────────────────────────────────────────

  TEST_CASE("poly: creatable through the registry (#542)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::M_POLY);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".poly");
  }

  TEST_CASE("poly: listed by pRegistry::AllNames (#542)") {
    auto names = Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".poly")) != names.end());
  }

  TEST_CASE("poly: the shape is two inlets, three int outlets and an overflow list (#542)") {
    // Max's shape, with the one documented departure: the overflow outlet is
    // always there rather than appearing only when stealing is off, ports here
    // being built before the creation arguments are parsed.
    mPoly obj;
    CHECK(obj.NumInputs() == 2);
    CHECK(obj.NumOutputs() == 4);
    CHECK(obj.GetOutputType(0) == YSE::OUT_TYPE::INT);
    CHECK(obj.GetOutputType(1) == YSE::OUT_TYPE::INT);
    CHECK(obj.GetOutputType(2) == YSE::OUT_TYPE::INT);
    CHECK(obj.GetOutputType(3) == YSE::OUT_TYPE::LIST);
    CHECK(obj.GetCategory() == YSE::PATCHER::pCategory::MIDI);
    CHECK_FALSE(obj.IsDSPObject());
    CHECK_FALSE(obj.WantsBlockPoll());
  }

  TEST_CASE("poly: documents itself, both creation arguments included (#542)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::M_POLY));
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == std::string(YSE::OBJ::M_POLY));
    CHECK_FALSE(obj->GetDescription().empty());
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MIDI);
    for (int i = 0; i < obj->NumInputs(); i++)
      CHECK_FALSE(obj->GetInlet(i)->GetDocLabel().empty());
    for (int i = 0; i < obj->NumOutputs(); i++)
      CHECK_FALSE(obj->GetOutlet(i)->GetDocLabel().empty());
    REQUIRE(obj->GetParamDocs().size() == 2);
    CHECK(obj->GetParamDocs()[0].name == "voices");
    CHECK(obj->GetParamDocs()[1].name == "steal");
  }

  TEST_CASE("poly: a fresh object holds nothing and is Max's 16 voices, not stealing (#542)") {
    Rig rig;
    CHECK(rig.obj.Voices() == mPoly::DEFAULT_VOICES);
    CHECK_FALSE(rig.obj.Steals());
    CHECK(rig.obj.Velocity() == 0);
    CHECK(rig.obj.Held() == 0);
    CHECK(rig.obj.PitchOfVoice(1) == -1);
  }

  // ─── the creation arguments ─────────────────────────────────────────────────

  TEST_CASE("poly: the creation arguments are the pool size and the steal flag (#542)") {
    Rig rig("4 1");
    CHECK(rig.obj.Voices() == 4);
    CHECK(rig.obj.Steals());
  }

  TEST_CASE("poly: the pool size is clamped to 1-128 when it is read (#542)") {
    // Clamped on read rather than on write, because a live SetParams re-parse
    // stores straight into the field from the audio thread. A pool of 0 would
    // allocate nothing at all and a pool past the table would walk off it.
    Rig zero("0");
    CHECK(zero.obj.Voices() == 1);

    Rig negative("-5");
    CHECK(negative.obj.Voices() == 1);

    Rig huge("9999");
    CHECK(huge.obj.Voices() == mPoly::MAX_VOICES);

    // And the clamp really binds the allocator: a pool of 0 plays one note and
    // overflows the second.
    zero.Note(60, 100);
    CHECK(zero.obj.Held() == 1);
    zero.out.Clear();
    zero.Note(64, 100);
    CHECK(zero.out.trace() == "o64 w100");
    CHECK(zero.obj.Held() == 1);
  }

  // ─── allocation ─────────────────────────────────────────────────────────────

  TEST_CASE("poly: a note-on takes a voice, and the outlets fire velocity, pitch, voice (#542)") {
    // Max's outlets fire right to left, and here that is load-bearing rather
    // than cosmetic: everything downstream takes the voice number on a hot inlet
    // — that is the value which routes the note — so a voice number sent first
    // would carry the *previous* note's pitch and velocity with it.
    Rig rig;
    rig.Note(60, 100);
    CHECK(rig.out.trace() == "v100 p60 n1");
    CHECK(rig.obj.Held() == 1);
    CHECK(rig.obj.PitchOfVoice(1) == 60);
  }

  TEST_CASE("poly: voices are handed out in ascending order (#542)") {
    // The engine synth's first allocation step: a free voice, scanned low to
    // high, so a patch with more voices than it uses always plays on the low
    // ones and can reason about which chains are live.
    Rig rig("4");
    CHECK(rig.VoiceOf(60, 100) == 1);
    CHECK(rig.VoiceOf(64, 100) == 2);
    CHECK(rig.VoiceOf(67, 100) == 3);
    CHECK(rig.obj.Held() == 3);
    CHECK(rig.obj.PitchOfVoice(2) == 64);
  }

  TEST_CASE("poly: a freed voice is reused, lowest first (#542)") {
    Rig rig("4");
    REQUIRE(rig.VoiceOf(60, 100) == 1);
    REQUIRE(rig.VoiceOf(64, 100) == 2);
    REQUIRE(rig.VoiceOf(67, 100) == 3);

    rig.Note(64, 0); // voice 2 comes free
    REQUIRE(rig.obj.Held() == 2);

    CHECK(rig.VoiceOf(72, 100) == 2);
    CHECK(rig.obj.PitchOfVoice(2) == 72);
  }

  // ─── note-off routing, which is what the object is for ──────────────────────

  TEST_CASE("poly: a release comes out on the voice that is playing the pitch (#542)") {
    // The claim the object exists to make. A release routed to the wrong voice
    // leaves one chain hanging for ever and cuts another one short, and both
    // failures look like "a note-off came out" from anywhere else in the patch.
    Rig rig("4");
    rig.Note(60, 100);
    rig.Note(64, 100);
    rig.Note(67, 100);
    REQUIRE(rig.obj.Held() == 3);

    rig.out.Clear();
    rig.Note(64, 0);
    CHECK(rig.out.trace() == "v0 p64 n2"); // voice 2, not voice 1 and not voice 3
    CHECK(rig.obj.Held() == 2);
    CHECK(rig.obj.PitchOfVoice(2) == -1);
    CHECK(rig.obj.PitchOfVoice(1) == 60);
    CHECK(rig.obj.PitchOfVoice(3) == 67);
  }

  TEST_CASE("poly: a release for a pitch no voice holds goes out the overflow outlet (#542)") {
    // Not silence: in overflow mode the attacks that were refused went out
    // there, so their releases have to follow them or a patch reading that
    // outlet would hang every note it was sent.
    Rig rig("4");
    rig.Note(60, 100);
    REQUIRE(rig.obj.Held() == 1);

    rig.out.Clear();
    rig.Note(72, 0);
    CHECK(rig.out.trace() == "o72 w0");
    CHECK(rig.obj.Held() == 1); // and nothing was freed by accident
  }

  TEST_CASE("poly: a repeated pitch takes a second voice, released oldest first (#542)") {
    // Max's default repeat mode, and the only answer that keeps a voice pool a
    // voice pool: collapsing the two would leave the second attack with no voice
    // of its own to be released from.
    Rig rig("4");
    REQUIRE(rig.VoiceOf(60, 100) == 1);
    REQUIRE(rig.VoiceOf(60, 90) == 2);
    CHECK(rig.obj.Held() == 2);

    rig.out.Clear();
    rig.Note(60, 0);
    CHECK(rig.out.trace() == "v0 p60 n1"); // the one played first
    CHECK(rig.obj.PitchOfVoice(1) == -1);
    CHECK(rig.obj.PitchOfVoice(2) == 60);

    rig.out.Clear();
    rig.Note(60, 0);
    CHECK(rig.out.trace() == "v0 p60 n2");
    CHECK(rig.obj.Held() == 0);
  }

  TEST_CASE("poly: any pitch at all can be allocated — this is not a bitmap (#542)") {
    // `.flush` and `.sustain` hold 128 bits indexed by pitch and cannot remember
    // anything outside the MIDI note range. This one holds voices, each with the
    // pitch it happens to be playing, so a patch driving a non-MIDI synth
    // through wider values is allocated and released like any other.
    Rig rig("4");
    CHECK(rig.VoiceOf(5000, 100) == 1);
    CHECK(rig.VoiceOf(-42, 100) == 2);
    CHECK(rig.obj.PitchOfVoice(1) == 5000);

    rig.out.Clear();
    rig.Note(-42, 0);
    CHECK(rig.out.trace() == "v0 p-42 n2");
    CHECK(rig.obj.Held() == 1);
  }

  // ─── overflow (Max's default) ───────────────────────────────────────────────

  TEST_CASE("poly: a full pool overflows rather than stealing by default (#542)") {
    // Max's first creation argument with no second one: "poly sends any notes it
    // cannot hold out the rightmost outlet".
    Rig rig("2");
    REQUIRE(rig.VoiceOf(60, 100) == 1);
    REQUIRE(rig.VoiceOf(64, 90) == 2);

    rig.out.Clear();
    rig.Note(67, 80);
    CHECK(rig.out.trace() == "o67 w80"); // the pair, out the fourth outlet
    CHECK(rig.obj.Held() == 2);
    CHECK(rig.obj.PitchOfVoice(1) == 60); // and nothing was displaced
    CHECK(rig.obj.PitchOfVoice(2) == 64);
  }

  TEST_CASE("poly: an overflowed note's release follows it out the same outlet (#542)") {
    Rig rig("2");
    rig.Note(60, 100);
    rig.Note(64, 90);
    rig.Note(67, 80); // overflowed
    REQUIRE(rig.obj.Held() == 2);

    rig.out.Clear();
    rig.Note(67, 0);
    CHECK(rig.out.trace() == "o67 w0");
    CHECK(rig.obj.Held() == 2);
  }

  // ─── voice stealing ─────────────────────────────────────────────────────────

  TEST_CASE("poly: a full stealing pool turns off the note held longest (#542)") {
    // Max: "it turns off the note it has held the longest and puts the new note
    // in its place", which is the engine synth's rule too. The release goes out
    // *before* the attack and on the same voice number, or the chain downstream
    // would hold two notes at once with no way of ever releasing the first.
    Rig rig("2 1");
    REQUIRE(rig.VoiceOf(60, 100) == 1);
    REQUIRE(rig.VoiceOf(64, 90) == 2);

    rig.out.Clear();
    rig.Note(67, 80);
    CHECK(rig.out.trace() == "v0 p60 n1 v80 p67 n1");
    CHECK(rig.obj.Held() == 2);
    CHECK(rig.obj.PitchOfVoice(1) == 67);
    CHECK(rig.obj.PitchOfVoice(2) == 64); // untouched
    CHECK(rig.out.n() == 6); // nothing went out the overflow outlet
  }

  TEST_CASE("poly: stealing picks the oldest note, not the lowest voice (#542)") {
    // The case built so index order and age order *disagree*. Voice 1 is freed
    // and reallocated, which makes it the newest note in the pool while still
    // being the lowest-numbered voice; an allocator that stole by index would
    // take voice 1 here and pass every other stealing test.
    Rig rig("2 1");
    REQUIRE(rig.VoiceOf(60, 100) == 1);
    REQUIRE(rig.VoiceOf(64, 90) == 2);

    rig.Note(60, 0); // voice 1 comes free
    REQUIRE(rig.VoiceOf(67, 80) == 1); // and is reused — now the newest note

    rig.out.Clear();
    rig.Note(72, 70);
    CHECK(rig.out.trace() == "v0 p64 n2 v70 p72 n2"); // voice 2, the oldest
    CHECK(rig.obj.PitchOfVoice(1) == 67);
    CHECK(rig.obj.PitchOfVoice(2) == 72);
  }

  TEST_CASE("poly: a stolen note's later release finds no voice and overflows (#542)") {
    // The key comes up long after the note was taken away. There is nothing to
    // release, and releasing whatever now occupies that voice would cut a
    // sounding note short.
    Rig rig("1 1");
    REQUIRE(rig.VoiceOf(60, 100) == 1);
    REQUIRE(rig.VoiceOf(64, 90) == 1); // 60 stolen

    rig.out.Clear();
    rig.Note(60, 0);
    CHECK(rig.out.trace() == "o60 w0");
    CHECK(rig.obj.PitchOfVoice(1) == 64); // still sounding
  }

  TEST_CASE("poly: stealing can be switched on and off live (#542)") {
    Rig rig("1");
    REQUIRE(rig.VoiceOf(60, 100) == 1);

    rig.out.Clear();
    rig.Note(64, 90);
    REQUIRE(rig.out.trace() == "o64 w90"); // overflowing

    rig.obj.SetParams("1 1");
    REQUIRE(rig.obj.Steals());

    rig.out.Clear();
    rig.Note(67, 80);
    CHECK(rig.out.trace() == "v0 p60 n1 v80 p67 n1"); // now stealing
  }

  // ─── `stop` ─────────────────────────────────────────────────────────────────

  TEST_CASE("poly: 'stop' releases every sounding voice in ascending voice order (#542)") {
    // Max's only message: "immediately sends note-offs for all the notes
    // currently being held by poly, freeing all voices".
    Rig rig("4");
    rig.Note(67, 100);
    rig.Note(60, 90);
    rig.Note(64, 80);
    REQUIRE(rig.obj.Held() == 3);

    rig.out.Clear();
    rig.List("stop");
    CHECK(rig.out.trace() == "v0 p67 n1 v0 p60 n2 v0 p64 n3");
    CHECK(rig.obj.Held() == 0);
    CHECK(rig.obj.PitchOfVoice(1) == -1);
  }

  TEST_CASE("poly: stopping twice sends nothing the second time (#542)") {
    Rig rig("4");
    rig.Note(60, 100);
    rig.List("stop");
    REQUIRE(rig.obj.Held() == 0);

    rig.out.Clear();
    rig.List("stop");
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("poly: a 'stop' with nothing sounding sends nothing (#542)") {
    // The half of the claim that keeps the message from being a burst of
    // spurious note-offs after a properly balanced phrase.
    Rig rig("4");
    rig.Note(60, 100);
    rig.Note(60, 0);
    REQUIRE(rig.obj.Held() == 0);

    rig.out.Clear();
    rig.List("stop");
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("poly: notes played after a stop are allocated again (#542)") {
    Rig rig("4");
    rig.Note(60, 100);
    rig.List("stop");
    REQUIRE(rig.obj.Held() == 0);

    CHECK(rig.VoiceOf(67, 100) == 1);
    CHECK(rig.obj.Held() == 1);
  }

  TEST_CASE("poly: 'stop' in the right inlet is not a command (#542)") {
    // Max's command is left-inlet only, and the right inlet takes numbers. A
    // word there is neither a velocity nor a stop.
    Rig rig("4");
    rig.Note(60, 100);
    REQUIRE(rig.obj.Held() == 1);

    rig.List("stop", 1);
    CHECK(rig.obj.Held() == 1);
    CHECK(rig.obj.Velocity() == 100);
  }

  TEST_CASE("poly: shrinking the pool strands no note (#542)") {
    // Only allocation is bounded by the voice count; a release, a `stop` and the
    // teardown pass walk the whole table. A voice left above the new bound still
    // gets its note-off and simply never plays again.
    Rig rig("4");
    REQUIRE(rig.VoiceOf(60, 100) == 1);
    REQUIRE(rig.VoiceOf(64, 100) == 2);
    REQUIRE(rig.VoiceOf(67, 100) == 3);

    rig.obj.SetParams("1");
    REQUIRE(rig.obj.Voices() == 1);

    // Its release still finds it.
    rig.out.Clear();
    rig.Note(67, 0);
    CHECK(rig.out.trace() == "v0 p67 n3");

    // And a `stop` still empties the whole table.
    rig.out.Clear();
    rig.List("stop");
    CHECK(rig.out.trace() == "v0 p60 n1 v0 p64 n2");
    CHECK(rig.obj.Held() == 0);
  }

  // ─── the grammar ────────────────────────────────────────────────────────────

  TEST_CASE("poly: the right inlet stores without emitting (#542)") {
    // Only the pitch half completes a note, so only the pitch half can take a
    // voice. A right inlet that emitted would turn every velocity change into a
    // phantom allocation.
    Rig rig("4");
    rig.Int(100, 1);
    rig.Float(70.9f, 1);
    rig.List("55", 1);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Held() == 0);
    CHECK(rig.obj.Velocity() == 55);

    rig.Int(60);
    CHECK(rig.out.trace() == "v55 p60 n1");
  }

  TEST_CASE("poly: the velocity survives across notes (#542)") {
    Rig rig("4");
    rig.Int(90, 1);
    rig.Int(60);
    rig.Int(64);
    CHECK(rig.out.trace() == "v90 p60 n1 v90 p64 n2");
    CHECK(rig.obj.Held() == 2);
  }

  TEST_CASE("poly: a list is Max's inlet distribution — pitch then velocity (#542)") {
    // The velocity is stored *before* the pitch is read against it, exactly as
    // if it had reached the right inlet first. A list that read the pitch first
    // would allocate the very first note against the wrong velocity — and with
    // velocity 0 that is not a wrong number, it is a release instead of a note.
    Rig rig("4");
    rig.List("60 100");
    CHECK(rig.out.trace() == "v100 p60 n1");
    CHECK(rig.obj.PitchOfVoice(1) == 60);

    // And it persists, as a right-inlet value would.
    rig.out.Clear();
    rig.List("62");
    CHECK(rig.out.trace() == "v100 p62 n2");

    // A release spelled as a list frees the right voice.
    rig.out.Clear();
    rig.List("60 0");
    CHECK(rig.out.trace() == "v0 p60 n1");
    CHECK(rig.obj.Velocity() == 0);

    // Further elements are ignored.
    rig.out.Clear();
    rig.List("64 90 7 7");
    CHECK(rig.out.trace() == "v90 p64 n1");
  }

  TEST_CASE("poly: anything that is not a number and not 'stop' does nothing (#542)") {
    Rig rig("4");
    rig.Int(100, 1);
    rig.List("60");
    REQUIRE(rig.obj.Held() == 1);

    rig.out.Clear();
    rig.List("wibble");
    rig.List("");
    rig.List("wibble", 1);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Velocity() == 100);
    CHECK(rig.obj.Held() == 1);
  }

  TEST_CASE("poly: a float is converted to an int in both inlets (#542)") {
    Rig rig("4");
    rig.Float(100.7f, 1);
    CHECK(rig.obj.Velocity() == 100);
    rig.Float(60.9f);
    CHECK(rig.out.trace() == "v100 p60 n1");
    CHECK(rig.obj.PitchOfVoice(1) == 60);
  }

  TEST_CASE("poly: a bang does nothing in either inlet (#542)") {
    // Max's poly has no bang method. One that stopped, or allocated, would kill
    // or invent notes in every patch that banged a note source.
    Rig rig("4");
    rig.Note(60, 100);
    rig.out.Clear();

    rig.Bang(0);
    rig.Bang(1);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Held() == 1);
  }

  TEST_CASE("poly: Calculate sends nothing (#542)") {
    // The object is driven entirely by its inlets; one that emitted would
    // allocate a voice on every DSP tick from a stimulus no patch sent.
    Rig rig("4");
    rig.Note(60, 100);
    rig.out.Clear();
    for (int i = 0; i < 8; i++)
      rig.obj.Calculate(YSE::T_DSP);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Held() == 1);
  }

  TEST_CASE("poly: an outlet wired back into the inlet is refused, not recursed (#542)") {
    // A `stop` sends out the pitch outlet, and a patch may wire that outlet back
    // into the left inlet. Without the guard the release would re-enter the
    // table mid-walk; with it the re-entrant message is refused and counted.
    Rig rig("4");
    Wire(rig.obj, 1, rig.obj, 0);
    rig.Note(60, 100);
    REQUIRE(rig.obj.Held() == 1);

    const std::uint64_t before = rig.obj.Dropped();
    rig.List("stop");
    CHECK(rig.obj.Dropped() > before);
    CHECK(rig.obj.Held() == 0);
  }

  // ─── what a save carries ────────────────────────────────────────────────────

  TEST_CASE("poly: the creation arguments survive a DumpJSON / ParseJSON round trip (#542)") {
    // The pool size and the steal flag are the whole of this object's persistent
    // state, and a patch that reloaded as a 16-voice overflowing `.poly` when it
    // was saved as a 4-voice stealing one would sound different for reasons
    // nothing in the patch could show.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* obj = src.CreateObject(YSE::OBJ::M_POLY, "4 1");
    REQUIRE(obj != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".poly") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".poly");
    CHECK(std::string(copy->GetParams()) == "4 1");
  }

  // ─── end to end, in a real patcher ──────────────────────────────────────────

  TEST_CASE("poly: the rig can see a hanging note (#542)") {
    // The control for the cases below, and it is not optional: a test that only
    // asserted "a release arrived" would pass just as happily against a chain
    // that never sounded anything. Here a real `.midiparse` decodes real bytes
    // into a real `.poly` into a real `.pack`, the patch is interrupted after the
    // attack, and what has reached the voice chain is one note-on and nothing
    // else — voice 1 holding a key with nobody left to lift it.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* parse = p.CreateObject(YSE::OBJ::M_PARSE, "");
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::M_POLY, "4");
    YSE::pHandle* pack = p.CreateObject(YSE::OBJ::G_PACK, "0 0 0");
    REQUIRE(parse != nullptr);
    REQUIRE(poly != nullptr);
    REQUIRE(pack != nullptr);

    p.Connect(parse, 0, poly, 0); // the note pair, on one cord
    p.Connect(poly, 2, pack, 2); // velocity, cold
    p.Connect(poly, 1, pack, 1); // pitch, cold
    p.Connect(poly, 0, pack, 0); // voice, hot — this is what sends
    p.Connect(pack, 0, &tapHandle, 0);

    const int bytes[] = {0x90, 60, 100}; // a key goes down, and the patch stops
    for (int b : bytes)
      parse->SetIntData(0, b);

    // "voice pitch velocity", assembled by a real `.pack` — which is also what
    // proves the outlet order: a voice number sent first would have found the
    // pack still holding the previous note's pair.
    CHECK(tap.trace() == "l1 60 100");
  }

  TEST_CASE("poly: 'stop' releases what the patch left hanging, per voice (#542)") {
    // The issue's claim at the level a patch makes it: real cords, a real
    // `.midiparse` in front and a real `.pack` behind, and the `stop` that turns
    // the hanging notes of the control case into released ones — each on the
    // voice number that is playing it.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* parse = p.CreateObject(YSE::OBJ::M_PARSE, "");
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::M_POLY, "4");
    YSE::pHandle* pack = p.CreateObject(YSE::OBJ::G_PACK, "0 0 0");
    REQUIRE(parse != nullptr);
    REQUIRE(poly != nullptr);
    REQUIRE(pack != nullptr);

    p.Connect(parse, 0, poly, 0);
    p.Connect(poly, 2, pack, 2);
    p.Connect(poly, 1, pack, 1);
    p.Connect(poly, 0, pack, 0);
    p.Connect(pack, 0, &tapHandle, 0);

    const int bytes[] = {0x90, 60, 100, 0x90, 67, 90};
    for (int b : bytes)
      parse->SetIntData(0, b);
    REQUIRE(tap.trace() == "l1 60 100 l2 67 90");

    tap.log.clear();
    poly->SetListData(0, "stop");
    CHECK(tap.trace() == "l1 60 0 l2 67 0");
  }

  TEST_CASE("poly: a note-off reaches the voice that is playing it, end to end (#542)") {
    // The routing claim through a real chain rather than through the object's
    // own accessors: three keys down, the middle one comes up, and the release
    // that arrives at the voice chains names voice 2.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* parse = p.CreateObject(YSE::OBJ::M_PARSE, "");
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::M_POLY, "4");
    YSE::pHandle* pack = p.CreateObject(YSE::OBJ::G_PACK, "0 0 0");
    REQUIRE(parse != nullptr);
    REQUIRE(poly != nullptr);
    REQUIRE(pack != nullptr);

    p.Connect(parse, 0, poly, 0);
    p.Connect(poly, 2, pack, 2);
    p.Connect(poly, 1, pack, 1);
    p.Connect(poly, 0, pack, 0);
    p.Connect(pack, 0, &tapHandle, 0);

    const int down[] = {0x90, 60, 100, 0x90, 64, 100, 0x90, 67, 100};
    for (int b : down)
      parse->SetIntData(0, b);
    REQUIRE(tap.log.size() == 3);

    tap.log.clear();
    const int up[] = {0x90, 64, 0}; // a note-on with velocity 0 is a release
    for (int b : up)
      parse->SetIntData(0, b);

    CHECK(tap.trace() == "l2 64 0");
  }

  TEST_CASE("poly: a steal reaches the voice chain as a release then an attack (#542)") {
    // Voice stealing at the level a patch hears it. The chain on voice 1 is told
    // to let go of 60 *before* it is told to play 67, which is the only order
    // that leaves one note sounding on one voice.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* parse = p.CreateObject(YSE::OBJ::M_PARSE, "");
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::M_POLY, "2 1");
    YSE::pHandle* pack = p.CreateObject(YSE::OBJ::G_PACK, "0 0 0");
    REQUIRE(parse != nullptr);
    REQUIRE(poly != nullptr);
    REQUIRE(pack != nullptr);

    p.Connect(parse, 0, poly, 0);
    p.Connect(poly, 2, pack, 2);
    p.Connect(poly, 1, pack, 1);
    p.Connect(poly, 0, pack, 0);
    p.Connect(pack, 0, &tapHandle, 0);

    const int bytes[] = {0x90, 60, 100, 0x90, 64, 90, 0x90, 67, 80};
    for (int b : bytes)
      parse->SetIntData(0, b);

    CHECK(tap.trace() == "l1 60 100 l2 64 90 l1 60 0 l1 67 80");
  }

  TEST_CASE("poly: an overflowed note reaches the overflow outlet, end to end (#542)") {
    // The default pool's answer, through a real chain: the third note never
    // reaches the voice chains at all, and comes out the fourth outlet as the
    // pair it was.
    Tap voices;
    Tap spill;
    YSE::pHandle voicesHandle(&voices);
    YSE::pHandle spillHandle(&spill);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* parse = p.CreateObject(YSE::OBJ::M_PARSE, "");
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::M_POLY, "2");
    YSE::pHandle* pack = p.CreateObject(YSE::OBJ::G_PACK, "0 0 0");
    REQUIRE(parse != nullptr);
    REQUIRE(poly != nullptr);
    REQUIRE(pack != nullptr);

    p.Connect(parse, 0, poly, 0);
    p.Connect(poly, 2, pack, 2);
    p.Connect(poly, 1, pack, 1);
    p.Connect(poly, 0, pack, 0);
    p.Connect(pack, 0, &voicesHandle, 0);
    p.Connect(poly, 3, &spillHandle, 0);

    const int bytes[] = {0x90, 60, 100, 0x90, 64, 90, 0x90, 67, 80};
    for (int b : bytes)
      parse->SetIntData(0, b);

    CHECK(voices.trace() == "l1 60 100 l2 64 90");
    CHECK(spill.trace() == "l67 80");
  }

  // ─── teardown (issue #758) ──────────────────────────────────────────────────

  TEST_CASE("poly: a cleared patcher releases every voice it was holding (#542, #758)") {
    // The tap stands in for the voice chains, and is declared before the patcher
    // so the patcher dies first and the stop pass has somewhere to send. This
    // object is the *only* thing that knows which voice holds which note, so
    // without the pass nothing in the patch could release them.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::M_POLY, "4");
    REQUIRE(poly != nullptr);
    p.Connect(poly, 2, &tapHandle, 2);
    p.Connect(poly, 1, &tapHandle, 1);
    p.Connect(poly, 0, &tapHandle, 0);

    poly->SetIntData(1, 100);
    poly->SetIntData(0, 60);
    poly->SetIntData(0, 67);
    REQUIRE(tap.trace() == "v100 p60 i1 v100 p67 i2");

    tap.log.clear();
    p.Clear();
    // The releases the patch never sent, sent by the teardown pass — down cords
    // that would have been gone had Clear unwired as it walked.
    CHECK(tap.trace() == "v0 p60 i1 v0 p67 i2");
  }

  TEST_CASE("poly: a destroyed patcher releases too (#542, #758)") {
    // ~patcherImplementation calls Clear(), so the destructor is the same route.
    // It is worth its own case because it is the one a host actually takes:
    // nobody clears a patcher on the way out, they drop it.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    {
      patcherImplementation p(1, nullptr);
      YSE::pHandle* poly = p.CreateObject(YSE::OBJ::M_POLY, "4");
      REQUIRE(poly != nullptr);
      p.Connect(poly, 2, &tapHandle, 2);
      p.Connect(poly, 1, &tapHandle, 1);
      p.Connect(poly, 0, &tapHandle, 0);

      poly->SetIntData(1, 100);
      poly->SetIntData(0, 62);
      REQUIRE(tap.log.size() == 3);
      tap.log.clear();
    }

    CHECK(tap.trace() == "v0 p62 i1");
  }

  TEST_CASE("poly: deleting the object on its own releases its voices (#542, #758)") {
    // DeleteObject is the second teardown route and has no Clear() to hook. An
    // object removed while its voices are sounding still owes them their
    // releases, and the rest of the patch is still there to receive them.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::M_POLY, "4");
    REQUIRE(poly != nullptr);
    p.Connect(poly, 2, &tapHandle, 2);
    p.Connect(poly, 1, &tapHandle, 1);
    p.Connect(poly, 0, &tapHandle, 0);

    poly->SetIntData(1, 100);
    poly->SetIntData(0, 64);
    REQUIRE(tap.log.size() == 3);
    tap.log.clear();

    p.DeleteObject(poly);
    CHECK(tap.trace() == "v0 p64 i1");
  }

  TEST_CASE("poly: a teardown with nothing sounding sends nothing (#542, #758)") {
    // The other half of the claim, and the one that keeps the pass from being a
    // burst of spurious releases at every Clear.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::M_POLY, "4");
    REQUIRE(poly != nullptr);
    p.Connect(poly, 2, &tapHandle, 2);
    p.Connect(poly, 1, &tapHandle, 1);
    p.Connect(poly, 0, &tapHandle, 0);

    poly->SetIntData(1, 100);
    poly->SetIntData(0, 60);
    poly->SetIntData(1, 0);
    poly->SetIntData(0, 60); // the key comes up
    REQUIRE(tap.log.size() == 6);
    tap.log.clear();

    p.Clear();
    CHECK(tap.log.empty());
  }

  // ─── real-time discipline ───────────────────────────────────────────────────

  TEST_CASE("poly: the message paths allocate nothing (#542)") {
    // Everything a message can do, measured on the thread that does it. A note
    // routinely arrives on the audio callback — an in-patcher dispatch runs on
    // T_DSP — so every one of these paths is audio-thread code, the steal, the
    // overflow list and the teardown included.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    Rig rig("2 1");

    // Built outside the probe: it is the *handler* that must not allocate, not
    // the test's own construction of the message.
    const std::string pair = "64 90";
    const std::string one = "67";
    const std::string release = "67 0";
    const std::string vel = "77";
    const std::string word = "wibble";
    const std::string stop = "stop";
    {
      TestHelpers::ProbeScope probe;
      rig.Int(100, 1);
      rig.Float(90.5f, 1);
      rig.List(vel, 1);
      rig.List(word, 1);
      rig.Int(60);
      rig.Float(62.5f);
      rig.List(pair);
      rig.List(one); // the pool is full by now — a steal
      rig.List(release);
      rig.List(word);
      rig.Int(72); // and again, with the other steal victim
      rig.List(stop);
      rig.obj.Teardown(YSE::T_GUI);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    // The probed messages really did something — an assertion that only proves
    // nothing happened proves nothing.
    CHECK(rig.out.n() > 0);
  }

  TEST_CASE("poly: the overflow path allocates nothing either (#542)") {
    // Its own case because it is the one path that builds *text* on the audio
    // thread: the list goes out a reserved buffer refilled in place, so a full
    // pool cannot turn every refused note into a heap allocation on the callback.
    if (!TestHelpers::probeCountsAllocations()) return;

    Rig rig("1"); // overflowing, not stealing

    const std::string first = "60 100";
    const std::string spill = "64 90";
    {
      TestHelpers::ProbeScope probe;
      rig.List(first);
      rig.List(spill); // overflowed
      rig.List(spill); // and again
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    CHECK(rig.out.trace() == "v100 p60 n1 o64 w90 o64 w90");
  }
}
