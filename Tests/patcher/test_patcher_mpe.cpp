// Tests for the MPE family (issue #535) — `.mpeconfig` and `.mpeformat` on the
// sending side, `.mpeparse` on the reading side.
//
// What is being pinned:
//
//   - **the configuration message**, which is the only part of MPE that is not
//     ordinary MIDI used in a particular way: registered parameter "00 06" sent
//     on a zone's master channel as three separate control changes, in the byte
//     order the specification itself prints (the parameter number's *fine* byte
//     first, which is the reverse of `.rpnout`'s), carrying the member channel
//     count — 0 among them, which switches the zone off;
//   - **the zone geometry**: the lower zone's master is channel 1 with members
//     counting up from 2, the upper zone's is channel 16 with members counting
//     down from 15, and a channel outside the configured run belongs to
//     neither. That reading is what the role outlet reports and it is the whole
//     of what makes an MPE stream different from a MIDI one;
//   - **the four dimensions**, each an ordinary message made per-note by the
//     channel it arrives on: notes, fourteen-bit pitch bend, channel pressure
//     and controller 74;
//   - **learning the zone from the stream**: a configuration message seen on
//     the master channel resizes `.mpeparse`'s zone live, and is consumed
//     rather than reported, so a patch that sends `.mpeconfig` at one end reads
//     the right roles at the other;
//   - **the deliberate deviations**: fourteen-bit bend where `.midiformat`
//     takes seven, a release as a note-on with velocity 0, one channel inlet
//     rather than Max's sixteen, and no decoding of the traffic that belongs to
//     `.midiparse`;
//   - **framing that cannot be desynchronised** by running status, by a
//     real-time byte landing inside a message, or by a system-exclusive dump;
//   - **parameters surviving a DumpJSON / ParseJSON round trip**, read back
//     through behaviour rather than an accessor.
//
// The end-to-end sections drive the real machinery: real objects built through
// `CreateObject` with real creation arguments in a real `YSE::patcher`, wired
// with real cords. The last two cases close both loops — a `.mpeconfig` whose
// own bytes reconfigure a `.mpeparse`, and a `.mpeformat` whose own bytes are
// read back by one — which is the level at which this issue's claim ("a patch
// can now speak MPE") is either true or not.
//
// No audio device and no MIDI hardware required, and no platform guard: none of
// the three objects opens a device, so all of them are registered everywhere.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <vector>

#include "headers/defines.hpp"

#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObject.h"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/parameters.h"
#include "patcher/patcher.hpp"

#include "patcher/inlet.h"
#include "patcher/midi/mMpe.h"
#include "sinks.hpp"

using YSE::PATCHER::Register;

namespace {

  // Every name this issue adds. `.polymidiin` is deliberately absent — see
  // mMpe.h: in Max it lives inside a `poly~` and receives that object's
  // `mpeevent` routing, and this patcher has no `poly~` for it to live in.
  const char* const kFamily[] = {
      YSE::OBJ::M_MPECONFIG,
      YSE::OBJ::M_MPEFORMAT,
      YSE::OBJ::M_MPEPARSE,
  };

  // Expectations below are spelled as the decimal byte lists the objects
  // actually exchange — "146 60 100" rather than a status constant plus two
  // numbers — because that string is the thing under test. A symbolic spelling
  // would let the same mistake into the test and the object.

  // Asserts the documentation metadata test_doc_coverage.cpp checks over every
  // registered object, repeated here so a regression names the object directly.
  void CheckDocumented(const char* type) {
    CAPTURE(type);
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(type));
    REQUIRE(obj != nullptr);

