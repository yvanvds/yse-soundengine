// Tests for the MIDI codec pair (issue #530) — `.midiparse` and `.midiformat`.
//
// What is being pinned:
//
//   - **the decoding**: every channel-voice message onto its own outlet, with
//     Max's readings (note-off as velocity 0, program change 1-128, pitch bend
//     coarse) and the channel out before the typed value;
//   - **the three things that make MIDI harder than it looks**: running status
//     (a chord sent as one status byte and pairs of data bytes), interleaving
//     (a timing clock between the bytes of a note), and messages that do not
//     fit (a system-exclusive dump longer than one buffer);
//   - **the encoding**: each inlet producing the right bytes on the channel the
//     cold inlet is holding, with the ranges clamped rather than refused;
//   - **the pair being an inverse**: what goes into `.midiformat` comes back
//     out of `.midiparse` unchanged, including the system traffic neither of
//     them decodes;
//   - **the deviations that were chosen rather than inherited**: an eighth
//     `raw` inlet on `.midiformat` that Max has not got, a whole message per
//     list rather than Max's byte at a time, and no platform guard at all.
//
// The end-to-end section drives a real `patcherImplementation`: both objects
// built through the registry, wired together with real cords, fed through real
// inlets and read through real ones — the level at which "a patch can now work
// with MIDI structurally" is either true or not. A unit test of the state
// machine would pass just as happily on a pair nothing could wire.
//
// No audio device and no MIDI hardware required, and — unlike the input family
// of issue #529 — no MIDI backend either: these two objects are arithmetic over
// bytes, which is why they carry no `#if` at all.

#include <doctest/doctest.h>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "patcher/midi/mMidiCodec.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObject.h"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "sinks.hpp"

using YSE::PATCHER::mMidiFormat;
using YSE::PATCHER::mMidiParse;
using YSE::PATCHER::Register;

namespace {

  // Records every message that arrived, tagged with the outlet it came out of,
  // into a log shared by all the taps of one rig. Order matters as much as
  // content here — "the channel is out before the note" is the claim — so a
  // sink that only kept the last value per outlet could not test it.
  struct Tap : YSE::PATCHER::pObject {
    std::vector<std::string>* log = nullptr;
    std::string tag;

