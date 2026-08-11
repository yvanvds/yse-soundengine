// Tests for the extended-precision MIDI family (issue #533) — `.xbendin`,
// `.xbendin2`, `.xctlin`, `.xnotein`, `.xmidiin` on the receiving side and
// `.xbendout`, `.xbendout2`, `.xctlout`, `.xnoteout` on the sending side.
//
// What is being pinned:
//
//   - **the bits the 7-bit family throws away**: a pitch bend as all fourteen
//     of them (0-16383, 8192 at rest) rather than the coarse seven, in both
//     directions and in both spellings — combined and as the MSB/LSB pair;
//   - **the controller pairing**, which is the only real logic in the issue:
//     controller n carries the MSB and n+32 the LSB of one value, an MSB
//     emits at once with the fine byte read as 0 (the specification's rule,
//     and what keeps a coarse-only device from leaving the object silent), the
//     LSB that follows refines it, the stored coarse byte is per channel, and
//     controllers with no fine half are not this object's business;
//   - **release velocity**: a note-off carries a velocity byte of its own and
//     `.notein` discards it. `.xnotein` reports it and `.xnoteout` sends it,
//     without breaking `.notein`'s test for a release (velocity 0);
//   - **the framing** `.xmidiin` adds over `.midiin`: whole messages as
//     decimal byte lists, a dump split across transport packets arriving as
//     one message, running status put back in front, and a real-time byte
//     passing through the middle of a message without disturbing it;
//   - **the family conventions carried over unchanged**: channel filters that
//     never remove an outlet, channels reported as 1-16, outlets firing right
//     to left, clamping rather than refusing, a channel argument that cannot
//     corrupt the status nibble, and complete documentation metadata;
//   - **parameters surviving a DumpJSON / ParseJSON round trip**, read back
//     through behaviour rather than an accessor.
//
// The end-to-end sections drive the real machinery: the input objects through
// a real `patcherImplementation` fed by injecting into the real `MIDI::inHub`
// — the same entry point the RtMidi callback uses — and drained by real
// `Calculate()` blocks, and the senders through a real `YSE::patcher` with
// real creation arguments and real cords. That is the level at which this
// issue's claim ("a patch can now work at MIDI's full resolution") is either
// true or not.
//
// No audio device and no MIDI hardware required. The input half is compiled
// behind YSE_ENABLE_MIDI_DEVICE with the family it extends and the sending
// half behind YSE_WINDOWS with the senders it extends — see mMidiXIn.h and
// mMidiXOut.h, and issue #746 for the sweep that lifts the latter guard.

#include <doctest/doctest.h>
#include <cstddef>
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

#if YSE_ENABLE_MIDI_DEVICE
#include "midi/midiInHub.h"
#include "patcher/midi/mMidiXIn.h"
#include "patcher/patcherImplementation.h"
#endif

#if YSE_WINDOWS
#include "patcher/inlet.h"
#include "patcher/midi/mMidiXOut.h"
#include "sinks.hpp"
#endif

using YSE::PATCHER::Register;

namespace {

  // Every name this issue adds, split by the guard each half carries.
  const char* const kInFamily[] = {
      YSE::OBJ::M_XBENDIN, YSE::OBJ::M_XBENDIN2, YSE::OBJ::M_XCTLIN,
      YSE::OBJ::M_XNOTEIN, YSE::OBJ::M_XMIDIIN,
  };

  const char* const kOutFamily[] = {
      YSE::OBJ::M_XBENDOUT,
      YSE::OBJ::M_XBENDOUT2,
      YSE::OBJ::M_XCTLOUT,
      YSE::OBJ::M_XNOTEOUT,
  };

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

} // namespace

#if YSE_ENABLE_MIDI_DEVICE

namespace {

  using YSE::MIDI::InHub;
  using YSE::PATCHER::patcherImplementation;