    CHECK_FALSE(obj->GetDescription().empty());
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MIDI);
    for (int i = 0; i < obj->NumInputs(); i++) {
      CHECK_FALSE(obj->GetInlet(i)->GetDocLabel().empty());
      CHECK_FALSE(obj->GetInlet(i)->GetDocDescription().empty());
    }
    for (int i = 0; i < obj->NumOutputs(); i++) {
      CHECK_FALSE(obj->GetOutlet(i)->GetDocLabel().empty());
      CHECK_FALSE(obj->GetOutlet(i)->GetDocDescription().empty());
    }
    for (const auto& p : obj->GetParamDocs()) {
      CHECK_FALSE(p.name.empty());
      CHECK_FALSE(p.doc.empty());
    }
  }

  // Keeps every list it was sent rather than only the last, which this family
  // needs: one value on `.mpeconfig`'s inlet is three MIDI messages.
  struct ListLog : YSE::PATCHER::pObject {
    std::vector<std::string> received;

    ListLog() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { received.push_back(v); });
    }
    const char* Type() const override {
      return "mpe_list_log";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A standalone sender with a ListLog on its outlet. The log is declared first
  // so it outlives the object that sends to it — sinks.hpp's teardown rule.
  template <typename T> struct SendRig {
    ListLog log;
    T op;

    explicit SendRig(const std::string& params = "") {
      if (!params.empty()) op.SetParams(params);
      TestHelpers::Wire(op, 0, log, 0);
    }
    SendRig(const SendRig&) = delete;
    SendRig& operator=(const SendRig&) = delete;

    void Send(int inlet, int value) {
      op.GetInlet(inlet)->SetInt(value, YSE::T_GUI);
    }
    void SendList(int inlet, const std::string& value) {
      op.GetInlet(inlet)->SetList(value, YSE::T_GUI);
    }
  };

  // Records everything that arrived tagged with the outlet that delivered it,
  // into one shared log — which is what makes "role first, then channel, then
  // the value" an assertion rather than an inference. Ints and lists both, so
  // one tap serves every outlet of `.mpeparse`.
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
      return "mpe_tap";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A registry-built `.mpeparse` in a real patcher, with one Tap on each outlet
  // writing into one shared log.
  struct ParseRig {
    YSE::patcher patch;
    std::vector<std::string> log;
    std::vector<std::unique_ptr<Tap>> taps;
    std::vector<std::unique_ptr<YSE::pHandle>> tapHandles;
    YSE::pHandle* object = nullptr;

    // `roundTrip` builds the object in a scratch patcher, dumps that to JSON and
    // loads it into this one, so the object under test is the one that came back
    // from storage. A round-trip case can then make exactly the assertions a
    // direct one makes, which is what "the parameters survived" has to mean.
    explicit ParseRig(const std::string& args, bool roundTrip = false) {
      patch.create(2);
      if (roundTrip) {
        YSE::patcher src;
        src.create(2);
        REQUIRE(src.CreateObject(YSE::OBJ::M_MPEPARSE, args) != nullptr);
        const std::string json = src.DumpJSON();
        CHECK(json.find(".mpeparse") != std::string::npos);
        patch.ParseJSON(json);
        REQUIRE(patch.Objects() == 1);
        object = patch.GetHandleFromList(0);
      } else {
        object = patch.CreateObject(YSE::OBJ::M_MPEPARSE, args);
      }
      REQUIRE(object != nullptr);
      log.reserve(64);
      for (int i = 0; i < object->GetOutputs(); i++) {
        auto tap = std::make_unique<Tap>();
        tap->log = &log;
        tap->tag = "o" + std::to_string(i);
        auto handle = std::make_unique<YSE::pHandle>(tap.get());
        patch.Connect(object, i, handle.get(), 0);
        taps.push_back(std::move(tap));
        tapHandles.push_back(std::move(handle));
      }
    }
    ParseRig(const ParseRig&) = delete;
    ParseRig& operator=(const ParseRig&) = delete;

    // One whole message, the shape `.mpeformat` and `.midiformat` send.
    void Message(const std::string& bytes) {
      object->SetListData(0, bytes);
    }

    // One byte, the shape `.midiin` and `.seq` send.
    void Byte(int value) {
      object->SetIntData(0, value);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── registration and shape ───────────────────────────────────────────────

  TEST_CASE("mpe: every object is registered on every platform (#535)") {
    // No `#if` here on purpose: that is part of the assertion. None of the
    // three opens a device, so a guard creeping in would make a patch lose
    // boxes when it moved between machines — the failure #746 fixed for the
    // sender family.
    auto names = Register().AllNames();
    auto registered = [&names](const char* type) {
      for (const auto& name : names) {
        if (name == std::string(type)) return true;
      }
      return false;
    };

    for (const char* type : kFamily) {
      CAPTURE(type);
      CHECK(registered(type));
      std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK(std::string(obj->Type()) == std::string(type));
    }
    CHECK(sizeof(kFamily) / sizeof(kFamily[0]) == 3);
  }

  TEST_CASE("mpe: every object documents itself (#535)") {
    for (const char* type : kFamily)
      CheckDocumented(type);
  }

  TEST_CASE("mpe: the objects have the shapes their siblings have (#535)") {
    struct Shape {
      const char* type;
      int inlets;
      int outlets;
    };
    const Shape shapes[] = {
        {YSE::OBJ::M_MPECONFIG, 1, 1},
        {YSE::OBJ::M_MPEFORMAT, 5, 1}, // note, bend, pressure, slide, channel
        {YSE::OBJ::M_MPEPARSE, 1, 6}, // note, bend, pressure, slide, channel, role
    };

    for (const Shape& shape : shapes) {
      CAPTURE(shape.type);
      std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(shape.type));
      REQUIRE(obj != nullptr);
      CHECK(obj->NumInputs() == shape.inlets);
      CHECK(obj->NumOutputs() == shape.outlets);
      // Message formatters and decoders: nothing renders them, and nothing has
      // to poll them because their input arrives on a cord rather than a wire.
      CHECK_FALSE(obj->IsDSPObject());
      CHECK_FALSE(obj->WantsBlockPoll());
    }
  }

  // ─── .mpeconfig ───────────────────────────────────────────────────────────

  TEST_CASE("mpeconfig: one count is three messages, in the specification's order (#535)") {
    // The object's whole reason to exist, and the one place the byte order is
    // load-bearing: the specification prints this message as [Bn 64 06]
    // [Bn 65 00] [Bn 06 mm] — the parameter number's *fine* byte first, which
    // is the reverse of `.rpnout`'s coarse-first order. A device that
    // pattern-matches the configuration message rather than running a general
    // parameter-number state machine only recognises it that way round.
    SendRig<YSE::PATCHER::mMpeConfig> rig;

    rig.Send(0, 15);
    REQUIRE(rig.log.received.size() == 3);
    CHECK(rig.log.received[0] == "176 100 6");
    CHECK(rig.log.received[1] == "176 101 0");
    CHECK(rig.log.received[2] == "176 6 15");

    // Three lists rather than one nine-byte list, because `.midiout` sends each
    // list it receives as a single MIDI message.
    CHECK(rig.op.Members() == 15);
    CHECK(rig.op.Zone() == YSE::PATCHER::MPE_ZONE_LOWER);
  }

  TEST_CASE("mpeconfig: the upper zone is addressed on channel 16 (#535)") {
    // The two zones grow away from each other so two instruments can share one
    // cable, and the master channel is the only thing that says which is being
    // configured.
    SendRig<YSE::PATCHER::mMpeConfig> rig("1");

    rig.Send(0, 7);
    REQUIRE(rig.log.received.size() == 3);
    CHECK(rig.log.received[0] == "191 100 6");
    CHECK(rig.log.received[1] == "191 101 0");
    CHECK(rig.log.received[2] == "191 6 7");
    CHECK(rig.op.Zone() == YSE::PATCHER::MPE_ZONE_UPPER);
  }

  TEST_CASE("mpeconfig: a count of zero switches the zone off (#535)") {
    // Not a degenerate value to be clamped away: it is the specification's own
    // way of saying "go back to plain MIDI", and a patch that hands its
    // controller to something not MPE-aware has to be able to send it.
    SendRig<YSE::PATCHER::mMpeConfig> rig;

    rig.Send(0, 0);
    REQUIRE(rig.log.received.size() == 3);
    CHECK(rig.log.received[2] == "176 6 0");
    CHECK(rig.op.Members() == 0);
  }

  TEST_CASE("mpeconfig: counts are clamped, not refused (#535)") {
    SendRig<YSE::PATCHER::mMpeConfig> rig;

    rig.Send(0, 900);
    REQUIRE(rig.log.received.size() == 3);
    CHECK(rig.log.received[2] == "176 6 15");

    rig.log.received.clear();
    rig.Send(0, -4);
    REQUIRE(rig.log.received.size() == 3);
    CHECK(rig.log.received[2] == "176 6 0");
  }

  TEST_CASE("mpeconfig: a zone argument outside 0-1 reads as the lower zone (#535)") {
    // Sixteen channels hold two zones and no more, so there is no third master
    // channel a stray number could name.
    SendRig<YSE::PATCHER::mMpeConfig> rig("9");

    rig.Send(0, 3);
    REQUIRE(rig.log.received.size() == 3);
    CHECK(rig.log.received[0] == "176 100 6");
    CHECK(rig.op.Zone() == YSE::PATCHER::MPE_ZONE_LOWER);
  }

  // ─── .mpeformat ───────────────────────────────────────────────────────────

  TEST_CASE("mpeformat: the four dimensions, each a whole MIDI message (#535)") {
    SendRig<YSE::PATCHER::mMpeFormat> rig;
    CHECK(rig.op.Channel() == 1);

    rig.SendList(0, "60 100");
    REQUIRE(rig.log.received.size() == 1);
    CHECK(rig.log.received[0] == "144 60 100");

    // Fourteen bits, not `.midiformat`'s coarse byte: a configured device uses
    // a 48-semitone per-note bend range, where seven bits would step in three
    // quarters of a semitone. 8192 is the rest position — fine byte 0, coarse
    // byte 64 — and the wire sends the fine byte first.
    rig.log.received.clear();
    rig.Send(1, 8192);
    REQUIRE(rig.log.received.size() == 1);
    CHECK(rig.log.received[0] == "224 0 64");

    rig.log.received.clear();
    rig.Send(2, 90);
    REQUIRE(rig.log.received.size() == 1);
    CHECK(rig.log.received[0] == "208 90"); // channel pressure is two bytes

    rig.log.received.clear();
    rig.Send(3, 30);
    REQUIRE(rig.log.received.size() == 1);
    CHECK(rig.log.received[0] == "176 74 30"); // controller 74
  }

  TEST_CASE("mpeformat: the channel inlet is cold and addresses every message (#535)") {
    // One channel is one note, so this is the inlet a polyphonic patch moves
    // between notes — and it must not fire a message of its own while doing it.
    SendRig<YSE::PATCHER::mMpeFormat> rig;

    rig.Send(4, 3);
    CHECK(rig.log.received.empty());
    CHECK(rig.op.Channel() == 3);

    rig.SendList(0, "60 100");
    rig.Send(1, 9000);
    rig.Send(2, 90);
    rig.Send(3, 30);
    REQUIRE(rig.log.received.size() == 4);
    CHECK(rig.log.received[0] == "146 60 100");
    CHECK(rig.log.received[1] == "226 40 70"); // 9000 = (70 << 7) | 40
    CHECK(rig.log.received[2] == "210 90");
    CHECK(rig.log.received[3] == "178 74 30");
  }

  TEST_CASE("mpeformat: a bare pitch is a release, and so is velocity 0 (#535)") {
    // `.midiformat`'s rule and `.mpeparse`'s reading, so a note round-trips
    // through the pair as the pair described it.
    SendRig<YSE::PATCHER::mMpeFormat> rig;

    rig.Send(0, 60);
    REQUIRE(rig.log.received.size() == 1);
    CHECK(rig.log.received[0] == "144 60 0");

    rig.log.received.clear();
    rig.SendList(0, "60 0");
    REQUIRE(rig.log.received.size() == 1);
    CHECK(rig.log.received[0] == "144 60 0");
  }

  TEST_CASE("mpeformat: values are clamped into the ranges MIDI has (#535)") {
    SendRig<YSE::PATCHER::mMpeFormat> rig;

    rig.SendList(0, "900 -5");
    rig.Send(1, 90000);
    rig.Send(2, -8);
    rig.Send(3, 400);
    rig.Send(4, 99); // and a channel that would corrupt the status nibble
    rig.SendList(0, "60 100");

    REQUIRE(rig.log.received.size() == 5);
    CHECK(rig.log.received[0] == "144 127 0");
    CHECK(rig.log.received[1] == "224 127 127"); // 16383
    CHECK(rig.log.received[2] == "208 0");
    CHECK(rig.log.received[3] == "176 74 127");
    CHECK(rig.log.received[4] == "159 60 100"); // channel 16, not channel 100
  }

  // ─── .mpeparse ────────────────────────────────────────────────────────────

  TEST_CASE("mpeparse: role and channel come out before the value (#535)") {
    // Right to left, as every MIDI reader in this patcher sends: whatever the
    // leftmost outlet triggers downstream already knows whose note it is.
    ParseRig rig("0 15");

    rig.Message("146 60 100"); // note-on, channel 3
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "o5:1"); // role: a member channel
    CHECK(rig.log[1] == "o4:3"); // channel
    CHECK(rig.log[2] == "o0:60 100"); // the note itself
  }

  TEST_CASE("mpeparse: the four dimensions come off the stream (#535)") {
    ParseRig rig("0 15");

    rig.Message("226 40 70"); // bend, channel 3
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[2] == "o1:9000"); // all fourteen bits

    rig.log.clear();
    rig.Message("210 90"); // channel pressure, channel 3
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[2] == "o2:90");

    rig.log.clear();
    rig.Message("178 74 30"); // controller 74, channel 3
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[2] == "o3:30");
  }

  TEST_CASE("mpeparse: a release is velocity 0 however the device spelled it (#535)") {
    // Both spellings fold to one shape so a patch tests for a release once,
    // with a `.sel 0` — `.notein`'s and `.midiparse`'s rule.
    ParseRig rig("0 15");

    rig.Message("146 60 0"); // note-on with velocity 0
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[2] == "o0:60 0");

    rig.log.clear();
    rig.Message("130 60 64"); // a real note-off carrying a release velocity
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[2] == "o0:60 0");
  }

  TEST_CASE("mpeparse: the role outlet tells master from member from neither (#535)") {
    // The whole of what makes an MPE stream different from a MIDI one. With
    // four member channels the lower zone is channel 1 (master) and channels
    // 2-5 (members); everything above that is another instrument's.
    ParseRig rig("0 4");

    rig.Message("144 60 100"); // channel 1 — the master
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "o5:2");
    CHECK(rig.log[1] == "o4:1");

    rig.log.clear();
    rig.Message("148 60 100"); // channel 5 — the last member
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "o5:1");
    CHECK(rig.log[1] == "o4:5");

    rig.log.clear();
    rig.Message("149 60 100"); // channel 6 — past the end of the zone
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "o5:0");
    CHECK(rig.log[1] == "o4:6");
  }

  TEST_CASE("mpeparse: the upper zone counts down from channel 15 (#535)") {
    // The mirror of the lower zone, and the reason two instruments can share a
    // cable: with four members the upper zone is channel 16 (master) and
    // channels 12-15.
    ParseRig rig("1 4");

    rig.Message("159 60 100"); // channel 16 — the master
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "o5:2");
    CHECK(rig.log[1] == "o4:16");

    rig.log.clear();
    rig.Message("155 60 100"); // channel 12 — the lowest member
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "o5:1");
    CHECK(rig.log[1] == "o4:12");

    rig.log.clear();
    rig.Message("154 60 100"); // channel 11 — below the zone
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "o5:0");
    CHECK(rig.log[1] == "o4:11");
  }

  TEST_CASE("mpeparse: a configuration message resizes the zone and is not reported (#535)") {
    // The object learns as it reads, which is what lets one `.mpeconfig` keep
    // both ends of a patch agreeing without being told twice. The message is
    // consumed rather than reported: it is configuration, not expression.
    ParseRig rig("0 15");
    CHECK(rig.object->GetName() == std::string(".mpeparse"));

    rig.Message("155 60 100"); // channel 12 is a member while the zone is wide
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "o5:1");

    rig.log.clear();
    rig.Message("176 100 6"); // the configuration message, cut to four members
    rig.Message("176 101 0");
    rig.Message("176 6 4");
    CHECK(rig.log.empty());

    rig.Message("155 60 100"); // and now channel 12 is outside the zone
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "o5:0");
  }

  TEST_CASE("mpeparse: only the master channel carries configuration (#535)") {
    // A member channel writing to some parameter of its own must not be read as
    // a zone resize — that would silently rewire the patch's idea of which
    // channels are notes.
    ParseRig rig("0 15");

    rig.Message("178 100 6"); // the same three messages, on channel 3
    rig.Message("178 101 0");
    rig.Message("178 6 4");

    // Controllers other than 74 are not this object's, so nothing comes out —
    // but the zone is untouched, which is the actual assertion.
    CHECK(rig.log.empty());
    rig.Message("155 60 100");
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "o5:1"); // channel 12 is still a member
  }

  TEST_CASE("mpeparse: the traffic that belongs to .midiparse is left alone (#535)") {
    // A deliberate deviation from Max, whose `mpeparse` is its `midiparse` plus
    // four MPE outlets. This patcher already has `.midiparse`; two decoders to
    // keep in step is one too many, and a patch wires both to one source.
    ParseRig rig("0 15");

    rig.Message("178 7 100"); // channel volume
    rig.Message("178 1 64"); // modulation wheel
    rig.Message("194 5"); // program change
    rig.Message("162 60 90"); // polyphonic key pressure
    CHECK(rig.log.empty());

    // And none of it disturbed the framing.
    rig.Message("146 60 100");
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[2] == "o0:60 100");
  }

  TEST_CASE("mpeparse: running status decodes a chord as the notes it is (#535)") {
    // One status byte followed by pairs of data bytes, which is what hardware
    // actually sends and what makes MIDI harder than it looks.
    ParseRig rig("0 15");

    rig.Byte(146);
    rig.Byte(60);
    rig.Byte(100);
    rig.Byte(64);
    rig.Byte(100);

    REQUIRE(rig.log.size() == 6);
    CHECK(rig.log[2] == "o0:60 100");
    CHECK(rig.log[5] == "o0:64 100");
  }

  TEST_CASE("mpeparse: a real-time byte inside a message does not break it (#535)") {
    // A timing clock may arrive between any two bytes of anything. It disturbs
    // neither running status nor the message it interrupted.
    ParseRig rig("0 15");

    rig.Byte(146);
    rig.Byte(60);
    rig.Byte(248); // MIDI clock, mid-message
    rig.Byte(100);

    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[2] == "o0:60 100");
  }

  TEST_CASE("mpeparse: a system-exclusive dump does not desynchronise the stream (#535)") {
    // The dump is not decoded here — that is `.midiparse`'s and `.sysexin`'s
    // job — but its bytes must not be read as a voice message's, or every
    // message after it would be wrong.
    ParseRig rig("0 15");

    rig.Byte(240); // sysex start
    rig.Byte(1);
    rig.Byte(2);
    rig.Byte(3);
    rig.Byte(247); // end of exclusive
    CHECK(rig.log.empty());

    rig.Message("146 60 100");
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[2] == "o0:60 100");
  }

  TEST_CASE("mpeparse: a song position pointer's data bytes are not read as a note (#535)") {
    // A system-common message clears running status and carries data bytes of
    // its own; swallowing exactly as many as it has is what keeps the framing.
    ParseRig rig("0 15");

    rig.Byte(146); // set running status
    rig.Byte(60);
    rig.Byte(100);
    REQUIRE(rig.log.size() == 3);
    rig.log.clear();

    rig.Byte(242); // song position pointer, two data bytes
    rig.Byte(10);
    rig.Byte(20);
    CHECK(rig.log.empty());

    // Running status is gone, so a bare pair of bytes now decodes as nothing.
    rig.Byte(64);
    rig.Byte(100);
    CHECK(rig.log.empty());
  }

  TEST_CASE("mpeparse: a byte that is not a byte is ignored, not clamped (#535)") {
    // Clamping would turn a stray number into a status byte and desynchronise
    // everything after it.
    ParseRig rig("0 15");

    rig.Byte(-5);
    rig.Byte(900);
    CHECK(rig.log.empty());

    rig.Message("146 60 100");
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[2] == "o0:60 100");
  }

  // ─── the JSON round trip ──────────────────────────────────────────────────

  TEST_CASE("mpeparse: zone and members survive a DumpJSON round trip (#535)") {
    // Read back through behaviour rather than an accessor: what has to survive
    // a save and a load is a patch that still reads the same zone the same way.
    ParseRig rig("1 4", /*roundTrip=*/true);
    CHECK(rig.object->GetName() == std::string(".mpeparse"));
    CHECK(std::string(rig.object->GetParams()) == "1 4");
    CHECK(rig.object->GetOutputs() == 6);

    rig.Message("159 60 100"); // channel 16 — the upper zone's master
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "o5:2");

    rig.log.clear();
    rig.Message("154 60 100"); // channel 11 — below a four-member upper zone
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "o5:0");
  }

  TEST_CASE("mpeconfig: the zone survives a DumpJSON / ParseJSON round trip (#535)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::M_MPECONFIG, "1") != nullptr);

    const std::string json = src.DumpJSON();
    CHECK(json.find(".mpeconfig") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* obj = loaded.GetHandleFromList(0);
    REQUIRE(obj != nullptr);
    CHECK(obj->GetName() == std::string(".mpeconfig"));
    CHECK(std::string(obj->GetParams()) == "1");

    ListLog log;
    YSE::pHandle logHandle(&log);
    loaded.Connect(obj, 0, &logHandle, 0);

    obj->SetIntData(0, 5);
    REQUIRE(log.received.size() == 3);
    CHECK(log.received[0] == "191 100 6"); // still the upper zone's master
    CHECK(log.received[2] == "191 6 5");

    loaded.DeleteObject(obj);
  }

  TEST_CASE("mpeformat: a real patch addresses a real member channel (#535)") {
    // The issue's claim at the level a patch makes it: built through
    // `CreateObject`, wired with a real cord, and fed the way a patch feeds it.
    // What comes out is what `.midiout` would hand the device.
    YSE::patcher patch;
    patch.create(2);

    YSE::pHandle* fmt = patch.CreateObject(YSE::OBJ::M_MPEFORMAT, "");
    REQUIRE(fmt != nullptr);
    CHECK(fmt->GetInputs() == 5);
    CHECK(fmt->GetOutputs() == 1);

    ListLog log;
    YSE::pHandle logHandle(&log);
    patch.Connect(fmt, 0, &logHandle, 0);

    fmt->SetIntData(4, 2); // this note lives on channel 2
    fmt->SetListData(0, "60 100");
    fmt->SetIntData(1, 9000);

    REQUIRE(log.received.size() == 2);
    CHECK(log.received[0] == "145 60 100");
    CHECK(log.received[1] == "225 40 70");

    patch.DeleteObject(fmt);
  }

  // ─── end to end, both loops closed ────────────────────────────────────────

  TEST_CASE("mpe: a .mpeconfig's own bytes reconfigure a .mpeparse (#535)") {
    // The configuration loop closed. A real `.mpeconfig` in a real patch builds
    // the message, every byte of it goes into a real `.mpeparse` in another
    // patch, and the reader's idea of the zone changes to match the writer's.
    // Neither object knows about the other, which is what makes this a test of
    // the wire format rather than of a shared assumption.
    ParseRig in("0 15");

    YSE::patcher patch;
    patch.create(2);
    YSE::pHandle* cfg = patch.CreateObject(YSE::OBJ::M_MPECONFIG, "0");
    REQUIRE(cfg != nullptr);

    ListLog log;
    YSE::pHandle logHandle(&log);
    patch.Connect(cfg, 0, &logHandle, 0);

    // Channel 8 is a member of a fifteen-member zone.
    in.Message("151 60 100");
    REQUIRE(in.log.size() == 3);
    CHECK(in.log[0] == "o5:1");
    in.log.clear();

    cfg->SetIntData(0, 3); // now cut the zone down to three member channels
    REQUIRE(log.received.size() == 3);
    for (const std::string& message : log.received)
      in.Message(message);
    CHECK(in.log.empty()); // configuration, not expression

    // Channel 8 is outside a three-member zone, and the reader knows it without
    // ever being told directly.
    in.Message("151 60 100");
    REQUIRE(in.log.size() == 3);
    CHECK(in.log[0] == "o5:0");
    CHECK(in.log[1] == "o4:8");

    // Channel 4 — the last member of the new zone — still is one.
    in.log.clear();
    in.Message("147 60 100");
    REQUIRE(in.log.size() == 3);
    CHECK(in.log[0] == "o5:1");

    patch.DeleteObject(cfg);
  }

  TEST_CASE("mpe: a .mpeformat's own bytes are read back by a .mpeparse (#535)") {
    // The expression loop closed, and the pair's inverse claim with it: every
    // one of MPE's four dimensions goes out of a real `.mpeformat` on a real
    // member channel and comes back out of a real `.mpeparse` as the value and
    // the note it started from.
    ParseRig in("0 15");

    YSE::patcher patch;
    patch.create(2);
    YSE::pHandle* fmt = patch.CreateObject(YSE::OBJ::M_MPEFORMAT, "");
    REQUIRE(fmt != nullptr);

    ListLog log;
    YSE::pHandle logHandle(&log);
    patch.Connect(fmt, 0, &logHandle, 0);

    fmt->SetIntData(4, 6); // one note, living on channel 6
    fmt->SetListData(0, "72 88");
    fmt->SetIntData(1, 12345);
    fmt->SetIntData(2, 77);
    fmt->SetIntData(3, 21);
    REQUIRE(log.received.size() == 4);

    for (const std::string& message : log.received)
      in.Message(message);

    REQUIRE(in.log.size() == 12);
    CHECK(in.log[0] == "o5:1"); // a member channel
    CHECK(in.log[1] == "o4:6");
    CHECK(in.log[2] == "o0:72 88"); // the note
    CHECK(in.log[5] == "o1:12345"); // the bend, all fourteen bits of it
    CHECK(in.log[8] == "o2:77"); // the pressure
    CHECK(in.log[11] == "o3:21"); // the slide

    patch.DeleteObject(fmt);
  }

} // TEST_SUITE
