// Tests for `.sustain` — Max's sustain, which "holds note-off messages for
// release" while the pedal is down (issue #541).
//
// The trap this object sets is that it looks like `.flush` and holds the
// opposite thing. `.flush` remembers the notes that are *sounding*, filled by
// note-ons, and releases them on a bang; this one remembers the *releases it
// swallowed*, filled by note-offs, and sends them when the pedal lifts. An
// implementation that tracked note-ons, or that delayed attacks as well as
// releases, or that swallowed a release and then forgot to send it, would still
// pass a naive "does lifting the pedal send something" test. So every case that
// checks something is sent is paired with one that checks something is *not*.
//
// Four layers, and they are not interchangeable:
//
//   - **standalone cases** pin the grammar and the shape: what each of the three
//     inlets accepts, that a list is Max's inlet distribution, that the middle
//     inlet stores without emitting, that the velocity outlet fires before the
//     pitch outlet, and that nothing is filtered or clamped on the way through.
//
//   - **the pedal**, which is the object: that attacks are never delayed, that
//     releases are held only while the pedal is down, what a lift sends and in
//     which order, and what `flush`, `clear` and `sustain 0/1` do instead.
//
//   - **end-to-end cases** run real chains inside a real patcher — a
//     `.midiparse` feeding a registry-built `.sustain` feeding a real `.noteon`,
//     and a chain that encodes the pairs back into wire bytes — and ask the
//     question a patch asks: with the pedal down and the key up, is the synth
//     still holding the note, and does lifting the pedal let it go?
//
//   - **teardown cases** (issue #758) take the three routes a patch really dies
//     by with the pedal held: Clear(), the destructor, and DeleteObject().
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
#include "patcher/midi/mSustain.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using YSE::PATCHER::mSustain;
using YSE::PATCHER::patcherImplementation;
using YSE::PATCHER::Register;

namespace {

  // Records both outlets into one ordered log, which is the only way to see the
  // half of this object that lives in the *order* the two outlets fire in.
  // Velocity has to reach a downstream object's cold inlet before the pitch
  // reaches its hot one, so a sink watching the pitch outlet alone would be
  // blind to the most consequential way of getting this object wrong.
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
      return "sustain_notes";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    std::size_t n() const {
      return events.size();
    }
    void Clear() {
      events.clear();
    }