  // A port high enough that a machine with real MIDI hardware is unlikely to
  // have one there injecting stray traffic into the assertions. Deliberately
  // not the 7 test_patcher_midiin.cpp uses, so a leaked subscription in either
  // file cannot make the other's assertions depend on test order.
  constexpr unsigned int kTestPort = 6;

  // Records every int that arrived tagged with the outlet that delivered it,
  // into a log shared by all the taps of one rig — which is what makes "channel
  // first, then release, then velocity, then pitch" an assertion rather than an
  // inference.
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
      return "xmidi_tap";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A registry-built extended-precision input object in a real patcher, with
  // one Tap on each outlet writing into one shared log.
  struct Rig {
    patcherImplementation patch{1, nullptr};
    std::vector<std::string> log;
    std::vector<std::unique_ptr<Tap>> taps;
    std::vector<std::unique_ptr<YSE::pHandle>> tapHandles;
    YSE::pHandle* object = nullptr;

    // `roundTrip` builds the object in a scratch patcher, dumps that to JSON
    // and loads it into this one, so the object under test is the one that
    // came back from storage. A round-trip case can then make exactly the
    // assertions a direct one makes, which is what "the parameters survived"
    // has to mean.
    Rig(const char* type, const std::string& args, bool roundTrip = false) {
      if (roundTrip) {
        patcherImplementation src{1, nullptr};
        REQUIRE(src.CreateObject(type, args) != nullptr);
        const std::string json = src.DumpJSON();
        CHECK(json.find(type) != std::string::npos);
        patch.ParseJSON(json);
        REQUIRE(patch.Objects() == 1);
        object = patch.GetHandleFromList(0);
      } else {
        object = patch.CreateObject(type, args);
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

    // Push one message onto the wire, exactly as the RtMidi callback would.
    void Wire(std::initializer_list<int> bytes) {
      std::vector<unsigned char> raw;
      raw.reserve(bytes.size());
      for (int b : bytes)
        raw.push_back(static_cast<unsigned char>(b));
      InHub().Deliver(kTestPort, raw.data(), raw.size());
    }

    // One audio block: the drain plus everything it triggers.
    void Block() {
      patch.Calculate(YSE::T_DSP);
    }

    void WireAndBlock(std::initializer_list<int> bytes) {
      Wire(bytes);
      Block();
    }
  };

  // The port argument every rig here is built with.
  const std::string kPortArg = std::to_string(kTestPort);

} // namespace

#endif // YSE_ENABLE_MIDI_DEVICE

#if YSE_WINDOWS

namespace {

  // The three wire bytes as a std::string, so an expectation reads as the
  // message it is.
  std::string Bytes(int status, int a, int b) {
    std::string s(3, '\0');
    s[0] = (char)status;
    s[1] = (char)a;
    s[2] = (char)b;
    return s;
  }

  // Keeps every list it was sent rather than only the last, which `.xctlout`
  // needs: one value on its inlet is two MIDI messages.
  struct ListLog : YSE::PATCHER::pObject {
    std::vector<std::string> received;

