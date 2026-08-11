// Tests for `.flush` — Max's flush, which "keeps track of all note-ons passed
// through it, and produces note-off messages for any held notes when it
// receives a bang" (issue #540).
//
// The trap this object sets is that it looks like `.midiflush` and is not. That
// one watches a MIDI *byte stream* and releases what the stream left sounding,
// per channel, as raw bytes; this one watches *pitch/velocity pairs* on
// ordinary cords and releases what the patcher is holding, as pairs, with no
// channel at all. An implementation that quietly filtered (that is
// `.stripnote`), or that forgot to pass a release through, or that released a
// note twice, would still pass a naive "does a bang send something" test. So
// every case that checks something is sent is paired with one that checks
// something is *not*.
//
// Four layers, and they are not interchangeable:
//
//   - **standalone cases** pin the grammar and the shape: what each inlet
//     accepts, that a list is Max's inlet distribution, that the right inlet
//     stores without emitting, that the velocity outlet fires before the pitch
//     outlet, and that nothing is filtered or clamped on the way through.
//
//   - **the held set**, which is the object: what marks a note, what clears
//     one, what a bang releases and in which order, and what `clear` does
//     instead.
//
//   - **end-to-end cases** run real chains inside a real patcher — a
//     `.midiparse` feeding a registry-built `.flush`, and a `.flush` feeding a
//     real `.noteon` — and ask the question a patch asks: after the bang, is
//     the synth still holding a key down? The control case proves the rig can
//     see a hanging note before the case that proves `.flush` clears it.
//
//   - **teardown cases** (issue #758) take the three routes a patch really
//     dies by: Clear(), the destructor, and DeleteObject().
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
#include "patcher/midi/mFlush.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using YSE::PATCHER::mFlush;
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
      return "flush_notes";
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
      return "flush_tap";
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

  // A standalone `.flush` with both outlets watched. The object needs no clock
  // and no patcher, so standalone is the whole of its message behaviour — the
  // one thing it cannot show is the teardown hook, which has its own cases.
  //
  // The sink is declared **before** the object so it is destroyed after it —
  // see Wire on why that matters even for a symmetric edge.
  struct Rig {
    Notes out;
    mFlush obj;

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
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── the object exists ──────────────────────────────────────────────────────

  TEST_CASE("flush: creatable through the registry (#540)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::M_FLUSH);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".flush");
  }

  TEST_CASE("flush: listed by pRegistry::AllNames (#540)") {
    auto names = Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".flush")) != names.end());
  }

  TEST_CASE("flush: the shape is two inlets and two int outlets (#540)") {
    // Max's shape, and `.stripnote`'s — not `.midiflush`'s one-in-one-out byte
    // stream. The shape *is* the difference between the two objects.
    mFlush obj;
    CHECK(obj.NumInputs() == 2);
    CHECK(obj.NumOutputs() == 2);
    CHECK(obj.GetOutputType(0) == YSE::OUT_TYPE::INT);
    CHECK(obj.GetOutputType(1) == YSE::OUT_TYPE::INT);
    CHECK(obj.GetCategory() == YSE::PATCHER::pCategory::MIDI);
    CHECK_FALSE(obj.IsDSPObject());
    CHECK_FALSE(obj.WantsBlockPoll());
  }

  TEST_CASE("flush: documents itself (#540)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::M_FLUSH));
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == std::string(YSE::OBJ::M_FLUSH));
    CHECK_FALSE(obj->GetDescription().empty());
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MIDI);
    for (int i = 0; i < obj->NumInputs(); i++)
      CHECK_FALSE(obj->GetInlet(i)->GetDocLabel().empty());
    for (int i = 0; i < obj->NumOutputs(); i++)
      CHECK_FALSE(obj->GetOutlet(i)->GetDocLabel().empty());
    // Max's flush takes no creation arguments, so there is nothing to document
    // and nothing a save has to carry.
    CHECK(obj->GetParamDocs().empty());
  }

  TEST_CASE("flush: a fresh object holds nothing and pairs with velocity 0 (#540)") {
    Rig rig;
    CHECK(rig.obj.Velocity() == 0);
    CHECK(rig.obj.Held() == 0);
  }

  // ─── everything passes through, which is not `.stripnote` ───────────────────

  TEST_CASE("flush: a note-on passes through, velocity outlet first (#540)") {
    // Max's outlets fire right to left, and here that is load-bearing rather
    // than cosmetic: everything downstream that takes a pair takes its pitch on
    // a hot inlet and its velocity on a cold one, so a pitch sent first would be
    // paired with the *previous* note's velocity.
    Rig rig;
    rig.Note(60, 100);
    CHECK(rig.out.trace() == "v100 p60");
  }

  TEST_CASE("flush: a release passes through too — this is not .stripnote (#540)") {
    // The distinction that would be easiest to get wrong by copying the wrong
    // sibling. `.stripnote` (#539) *drops* a velocity-0 pair; this object passes
    // it on and merely stops remembering the note. A `.flush` that swallowed
    // releases would break every patch it was inserted into, because the
    // note-off would never reach the synth at all.
    Rig rig;
    rig.Note(60, 100);
    REQUIRE(rig.out.trace() == "v100 p60");

    rig.out.Clear();
    rig.Note(60, 0);
    CHECK(rig.out.trace() == "v0 p60");
    CHECK(rig.obj.Held() == 0);
  }

  TEST_CASE("flush: nothing is clamped or range-checked on the way through (#540)") {
    // This object remembers notes; it does not rewrite them, and range belongs
    // to the formatters downstream.
    Rig rig;
    rig.Note(60, 200);
    CHECK(rig.obj.Velocity() == 200);
    CHECK(rig.out.trace() == "v200 p60");

    // Negative is not 0, so it is a note-on, and it passes unaltered.
    rig.out.Clear();
    rig.Note(64, -5);
    CHECK(rig.out.trace() == "v-5 p64");
    CHECK(rig.obj.IsHeld(64));
  }

  // ─── the held set ───────────────────────────────────────────────────────────

  TEST_CASE("flush: a note-on is remembered and its release forgets it (#540)") {
    Rig rig;
    rig.Note(60, 100);
    CHECK(rig.obj.Held() == 1);
    CHECK(rig.obj.IsHeld(60));
    CHECK_FALSE(rig.obj.IsHeld(61));

    rig.Note(60, 0);
    CHECK(rig.obj.Held() == 0);
    CHECK_FALSE(rig.obj.IsHeld(60));
  }

  TEST_CASE("flush: a repeated note-on is one note, and a stray release is nothing (#540)") {
    // What makes the count a count. Without this, a patch that retriggers a held
    // pitch would leave a phantom entry that a bang releases a second time.
    Rig rig;
    rig.Note(60, 100);
    rig.Note(60, 90);
    rig.Note(60, 80);
    CHECK(rig.obj.Held() == 1);

    // A release for a pitch that is not sounding changes nothing.
    rig.Note(72, 0);
    CHECK(rig.obj.Held() == 1);

    rig.out.Clear();
    rig.Bang();
    CHECK(rig.out.trace() == "v0 p60"); // once, not three times
    CHECK(rig.obj.Held() == 0);
  }

  TEST_CASE("flush: a bang releases every sounding note in ascending pitch order (#540)") {
    // The object. The notes go in out of order and come back sorted, because the
    // set is a bitmap and that is the order it walks in — a deterministic order
    // a patch can rely on rather than the accident of arrival.
    Rig rig;
    rig.Note(67, 100);
    rig.Note(60, 90);
    rig.Note(64, 80);
    REQUIRE(rig.obj.Held() == 3);

    rig.out.Clear();
    rig.Bang();

    // Each as a pair with velocity 0 — the release — and the velocity first.
    CHECK(rig.out.trace() == "v0 p60 v0 p64 v0 p67");
    CHECK(rig.obj.Held() == 0);
  }

  TEST_CASE("flush: banging twice sends nothing the second time (#540)") {
    Rig rig;
    rig.Note(60, 100);
    rig.Bang();
    REQUIRE(rig.obj.Held() == 0);

    rig.out.Clear();
    rig.Bang();
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("flush: a bang with nothing sounding sends nothing (#540)") {
    // The half of the claim that keeps the object from being a burst of
    // spurious note-offs: after a properly balanced phrase there is nothing to
    // release, and a bang must be silent.
    Rig rig;
    rig.Note(60, 100);
    rig.Note(60, 0);
    REQUIRE(rig.obj.Held() == 0);

    rig.out.Clear();
    rig.Bang();
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("flush: notes played after a flush are tracked again (#540)") {
    Rig rig;
    rig.Note(60, 100);
    rig.Bang();
    REQUIRE(rig.obj.Held() == 0);

    rig.Note(67, 100);
    CHECK(rig.obj.Held() == 1);

    rig.out.Clear();
    rig.Bang();
    CHECK(rig.out.trace() == "v0 p67");
  }

  TEST_CASE("flush: 'clear' empties the set without sending anything (#540)") {
    // Max's second command, and the exact counterpart of `.makenote`'s: the one
    // to reach for when the notes have already been released some other way. A
    // `clear` that sent releases would double every note-off in such a patch.
    Rig rig;
    rig.Note(60, 100);
    rig.Note(64, 100);
    REQUIRE(rig.obj.Held() == 2);

    rig.out.Clear();
    rig.List("clear");
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Held() == 0);

    // And it really emptied the set: a bang afterwards has nothing to send.
    rig.Bang();
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("flush: 'clear' in the right inlet is not a command (#540)") {
    // Max's commands are left-inlet only, and the right inlet takes numbers. A
    // word there is neither a velocity nor a clear.
    Rig rig;
    rig.Note(60, 100);
    REQUIRE(rig.obj.Held() == 1);

    rig.List("clear", 1);
    CHECK(rig.obj.Held() == 1);
    CHECK(rig.obj.Velocity() == 100);
  }

  TEST_CASE("flush: the whole MIDI note range can be held at once (#540)") {
    // The bitmap's own bounds: 128 pitches is the whole of MIDI, and the flush
    // is bounded by the bitmap rather than by how many notes a patch stranded.
    Rig rig;
    rig.Int(100, 1);
    for (int pitch = 0; pitch < 128; pitch++)
      rig.Int(pitch, 0);
    REQUIRE(rig.obj.Held() == 128);
    CHECK(rig.obj.IsHeld(0));
    CHECK(rig.obj.IsHeld(127));

    rig.out.Clear();
    rig.Bang();
    CHECK(rig.out.n() == (std::size_t)(128 * 2)); // a pitch and a velocity each
    CHECK(rig.obj.Held() == 0);
  }

  TEST_CASE("flush: a pitch outside 0-127 passes through but is not tracked (#540)") {
    // The honest answer rather than a clamp: filing note 200 under pitch 127
    // would make a bang release a note nobody played. It still passes through —
    // a patch driving a non-MIDI synth through wider values keeps its values.
    Rig rig;
    rig.Note(200, 100);
    CHECK(rig.out.trace() == "v100 p200");
    CHECK(rig.obj.Held() == 0);
    CHECK_FALSE(rig.obj.IsHeld(200));

    rig.out.Clear();
    rig.Note(-1, 100);
    CHECK(rig.out.trace() == "v100 p-1");
    CHECK(rig.obj.Held() == 0);

    rig.out.Clear();
    rig.Bang();
    CHECK(rig.out.n() == 0);
  }

  // ─── the grammar ────────────────────────────────────────────────────────────

  TEST_CASE("flush: the right inlet stores without emitting (#540)") {
    // Only the pitch half completes a note, so only the pitch half can pass one
    // on or mark one sounding. A right inlet that emitted would turn every
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

  TEST_CASE("flush: the velocity survives across notes (#540)") {
    Rig rig;
    rig.Int(90, 1);
    rig.Int(60);
    rig.Int(64);
    rig.Int(67);
    CHECK(rig.out.trace() == "v90 p60 v90 p64 v90 p67");
    CHECK(rig.obj.Held() == 3);
  }

  TEST_CASE("flush: a list is Max's inlet distribution — pitch then velocity (#540)") {
    // The velocity is stored *before* the pitch is read against it, exactly as
    // if it had reached the right inlet first. A list that read the pitch first
    // would track the very first note against the wrong velocity — a bug that
    // only ever shows on note one, and the reason this is the shape
    // `.midiparse`'s note outlet can be wired straight into.
    Rig rig;
    rig.List("60 100");
    CHECK(rig.out.trace() == "v100 p60");
    CHECK(rig.obj.IsHeld(60));

    // And it persists, as a right-inlet value would.
    rig.out.Clear();
    rig.Int(62);
    CHECK(rig.out.trace() == "v100 p62");

    // A release spelled as a list passes through and clears the note.
    rig.out.Clear();
    rig.List("62 0");
    CHECK(rig.out.trace() == "v0 p62");
    CHECK_FALSE(rig.obj.IsHeld(62));
    CHECK(rig.obj.Velocity() == 0);

    // Further elements are ignored.
    rig.out.Clear();
    rig.List("64 90 7 7");
    CHECK(rig.out.trace() == "v90 p64");
  }

  TEST_CASE("flush: a single-token numeric list is a pitch (#540)") {
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
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Velocity() == 100);
    CHECK(rig.obj.Held() == 1);
  }

  TEST_CASE("flush: a float is converted to an int in both inlets (#540)") {
    Rig rig;
    rig.Float(100.7f, 1);
    CHECK(rig.obj.Velocity() == 100);
    rig.Float(60.9f);
    CHECK(rig.out.trace() == "v100 p60");
    CHECK(rig.obj.IsHeld(60));
  }

  TEST_CASE("flush: a bang in the right inlet does nothing (#540)") {
    // Max's bang method is on the left inlet. One on the right must not flush,
    // or a patch banging a velocity source would silently kill its own notes.
    Rig rig;
    rig.Note(60, 100);
    rig.out.Clear();

    rig.Bang(1);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Held() == 1);
  }

  TEST_CASE("flush: Calculate sends nothing (#540)") {
    // The object is driven entirely by its inlets; one that emitted would
    // release a note on every DSP tick from a stimulus no patch sent.
    Rig rig;
    rig.Note(60, 100);
    rig.out.Clear();
    for (int i = 0; i < 8; i++)
      rig.obj.Calculate(YSE::T_DSP);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Held() == 1);
  }

  TEST_CASE("flush: an outlet wired back into the inlet is refused, not recursed (#540)") {
    // A flush sends out the pitch outlet, and a patch may wire that outlet back
    // into the left inlet. Without the guard the release would re-enter the set
    // mid-walk; with it the re-entrant message is refused and counted.
    Rig rig;
    Wire(rig.obj, 0, rig.obj, 0);
    rig.Note(60, 100);
    REQUIRE(rig.obj.Held() == 1);

    const std::uint64_t before = rig.obj.Dropped();
    rig.Bang();
    CHECK(rig.obj.Dropped() > before);
    CHECK(rig.obj.Held() == 0);
  }

  // ─── what a save carries ────────────────────────────────────────────────────

  TEST_CASE("flush: it survives a DumpJSON / ParseJSON round trip (#540)") {
    // There are no creation arguments to carry — Max's flush takes none — so
    // what a save has to preserve is the object itself, under its own name.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* obj = src.CreateObject(YSE::OBJ::M_FLUSH, "");
    REQUIRE(obj != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".flush") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".flush");
    CHECK(std::string(copy->GetParams()).empty());
  }

  // ─── end to end, in a real patcher ──────────────────────────────────────────

  TEST_CASE("flush: the rig can see a hanging note (#540)") {
    // The control for the case below, and it is not optional: a test that only
    // asserted "a release arrived" would pass just as happily against a chain
    // that never sounded anything. Here a real `.midiparse` decodes real bytes
    // into a real `.flush` into a real `.noteon`, the patch is interrupted after
    // the attack, and what has reached the synth is a note-on and nothing else —
    // a key held down with nobody left to lift it. That is the bug.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* parse = p.CreateObject(YSE::OBJ::M_PARSE, "");
    YSE::pHandle* flush = p.CreateObject(YSE::OBJ::M_FLUSH, "");
    YSE::pHandle* noteon = p.CreateObject(YSE::OBJ::M_NOTEON, "");
    REQUIRE(parse != nullptr);
    REQUIRE(flush != nullptr);
    REQUIRE(noteon != nullptr);

    p.Connect(parse, 0, flush, 0); // the note pair, on one cord
    p.Connect(flush, 1, noteon, 1); // velocity, cold
    p.Connect(flush, 0, noteon, 0); // pitch, hot — this is what sends
    p.Connect(noteon, 0, &tapHandle, 0);

    const int bytes[] = {0x90, 60, 100}; // a key goes down, and the patch stops
    for (int b : bytes)
      parse->SetIntData(0, b);

    REQUIRE(tap.log.size() == 1);
    const std::string message = tap.log[0];
    REQUIRE(message.size() == 4); // "l" + three binary MIDI bytes
    CHECK((int)(unsigned char)message[1] == 0x90);
    CHECK((int)(unsigned char)message[2] == 60);
    CHECK((int)(unsigned char)message[3] == 100); // sounding, and nothing released it
  }

  TEST_CASE("flush: a bang releases what the patch left hanging (#540)") {
    // The issue's claim at the level a patch makes it: real cords, a real
    // `.midiparse` in front and a real `.noteon` behind, and the bang that turns
    // the hanging note of the control case into a released one.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* parse = p.CreateObject(YSE::OBJ::M_PARSE, "");
    YSE::pHandle* flush = p.CreateObject(YSE::OBJ::M_FLUSH, "");
    YSE::pHandle* noteon = p.CreateObject(YSE::OBJ::M_NOTEON, "");
    REQUIRE(parse != nullptr);
    REQUIRE(flush != nullptr);
    REQUIRE(noteon != nullptr);

    p.Connect(parse, 0, flush, 0);
    p.Connect(flush, 1, noteon, 1);
    p.Connect(flush, 0, noteon, 0);
    p.Connect(noteon, 0, &tapHandle, 0);

    const int bytes[] = {0x90, 60, 100, 0x90, 67, 90};
    for (int b : bytes)
      parse->SetIntData(0, b);
    REQUIRE(tap.log.size() == 2);

    tap.log.clear();
    flush->SetBang(0);

    // Two note-ons with velocity 0, which is a release in MIDI just as much as a
    // 0x80 is, and is what `.noteon` builds out of the pair.
    REQUIRE(tap.log.size() == 2);
    for (const std::string& entry : tap.log) {
      REQUIRE(entry.size() == 4);
      CHECK((int)(unsigned char)entry[1] == 0x90);
      CHECK((int)(unsigned char)entry[3] == 0); // the release
    }
    CHECK((int)(unsigned char)tap.log[0][2] == 60);
    CHECK((int)(unsigned char)tap.log[1][2] == 67);
  }

  TEST_CASE("flush: the releases it sends read back as releases through .midiparse (#540)") {
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

    YSE::pHandle* flush = patch.CreateObject(YSE::OBJ::M_FLUSH, "");
    YSE::pHandle* pack = patch.CreateObject(YSE::OBJ::G_PACK, "0 0");
    YSE::pHandle* format = patch.CreateObject(YSE::OBJ::M_FORMAT, "");
    YSE::pHandle* parse = patch.CreateObject(YSE::OBJ::M_PARSE, "");
    REQUIRE(flush != nullptr);
    REQUIRE(pack != nullptr);
    REQUIRE(format != nullptr);
    REQUIRE(parse != nullptr);

    patch.Connect(flush, 1, pack, 1); // velocity, cold
    patch.Connect(flush, 0, pack, 0); // pitch, hot — this is what sends
    patch.Connect(pack, 0, format, 0); // the "pitch velocity" list
    patch.Connect(format, 0, parse, 0);
    patch.Connect(parse, 0, &noteHandle, 0); // the note outlet

    flush->SetIntData(1, 100);
    flush->SetIntData(0, 72);
    REQUIRE(note.trace() == "l72 100");

    note.log.clear();
    flush->SetBang(0);
    CHECK(note.trace() == "l72 0"); // pitch, velocity 0 — a release

    patch.DeleteObject(flush);
    patch.DeleteObject(pack);
    patch.DeleteObject(format);
    patch.DeleteObject(parse);
  }

  // ─── teardown (issue #758) ──────────────────────────────────────────────────

  TEST_CASE("flush: a cleared patcher releases what it was holding (#540, #758)") {
    // The tap stands in for `.midiout` and a device, and is declared before the
    // patcher so the patcher dies first and the stop pass has somewhere to send.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* flush = p.CreateObject(YSE::OBJ::M_FLUSH, "");
    REQUIRE(flush != nullptr);
    p.Connect(flush, 1, &tapHandle, 1);
    p.Connect(flush, 0, &tapHandle, 0);

    flush->SetIntData(1, 100);
    flush->SetIntData(0, 60);
    REQUIRE(tap.trace() == "v100 i60");

    tap.log.clear();
    p.Clear();
    // The release the patch never sent, sent by the teardown pass — down a cord
    // that would have been gone had Clear unwired as it walked.
    CHECK(tap.trace() == "v0 i60");
  }

  TEST_CASE("flush: a destroyed patcher releases too (#540, #758)") {
    // ~patcherImplementation calls Clear(), so the destructor is the same route.
    // It is worth its own case because it is the one a host actually takes:
    // nobody clears a patcher on the way out, they drop it.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    {
      patcherImplementation p(1, nullptr);
      YSE::pHandle* flush = p.CreateObject(YSE::OBJ::M_FLUSH, "");
      REQUIRE(flush != nullptr);
      p.Connect(flush, 1, &tapHandle, 1);
      p.Connect(flush, 0, &tapHandle, 0);

      flush->SetIntData(1, 100);
      flush->SetIntData(0, 62);
      REQUIRE(tap.log.size() == 2);
      tap.log.clear();
    }

    CHECK(tap.trace() == "v0 i62");
  }

  TEST_CASE("flush: deleting the object on its own releases its notes (#540, #758)") {
    // DeleteObject is the second teardown route and has no Clear() to hook. An
    // object removed while it is holding notes still owes them their releases,
    // and the rest of the patch is still there to receive them.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* flush = p.CreateObject(YSE::OBJ::M_FLUSH, "");
    REQUIRE(flush != nullptr);
    p.Connect(flush, 1, &tapHandle, 1);
    p.Connect(flush, 0, &tapHandle, 0);

    flush->SetIntData(1, 100);
    flush->SetIntData(0, 64);
    flush->SetIntData(0, 67);
    REQUIRE(tap.log.size() == 4);
    tap.log.clear();

    p.DeleteObject(flush);
    CHECK(tap.trace() == "v0 i64 v0 i67");
  }

  TEST_CASE("flush: a teardown with nothing sounding sends nothing (#540, #758)") {
    // The other half of the claim, and the one that keeps the pass from being a
    // burst of spurious releases at every Clear.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* flush = p.CreateObject(YSE::OBJ::M_FLUSH, "");
    REQUIRE(flush != nullptr);
    p.Connect(flush, 1, &tapHandle, 1);
    p.Connect(flush, 0, &tapHandle, 0);

    flush->SetIntData(1, 100);
    flush->SetIntData(0, 60);
    flush->SetIntData(1, 0);
    flush->SetIntData(0, 60); // the key comes up
    REQUIRE(tap.log.size() == 4);
    tap.log.clear();

    p.Clear();
    CHECK(tap.log.empty());
  }

  // ─── real-time discipline ───────────────────────────────────────────────────

  TEST_CASE("flush: the message paths allocate nothing (#540)") {
    // Everything a message can do, measured on the thread that does it. A note
    // routinely arrives on the audio callback — an in-patcher dispatch runs on
    // T_DSP — so every one of these paths is audio-thread code, the flush and
    // the teardown included.
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
      rig.List(release);
      rig.List(word);
      rig.Bang(); // the flush, walking the whole bitmap
      rig.Int(60);
      rig.List(clear);
      rig.obj.Teardown(YSE::T_GUI);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    // The probed messages really did something — an assertion that only proves
    // nothing happened proves nothing.
    CHECK(rig.out.n() > 0);
  }
}