    Tap() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        if (log) log->push_back(tag + ":" + std::to_string(v));
      });
      inputs.back().RegisterList([this](const std::string& v, int, YSE::THREAD) {
        if (log) log->push_back(tag + ":" + v);
      });
    }
    const char* Type() const override {
      return "midicodec_tap";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A standalone object with one Tap on each outlet, all writing into one log.
  // Taps are held by unique_ptr so growing the vector cannot move one an inlet
  // already points at, and they are declared *after* the object so that member
  // destruction tears the object down while its targets still exist —
  // sinks.hpp's teardown rule.
  template <typename T> struct Rig {
    std::vector<std::unique_ptr<Tap>> taps;
    std::unique_ptr<T> op;
    std::vector<std::string> log;

    Rig() : op(new T()) {
      log.reserve(256);
      for (int i = 0; i < op->NumOutputs(); i++) {
        auto tap = std::make_unique<Tap>();
        tap->log = &log;
        tap->tag = "o" + std::to_string(i);
        TestHelpers::Wire(*op, i, *tap, 0);
        taps.push_back(std::move(tap));
      }
    }
    ~Rig() {
      // The object goes first, while every tap it points at is still alive.
      op.reset();
    }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    void Int(int value, int inlet = 0) {
      op->GetInlet(inlet)->SetInt(value, YSE::T_GUI);
    }
    void Float(float value, int inlet = 0) {
      op->GetInlet(inlet)->SetFloat(value, YSE::T_GUI);
    }
    void List(const std::string& value, int inlet = 0) {
      op->GetInlet(inlet)->SetList(value, YSE::T_GUI);
    }
    void Bytes(std::initializer_list<int> bytes) {
      for (int b : bytes)
        Int(b);
    }
    void Clear() {
      log.clear();
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── registration and shape ───────────────────────────────────────────────

  TEST_CASE("midi codec: both objects are registered and creatable (#530)") {
    auto names = Register().AllNames();
    for (const char* type : {YSE::OBJ::M_PARSE, YSE::OBJ::M_FORMAT}) {
      CAPTURE(type);
      bool found = false;
      for (const auto& name : names) {
        if (name == std::string(type)) found = true;
      }
      CHECK(found);

      std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK(std::string(obj->Type()) == std::string(type));
      // Control-rate message objects: nothing polls them and nothing renders
      // them, unlike the MIDI *input* family of #529.
      CHECK_FALSE(obj->IsDSPObject());
      CHECK_FALSE(obj->WantsBlockPoll());
    }
  }

  TEST_CASE("midi codec: the pair's ports mirror each other (#530)") {
    // Inlet for outlet, which is what lets a patch wire the two straight
    // across. Eight on each side: Max's seven plus the raw passthrough.
    std::unique_ptr<YSE::PATCHER::pObject> parse(Register().Get(YSE::OBJ::M_PARSE));
    std::unique_ptr<YSE::PATCHER::pObject> format(Register().Get(YSE::OBJ::M_FORMAT));
    REQUIRE(parse != nullptr);
    REQUIRE(format != nullptr);

    CHECK(parse->NumInputs() == 1);
    CHECK(parse->NumOutputs() == 8);
    CHECK(format->NumInputs() == 8);
    CHECK(format->NumOutputs() == 1);
  }

  TEST_CASE("midi codec: neither object is gated on a MIDI backend (#530)") {
    // The one structural claim this file can make about a `#if` that is not
    // there: the pair decodes bytes and needs no device, so it is registered on
    // every platform — including the ones where the whole #529 input family and
    // `.midiout` are compiled out. A patch that loses two boxes when it is
    // opened on a phone is the failure being avoided.
    auto names = Register().AllNames();
    int found = 0;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::M_PARSE)) found++;
      if (name == std::string(YSE::OBJ::M_FORMAT)) found++;
    }
    CHECK(found == 2);
  }

  // ─── .midiparse: the channel-voice messages ───────────────────────────────

  TEST_CASE("midiparse: a note-on decodes to a pitch/velocity list, channel first (#530)") {
    Rig<mMidiParse> rig;
    rig.Bytes({0x94, 60, 100}); // note-on, wire nibble 4 -> channel 5

    REQUIRE(rig.log.size() == 2);
    // Channel before the typed outlet: whatever the note triggers downstream
    // already knows which channel it belongs to.
    CHECK(rig.log[0] == "o6:5");
    CHECK(rig.log[1] == "o0:60 100");
  }

  TEST_CASE("midiparse: both spellings of a release report velocity 0 (#530)") {
    // Max's rule, shared with `.notein`: hardware sends a release as a note-off
    // or as a note-on with velocity 0, and a patch should not have to test for
    // both.
    Rig<mMidiParse> rig;

    rig.Bytes({0x80, 60, 64}); // note-off carrying a release velocity
    REQUIRE(rig.log.size() == 2);
    CHECK(rig.log[1] == "o0:60 0");

    rig.Clear();
    rig.Bytes({0x90, 60, 0});
    REQUIRE(rig.log.size() == 2);
    CHECK(rig.log[1] == "o0:60 0");
  }

  TEST_CASE("midiparse: poly pressure and control change decode to lists (#530)") {
    Rig<mMidiParse> rig;

    rig.Bytes({0xA0, 60, 90});
    REQUIRE(rig.log.size() == 2);
    CHECK(rig.log[0] == "o6:1");
    CHECK(rig.log[1] == "o1:60 90");

    rig.Clear();
    rig.Bytes({0xB2, 7, 64});
    REQUIRE(rig.log.size() == 2);
    CHECK(rig.log[0] == "o6:3");
    CHECK(rig.log[1] == "o2:7 64");
  }

  TEST_CASE("midiparse: program change is reported 1-128, not the wire's 0-127 (#530)") {
    // The numbering hardware displays, `.pgmin` reports and `.midiformat`
    // takes back — so a round trip gives back the number the user saw.
    Rig<mMidiParse> rig;
    rig.Bytes({0xC0, 0});
    REQUIRE(rig.log.size() == 2);
    CHECK(rig.log[1] == "o3:1");

    rig.Clear();
    rig.Bytes({0xC0, 127});
    REQUIRE(rig.log.size() == 2);
    CHECK(rig.log[1] == "o3:128");
  }

  TEST_CASE("midiparse: aftertouch and pitch bend (#530)") {
    Rig<mMidiParse> rig;

    rig.Bytes({0xD0, 88});
    REQUIRE(rig.log.size() == 2);
    CHECK(rig.log[1] == "o4:88");

    // Pitch bend: the wire sends the fine byte first and only the coarse one is
    // reported, which is Max's 7-bit reading and `.bendin`'s. `.xbendin` (#533)
    // is the object that keeps all fourteen bits.
    rig.Clear();
    rig.Bytes({0xE0, 0x7F, 64});
    REQUIRE(rig.log.size() == 2);
    CHECK(rig.log[1] == "o5:64");
  }

  TEST_CASE("midiparse: a one-data-byte message is not left waiting for a second (#530)") {
    // Program change and channel pressure carry one data byte; every other
    // channel-voice message carries two. Reading the wrong count would stall
    // the parser on the next message.
    Rig<mMidiParse> rig;
    rig.Bytes({0xC0, 5, 0xD0, 6});
    REQUIRE(rig.log.size() == 4);
    CHECK(rig.log[1] == "o3:6");
    CHECK(rig.log[3] == "o4:6");
  }

  // ─── .midiparse: the hard parts ───────────────────────────────────────────

  TEST_CASE("midiparse: running status decodes a chord sent as one status byte (#530)") {
    // The whole reason this object exists rather than a handful of comparisons
    // in a patch: MIDI omits a status byte that repeats, so a three-note chord
    // is 0x90 followed by three pairs of data bytes.
    Rig<mMidiParse> rig;
    rig.Bytes({0x90, 60, 100, 64, 101, 67, 102});

    REQUIRE(rig.log.size() == 6);
    CHECK(rig.log[1] == "o0:60 100");
    CHECK(rig.log[3] == "o0:64 101");
    CHECK(rig.log[5] == "o0:67 102");
    CHECK(rig.op->RunningStatus() == 0x90);
  }

  TEST_CASE("midiparse: a real-time byte passes through a message without breaking it (#530)") {
    // 0xF8-0xFF may appear *between* the bytes of any other message. The clock
    // leaves on the raw outlet at once and the note carries on where it was —
    // the one rule that a naive byte-at-a-time parser always gets wrong.
    Rig<mMidiParse> rig;
    rig.Bytes({0x90, 60, 0xF8, 100});

    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "o7:248"); // the clock, on its own, immediately
    CHECK(rig.log[1] == "o6:1");
    CHECK(rig.log[2] == "o0:60 100");
    // And it did not clear running status, so the next pair is still a note.
    CHECK(rig.op->RunningStatus() == 0x90);
  }

  TEST_CASE("midiparse: a system-common message clears running status (#530)") {
    // Unlike a real-time byte. Song position is 0xF2 with two data bytes; the
    // pair after it is no longer a note and must not be read as one.
    Rig<mMidiParse> rig;
    rig.Bytes({0x90, 60, 100});
    rig.Clear();

    rig.Bytes({0xF2, 0, 4});
    REQUIRE(rig.log.size() == 1);
    CHECK(rig.log[0] == "o7:242 0 4");
    CHECK(rig.op->RunningStatus() == 0);

    // Data bytes with no status in front of them now: dropped rather than
    // guessed at.
    rig.Clear();
    rig.Bytes({64, 101});
    CHECK(rig.log.empty());
  }

  TEST_CASE("midiparse: a one-byte system-common message needs no data (#530)") {
    // Tune request. 0xF4 and 0xF5 are undefined and read the same way, which is
    // what lets the parser recover at the next status byte.
    Rig<mMidiParse> rig;
    rig.Bytes({0xF6});
    REQUIRE(rig.log.size() == 1);
    CHECK(rig.log[0] == "o7:246");
  }

  TEST_CASE("midiparse: a system-exclusive dump leaves whole on the raw outlet (#530)") {
    Rig<mMidiParse> rig;
    rig.Bytes({0xF0, 0x43, 0x10, 0x4C, 0xF7});
    REQUIRE(rig.log.size() == 1);
    CHECK(rig.log[0] == "o7:240 67 16 76 247");
  }

  TEST_CASE("midiparse: a dump longer than the buffer leaves in chunks (#530)") {
    // Bounded rather than truncated and bounded rather than grown: growing
    // would allocate on the audio thread and truncating would corrupt the dump
    // silently. Same treatment `MIDI::inHub` gives a message too long for one
    // transport event.
    Rig<mMidiParse> rig;
    rig.Int(0xF0);
    for (int i = 0; i < mMidiParse::RAW_CHUNK_BYTES + 4; i++)
      rig.Int(1);
    rig.Int(0xF7);

    // 1 + (chunk + 4) + 1 bytes over chunks of RAW_CHUNK_BYTES.
    REQUIRE(rig.log.size() == 2);
    CHECK(rig.log[0].substr(0, 8) == "o7:240 1");
    CHECK(rig.log[1].substr(rig.log[1].size() - 3) == "247");

    // Nothing was lost: every byte of the dump is somewhere in the two chunks.
    std::size_t bytes = 0;
    for (const std::string& chunk : rig.log) {
      // one atom per space, plus the first
      bytes += 1;
      for (char c : chunk)
        if (c == ' ') bytes++;
    }
    CHECK(bytes == (std::size_t)(mMidiParse::RAW_CHUNK_BYTES + 6));
  }

  TEST_CASE("midiparse: a status byte ends a dump the device never terminated (#530)") {
    // Hardware interrupted mid-dump simply stops sending. A parser that waited
    // for the EOX would swallow every message after it — so the note here has
    // to arrive.
    Rig<mMidiParse> rig;
    rig.Bytes({0xF0, 0x43, 0x10, 0x90, 60, 100});

    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "o7:240 67 16"); // flushed without an EOX
    CHECK(rig.log[1] == "o6:1");
    CHECK(rig.log[2] == "o0:60 100");
  }

  TEST_CASE("midiparse: a stray data byte with no status is ignored (#530)") {
    // A stream joined part-way through, which is the normal case when a cable
    // is plugged into a device that is already playing.
    Rig<mMidiParse> rig;
    rig.Bytes({60, 100, 64});
    CHECK(rig.log.empty());
  }

  TEST_CASE("midiparse: a value that is not a byte is ignored, not clamped (#530)") {
    // Clamping would turn a stray number into a status byte and desynchronise
    // everything after it.
    Rig<mMidiParse> rig;
    rig.Bytes({0x90, 60, 999, -1, 100});
    REQUIRE(rig.log.size() == 2);
    CHECK(rig.log[1] == "o0:60 100");
  }

  TEST_CASE("midiparse: a list of bytes decodes exactly as one byte at a time does (#530)") {
    // The two shapes the patcher's raw MIDI travels in: ints from `.midiin` and
    // `.seq`, and a numeric list from `.midiformat`. One state machine, so a
    // message split across several messages decodes as one arriving whole.
    Rig<mMidiParse> whole;
    whole.List("144 60 100");

    Rig<mMidiParse> split;
    split.List("144 60");
    split.List("100");

    REQUIRE(whole.log.size() == 2);
    CHECK(whole.log == split.log);
  }

  TEST_CASE("midiparse: a float in the inlet is read as an int (#530)") {
    Rig<mMidiParse> rig;
    rig.Float(144.0f);
    rig.Float(60.4f);
    rig.Float(100.0f);
    REQUIRE(rig.log.size() == 2);
    CHECK(rig.log[1] == "o0:60 100");
  }

  // ─── .midiformat ──────────────────────────────────────────────────────────

  TEST_CASE("midiformat: each inlet builds its own message (#530)") {
    Rig<mMidiFormat> rig;

    rig.List("60 100", 0);
    REQUIRE(rig.log.size() == 1);
    CHECK(rig.log[0] == "o0:144 60 100");

    rig.Clear();
    rig.List("60 90", 1);
    CHECK(rig.log[0] == "o0:160 60 90");

    rig.Clear();
    rig.List("7 64", 2);
    CHECK(rig.log[0] == "o0:176 7 64");

    rig.Clear();
    rig.Int(1, 3);
    CHECK(rig.log[0] == "o0:192 0"); // 1-128 in, 0-127 on the wire

    rig.Clear();
    rig.Int(88, 4);
    CHECK(rig.log[0] == "o0:208 88");

    rig.Clear();
    rig.Int(64, 5);
    CHECK(rig.log[0] == "o0:224 0 64"); // fine byte first, and always 0 here
  }

  TEST_CASE("midiformat: the channel inlet is cold and sets the channel (#530)") {
    Rig<mMidiFormat> rig;
    CHECK(rig.op->Channel() == 1);

    rig.Int(10, 6);
    // Max's rule for this inlet: it stores and sends nothing.
    CHECK(rig.log.empty());
    CHECK(rig.op->Channel() == 10);

    rig.List("60 100", 0);
    REQUIRE(rig.log.size() == 1);
    CHECK(rig.log[0] == "o0:153 60 100"); // 0x90 | 9
  }

  TEST_CASE("midiformat: a channel outside 1-16 is clamped into it (#530)") {
    Rig<mMidiFormat> rig;
    rig.Int(99, 6);
    CHECK(rig.op->Channel() == 16);
    rig.Int(0, 6);
    CHECK(rig.op->Channel() == 1);
  }

  TEST_CASE("midiformat: a bare number on a two-number inlet is the first of the pair (#530)") {
    // Which makes a bare pitch a release, since velocity 0 is how this pair
    // spells one.
    Rig<mMidiFormat> rig;
    rig.Int(60, 0);
    REQUIRE(rig.log.size() == 1);
    CHECK(rig.log[0] == "o0:144 60 0");
  }

  TEST_CASE("midiformat: values are clamped into the ranges MIDI has (#530)") {
    // Rather than refused: a patch that scales a controller into 0-140 should
    // produce sensible notes at the top of its range, not silence.
    Rig<mMidiFormat> rig;

    rig.List("200 -5", 0);
    REQUIRE(rig.log.size() == 1);
    CHECK(rig.log[0] == "o0:144 127 0");

    rig.Clear();
    rig.Int(500, 3);
    CHECK(rig.log[0] == "o0:192 127"); // program clamps to 128, minus one
  }

  TEST_CASE("midiformat: the raw inlet passes bytes straight through (#530)") {
    // Not Max's inlet. It is what makes the pair a true inverse: without it a
    // patch could take a stream apart but not put the whole of it back
    // together, the system traffic having nowhere to go back in.
    Rig<mMidiFormat> rig;
    rig.List("240 67 16 247", 7);
    REQUIRE(rig.log.size() == 1);
    CHECK(rig.log[0] == "o0:240 67 16 247");

    rig.Clear();
    rig.Int(248, 7);
    REQUIRE(rig.log.size() == 1);
    CHECK(rig.log[0] == "o0:248");
  }

  TEST_CASE("midiformat: a raw value that is not a byte is dropped and counted (#530)") {
    // The one place clamping is wrong: clamping a status byte would change
    // which message it is.
    Rig<mMidiFormat> rig;
    const std::uint64_t before = rig.op->Dropped();
    rig.Int(300, 7);
    CHECK(rig.log.empty());
    CHECK(rig.op->Dropped() == before + 1);
  }

  TEST_CASE("midiformat: a raw list longer than the ceiling is cut there (#530)") {
    Rig<mMidiFormat> rig;
    std::string list;
    for (int i = 0; i < mMidiFormat::RAW_MAX_BYTES + 10; i++) {
      if (i > 0) list += " ";
      list += "1";
    }
    rig.List(list, 7);

    REQUIRE(rig.log.size() == 1);
    std::size_t bytes = 1;
    for (char c : rig.log[0])
      if (c == ' ') bytes++;
    CHECK(bytes == (std::size_t)mMidiFormat::RAW_MAX_BYTES);
    CHECK(rig.op->Dropped() == 1);
  }

  // ─── the pair, end to end in a real patcher ───────────────────────────────

  TEST_CASE("midi codec: a message built by .midiformat comes back out of .midiparse (#530)") {
    // The claim of the issue, at the level a patch would make it: two objects
    // built through the registry, wired with a real cord in a real patcher, and
    // read through real inlets. Every channel-voice type, and the channel with
    // it.
    YSE::patcher patch;
    patch.create(2);

    YSE::pHandle* format = patch.CreateObject(YSE::OBJ::M_FORMAT, "");
    YSE::pHandle* parse = patch.CreateObject(YSE::OBJ::M_PARSE, "");
    REQUIRE(format != nullptr);
    REQUIRE(parse != nullptr);
    patch.Connect(format, 0, parse, 0);

    std::vector<std::string> log;
    std::vector<std::unique_ptr<Tap>> taps;
    std::vector<std::unique_ptr<YSE::pHandle>> handles;
    log.reserve(64);
    for (int i = 0; i < parse->GetOutputs(); i++) {
      auto tap = std::make_unique<Tap>();
      tap->log = &log;
      tap->tag = "o" + std::to_string(i);
      auto handle = std::make_unique<YSE::pHandle>(tap.get());
      patch.Connect(parse, i, handle.get(), 0);
      taps.push_back(std::move(tap));
      handles.push_back(std::move(handle));
    }

    format->SetIntData(6, 5); // channel 5

    format->SetListData(0, "60 100");
    REQUIRE(log.size() == 2);
    CHECK(log[0] == "o6:5");
    CHECK(log[1] == "o0:60 100");

    log.clear();
    format->SetListData(2, "74 32");
    REQUIRE(log.size() == 2);
    CHECK(log[0] == "o6:5");
    CHECK(log[1] == "o2:74 32");

    log.clear();
    format->SetIntData(3, 42); // program change, 1-128 both ways
    REQUIRE(log.size() == 2);
    CHECK(log[1] == "o3:42");

    log.clear();
    format->SetIntData(4, 70);
    REQUIRE(log.size() == 2);
    CHECK(log[1] == "o4:70");

    log.clear();
    format->SetIntData(5, 96); // bend, coarse both ways
    REQUIRE(log.size() == 2);
    CHECK(log[1] == "o5:96");

    log.clear();
    format->SetListData(1, "60 44");
    REQUIRE(log.size() == 2);
    CHECK(log[1] == "o1:60 44");

    // And the system traffic neither of them decodes, which is what the extra
    // raw inlet is for: in one end, out the other, byte for byte.
    log.clear();
    format->SetListData(7, "240 67 16 247");
    REQUIRE(log.size() == 1);
    CHECK(log[0] == "o7:240 67 16 247");

    // Sinks are disconnected before they go out of scope with the vectors.
    patch.DeleteObject(format);
    patch.DeleteObject(parse);
  }

  TEST_CASE("midi codec: the pair survives a DumpJSON / ParseJSON round trip (#530)") {
    // Neither object takes a creation argument — Max's do not either — so what
    // has to survive is the pair and the cord between them, and the fact that
    // the reloaded patch still codes MIDI.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* format = src.CreateObject(YSE::OBJ::M_FORMAT, "");
    YSE::pHandle* parse = src.CreateObject(YSE::OBJ::M_PARSE, "");
    REQUIRE(format != nullptr);
    REQUIRE(parse != nullptr);
    src.Connect(format, 0, parse, 0);

    const std::string json = src.DumpJSON();
    CHECK(json.find(".midiformat") != std::string::npos);
    CHECK(json.find(".midiparse") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 2);

    YSE::pHandle* a = loaded.GetHandleFromList(0);
    YSE::pHandle* b = loaded.GetHandleFromList(1);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    YSE::pHandle* loadedFormat = a->GetName() == std::string(".midiformat") ? a : b;
    YSE::pHandle* loadedParse = loadedFormat == a ? b : a;
    CHECK(loadedFormat->GetName() == std::string(".midiformat"));
    CHECK(loadedParse->GetName() == std::string(".midiparse"));
    // The cord came back with them.
    CHECK(loadedFormat->GetConnections(0) == 1);

    // Read the pair back through its own behaviour rather than an accessor:
    // what has to survive is a patch that still works.
    std::vector<std::string> log;
    log.reserve(16);
    Tap tap;
    tap.log = &log;
    tap.tag = "note";
    YSE::pHandle tapHandle(&tap);
    loaded.Connect(loadedParse, 0, &tapHandle, 0);

    loadedFormat->SetListData(0, "60 100");
    REQUIRE(log.size() == 1);
    CHECK(log[0] == "note:60 100");

    loaded.DeleteObject(loadedParse);
    loaded.DeleteObject(loadedFormat);
  }

} // TEST_SUITE