    ListLog() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { received.push_back(v); });
    }
    const char* Type() const override {
      return "list_log";
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
    const std::string& Last() const {
      REQUIRE_FALSE(log.received.empty());
      return log.received.back();
    }
  };

} // namespace

#endif // YSE_WINDOWS

TEST_SUITE("patcher") {

  // ─── registration and shape ───────────────────────────────────────────────

  TEST_CASE("xmidi: every object this platform compiles is registered (#533)") {
    auto names = Register().AllNames();
    auto registered = [&names](const char* type) {
      for (const auto& name : names) {
        if (name == std::string(type)) return true;
      }
      return false;
    };

#if YSE_ENABLE_MIDI_DEVICE
    for (const char* type : kInFamily) {
      CAPTURE(type);
      CHECK(registered(type));
      std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK(std::string(obj->Type()) == std::string(type));
    }
#endif
#if YSE_WINDOWS
    for (const char* type : kOutFamily) {
      CAPTURE(type);
      CHECK(registered(type));
      std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK(std::string(obj->Type()) == std::string(type));
    }
#endif
    // A platform-guarded family whose guard is wrong registers nothing at all,
    // and a test that only looped over an empty list would pass. The names are
    // spelled out so the loop above cannot be vacuous.
    CHECK(sizeof(kInFamily) / sizeof(kInFamily[0]) == 5);
    CHECK(sizeof(kOutFamily) / sizeof(kOutFamily[0]) == 4);
  }

  TEST_CASE("xmidi: every object documents itself (#533)") {
#if YSE_ENABLE_MIDI_DEVICE
    for (const char* type : kInFamily)
      CheckDocumented(type);
#endif
#if YSE_WINDOWS
    for (const char* type : kOutFamily)
      CheckDocumented(type);
#endif
  }

#if YSE_ENABLE_MIDI_DEVICE

  TEST_CASE("xmidi: the input objects take their input from the wire, not a cord (#533)") {
    // No inlets and no DSP edge, so `WantsBlockPoll()` is the only thing that
    // gets Calculate() called on them at all — inherited whole from #529.
    struct Expected {
      const char* type;
      int outlets;
    };
    const Expected expected[] = {
        {YSE::OBJ::M_XBENDIN, 2}, // bend, channel
        {YSE::OBJ::M_XBENDIN2, 3}, // msb, lsb, channel
        {YSE::OBJ::M_XCTLIN, 3}, // value, controller, channel
        {YSE::OBJ::M_XNOTEIN, 4}, // pitch, velocity, release, channel
        {YSE::OBJ::M_XMIDIIN, 1}, // the framed message
    };
    for (const Expected& e : expected) {
      CAPTURE(e.type);
      std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(e.type));
      REQUIRE(obj != nullptr);
      CHECK(obj->NumInputs() == 0);
      CHECK(obj->NumOutputs() == e.outlets);
      CHECK(obj->WantsBlockPoll());
      CHECK_FALSE(obj->IsDSPObject());
    }
  }

  // ─── .xbendin / .xbendin2 ─────────────────────────────────────────────────

  TEST_CASE("xbendin: the wheel is fourteen bits, centred at 8192 (#533)") {
    Rig rig(YSE::OBJ::M_XBENDIN, kPortArg);

    // Fine byte first on the wire, then coarse: 0x40 << 7 == 8192, the rest
    // position `.bendin` reports as 64.
    rig.WireAndBlock({0xE0, 0x00, 0x40});
    REQUIRE(rig.log.size() == 2);
    CHECK(rig.log[0] == "o1:1"); // channel first, right to left
    CHECK(rig.log[1] == "o0:8192");

    rig.log.clear();
    rig.WireAndBlock({0xE0, 0x7F, 0x7F});
    REQUIRE(rig.log.size() == 2);
    CHECK(rig.log[1] == "o0:16383");

    rig.log.clear();
    rig.WireAndBlock({0xE0, 0x00, 0x00});
    REQUIRE(rig.log.size() == 2);
    CHECK(rig.log[1] == "o0:0");
  }

  TEST_CASE("xbendin: the fine byte is not ignored, which is the whole point (#533)") {
    // The regression that matters: a `.bendin` and an `.xbendin` fed the same
    // message must differ, and they differ by exactly the fine byte. Two
    // messages with the same coarse byte and different fine bytes are one
    // value apart here and indistinguishable to `.bendin`.
    Rig rig(YSE::OBJ::M_XBENDIN, kPortArg);

    rig.WireAndBlock({0xE0, 0x00, 0x40});
    rig.WireAndBlock({0xE0, 0x01, 0x40});
    REQUIRE(rig.log.size() == 4);
    CHECK(rig.log[1] == "o0:8192");
    CHECK(rig.log[3] == "o0:8193");
  }

