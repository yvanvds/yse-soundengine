// Tests for `.midiflush` (issue #537) — the object that releases the notes a
// patch stopped mid-phrase left hanging.
//
// What is being pinned:
//
//   - **the pass-through**: everything that arrives leaves untouched and in the
//     spelling it arrived in, so the object can be dropped into an existing
//     stream without changing what the device receives;
//   - **the tracking**: which notes a byte stream leaves sounding, through the
//     three things that make MIDI harder than it looks — running status, a
//     real-time byte between the two data bytes of a note, and a
//     system-exclusive dump full of bytes that would read as notes;
//   - **both spellings of a release**: a note-off message and a note-on with
//     velocity 0 both clear a note, so a flush never re-releases what the
//     device already let go;
//   - **the flush**: one note-off per sounding note, on the channel it was
//     played on, and an emptied set afterwards;
//   - **the three spellings of the input**: an int per byte (`.midiin`'s
//     shape), a numeric byte list (`.midiformat`'s) and a binary one (what
//     `.noteon` and the older senders build);
//   - **no platform guard at all**, which is `.midiparse`'s claim and this
//     object's: it opens no device, so a patch does not lose its safety valve
//     on a phone.
//
// The end-to-end section drives a real `patcherImplementation`: `.noteon` and
// `.midiformat` built through the registry, wired to a registry-built
// `.midiflush` with real cords, fed through real inlets and read through real
// ones. That is the level at which "a stuck note can now be cleared" is either
// true or not — a unit test of the bitmap would pass just as happily on an
// object nothing could wire.
//
// No audio device and no MIDI hardware required.

#include <doctest/doctest.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "patcher/midi/mMidiFlush.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObject.h"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "sinks.hpp"

using YSE::PATCHER::mMidiFlush;
using YSE::PATCHER::Register;

namespace {

  // Records every message that arrived, in order and in the shape it arrived
  // in — an int logged as an int and a list as its text. Order and shape are
  // both claims here ("the stream leaves in the spelling it arrived in"), so a
  // sink that normalised either could not test them.
  struct Tap : YSE::PATCHER::pObject {
    std::vector<std::string>* log = nullptr;