    // "v100 p60" — the whole exchange in the order it happened.
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
  // chains where the message may be a binary MIDI list rather than a pair of
  // ints. Deliberately not owned by the patcher: it stands in for everything
  // downstream that a release has to reach.
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
          [this](int v, int, YSE::THREAD) { log.push_back("v" + std::to_string(v)); });
    }
    const char* Type() const override {
      return "sustain_tap";
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

  // A standalone `.sustain` with both outlets watched. The object needs no clock
  // and no patcher, so standalone is the whole of its message behaviour — the
  // one thing it cannot show is the teardown hook, which has its own cases.
  //
  // The sink is declared **before** the object so it is destroyed after it —
  // see Wire on why that matters even for a symmetric edge.
  struct Rig {
    Notes out;
    mSustain obj;

    Rig() {
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
    void Bang(int inlet = 0) {
      obj.GetInlet(inlet)->SetBang(YSE::T_GUI);
    }
    // Play a note the way a patch does: velocity first, then pitch.
    void Note(int pitch, int noteVelocity) {
      Int(noteVelocity, 1);
      Int(pitch, 0);
    }
    // The pedal, as a cord into the right inlet.
    void Pedal(bool down) {
      Int(down ? 1 : 0, 2);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── the object exists ──────────────────────────────────────────────────────

  TEST_CASE("sustain: creatable through the registry (#541)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::M_SUSTAIN);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".sustain");
  }

  TEST_CASE("sustain: listed by pRegistry::AllNames (#541)") {
    auto names = Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".sustain")) != names.end());
  }

  TEST_CASE("sustain: the shape is three inlets and two int outlets (#541)") {
    // Max's shape. The third inlet is the pedal, and it is what separates this
    // object from `.flush` at a glance: that one is banged, this one is pedalled.
    mSustain obj;
    CHECK(obj.NumInputs() == 3);
    CHECK(obj.NumOutputs() == 2);
    CHECK(obj.GetOutputType(0) == YSE::OUT_TYPE::INT);
    CHECK(obj.GetOutputType(1) == YSE::OUT_TYPE::INT);
    CHECK(obj.GetCategory() == YSE::PATCHER::pCategory::MIDI);
    CHECK_FALSE(obj.IsDSPObject());
    CHECK_FALSE(obj.WantsBlockPoll());
  }

  TEST_CASE("sustain: documents itself (#541)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::M_SUSTAIN));
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == std::string(YSE::OBJ::M_SUSTAIN));
    CHECK_FALSE(obj->GetDescription().empty());
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MIDI);
    for (int i = 0; i < obj->NumInputs(); i++)
      CHECK_FALSE(obj->GetInlet(i)->GetDocLabel().empty());
    for (int i = 0; i < obj->NumOutputs(); i++)
      CHECK_FALSE(obj->GetOutlet(i)->GetDocLabel().empty());
    // Max's sustain takes no creation arguments, so there is nothing to document
    // and nothing a save has to carry.
    CHECK(obj->GetParamDocs().empty());
  }

  TEST_CASE("sustain: a fresh object has the pedal up and holds nothing (#541)") {
    // The default that matters: an object nobody has touched must pass
    // everything through, or inserting one into a patch would silently swallow
    // every note-off in it.
    Rig rig;
    CHECK_FALSE(rig.obj.Pedal());
    CHECK(rig.obj.Velocity() == 0);
    CHECK(rig.obj.Held() == 0);
  }

  // ─── with the pedal up, nothing happens at all ──────────────────────────────

  TEST_CASE("sustain: with the pedal up a note-on passes through, velocity first (#541)") {
    // Max's outlets fire right to left, and here that is load-bearing rather
    // than cosmetic: everything downstream that takes a pair takes its pitch on
    // a hot inlet and its velocity on a cold one, so a pitch sent first would be
    // paired with the *previous* note's velocity.
    Rig rig;
    rig.Note(60, 100);
    CHECK(rig.out.trace() == "v100 p60");
    CHECK(rig.obj.Held() == 0);
  }

  TEST_CASE("sustain: with the pedal up a note-off passes through too (#541)") {
    // The half that keeps an untouched object transparent. A `.sustain` that
    // held releases with the pedal up would break every patch it was dropped
    // into.
    Rig rig;
    rig.Note(60, 100);
    rig.out.Clear();

    rig.Note(60, 0);
    CHECK(rig.out.trace() == "v0 p60");
    CHECK(rig.obj.Held() == 0);
  }

  TEST_CASE("sustain: nothing is clamped or range-checked on the way through (#541)") {
    // This object holds releases; it does not rewrite notes, and range belongs
    // to the formatters downstream.
    Rig rig;
    rig.Note(60, 200);
    CHECK(rig.obj.Velocity() == 200);
    CHECK(rig.out.trace() == "v200 p60");

    // Negative is not 0, so it is an attack, and it passes unaltered.
    rig.out.Clear();
    rig.Note(64, -5);
    CHECK(rig.out.trace() == "v-5 p64");
    CHECK(rig.obj.Held() == 0);
  }

  // ─── the pedal ──────────────────────────────────────────────────────────────

  TEST_CASE("sustain: pressing the pedal sends nothing and delays no attack (#541)") {
    // A pedal holds notes *on*; it does not hold them off. An implementation
    // that deferred note-ons as well would turn every pedalled passage into
    // silence until the foot came up.
    Rig rig;
    rig.Pedal(true);
    CHECK(rig.obj.Pedal());
    CHECK(rig.out.n() == 0);

    rig.Note(60, 100);
    CHECK(rig.out.trace() == "v100 p60");
    CHECK(rig.obj.Held() == 0);
  }

  TEST_CASE("sustain: with the pedal down a note-off is held back (#541)") {
    // The object. The key comes up, the note does not: nothing reaches the
    // outlets, and the release is remembered against its own pitch.
    Rig rig;
    rig.Pedal(true);
    rig.Note(60, 100);
    rig.out.Clear();

    rig.Note(60, 0);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Held() == 1);
    CHECK(rig.obj.IsHeld(60));
    CHECK_FALSE(rig.obj.IsHeld(61));
  }

  TEST_CASE("sustain: lifting the pedal sends the held note-offs, ascending (#541)") {
    // The other half of the object. The keys come up out of order and the
    // releases come back sorted, because the set is a bitmap and that is the
    // order it walks in — a deterministic order a patch can rely on rather than
    // the accident of arrival.
    Rig rig;
    rig.Pedal(true);
    rig.Note(67, 100);
    rig.Note(60, 100);
    rig.Note(64, 100);
    rig.Note(67, 0);
    rig.Note(60, 0);
    rig.Note(64, 0);
    REQUIRE(rig.obj.Held() == 3);

    rig.out.Clear();
    rig.Pedal(false);

    // Each as a pair with velocity 0 — the release — and the velocity first.
    CHECK(rig.out.trace() == "v0 p60 v0 p64 v0 p67");
    CHECK(rig.obj.Held() == 0);
    CHECK_FALSE(rig.obj.Pedal());
  }

  TEST_CASE("sustain: a key still down when the pedal lifts is not released (#541)") {
    // The case that separates a pedal from a panic button. The set is filled by
    // note-*offs*, so a note whose key never came up owes nothing: releasing it
    // would cut a note the player is still holding.
    Rig rig;
    rig.Pedal(true);
    rig.Note(60, 100);
    rig.Note(64, 100);
    rig.Note(64, 0); // one key comes up, one stays down
    REQUIRE(rig.obj.Held() == 1);

    rig.out.Clear();
    rig.Pedal(false);
    CHECK(rig.out.trace() == "v0 p64");

    // And the key that stayed down still releases normally afterwards.
    rig.out.Clear();
    rig.Note(60, 0);
    CHECK(rig.out.trace() == "v0 p60");
  }

  TEST_CASE("sustain: lifting a pedal that held nothing sends nothing (#541)") {
    // The half of the claim that keeps a lift from being a burst of spurious
    // note-offs: after a phrase played with the pedal up there is nothing owed.
    Rig rig;
    rig.Note(60, 100);
    rig.Note(60, 0);
    rig.Pedal(true);
    rig.out.Clear();

    rig.Pedal(false);
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("sustain: lifting twice sends nothing the second time (#541)") {
    Rig rig;
    rig.Pedal(true);
    rig.Note(60, 100);
    rig.Note(60, 0);
    rig.Pedal(false);
    REQUIRE(rig.obj.Held() == 0);

    rig.out.Clear();
    rig.Pedal(false);
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("sustain: notes pedalled after a lift are held again (#541)") {
    Rig rig;
    rig.Pedal(true);
    rig.Note(60, 100);
    rig.Note(60, 0);
    rig.Pedal(false);
    REQUIRE(rig.obj.Held() == 0);

    rig.Pedal(true);
    rig.Note(67, 100);
    rig.Note(67, 0);
    CHECK(rig.obj.Held() == 1);

    rig.out.Clear();
    rig.Pedal(false);
    CHECK(rig.out.trace() == "v0 p67");
  }

  TEST_CASE("sustain: a repeated note-off for one pitch is one held release (#541)") {
    // What makes the count a count. Without this, a patch that sent a release
    // twice would get two note-offs back out of one lift.
    Rig rig;
    rig.Pedal(true);
    rig.Note(60, 100);
    rig.Note(60, 0);
    rig.Note(60, 0);
    rig.Note(60, 0);
    CHECK(rig.obj.Held() == 1);

    rig.out.Clear();
    rig.Pedal(false);
    CHECK(rig.out.trace() == "v0 p60"); // once, not three times
  }

  TEST_CASE("sustain: replaying a held pitch keeps its one owed release (#541)") {
    // Max's repeat mode 0, the default and the only one implemented: the held
    // release stays held and the new attack goes out on its own. Mode 1 would
    // emit a release this object was asked to swallow; mode 2 would count
    // repeats. Both are a separate ask — see the header.
    Rig rig;
    rig.Pedal(true);
    rig.Note(60, 100);
    rig.Note(60, 0);
    REQUIRE(rig.obj.Held() == 1);

    rig.out.Clear();
    rig.Note(60, 90); // the key goes down again while the pedal is still down
    CHECK(rig.out.trace() == "v90 p60"); // the attack alone — no retrigger release
    CHECK(rig.obj.Held() == 1); // and the pedal still owes exactly one note-off

    rig.out.Clear();
    rig.Pedal(false);
    CHECK(rig.out.trace() == "v0 p60");
  }

  // ─── Max's three commands ───────────────────────────────────────────────────

  TEST_CASE("sustain: 'sustain 1' and 'sustain 0' are the pedal (#541)") {
    // Max's attribute reached by message — "equivalent to pressing or releasing
    // the sustain pedal" — so it must be the same code path, lift included.
    Rig rig;
    rig.List("sustain 1");
    CHECK(rig.obj.Pedal());

    rig.Note(60, 100);
    rig.Note(60, 0);
    REQUIRE(rig.obj.Held() == 1);

    rig.out.Clear();
    rig.List("sustain 0");
    CHECK_FALSE(rig.obj.Pedal());
    CHECK(rig.out.trace() == "v0 p60");
  }

  TEST_CASE("sustain: a bare 'sustain' names no state and is ignored (#541)") {
    // Pressing and lifting are opposite mistakes and neither is the safe one, so
    // a message that gives no state is refused rather than guessed at.
    Rig rig;
    rig.Pedal(true);
    rig.Note(60, 100);
    rig.Note(60, 0);
    REQUIRE(rig.obj.Held() == 1);

    rig.out.Clear();
    rig.List("sustain");
    CHECK(rig.obj.Pedal());
    CHECK(rig.obj.Held() == 1);
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("sustain: 'flush' sends the held note-offs and leaves the pedal down (#541)") {
    // Max: "output all held note-offs". The pedal is not lifted by it — that is
    // the whole difference from `sustain 0`, and a patch that flushed mid-phrase
    // must still be pedalling afterwards.
    Rig rig;
    rig.Pedal(true);
    rig.Note(60, 100);
    rig.Note(64, 100);
    rig.Note(60, 0);
    rig.Note(64, 0);
    REQUIRE(rig.obj.Held() == 2);

    rig.out.Clear();
    rig.List("flush");
    CHECK(rig.out.trace() == "v0 p60 v0 p64");
    CHECK(rig.obj.Held() == 0);
    CHECK(rig.obj.Pedal()); // still down

    // And it really is still down: the next release is held too.
    rig.out.Clear();
    rig.Note(67, 100);
    rig.Note(67, 0);
    CHECK(rig.obj.Held() == 1);
    CHECK(rig.out.trace() == "v100 p67");
  }

  TEST_CASE("sustain: 'clear' empties the set without sending anything (#541)") {
    // Max's second command, and the exact counterpart of `.flush`'s and
    // `.makenote`'s: the one to reach for when the notes have already been
    // released some other way. A `clear` that sent releases would double every
    // note-off in such a patch.
    Rig rig;
    rig.Pedal(true);
    rig.Note(60, 100);
    rig.Note(64, 100);
    rig.Note(60, 0);
    rig.Note(64, 0);
    REQUIRE(rig.obj.Held() == 2);

    rig.out.Clear();
    rig.List("clear");
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Held() == 0);

    // And it really emptied the set: lifting the pedal afterwards sends nothing.
    rig.Pedal(false);
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("sustain: the commands are left-inlet only (#541)") {
    // Max's commands reach the left inlet; the other two take numbers. A word
    // there is neither a velocity, nor a pedal, nor a command.
    Rig rig;
    rig.Pedal(true);
    rig.Note(60, 100);
    rig.Note(60, 0);
    REQUIRE(rig.obj.Held() == 1);

    rig.out.Clear();
    rig.List("flush", 1);
    rig.List("clear", 1);
    rig.List("sustain 0", 1);
    rig.List("flush", 2);
    rig.List("clear", 2);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Held() == 1);
    CHECK(rig.obj.Pedal());
    CHECK(rig.obj.Velocity() == 0); // "60 0" left it at 0; no word changed it
  }

  // ─── the bounds of the set ──────────────────────────────────────────────────

  TEST_CASE("sustain: the whole MIDI note range can be held at once (#541)") {
    // The bitmap's own bounds: 128 pitches is the whole of MIDI, and a lift is
    // bounded by the bitmap rather than by how many keys the player let go of.
    Rig rig;
    rig.Pedal(true);
    rig.Int(0, 1); // every pitch below is a release
    for (int pitch = 0; pitch < 128; pitch++)
      rig.Int(pitch, 0);
    REQUIRE(rig.obj.Held() == 128);
    CHECK(rig.obj.IsHeld(0));
    CHECK(rig.obj.IsHeld(127));
    CHECK(rig.out.n() == 0);

    rig.Pedal(false);
    CHECK(rig.out.n() == (std::size_t)(128 * 2)); // a pitch and a velocity each
    CHECK(rig.obj.Held() == 0);
  }

  TEST_CASE("sustain: a release outside 0-127 goes straight out, pedal or not (#541)") {
    // The safe half of the choice rather than the tidy one. The set is a fixed
    // bitmap, so a release it cannot remember would never be sent by anything —
    // a hanging note — while a release sent early is merely a short note.
    Rig rig;
    rig.Pedal(true);
    rig.Note(200, 0);
    CHECK(rig.out.trace() == "v0 p200");
    CHECK(rig.obj.Held() == 0);
    CHECK_FALSE(rig.obj.IsHeld(200));

    rig.out.Clear();
    rig.Note(-1, 0);
    CHECK(rig.out.trace() == "v0 p-1");
    CHECK(rig.obj.Held() == 0);

    rig.out.Clear();
    rig.Pedal(false);
    CHECK(rig.out.n() == 0);
  }

  // ─── the grammar ────────────────────────────────────────────────────────────

  TEST_CASE("sustain: the middle inlet stores without emitting (#541)") {
    // Only the pitch half completes a note, so only the pitch half can pass one
    // on or have its release held. A middle inlet that emitted would turn every
    // velocity change into a phantom note.
    Rig rig;
    rig.Int(100, 1);
    rig.Float(70.9f, 1);
    rig.List("55", 1);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Held() == 0);
    CHECK(rig.obj.Velocity() == 55);

    rig.Int(60);
    CHECK(rig.out.trace() == "v55 p60");
  }

  TEST_CASE("sustain: the velocity survives across notes (#541)") {
    Rig rig;
    rig.Int(90, 1);
    rig.Int(60);
    rig.Int(64);
    rig.Int(67);
    CHECK(rig.out.trace() == "v90 p60 v90 p64 v90 p67");
  }

  TEST_CASE("sustain: a list is Max's inlet distribution — pitch then velocity (#541)") {
    // The velocity is stored *before* the pitch is read against it, exactly as
    // if it had reached the middle inlet first. A list that read the pitch first
    // would judge the very first note against the wrong velocity — a bug that
    // only ever shows on note one, and the reason this is the shape
    // `.midiparse`'s note outlet can be wired straight into.
    Rig rig;
    rig.Pedal(true);
    rig.List("60 100");
    CHECK(rig.out.trace() == "v100 p60");
    CHECK(rig.obj.Held() == 0);

    // And it persists, as a middle-inlet value would.
    rig.out.Clear();
    rig.Int(62);
    CHECK(rig.out.trace() == "v100 p62");

    // A release spelled as a list is held by the pedal rather than passed on.
    rig.out.Clear();
    rig.List("62 0");
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.IsHeld(62));
    CHECK(rig.obj.Velocity() == 0);

    // Further elements are ignored.
    rig.out.Clear();
    rig.List("64 90 7 7");
    CHECK(rig.out.trace() == "v90 p64");
  }

  TEST_CASE("sustain: a single-token numeric list is a pitch (#541)") {
    // A `.m 60` reaches this inlet as a list carrying "60"; one that did not
    // read it as a pitch would silently pass nothing.
    Rig rig;
    rig.Int(100, 1);
    rig.List("60");
    CHECK(rig.out.trace() == "v100 p60");

    // And anything that is not a number and not a command is not a pitch.
    rig.out.Clear();
    rig.List("wibble");
    rig.List("");
    rig.List("wibble", 1);
    rig.List("wibble", 2);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Velocity() == 100);
    CHECK_FALSE(rig.obj.Pedal());
  }

  TEST_CASE("sustain: a float is converted to an int in every inlet (#541)") {
    Rig rig;
    rig.Float(100.7f, 1);
    CHECK(rig.obj.Velocity() == 100);

    // Truncation is what decides the pedal too: anything in (-1, 1) is a lift.
    rig.Float(0.6f, 2);
    CHECK_FALSE(rig.obj.Pedal());
    rig.Float(1.9f, 2);
    CHECK(rig.obj.Pedal());
    rig.List("0", 2);
    CHECK_FALSE(rig.obj.Pedal());

    rig.Float(60.9f);
    CHECK(rig.out.trace() == "v100 p60");
  }

  TEST_CASE("sustain: a bang does nothing anywhere (#541)") {
    // Max's sustain has no bang method — `flush` is the word that does what a
    // bang does on a `.flush`. Giving a bang the same meaning here would make
    // the two objects differ in the one place a patch would never look.
    Rig rig;
    rig.Pedal(true);
    rig.Note(60, 100);
    rig.Note(60, 0);
    REQUIRE(rig.obj.Held() == 1);
    rig.out.Clear();

    rig.Bang(0);
    rig.Bang(1);
    rig.Bang(2);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Held() == 1);
    CHECK(rig.obj.Pedal());
  }

  TEST_CASE("sustain: Calculate sends nothing (#541)") {
    // The object is driven entirely by its inlets; one that emitted would
    // release a note on every DSP tick from a stimulus no patch sent.
    Rig rig;
    rig.Pedal(true);
    rig.Note(60, 100);
    rig.Note(60, 0);
    rig.out.Clear();
    for (int i = 0; i < 8; i++)
      rig.obj.Calculate(YSE::T_DSP);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Held() == 1);
  }

  TEST_CASE("sustain: an outlet wired back into the inlet is refused, not recursed (#541)") {
    // A lift sends out the pitch outlet, and a patch may wire that outlet back
    // into the left inlet. Without the guard the release would re-enter the set
    // mid-walk; with it the re-entrant message is refused and counted. The pedal
    // still lifts either way — it is stored outside the guard, because an object
    // whose idea of the pedal contradicted the player's foot would stay wrong
    // for as long as the patch ran.
    Rig rig;
    Wire(rig.obj, 0, rig.obj, 0);
    rig.Pedal(true);
    rig.Note(60, 100);
    rig.Note(60, 0);
    REQUIRE(rig.obj.Held() == 1);

    const std::uint64_t before = rig.obj.Dropped();
    rig.Pedal(false);
    CHECK(rig.obj.Dropped() > before);
    CHECK_FALSE(rig.obj.Pedal());
    CHECK(rig.obj.Held() == 0);
  }

  // ─── what a save carries ────────────────────────────────────────────────────

  TEST_CASE("sustain: it survives a DumpJSON / ParseJSON round trip (#541)") {
    // There are no creation arguments to carry — Max's sustain takes none — so
    // what a save has to preserve is the object itself, under its own name.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* obj = src.CreateObject(YSE::OBJ::M_SUSTAIN, "");
    REQUIRE(obj != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".sustain") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".sustain");
    CHECK(std::string(copy->GetParams()).empty());
  }

  // ─── end to end, in a real patcher ──────────────────────────────────────────

  TEST_CASE("sustain: the pedal really holds the note down through a real chain (#541)") {
    // The issue's claim at the level a patch makes it: a real `.midiparse`
    // decodes real bytes into a registry-built `.sustain` into a real `.noteon`,
    // the pedal goes down, the key goes down and comes back up — and what has
    // reached the synth is a note-on and nothing else. That is not a bug here,
    // it is the object working: the note is meant to keep sounding until the
    // foot lifts. Lifting it is what finally releases the key.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* parse = p.CreateObject(YSE::OBJ::M_PARSE, "");
    YSE::pHandle* sustain = p.CreateObject(YSE::OBJ::M_SUSTAIN, "");
    YSE::pHandle* noteon = p.CreateObject(YSE::OBJ::M_NOTEON, "");
    REQUIRE(parse != nullptr);
    REQUIRE(sustain != nullptr);
    REQUIRE(noteon != nullptr);

    p.Connect(parse, 0, sustain, 0); // the note pair, on one cord
    p.Connect(sustain, 1, noteon, 1); // velocity, cold
    p.Connect(sustain, 0, noteon, 0); // pitch, hot — this is what sends
    p.Connect(noteon, 0, &tapHandle, 0);

    sustain->SetIntData(2, 1); // the pedal goes down

    const int down[] = {0x90, 60, 100};
    for (int b : down)
      parse->SetIntData(0, b);
    REQUIRE(tap.log.size() == 1);
    REQUIRE(tap.log[0].size() == 4);
    CHECK((int)(unsigned char)tap.log[0][3] == 100); // sounding

    tap.log.clear();
    const int up[] = {0x90, 60, 0}; // the key comes up under the pedal
    for (int b : up)
      parse->SetIntData(0, b);
    CHECK(tap.log.empty()); // and the synth never hears about it

    sustain->SetIntData(2, 0); // the foot lifts
    REQUIRE(tap.log.size() == 1);
    const std::string& message = tap.log[0];
    REQUIRE(message.size() == 4);
    CHECK((int)(unsigned char)message[1] == 0x90);
    CHECK((int)(unsigned char)message[2] == 60);
    CHECK((int)(unsigned char)message[3] == 0); // released at last
  }

  TEST_CASE("sustain: the releases it sends read back as releases through .midiparse (#541)") {
    // The pairs are not merely plausible: assembled by a real `.pack`, encoded
    // into wire bytes by a real `.midiformat` and decoded again by a real
    // `.midiparse`, they come out the far end as the note-offs they are meant to
    // be. This is also the chain that shows the outlet *order* is right — a
    // pitch sent before the velocity would leave `.pack` holding the previous
    // note's velocity when its hot inlet fired.
    Tap note;
    YSE::pHandle noteHandle(&note);

    YSE::patcher patch;
    patch.create(2);

    YSE::pHandle* sustain = patch.CreateObject(YSE::OBJ::M_SUSTAIN, "");
    YSE::pHandle* pack = patch.CreateObject(YSE::OBJ::G_PACK, "0 0");
    YSE::pHandle* format = patch.CreateObject(YSE::OBJ::M_FORMAT, "");
    YSE::pHandle* parse = patch.CreateObject(YSE::OBJ::M_PARSE, "");
    REQUIRE(sustain != nullptr);
    REQUIRE(pack != nullptr);
    REQUIRE(format != nullptr);
    REQUIRE(parse != nullptr);

    patch.Connect(sustain, 1, pack, 1); // velocity, cold
    patch.Connect(sustain, 0, pack, 0); // pitch, hot — this is what sends
    patch.Connect(pack, 0, format, 0); // the "pitch velocity" list
    patch.Connect(format, 0, parse, 0);
    patch.Connect(parse, 0, &noteHandle, 0); // the note outlet

    sustain->SetIntData(2, 1);
    sustain->SetIntData(1, 100);
    sustain->SetIntData(0, 72);
    REQUIRE(note.trace() == "l72 100");

    note.log.clear();
    sustain->SetIntData(1, 0);
    sustain->SetIntData(0, 72); // the key comes up, and the pedal keeps it
    REQUIRE(note.log.empty());

    sustain->SetIntData(2, 0);
    CHECK(note.trace() == "l72 0"); // pitch, velocity 0 — a release

    patch.DeleteObject(sustain);
    patch.DeleteObject(pack);
    patch.DeleteObject(format);
    patch.DeleteObject(parse);
  }

  // ─── teardown (issue #758) ──────────────────────────────────────────────────

  TEST_CASE("sustain: a cleared patcher sends what the pedal was holding (#541, #758)") {
    // The failure this prevents is the worst one this object can cause: it
    // *swallowed* the note-off, so nobody else in the patch is holding a copy of
    // it. A patcher torn down mid-pedal without this leaves the rack sounding
    // with nothing left anywhere to lift the foot.
    //
    // The tap stands in for `.midiout` and a device, and is declared before the
    // patcher so the patcher dies first and the stop pass has somewhere to send.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* sustain = p.CreateObject(YSE::OBJ::M_SUSTAIN, "");
    REQUIRE(sustain != nullptr);
    p.Connect(sustain, 1, &tapHandle, 1);
    p.Connect(sustain, 0, &tapHandle, 0);

    sustain->SetIntData(2, 1);
    sustain->SetIntData(1, 100);
    sustain->SetIntData(0, 60);
    sustain->SetIntData(1, 0);
    sustain->SetIntData(0, 60); // the key comes up under the pedal
    REQUIRE(tap.trace() == "v100 i60");

    tap.log.clear();
    p.Clear();
    // The release the pedal swallowed, sent by the teardown pass — down a cord
    // that would have been gone had Clear unwired as it walked.
    CHECK(tap.trace() == "v0 i60");
  }

  TEST_CASE("sustain: a destroyed patcher sends them too (#541, #758)") {
    // ~patcherImplementation calls Clear(), so the destructor is the same route.
    // It is worth its own case because it is the one a host actually takes:
    // nobody clears a patcher on the way out, they drop it.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    {
      patcherImplementation p(1, nullptr);
      YSE::pHandle* sustain = p.CreateObject(YSE::OBJ::M_SUSTAIN, "");
      REQUIRE(sustain != nullptr);
      p.Connect(sustain, 1, &tapHandle, 1);
      p.Connect(sustain, 0, &tapHandle, 0);

      sustain->SetIntData(2, 1);
      sustain->SetIntData(1, 100);
      sustain->SetIntData(0, 62);
      sustain->SetIntData(1, 0);
      sustain->SetIntData(0, 62);
      REQUIRE(tap.log.size() == 2);
      tap.log.clear();
    }

    CHECK(tap.trace() == "v0 i62");
  }

  TEST_CASE("sustain: deleting the object on its own sends them (#541, #758)") {
    // DeleteObject is the second teardown route and has no Clear() to hook. An
    // object removed while the pedal is holding releases still owes them, and
    // the rest of the patch is still there to receive them.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* sustain = p.CreateObject(YSE::OBJ::M_SUSTAIN, "");
    REQUIRE(sustain != nullptr);
    p.Connect(sustain, 1, &tapHandle, 1);
    p.Connect(sustain, 0, &tapHandle, 0);

    sustain->SetIntData(2, 1);
    sustain->SetIntData(1, 0);
    sustain->SetIntData(0, 64);
    sustain->SetIntData(0, 67);
    REQUIRE(tap.log.empty()); // both releases held by the pedal

    p.DeleteObject(sustain);
    CHECK(tap.trace() == "v0 i64 v0 i67");
  }

  TEST_CASE("sustain: a teardown with the pedal holding nothing sends nothing (#541, #758)") {
    // The other half of the claim, and the one that keeps the pass from being a
    // burst of spurious releases at every Clear.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* sustain = p.CreateObject(YSE::OBJ::M_SUSTAIN, "");
    REQUIRE(sustain != nullptr);
    p.Connect(sustain, 1, &tapHandle, 1);
    p.Connect(sustain, 0, &tapHandle, 0);

    sustain->SetIntData(2, 1);
    sustain->SetIntData(1, 100);
    sustain->SetIntData(0, 60);
    sustain->SetIntData(2, 0); // the foot lifts before the key does
    sustain->SetIntData(1, 0);
    sustain->SetIntData(0, 60); // and the release passes straight through
    REQUIRE(tap.log.size() == 4);
    tap.log.clear();

    p.Clear();
    CHECK(tap.log.empty());
  }

  // ─── real-time discipline ───────────────────────────────────────────────────

  TEST_CASE("sustain: the message paths allocate nothing (#541)") {
    // Everything a message can do, measured on the thread that does it. A note
    // routinely arrives on the audio callback — an in-patcher dispatch runs on
    // T_DSP — so every one of these paths is audio-thread code, the pedal lift
    // and the teardown included.
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
    const std::string clear = "clear";
    const std::string flush = "flush";
    const std::string down = "sustain 1";
    const std::string up = "sustain 0";
    const std::string bare = "sustain";
    {
      TestHelpers::ProbeScope probe;
      rig.Int(1, 2);
      rig.Float(0.2f, 2);
      rig.List(one, 2);
      rig.List(word, 2);
      rig.Int(100, 1);
      rig.Float(90.5f, 1);
      rig.List(vel, 1);
      rig.List(word, 1);
      rig.Int(60);
      rig.Float(62.5f);
      rig.List(pair);
      rig.List(one);
      rig.List(down);
      rig.List(bare);
      rig.Int(0, 1);
      rig.Int(60); // a release, swallowed by the pedal
      rig.List(release);
      rig.List(word);
      rig.List(flush); // the send, walking the whole bitmap
      rig.Int(62);
      rig.List(clear);
      rig.List(up); // the lift
      rig.obj.Teardown(YSE::T_GUI);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    // The probed messages really did something — an assertion that only proves
    // nothing happened proves nothing.
    CHECK(rig.out.n() > 0);
  }
}