  TEST_CASE("xbendin2: the two bytes come out apart, right to left (#533)") {
    Rig rig(YSE::OBJ::M_XBENDIN2, kPortArg);

    rig.WireAndBlock({0xE0, 0x05, 0x40});
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "o2:1"); // channel
    CHECK(rig.log[1] == "o1:5"); // LSB
    CHECK(rig.log[2] == "o0:64"); // MSB — `.bendin`'s reading
  }

  TEST_CASE("xbendin: the channel argument filters and keeps its outlet (#533)") {
    Rig rig(YSE::OBJ::M_XBENDIN, kPortArg + " 3");

    rig.WireAndBlock({0xE0 + 4, 0x00, 0x40}); // channel 5 on the wire
    CHECK(rig.log.empty());

    rig.WireAndBlock({0xE0 + 2, 0x00, 0x50}); // channel 3
    REQUIRE(rig.log.size() == 2);
    CHECK(rig.log[0] == "o1:3");
    CHECK(rig.log[1] == "o0:10240");
  }

  // ─── .xnotein ─────────────────────────────────────────────────────────────

  TEST_CASE("xnotein: a note-on reports its velocity and no release (#533)") {
    Rig rig(YSE::OBJ::M_XNOTEIN, kPortArg);

    rig.WireAndBlock({0x90, 60, 100});
    REQUIRE(rig.log.size() == 4);
    CHECK(rig.log[0] == "o3:1"); // channel
    CHECK(rig.log[1] == "o2:0"); // release velocity
    CHECK(rig.log[2] == "o1:100"); // velocity
    CHECK(rig.log[3] == "o0:60"); // pitch
  }

  TEST_CASE("xnotein: a note-off reports the release velocity .notein throws away (#533)") {
    // The object's reason to exist. Velocity stays 0 so `.notein`'s test for a
    // release still reads a release here; the number the message actually
    // carried comes out on its own outlet.
    Rig rig(YSE::OBJ::M_XNOTEIN, kPortArg);

    rig.WireAndBlock({0x80, 60, 77});
    REQUIRE(rig.log.size() == 4);
    CHECK(rig.log[1] == "o2:77"); // release velocity — the new information
    CHECK(rig.log[2] == "o1:0"); // velocity, as `.notein` reports a release
    CHECK(rig.log[3] == "o0:60");
  }

  TEST_CASE("xnotein: a note-on with velocity 0 invents no release velocity (#533)") {
    // The other spelling of a release. The device sent no release velocity, so
    // reporting 64 (or anything else) would be a number nobody transmitted.
    Rig rig(YSE::OBJ::M_XNOTEIN, kPortArg);

    rig.WireAndBlock({0x90, 60, 0});
    REQUIRE(rig.log.size() == 4);
    CHECK(rig.log[1] == "o2:0");
    CHECK(rig.log[2] == "o1:0");
    CHECK(rig.log[3] == "o0:60");
  }

  // ─── .xctlin ──────────────────────────────────────────────────────────────

  TEST_CASE("xctlin: an MSB emits at once with the fine byte read as 0 (#533)") {
    // The specification's rule, and the decision that keeps a coarse-only
    // device — which is most of them — from leaving this object silent.
    Rig rig(YSE::OBJ::M_XCTLIN, kPortArg);

    rig.WireAndBlock({0xB0, 7, 100});
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "o2:1"); // channel
    CHECK(rig.log[1] == "o1:7"); // controller
    CHECK(rig.log[2] == "o0:12800"); // 100 << 7
  }

  TEST_CASE("xctlin: the LSB pairs with controller n+32 and refines the value (#533)") {
    // The only real logic in the issue: controller 7 carries the MSB and
    // controller 39 the LSB of one fourteen-bit value, reported on 7.
    Rig rig(YSE::OBJ::M_XCTLIN, kPortArg);

    rig.WireAndBlock({0xB0, 7, 100});
    rig.log.clear();
    rig.WireAndBlock({0xB0, 7 + 32, 64});

    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[1] == "o1:7"); // the coarse number, not 39
    CHECK(rig.log[2] == "o0:12864"); // (100 << 7) | 64
  }

  TEST_CASE("xctlin: the stored coarse byte is per channel (#533)") {
    // Two keyboards sending the same controller on different channels must not
    // cross-contaminate each other's fine bytes.
    Rig rig(YSE::OBJ::M_XCTLIN, kPortArg);

    rig.WireAndBlock({0xB0 + 0, 7, 100}); // channel 1, MSB 100
    rig.WireAndBlock({0xB0 + 1, 7, 20}); // channel 2, MSB 20
    rig.log.clear();

    rig.WireAndBlock({0xB0 + 0, 39, 1}); // channel 1's LSB
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "o2:1");
    CHECK(rig.log[2] == "o0:12801"); // (100 << 7) | 1, not (20 << 7) | 1
  }

  TEST_CASE("xctlin: controllers with no fine half are not its business (#533)") {
    // 64 is the sustain pedal — `.ctlin`'s. Reporting it here with a
    // fabricated fourteen-bit value would make a switch look like a fader.
    Rig rig(YSE::OBJ::M_XCTLIN, kPortArg);

    rig.WireAndBlock({0xB0, 64, 127});
    CHECK(rig.log.empty());
  }

  TEST_CASE("xctlin: the controller argument names the coarse half (#533)") {
    Rig rig(YSE::OBJ::M_XCTLIN, kPortArg + " 0 7");

    rig.WireAndBlock({0xB0, 10, 100}); // a different controller
    CHECK(rig.log.empty());

    rig.WireAndBlock({0xB0, 7, 100});
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[2] == "o0:12800");

    // The fine half of the accepted pair passes the filter too — it is the
    // same controller.
    rig.log.clear();
    rig.WireAndBlock({0xB0, 39, 64});
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[2] == "o0:12864");
  }

  // ─── .xmidiin ─────────────────────────────────────────────────────────────

  TEST_CASE("xmidiin: a message arrives as one list of decimal bytes (#533)") {
    // `.midiformat`'s output shape and `.midiparse`'s input shape, so the
    // framed stream wires straight across.
    Rig rig(YSE::OBJ::M_XMIDIIN, kPortArg);

    rig.WireAndBlock({0x90, 60, 100});
    REQUIRE(rig.log.size() == 1);
    CHECK(rig.log[0] == "o0:144 60 100");
  }

  TEST_CASE("xmidiin: a dump split across transport packets arrives whole (#533)") {
    // The behaviour the object is named for. `inEvent` carries eight bytes, so
    // a twelve-byte dump is delivered as two chunks; `.midiin` would report
    // twelve loose ints with nothing saying they were one message.
    Rig rig(YSE::OBJ::M_XMIDIIN, kPortArg);

    rig.WireAndBlock({0xF0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0xF7});
    REQUIRE(rig.log.size() == 1);
    CHECK(rig.log[0] == "o0:240 1 2 3 4 5 6 7 8 9 10 247");
  }

  TEST_CASE("xmidiin: a clock inside a dump comes out on its own and disturbs nothing (#533)") {
    // A real-time byte may legally appear between any two bytes of any other
    // message. The dump must survive it whole.
    Rig rig(YSE::OBJ::M_XMIDIIN, kPortArg);

    rig.Wire({0xF0, 1, 2});
    rig.Wire({0xF8}); // timing clock, as the backend delivers it
    rig.Wire({3, 0xF7});
    rig.Block();

    REQUIRE(rig.log.size() == 2);
    CHECK(rig.log[0] == "o0:248"); // the clock, at once and on its own
    CHECK(rig.log[1] == "o0:240 1 2 3 247"); // the dump, unbroken
  }

  TEST_CASE("xmidiin: running status is put back in front of each message (#533)") {
    // A chord arrives as one status byte and then pairs of data bytes. Each
    // pair leaves as a complete message, which is what makes the lists uniform.
    Rig rig(YSE::OBJ::M_XMIDIIN, kPortArg);

    rig.Wire({0x90, 60, 100});
    rig.Wire({64, 100});
    rig.Wire({67, 100});
    rig.Block();

    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "o0:144 60 100");
    CHECK(rig.log[1] == "o0:144 64 100");
    CHECK(rig.log[2] == "o0:144 67 100");
  }

  TEST_CASE("xmidiin: a two-byte message is framed by its own length (#533)") {
    Rig rig(YSE::OBJ::M_XMIDIIN, kPortArg);

    rig.Wire({0xC0, 5}); // program change: one data byte
    rig.Wire({0xD0, 90}); // channel pressure: one data byte
    rig.Block();

    REQUIRE(rig.log.size() == 2);
    CHECK(rig.log[0] == "o0:192 5");
    CHECK(rig.log[1] == "o0:208 90");
  }

  TEST_CASE("xmidiin: a new status abandons an unfinished message (#533)") {
    // Hardware interrupted mid-message starts sending something else rather
    // than finishing what it began; waiting for data that is not coming would
    // put the fragment in front of the next message.
    Rig rig(YSE::OBJ::M_XMIDIIN, kPortArg);

    rig.Wire({0x90, 60}); // a note-on that never gets its velocity
    rig.Wire({0xC0, 5});
    rig.Block();

    REQUIRE(rig.log.size() == 1);
    CHECK(rig.log[0] == "o0:192 5");
  }

  // ─── the JSON round trip ──────────────────────────────────────────────────

  TEST_CASE("xctlin: port, channel and controller survive a DumpJSON round trip (#533)") {
    // Read back through behaviour rather than an accessor: what has to survive
    // a save and a load is a patch that still listens to the same port, the
    // same channel and the same controller pair.
    Rig rig(YSE::OBJ::M_XCTLIN, kPortArg + " 3 7", /*roundTrip=*/true);
    CHECK(rig.object->GetName() == std::string(".xctlin"));
    CHECK(std::string(rig.object->GetParams()) == kPortArg + " 3 7");

    rig.WireAndBlock({0xB0 + 0, 7, 100}); // channel 1 — filtered out
    CHECK(rig.log.empty());

    rig.WireAndBlock({0xB0 + 2, 10, 100}); // channel 3, wrong controller
    CHECK(rig.log.empty());

    rig.WireAndBlock({0xB0 + 2, 7, 100}); // channel 3, controller 7
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "o2:3");
    CHECK(rig.log[1] == "o1:7");
    CHECK(rig.log[2] == "o0:12800");
  }