    Tap() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        if (log) log->push_back("i" + std::to_string(v));
      });
      inputs.back().RegisterList([this](const std::string& v, int, YSE::THREAD) {
        if (log) log->push_back("l" + v);
      });
    }
    const char* Type() const override {
      return "midiflush_tap";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A standalone `.midiflush` with one Tap on its outlet. Declared after the
  // object so member destruction tears the object down while its target still
  // exists — sinks.hpp's teardown rule.
  struct Rig {
    std::unique_ptr<Tap> tap;
    std::unique_ptr<mMidiFlush> op;
    std::vector<std::string> log;

    Rig() : tap(new Tap()), op(new mMidiFlush()) {
      log.reserve(512);
      tap->log = &log;
      TestHelpers::Wire(*op, 0, *tap, 0);
    }
    ~Rig() {
      op.reset();
    }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    void Int(int value) {
      op->GetInlet(0)->SetInt(value, YSE::T_GUI);
    }
    void Float(float value) {
      op->GetInlet(0)->SetFloat(value, YSE::T_GUI);
    }
    void List(const std::string& value) {
      op->GetInlet(0)->SetList(value, YSE::T_GUI);
    }
    void Bytes(std::initializer_list<int> bytes) {
      for (int b : bytes)
        Int(b);
    }
    void Bang() {
      op->GetInlet(0)->SetBang(YSE::T_GUI);
    }
    void Clear() {
      log.clear();
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── registration and shape ───────────────────────────────────────────────

  TEST_CASE("midiflush: registered and creatable (#537)") {
    auto names = Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::M_MIDIFLUSH)) found = true;
    }
    CHECK(found);

    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::M_MIDIFLUSH));
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == std::string(YSE::OBJ::M_MIDIFLUSH));
    // One inlet taking both the stream and the bang, one outlet carrying both
    // the stream and the note-offs — Max's shape.
    CHECK(obj->NumInputs() == 1);
    CHECK(obj->NumOutputs() == 1);
    // A control-rate message object: nothing polls it and nothing renders it.
    CHECK_FALSE(obj->IsDSPObject());
    CHECK_FALSE(obj->WantsBlockPoll());
  }

  TEST_CASE("midiflush: is not gated on a MIDI backend (#537)") {
    // The structural claim about a `#if` that is not there, the same one
    // `.midiparse` makes: a patch driving a software synth built out of patcher
    // objects strands notes exactly as one driving hardware does, so the object
    // that clears them must exist where there is no hardware to blame.
    auto names = Register().AllNames();
    int found = 0;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::M_MIDIFLUSH)) found++;
    }
    CHECK(found == 1);
  }

  TEST_CASE("midiflush: documents itself (#537)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::M_MIDIFLUSH));
    REQUIRE(obj != nullptr);
    CHECK_FALSE(obj->GetDescription().empty());
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MIDI);
    CHECK_FALSE(obj->GetInlet(0)->GetDocLabel().empty());
    CHECK_FALSE(obj->GetOutlet(0)->GetDocLabel().empty());
  }

  // ─── the pass-through ─────────────────────────────────────────────────────

  TEST_CASE("midiflush: an int stream passes through byte for byte (#537)") {
    // `.midiin`'s shape: one int per byte. Nothing may be swallowed, reordered
    // or turned into a list on the way — the object is meant to be droppable
    // into a stream that already works.
    Rig rig;
    rig.Bytes({0x90, 60, 100, 0x80, 60, 0});

    REQUIRE(rig.log.size() == 6);
    CHECK(rig.log[0] == "i144");
    CHECK(rig.log[1] == "i60");
    CHECK(rig.log[2] == "i100");
    CHECK(rig.log[3] == "i128");
    CHECK(rig.log[4] == "i60");
    CHECK(rig.log[5] == "i0");
  }

  TEST_CASE("midiflush: a numeric byte list passes through whole (#537)") {
    // `.midiformat`'s shape: one complete message per list. It leaves as one
    // list too, so three bytes that travel together cannot be split downstream.
    Rig rig;
    rig.List("144 60 100");

    REQUIRE(rig.log.size() == 1);
    CHECK(rig.log[0] == "l144 60 100");
  }

  TEST_CASE("midiflush: a binary byte list passes through unaltered (#537)") {
    // What `.noteon` and the older senders build: a three-character string
    // whose characters are the bytes. It must reach `.midiout` in that same
    // spelling, or the patch this object was inserted into stops working.
    Rig rig;
    std::string binary = "000";
    binary[0] = (char)0x90;
    binary[1] = (char)60;
    binary[2] = (char)100;
    rig.List(binary);

    REQUIRE(rig.log.size() == 1);
    CHECK(rig.log[0] == "l" + binary);
    // And it was read, not merely copied.
    CHECK(rig.op->Held() == 1);
    CHECK(rig.op->IsHeld(1, 60));
  }

  TEST_CASE("midiflush: a value that is not a byte is ignored, not clamped (#537)") {
    Rig rig;
    rig.Int(300);
    rig.Int(-1);
    CHECK(rig.log.empty());
    CHECK(rig.op->Held() == 0);
  }

  TEST_CASE("midiflush: a numeric list that is not a message is dropped whole (#537)") {
    // `.midiout`'s reading: half a message at a device is worse than none.
    Rig rig;
    rig.List("144 60 300");
    CHECK(rig.log.empty());
    CHECK(rig.op->Dropped() == 1);
    CHECK(rig.op->Held() == 0);
  }

  TEST_CASE("midiflush: a float in the inlet is read as an int (#537)") {
    Rig rig;
    rig.Float(144.0f);
    rig.Float(60.4f);
    rig.Float(100.0f);
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[1] == "i60");
    CHECK(rig.op->IsHeld(1, 60));
  }

  // ─── what the tracking makes of a stream ──────────────────────────────────

  TEST_CASE("midiflush: a note-on is held until it is released (#537)") {
    Rig rig;
    rig.Bytes({0x90, 60, 100});
    CHECK(rig.op->Held() == 1);
    CHECK(rig.op->IsHeld(1, 60));
    CHECK_FALSE(rig.op->IsHeld(2, 60));
    CHECK_FALSE(rig.op->IsHeld(1, 61));

    rig.Bytes({0x80, 60, 64});
    CHECK(rig.op->Held() == 0);
    CHECK_FALSE(rig.op->IsHeld(1, 60));
  }

  TEST_CASE("midiflush: a note-on with velocity 0 is a release (#537)") {
    // Hardware spells a release both ways. An object that understood only the
    // note-off message would flush notes the device had already let go — the
    // one failure worse than a stuck note, since it interrupts a live one.
    Rig rig;
    rig.Bytes({0x90, 60, 100});
    REQUIRE(rig.op->Held() == 1);

    rig.Bytes({0x90, 60, 0});
    CHECK(rig.op->Held() == 0);
  }

  TEST_CASE("midiflush: the channel is part of a note's identity (#537)") {
    Rig rig;
    rig.Bytes({0x90, 60, 100}); // channel 1
    rig.Bytes({0x94, 60, 100}); // channel 5, same pitch
    CHECK(rig.op->Held() == 2);
    CHECK(rig.op->IsHeld(1, 60));
    CHECK(rig.op->IsHeld(5, 60));

    // Releasing on one channel leaves the other sounding.
    rig.Bytes({0x80, 60, 0});
    CHECK(rig.op->Held() == 1);
    CHECK(rig.op->IsHeld(5, 60));
  }

  TEST_CASE("midiflush: a repeated note-on for a sounding pitch is one note (#537)") {
    // Otherwise the count drifts and one release would leave the bit set.
    Rig rig;
    rig.Bytes({0x90, 60, 100});
    rig.Bytes({0x90, 60, 40});
    CHECK(rig.op->Held() == 1);

    rig.Bytes({0x80, 60, 0});
    CHECK(rig.op->Held() == 0);
  }

  TEST_CASE("midiflush: a release for a pitch that is not sounding is nothing (#537)") {
    Rig rig;
    rig.Bytes({0x80, 60, 0});
    CHECK(rig.op->Held() == 0);
    rig.Bang();
    // Nothing to release: only the three pass-through bytes are in the log.
    CHECK(rig.log.size() == 3);
  }

  TEST_CASE("midiflush: running status tracks a chord sent as one status byte (#537)") {
    // The reason this object decodes rather than pattern-matches: a chord is
    // normally one 0x90 followed by pairs of data bytes, and an object looking
    // for a status byte in front of each note would see one note in three.
    Rig rig;
    rig.Bytes({0x90, 60, 100, 64, 100, 67, 100});
    CHECK(rig.op->Held() == 3);
    CHECK(rig.op->IsHeld(1, 60));
    CHECK(rig.op->IsHeld(1, 64));
    CHECK(rig.op->IsHeld(1, 67));

    // And running status releases the same way.
    rig.Bytes({0x80, 60, 0, 64, 0, 67, 0});
    CHECK(rig.op->Held() == 0);
  }

  TEST_CASE("midiflush: a real-time byte between two data bytes does not break a note (#537)") {
    Rig rig;
    rig.Bytes({0x90, 60, 0xF8, 100});
    CHECK(rig.op->Held() == 1);
    CHECK(rig.op->IsHeld(1, 60));
    // The clock still went through — this is a pass-through above all else.
    REQUIRE(rig.log.size() == 4);
    CHECK(rig.log[2] == "i248");
  }

  TEST_CASE("midiflush: bytes inside a system-exclusive dump are not notes (#537)") {
    // A dump is arbitrary data, and its bytes are exactly the 0-127 range note
    // data lives in. An object that let a note-on's running status reach into
    // one would invent notes and then release them into a patch that never
    // played any — a spurious note-off mid-performance, which is worse than the
    // stuck note this object exists to clear.
    Rig rig;
    rig.Bytes({0x90, 60, 100}); // one real note, and a running status behind it
    REQUIRE(rig.op->Held() == 1);

    rig.Bytes({0xF0, 0x43, 64, 100, 67, 100, 0xF7});
    CHECK(rig.op->Held() == 1);
    CHECK(rig.log.size() == 10); // and every byte of it still went through

    // The dump cleared running status with it, so these two belong to nothing.
    rig.Bytes({72, 100});
    CHECK(rig.op->Held() == 1);

    // And the stream decodes again once a status byte says what it is.
    rig.Bytes({0x90, 72, 100});
    CHECK(rig.op->Held() == 2);
  }

  TEST_CASE("midiflush: a status byte ends a dump the device never terminated (#537)") {
    // `.midiparse`'s reading, and it matters more here: hardware interrupted
    // mid-dump simply starts sending the next message, and a decoder that
    // waited for the EOX would swallow every note-on after it — leaving notes
    // sounding that a flush then could not release.
    Rig rig;
    rig.Bytes({0xF0, 0x43, 0x10}); // a dump, never terminated
    CHECK(rig.op->Held() == 0);

    rig.Bytes({0x90, 60, 100});
    CHECK(rig.op->Held() == 1);
  }

  TEST_CASE("midiflush: a system-common message's data bytes are skipped (#537)") {
    // A song position pointer carries two data bytes that must not be read as
    // half a note, and it clears running status behind it.
    Rig rig;
    rig.Bytes({0x90, 60, 100});
    REQUIRE(rig.op->Held() == 1);

    rig.Bytes({0xF2, 0x00, 0x10});
    CHECK(rig.op->Held() == 1);

    // Running status is gone, so these two bytes belong to nothing.
    rig.Bytes({64, 100});
    CHECK(rig.op->Held() == 1);
  }

  TEST_CASE("midiflush: a stray data byte with no status is ignored (#537)") {
    Rig rig;
    rig.Bytes({60, 100});
    CHECK(rig.op->Held() == 0);
  }

  TEST_CASE("midiflush: other channel-voice messages leave the set alone (#537)") {
    // Control change and poly pressure carry two data bytes, program change and
    // aftertouch one. Reading either length wrong would desynchronise the notes
    // that follow, which is what this case is really watching.
    Rig rig;
    rig.Bytes({0xB0, 7, 100}); // control change
    rig.Bytes({0xC0, 4}); // program change, one data byte
    rig.Bytes({0xD0, 90}); // channel aftertouch, one data byte
    rig.Bytes({0xA0, 60, 40}); // poly pressure
    rig.Bytes({0xE0, 0, 96}); // pitch bend
    CHECK(rig.op->Held() == 0);

    rig.Bytes({0x90, 60, 100});
    CHECK(rig.op->Held() == 1);
  }

  // ─── the flush ────────────────────────────────────────────────────────────

  TEST_CASE("midiflush: a bang sends a note-off for every sounding note (#537)") {
    Rig rig;
    rig.Bytes({0x90, 60, 100, 64, 100});
    rig.Bytes({0x94, 72, 100});
    REQUIRE(rig.op->Held() == 3);

    rig.Clear();
    rig.Bang();

    // Channel then pitch order, as numeric byte lists with a release velocity
    // of 0. Nothing but the note-offs: a bang is not passed through.
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "l128 60 0");
    CHECK(rig.log[1] == "l128 64 0");
    // 132 is 0x84: note-off on channel 5, not the 0x94 the note-on carried.
    CHECK(rig.log[2] == "l132 72 0");

    CHECK(rig.op->Held() == 0);
    CHECK_FALSE(rig.op->IsHeld(1, 60));
    CHECK_FALSE(rig.op->IsHeld(5, 72));
  }

  TEST_CASE("midiflush: banging twice sends nothing the second time (#537)") {
    Rig rig;
    rig.Bytes({0x90, 60, 100});
    rig.Bang();
    REQUIRE(rig.op->Held() == 0);

    rig.Clear();
    rig.Bang();
    CHECK(rig.log.empty());
  }

  TEST_CASE("midiflush: a bang on an empty set sends nothing (#537)") {
    Rig rig;
    rig.Bang();
    CHECK(rig.log.empty());
  }

  TEST_CASE("midiflush: notes played after a flush are tracked again (#537)") {
    Rig rig;
    rig.Bytes({0x90, 60, 100});
    rig.Bang();
    REQUIRE(rig.op->Held() == 0);

    rig.Bytes({0x90, 67, 100});
    CHECK(rig.op->Held() == 1);

    rig.Clear();
    rig.Bang();
    REQUIRE(rig.log.size() == 1);
    CHECK(rig.log[0] == "l128 67 0");
  }

  TEST_CASE("midiflush: a flush releases notes the flush itself did not see arrive (#537)") {
    // The whole point of the object, stated as a patch would: a sequence
    // interrupted after its note-ons and before its note-offs.
    Rig rig;
    for (int pitch = 36; pitch < 96; pitch++)
      rig.Bytes({0x90, pitch, 100});
    REQUIRE(rig.op->Held() == 60);

    rig.Clear();
    rig.Bang();
    REQUIRE(rig.log.size() == 60);
    CHECK(rig.log.front() == "l128 36 0");
    CHECK(rig.log.back() == "l128 95 0");
    CHECK(rig.op->Held() == 0);
  }

  TEST_CASE("midiflush: every channel and every pitch can be held at once (#537)") {
    // The bitmap's own bounds: 16 x 128 is the whole of MIDI, and the flush is
    // bounded by the bitmap rather than by how many notes a patch stranded.
    Rig rig;
    for (int channel = 0; channel < 16; channel++) {
      for (int pitch = 0; pitch < 128; pitch++)
        rig.Bytes({0x90 + channel, pitch, 100});
    }
    REQUIRE(rig.op->Held() == 16 * 128);
    CHECK(rig.op->IsHeld(16, 127));

    rig.Clear();
    rig.Bang();
    CHECK(rig.log.size() == (std::size_t)(16 * 128));
    CHECK(rig.op->Held() == 0);
  }

  TEST_CASE("midiflush: the note-offs it sends read back as releases (#537)") {
    // The bytes are not merely plausible: fed to `.midiparse` they decode as
    // the releases they are meant to be, on the channel the note was played on.
    YSE::patcher patch;
    patch.create(2);

    YSE::pHandle* flush = patch.CreateObject(YSE::OBJ::M_MIDIFLUSH, "");
    YSE::pHandle* parse = patch.CreateObject(YSE::OBJ::M_PARSE, "");
    REQUIRE(flush != nullptr);
    REQUIRE(parse != nullptr);
    patch.Connect(flush, 0, parse, 0);

    std::vector<std::string> log;
    log.reserve(32);
    Tap note;
    Tap channel;
    note.log = &log;
    channel.log = &log;
    YSE::pHandle noteHandle(&note);
    YSE::pHandle channelHandle(&channel);
    patch.Connect(parse, 0, &noteHandle, 0); // note
    patch.Connect(parse, 6, &channelHandle, 0); // channel

    flush->SetListData(0, "148 72 100"); // note-on, channel 5
    REQUIRE(log.size() == 2);
    log.clear();

    flush->SetBang(0);
    REQUIRE(log.size() == 2);
    CHECK(log[0] == "i5"); // channel, out first
    CHECK(log[1] == "l72 0"); // pitch, velocity 0 — a release

    patch.DeleteObject(flush);
    patch.DeleteObject(parse);
  }

  TEST_CASE("midiflush: an outlet wired back into the inlet is refused, not recursed (#537)") {
    // A flush sends out the outlet, and a patch may wire that outlet back into
    // this inlet. Without the guard the note-off would re-enter the decoder
    // mid-walk; with it the re-entrant message is refused and counted.
    Rig rig;
    TestHelpers::Wire(*rig.op, 0, *rig.op, 0);
    rig.Bytes({0x90, 60, 100});
    REQUIRE(rig.op->Held() == 1);

    const std::uint64_t before = rig.op->Dropped();
    rig.Bang();
    CHECK(rig.op->Dropped() > before);
    CHECK(rig.op->Held() == 0);
  }

  // ─── end to end, in a real patcher ────────────────────────────────────────

  TEST_CASE("midiflush: clears the notes a stopped patch left hanging (#537)") {
    // The issue's claim at the level a patch makes it: `.noteon` (the binary
    // spelling) and `.midiformat` (the numeric one) both feeding a registry-
    // built `.midiflush` through real cords, and a bang releasing exactly what
    // they left sounding.
    YSE::patcher patch;
    patch.create(2);

    YSE::pHandle* noteon = patch.CreateObject(YSE::OBJ::M_NOTEON, "");
    YSE::pHandle* format = patch.CreateObject(YSE::OBJ::M_FORMAT, "");
    YSE::pHandle* flush = patch.CreateObject(YSE::OBJ::M_MIDIFLUSH, "");
    REQUIRE(noteon != nullptr);
    REQUIRE(format != nullptr);
    REQUIRE(flush != nullptr);
    patch.Connect(noteon, 0, flush, 0);
    patch.Connect(format, 0, flush, 0);

    std::vector<std::string> log;
    log.reserve(32);
    Tap tap;
    tap.log = &log;
    YSE::pHandle tapHandle(&tap);
    patch.Connect(flush, 0, &tapHandle, 0);

    // `.noteon` builds a binary message; it must pass through untouched and be
    // read for the note it is.
    noteon->SetIntData(1, 100); // velocity
    noteon->SetIntData(0, 60); // pitch — fires
    REQUIRE(log.size() == 1);

    // `.midiformat` builds the numeric spelling, on another channel.
    format->SetIntData(6, 5);
    format->SetListData(0, "72 90");
    REQUIRE(log.size() == 2);
    CHECK(log[1] == "l148 72 90");

    // Both are sounding, and the patch stops without releasing either.
    log.clear();
    flush->SetBang(0);
    REQUIRE(log.size() == 2);
    CHECK(log[0] == "l128 60 0");
    CHECK(log[1] == "l132 72 0");

    patch.DeleteObject(noteon);
    patch.DeleteObject(format);
    patch.DeleteObject(flush);
  }

  TEST_CASE("midiflush: survives a DumpJSON / ParseJSON round trip (#537)") {
    // No creation arguments — Max's has none either — so what has to survive is
    // the object, its cord, and the fact that the reloaded patch still flushes.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* format = src.CreateObject(YSE::OBJ::M_FORMAT, "");
    YSE::pHandle* flush = src.CreateObject(YSE::OBJ::M_MIDIFLUSH, "");
    REQUIRE(format != nullptr);
    REQUIRE(flush != nullptr);
    src.Connect(format, 0, flush, 0);

    const std::string json = src.DumpJSON();
    CHECK(json.find(".midiflush") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 2);

    YSE::pHandle* a = loaded.GetHandleFromList(0);
    YSE::pHandle* b = loaded.GetHandleFromList(1);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    YSE::pHandle* loadedFormat = a->GetName() == std::string(".midiformat") ? a : b;
    YSE::pHandle* loadedFlush = loadedFormat == a ? b : a;
    CHECK(loadedFlush->GetName() == std::string(".midiflush"));
    CHECK(loadedFormat->GetConnections(0) == 1);

    // Read it back through its own behaviour rather than an accessor: what has
    // to survive is a patch that still clears its stuck notes.
    std::vector<std::string> log;
    log.reserve(16);
    Tap tap;
    tap.log = &log;
    YSE::pHandle tapHandle(&tap);
    loaded.Connect(loadedFlush, 0, &tapHandle, 0);

    loadedFormat->SetListData(0, "60 100");
    REQUIRE(log.size() == 1);
    log.clear();

    loadedFlush->SetBang(0);
    REQUIRE(log.size() == 1);
    CHECK(log[0] == "l128 60 0");

    loaded.DeleteObject(loadedFlush);
    loaded.DeleteObject(loadedFormat);
  }

} // TEST_SUITE