#endif // YSE_ENABLE_MIDI_DEVICE

#if YSE_WINDOWS

  TEST_CASE("xmidi: the senders have the shape their seven-bit siblings have (#533)") {
    struct Expected {
      const char* type;
      int inlets;
    };
    const Expected expected[] = {
        {YSE::OBJ::M_XBENDOUT, 1}, // value
        {YSE::OBJ::M_XBENDOUT2, 2}, // msb, lsb
        {YSE::OBJ::M_XCTLOUT, 2}, // value, controller
        {YSE::OBJ::M_XNOTEOUT, 3}, // pitch, velocity, release
    };
    for (const Expected& e : expected) {
      CAPTURE(e.type);
      std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(e.type));
      REQUIRE(obj != nullptr);
      CHECK(obj->NumInputs() == e.inlets);
      CHECK(obj->NumOutputs() == 1);
      // Message formatters: nothing renders them and nothing polls them.
      CHECK_FALSE(obj->IsDSPObject());
      CHECK_FALSE(obj->WantsBlockPoll());
    }
  }

  // ─── .xbendout / .xbendout2 ───────────────────────────────────────────────

  TEST_CASE("xbendout: a value fills both bytes, fine first (#533)") {
    // Where `.bendout` sends the value as the coarse byte and 0 as the fine
    // one, this fills both — and the wire order is fine then coarse, so a
    // generator that swapped them would bend by almost nothing and look right.
    SendRig<YSE::PATCHER::mXBendOut> rig;

    rig.Send(0, 8192); // the wheel at rest
    CHECK(rig.Last() == Bytes(0xE0, 0, 64));

    rig.Send(0, 8193);
    CHECK(rig.Last() == Bytes(0xE0, 1, 64));

    rig.Send(0, 16383);
    CHECK(rig.Last() == Bytes(0xE0, 127, 127));

    rig.Send(0, 0);
    CHECK(rig.Last() == Bytes(0xE0, 0, 0));
  }

  TEST_CASE("xbendout: values outside 0-16383 are clamped, not refused (#533)") {
    SendRig<YSE::PATCHER::mXBendOut> rig;

    rig.Send(0, -40);
    CHECK(rig.Last() == Bytes(0xE0, 0, 0));

    rig.Send(0, 90000);
    CHECK(rig.Last() == Bytes(0xE0, 127, 127));
  }

  TEST_CASE("xbendout: a channel outside 0-15 cannot corrupt the status byte (#533)") {
    // 0xE0 + 16 is 0xF0, a system-exclusive start rather than a bend at all —
    // `.bendout`'s rule (#532), applied to every sender this issue adds.
    {
      SendRig<YSE::PATCHER::mXBendOut> rig("5");
      rig.Send(0, 8192);
      CHECK(rig.Last() == Bytes(0xE5, 0, 64));
    }
    {
      SendRig<YSE::PATCHER::mXBendOut> rig("16");
      rig.Send(0, 8192);
      CHECK(rig.Last() == Bytes(0xEF, 0, 64));
    }
    {
      SendRig<YSE::PATCHER::mXBendOut> rig("-3");
      rig.Send(0, 8192);
      CHECK(rig.Last() == Bytes(0xE0, 0, 64));
    }
  }

  TEST_CASE("xbendout2: the MSB is hot and the LSB stores (#533)") {
    SendRig<YSE::PATCHER::mXBendOut2> rig;

    rig.Send(1, 5); // LSB — stores, emits nothing
    CHECK(rig.log.received.empty());

    rig.Send(0, 64); // MSB — fires
    CHECK(rig.Last() == Bytes(0xE0, 5, 64));

    // The stored LSB survives until it is replaced.
    rig.Send(0, 65);
    CHECK(rig.Last() == Bytes(0xE0, 5, 65));
  }

  TEST_CASE("xbendout2 round-trips .xbendin2 byte for byte (#533)") {
    // The pair's reason to be separate objects: the two halves come out of one
    // and into the other without ever being combined and re-split.
    SendRig<YSE::PATCHER::mXBendOut2> rig;
    rig.Send(1, 100);
    rig.Send(0, 33);
    CHECK(rig.Last() == Bytes(0xE0, 100, 33));
  }

  // ─── .xctlout ─────────────────────────────────────────────────────────────

  TEST_CASE("xctlout: one value is two messages, coarse first (#533)") {
    // The order the specification requires. Two separate lists rather than one
    // six-byte list, because `.midiout` sends each list as a single message.
    SendRig<YSE::PATCHER::mXCtlOut> rig;

    rig.Send(1, 7); // controller 7
    rig.Send(0, 12864); // (100 << 7) | 64

    REQUIRE(rig.log.received.size() == 2);
    CHECK(rig.log.received[0] == Bytes(0xB0, 7, 100));
    CHECK(rig.log.received[1] == Bytes(0xB0, 7 + 32, 64));
  }

  TEST_CASE("xctlout: the controller is clamped into the paired range (#533)") {
    // Above 31 there is no fine half, and sending its "LSB" would write to an
    // unrelated controller 32 higher up.
    SendRig<YSE::PATCHER::mXCtlOut> rig("2");

    rig.Send(1, 64);
    rig.Send(0, 16383);

    REQUIRE(rig.log.received.size() == 2);
    CHECK(rig.log.received[0] == Bytes(0xB2, 31, 127));
    CHECK(rig.log.received[1] == Bytes(0xB2, 63, 127));
  }

  // ─── .xnoteout ────────────────────────────────────────────────────────────

  TEST_CASE("xnoteout: velocity 0 sends a real note-off carrying the release (#533)") {
    // Both `.noteon` and `.noteoff` in one box, plus the number neither can
    // send.
    SendRig<YSE::PATCHER::mXNoteOut> rig;

    rig.Send(1, 100); // velocity
    rig.Send(2, 77); // release velocity
    rig.Send(0, 60); // pitch — fires
    CHECK(rig.Last() == Bytes(0x90, 60, 100));

    rig.Send(1, 0); // a release
    rig.Send(0, 60);
    CHECK(rig.Last() == Bytes(0x80, 60, 77));
  }

  TEST_CASE("xnoteout: the release velocity is ignored while a note is on (#533)") {
    // It belongs to the note-off alone; putting it in a note-on would replace
    // the striking velocity with it.
    SendRig<YSE::PATCHER::mXNoteOut> rig;

    rig.Send(2, 5);
    rig.Send(1, 90);
    rig.Send(0, 61);
    CHECK(rig.Last() == Bytes(0x90, 61, 90));
  }

  TEST_CASE("xnoteout: all three values are clamped rather than refused (#533)") {
    SendRig<YSE::PATCHER::mXNoteOut> rig;

    rig.Send(1, 0);
    rig.Send(2, 500);
    rig.Send(0, 900);
    CHECK(rig.Last() == Bytes(0x80, 127, 127));
  }

  // ─── end to end, in a real patcher ────────────────────────────────────────

  TEST_CASE("xnoteout: a real patch sends a release velocity on a real channel (#533)") {
    // The issue's claim at the level a patch makes it: built through
    // `CreateObject` with a creation argument, wired with a real cord, and fed
    // the way a patch feeds it. What comes out is what `.midiout` would hand
    // the device.
    YSE::patcher patch;
    patch.create(2);

    YSE::pHandle* note = patch.CreateObject(YSE::OBJ::M_XNOTEOUT, "2");
    REQUIRE(note != nullptr);
    CHECK(note->GetInputs() == 3);
    CHECK(note->GetOutputs() == 1);

    ListLog log;
    YSE::pHandle logHandle(&log);
    patch.Connect(note, 0, &logHandle, 0);

    note->SetIntData(2, 88); // release velocity
    note->SetIntData(1, 0); // velocity 0 — this is a release
    note->SetIntData(0, 64); // pitch fires

    REQUIRE(log.received.size() == 1);
    CHECK(log.received[0] == Bytes(0x82, 64, 88));

    patch.DeleteObject(note);
  }

  TEST_CASE("xctlout: the channel survives a DumpJSON / ParseJSON round trip (#533)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::M_XCTLOUT, "7") != nullptr);

    const std::string json = src.DumpJSON();
    CHECK(json.find(".xctlout") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* obj = loaded.GetHandleFromList(0);
    REQUIRE(obj != nullptr);
    CHECK(obj->GetName() == std::string(".xctlout"));
    CHECK(std::string(obj->GetParams()) == "7");

    ListLog log;
    YSE::pHandle logHandle(&log);
    loaded.Connect(obj, 0, &logHandle, 0);

    obj->SetIntData(1, 10);
    obj->SetIntData(0, 12864);
    REQUIRE(log.received.size() == 2);
    CHECK(log.received[0] == Bytes(0xB7, 10, 100));
    CHECK(log.received[1] == Bytes(0xB7, 42, 64));

    loaded.DeleteObject(obj);
  }

#endif // YSE_WINDOWS

} // TEST_SUITE
