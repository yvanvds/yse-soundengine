// Tests for the RPN/NRPN family (issue #534) — `.rpnin` and `.nrpnin` on the
// receiving side, `.rpnout` and `.nrpnout` on the sending side.
//
// What is being pinned:
//
//   - **the sequence**, which is the whole family: a parameter-number write is
//     not one MIDI message but four control changes — two that select a 14-bit
//     parameter number and two that write a 14-bit value into whatever is
//     selected. The senders emit all four, in order and as four separate lists;
//     the input objects reassemble them;
//   - **the state machine**, which is the only real logic in the issue: a value
//     byte carries no parameter number of its own, so the input objects
//     remember what was selected. Per channel, so two devices on one cable
//     cannot cross-contaminate; and over one shared selection register, so an
//     interleaved RPN/NRPN stream comes out of the right box and neither object
//     reports the other's writes;
//   - **abandoned sequences**: the RPN Null (parameter 16383, selected with
//     controllers 101/100) deselects, so a Data Entry that follows an abandoned
//     sequence is reported by nobody — and the Null is registered-only, so a
//     legal NRPN 16383 is still heard;
//   - **emitting on both halves of the value**, `.xctlin`'s rule (#533): the
//     coarse byte emits at once with the fine byte read as 0, because the many
//     devices that send only the coarse byte would otherwise leave the object
//     silent forever;
//   - **the family conventions carried over unchanged**: channel filters that
//     never remove an outlet, channels reported as 1-16, outlets firing right
//     to left, clamping rather than refusing, a channel argument that cannot
//     corrupt the status nibble, and complete documentation metadata;
//   - **parameters surviving a DumpJSON / ParseJSON round trip**, read back
//     through behaviour rather than an accessor.
//
// The end-to-end sections drive the real machinery: the input objects through a
// real `patcherImplementation` fed by injecting into the real `MIDI::inHub` —
// the same entry point the RtMidi callback uses — and drained by real
// `Calculate()` blocks, and the senders through a real `YSE::patcher` with real
// creation arguments and real cords. The last case of all closes the loop: a
// `.rpnout` in one patch feeding a `.rpnin` in another, over the wire, which is
// the level at which this issue's claim ("a patcher can now speak to a hardware
// synth's deeper parameters") is either true or not.
//
// No audio device and no MIDI hardware required. The input half is compiled
// behind YSE_ENABLE_MIDI_DEVICE with the family it extends (see mMidiRpnIn.h);
// the sending half is unconditional.

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
#include "patcher/midi/mMidiRpnOut.h"
#include "sinks.hpp"

#if YSE_ENABLE_MIDI_DEVICE
#include "midi/midiInHub.h"
#include "patcher/midi/mMidiRpnIn.h"
#include "patcher/patcherImplementation.h"
#endif

using YSE::PATCHER::Register;

namespace {

  // Every name this issue adds, split by the guard each half carries.
  const char* const kInFamily[] = {
      YSE::OBJ::M_RPNIN,
      YSE::OBJ::M_NRPNIN,
  };

  const char* const kOutFamily[] = {
      YSE::OBJ::M_RPNOUT,
      YSE::OBJ::M_NRPNOUT,
  };

  // The six controllers the family speaks in, spelled so an expectation reads
  // as the message it is.
  constexpr int kNrpnLsb = 98;
  constexpr int kNrpnMsb = 99;
  constexpr int kRpnLsb = 100;
  constexpr int kRpnMsb = 101;
  constexpr int kDataMsb = 6;
  constexpr int kDataLsb = 38;

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

  // A port of this file's own, so a leaked subscription in a neighbouring MIDI
  // test file cannot make these assertions depend on test order. Deliberately
  // not the 7 of test_patcher_midiin.cpp nor the 6 of test_patcher_xmidi.cpp.
  constexpr unsigned int kTestPort = 5;

  // Records every int that arrived tagged with the outlet that delivered it,
  // into a log shared by all the taps of one rig — which is what makes "channel
  // first, then parameter, then value" an assertion rather than an inference.
  struct Tap : YSE::PATCHER::pObject {
    std::vector<std::string>* log = nullptr;
    std::string tag;

    Tap() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        if (log) log->push_back(tag + ":" + std::to_string(v));
      });
    }
    const char* Type() const override {
      return "rpn_tap";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A registry-built parameter-number input object in a real patcher, with one
  // Tap on each outlet writing into one shared log.
  struct Rig {
    patcherImplementation patch{1, nullptr};
    std::vector<std::string> log;
    std::vector<std::unique_ptr<Tap>> taps;
    std::vector<std::unique_ptr<YSE::pHandle>> tapHandles;
    YSE::pHandle* object = nullptr;

    // `roundTrip` builds the object in a scratch patcher, dumps that to JSON and
    // loads it into this one, so the object under test is the one that came back
    // from storage. A round-trip case can then make exactly the assertions a
    // direct one makes, which is what "the parameters survived" has to mean.
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

    // Push one control change onto the wire, exactly as the RtMidi callback
    // would. `channel` is the 0-15 nibble.
    void CC(int channel, int controller, int value) {
      const unsigned char raw[3] = {
          static_cast<unsigned char>(0xB0 + channel),
          static_cast<unsigned char>(controller),
          static_cast<unsigned char>(value),
      };
      InHub().Deliver(kTestPort, raw, 3);
    }

    // One audio block: the drain plus everything it triggers.
    void Block() {
      patch.Calculate(YSE::T_DSP);
    }

    // The whole gesture a well-behaved controller sends: select, then write both
    // halves of the value. `selectMsb` / `selectLsb` pick the RPN or NRPN pair.
    void Sequence(int channel, int selectMsb, int selectLsb, int parameter, int value) {
      CC(channel, selectMsb, (parameter >> 7) & 0x7F);
      CC(channel, selectLsb, parameter & 0x7F);
      CC(channel, kDataMsb, (value >> 7) & 0x7F);
      CC(channel, kDataLsb, value & 0x7F);
      Block();
    }
  };

  // The port argument every rig here is built with.
  const std::string kPortArg = std::to_string(kTestPort);

} // namespace

#endif // YSE_ENABLE_MIDI_DEVICE

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

  // Keeps every list it was sent rather than only the last, which this family
  // needs: one value on a sender's inlet is four MIDI messages.
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
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── registration and shape ───────────────────────────────────────────────

  TEST_CASE("rpn: every object this platform compiles is registered (#534)") {
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
    // The sending half has no platform guard at all, so this loop runs
    // everywhere.
    for (const char* type : kOutFamily) {
      CAPTURE(type);
      CHECK(registered(type));
      std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK(std::string(obj->Type()) == std::string(type));
    }
    // A platform-guarded family whose guard is wrong registers nothing at all,
    // and a test that only looped over an empty list would pass. The names are
    // spelled out so the loops above cannot be vacuous.
    CHECK(sizeof(kInFamily) / sizeof(kInFamily[0]) == 2);
    CHECK(sizeof(kOutFamily) / sizeof(kOutFamily[0]) == 2);
  }

  TEST_CASE("rpn: every object documents itself (#534)") {
#if YSE_ENABLE_MIDI_DEVICE
    for (const char* type : kInFamily)
      CheckDocumented(type);
#endif
    for (const char* type : kOutFamily)
      CheckDocumented(type);
  }

  TEST_CASE("rpn: the senders have the shape their siblings have (#534)") {
    for (const char* type : kOutFamily) {
      CAPTURE(type);
      std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK(obj->NumInputs() == 2); // value, parameter number
      CHECK(obj->NumOutputs() == 1);
      // Message formatters: nothing renders them and nothing polls them.
      CHECK_FALSE(obj->IsDSPObject());
      CHECK_FALSE(obj->WantsBlockPoll());
    }
  }

  // ─── .rpnout / .nrpnout ───────────────────────────────────────────────────

  TEST_CASE("rpnout: one value is four messages, selection first (#534)") {
    // The object's whole reason to exist. Four separate lists rather than one
    // twelve-byte list, because `.midiout` sends each list it receives as a
    // single MIDI message.
    SendRig<YSE::PATCHER::mRpnOut> rig;

    rig.Send(1, 0); // parameter 0 — pitch-bend sensitivity
    CHECK(rig.log.received.empty()); // the cold inlet stores and does not fire

    rig.Send(0, 258); // value: MSB 2, LSB 2
    REQUIRE(rig.log.received.size() == 4);
    CHECK(rig.log.received[0] == Bytes(0xB0, kRpnMsb, 0));
    CHECK(rig.log.received[1] == Bytes(0xB0, kRpnLsb, 0));
    CHECK(rig.log.received[2] == Bytes(0xB0, kDataMsb, 2));
    CHECK(rig.log.received[3] == Bytes(0xB0, kDataLsb, 2));
  }

  TEST_CASE("nrpnout: the same four messages on the non-registered pair (#534)") {
    // The only difference between the two objects, and one a copy-paste could
    // easily get wrong: 99/98 selects instead of 101/100.
    SendRig<YSE::PATCHER::mNrpnOut> rig;

    rig.Send(1, 1057); // MSB 8, LSB 33
    rig.Send(0, 16383);
    REQUIRE(rig.log.received.size() == 4);
    CHECK(rig.log.received[0] == Bytes(0xB0, kNrpnMsb, 8));
    CHECK(rig.log.received[1] == Bytes(0xB0, kNrpnLsb, 33));
    CHECK(rig.log.received[2] == Bytes(0xB0, kDataMsb, 127));
    CHECK(rig.log.received[3] == Bytes(0xB0, kDataLsb, 127));
  }

  TEST_CASE("rpnout: the selection is re-sent with every value (#534)") {
    // Not an optimisation to be reclaimed later: the receiving device has one
    // selected parameter, and anything else on the cable can move it between
    // two values from this box. A sender that assumed its selection survived
    // would write into whatever was selected last.
    SendRig<YSE::PATCHER::mRpnOut> rig;

    rig.Send(1, 2);
    rig.Send(0, 1000);
    rig.Send(0, 2000);

    REQUIRE(rig.log.received.size() == 8);
    CHECK(rig.log.received[4] == Bytes(0xB0, kRpnMsb, 0));
    CHECK(rig.log.received[5] == Bytes(0xB0, kRpnLsb, 2));
  }

  TEST_CASE("rpnout: values and parameter numbers are clamped, not refused (#534)") {
    SendRig<YSE::PATCHER::mRpnOut> rig;

    rig.Send(1, -8);
    rig.Send(0, -40);
    REQUIRE(rig.log.received.size() == 4);
    CHECK(rig.log.received[0] == Bytes(0xB0, kRpnMsb, 0));
    CHECK(rig.log.received[1] == Bytes(0xB0, kRpnLsb, 0));
    CHECK(rig.log.received[2] == Bytes(0xB0, kDataMsb, 0));
    CHECK(rig.log.received[3] == Bytes(0xB0, kDataLsb, 0));

    rig.log.received.clear();
    rig.Send(1, 90000);
    rig.Send(0, 90000);
    REQUIRE(rig.log.received.size() == 4);
    CHECK(rig.log.received[0] == Bytes(0xB0, kRpnMsb, 127));
    CHECK(rig.log.received[1] == Bytes(0xB0, kRpnLsb, 127));
    CHECK(rig.log.received[2] == Bytes(0xB0, kDataMsb, 127));
    CHECK(rig.log.received[3] == Bytes(0xB0, kDataLsb, 127));
  }

  TEST_CASE("rpnout: a channel outside 0-15 cannot corrupt the status byte (#534)") {
    // 0xB0 + 16 is 0xC0, a program change rather than a control change at all —
    // `.bendout`'s rule (#532), applied to every sender in the MIDI family.
    {
      SendRig<YSE::PATCHER::mRpnOut> rig("5");
      rig.Send(0, 0);
      REQUIRE(rig.log.received.size() == 4);
      CHECK(rig.log.received[0] == Bytes(0xB5, kRpnMsb, 0));
    }
    {
      SendRig<YSE::PATCHER::mNrpnOut> rig("16");
      rig.Send(0, 0);
      REQUIRE(rig.log.received.size() == 4);
      CHECK(rig.log.received[0] == Bytes(0xBF, kNrpnMsb, 0));
    }
    {
      SendRig<YSE::PATCHER::mNrpnOut> rig("-3");
      rig.Send(0, 0);
      REQUIRE(rig.log.received.size() == 4);
      CHECK(rig.log.received[0] == Bytes(0xB0, kNrpnMsb, 0));
    }
  }

#if YSE_ENABLE_MIDI_DEVICE

  // ─── shape of the input objects ───────────────────────────────────────────

  TEST_CASE("rpn: the input objects take their input from the wire, not a cord (#534)") {
    // No inlets and no DSP edge, so `WantsBlockPoll()` is the only thing that
    // gets Calculate() called on them at all — inherited whole from #529.
    for (const char* type : kInFamily) {
      CAPTURE(type);
      std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK(obj->NumInputs() == 0);
      CHECK(obj->NumOutputs() == 3); // value, parameter, channel
      CHECK(obj->WantsBlockPoll());
      CHECK_FALSE(obj->IsDSPObject());
    }
  }

  // ─── .rpnin / .nrpnin: the sequence ───────────────────────────────────────

  TEST_CASE("rpnin: four control changes come out as one parameter write (#534)") {
    Rig rig(YSE::OBJ::M_RPNIN, kPortArg);

    // Parameter 2 (coarse tuning), value 8192.
    rig.Sequence(0, kRpnMsb, kRpnLsb, 2, 8192);

    // Two emissions, because the value arrives in two halves: the coarse byte
    // emits at once with the fine byte read as 0, then the LSB refines it.
    REQUIRE(rig.log.size() == 6);
    CHECK(rig.log[0] == "o2:1"); // channel first, right to left
    CHECK(rig.log[1] == "o1:2"); // parameter number
    CHECK(rig.log[2] == "o0:8192"); // MSB alone
    CHECK(rig.log[3] == "o2:1");
    CHECK(rig.log[4] == "o1:2");
    CHECK(rig.log[5] == "o0:8192"); // refined — the LSB here is 0
  }

  TEST_CASE("rpnin: the coarse byte emits without waiting for a partner (#534)") {
    // The decision `.xctlin` made (#533), and the one that matters most in
    // practice: a great many devices send Data Entry MSB alone, and an object
    // that waited for the pair would be silent forever on all of them.
    Rig rig(YSE::OBJ::M_RPNIN, kPortArg);

    rig.CC(0, kRpnMsb, 0);
    rig.CC(0, kRpnLsb, 0);
    rig.CC(0, kDataMsb, 2); // and no LSB ever follows
    rig.Block();

    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[2] == "o0:256"); // 2 << 7
  }

  TEST_CASE("rpnin: the fine byte pairs with the coarse one it follows (#534)") {
    Rig rig(YSE::OBJ::M_RPNIN, kPortArg);

    rig.CC(0, kRpnMsb, 0);
    rig.CC(0, kRpnLsb, 1); // parameter 1 — fine tuning
    rig.CC(0, kDataMsb, 64);
    rig.CC(0, kDataLsb, 5);
    rig.Block();

    REQUIRE(rig.log.size() == 6);
    CHECK(rig.log[1] == "o1:1");
    CHECK(rig.log[2] == "o0:8192"); // 64 << 7
    CHECK(rig.log[5] == "o0:8197"); // (64 << 7) | 5
  }

  TEST_CASE("rpnin: a new selection drops the coarse byte of the old one (#534)") {
    // A fine byte left over from the previous parameter is not this one's, and
    // pairing it would report a value that was never written.
    Rig rig(YSE::OBJ::M_RPNIN, kPortArg);

    rig.CC(0, kRpnMsb, 0);
    rig.CC(0, kRpnLsb, 0);
    rig.CC(0, kDataMsb, 127); // a big coarse value on parameter 0
    rig.CC(0, kRpnLsb, 1); // now select parameter 1 instead
    rig.CC(0, kDataLsb, 3); // a lone fine byte
    rig.Block();

    REQUIRE(rig.log.size() == 6);
    CHECK(rig.log[2] == "o0:16256"); // 127 << 7, on parameter 0
    CHECK(rig.log[4] == "o1:1"); // parameter 1
    CHECK(rig.log[5] == "o0:3"); // the stale 127 did not carry over
  }

  TEST_CASE("nrpnin: the non-registered pair selects and the registered one does not (#534)") {
    Rig rig(YSE::OBJ::M_NRPNIN, kPortArg);

    rig.Sequence(0, kNrpnMsb, kNrpnLsb, 1057, 300);
    REQUIRE(rig.log.size() == 6);
    CHECK(rig.log[1] == "o1:1057");
    CHECK(rig.log[5] == "o0:300");

    // The same gesture on the registered pair is not this object's.
    rig.log.clear();
    rig.Sequence(0, kRpnMsb, kRpnLsb, 2, 300);
    CHECK(rig.log.empty());
  }

  TEST_CASE("rpnin: an interleaved NRPN write is not reported as an RPN one (#534)") {
    // The reason both objects track the whole stream rather than only their own
    // controllers: RPN and NRPN move the *same* selection register on a real
    // device, so a `.rpnin` that ignored 99/98 would go on believing its
    // parameter was selected and would report the NRPN's value as its own.
    Rig rig(YSE::OBJ::M_RPNIN, kPortArg);

    rig.Sequence(0, kRpnMsb, kRpnLsb, 2, 1000);
    REQUIRE(rig.log.size() == 6);
    rig.log.clear();

    // Something else on the cable now selects a non-registered parameter and
    // writes to it. None of that belongs to this object.
    rig.Sequence(0, kNrpnMsb, kNrpnLsb, 40, 9000);
    CHECK(rig.log.empty());

    // And a bare Data Entry after it still belongs to the NRPN selection.
    rig.CC(0, kDataMsb, 100);
    rig.Block();
    CHECK(rig.log.empty());
  }

  TEST_CASE("rpnin: the RPN Null deselects, so an abandoned sequence leaks nothing (#534)") {
    // Parameter 16383 is the specification's "nothing is selected now", which
    // well-behaved controllers send after a write precisely so a stray Data
    // Entry cannot land somewhere unintended.
    Rig rig(YSE::OBJ::M_RPNIN, kPortArg);

    rig.Sequence(0, kRpnMsb, kRpnLsb, 0, 400);
    REQUIRE(rig.log.size() == 6);
    rig.log.clear();

    rig.CC(0, kRpnMsb, 127);
    rig.CC(0, kRpnLsb, 127); // the Null
    rig.CC(0, kDataMsb, 100); // and a stray value after it
    rig.CC(0, kDataLsb, 3);
    rig.Block();
    CHECK(rig.log.empty());
    CHECK(rig.object->GetName() == std::string(".rpnin"));
  }

  TEST_CASE("nrpnin: parameter 16383 is a real address, not a Null (#534)") {
    // The Null is defined for registered numbers only. Stealing a legal
    // non-registered address would silence a device that happened to use it.
    Rig rig(YSE::OBJ::M_NRPNIN, kPortArg);

    rig.Sequence(0, kNrpnMsb, kNrpnLsb, 16383, 500);
    REQUIRE(rig.log.size() == 6);
    CHECK(rig.log[1] == "o1:16383");
    CHECK(rig.log[5] == "o0:500");
  }

  TEST_CASE("rpnin: the selection is remembered per channel (#534)") {
    // Two devices on one cable select independently. A single shared selection
    // would report one device's value against the other's parameter number.
    Rig rig(YSE::OBJ::M_RPNIN, kPortArg);

    rig.CC(0, kRpnMsb, 0);
    rig.CC(0, kRpnLsb, 2); // channel 1 selects parameter 2
    rig.CC(4, kRpnMsb, 0);
    rig.CC(4, kRpnLsb, 4); // channel 5 selects parameter 4
    rig.CC(0, kDataMsb, 10);
    rig.CC(4, kDataMsb, 20);
    rig.Block();

    REQUIRE(rig.log.size() == 6);
    CHECK(rig.log[0] == "o2:1");
    CHECK(rig.log[1] == "o1:2");
    CHECK(rig.log[2] == "o0:1280");
    CHECK(rig.log[3] == "o2:5");
    CHECK(rig.log[4] == "o1:4");
    CHECK(rig.log[5] == "o0:2560");
  }

  TEST_CASE("rpnin: a Data Entry with nothing selected is reported by nobody (#534)") {
    Rig rig(YSE::OBJ::M_RPNIN, kPortArg);

    rig.CC(0, kDataMsb, 100);
    rig.CC(0, kDataLsb, 3);
    rig.Block();
    CHECK(rig.log.empty());
  }

  TEST_CASE("rpnin: unrelated controllers pass through untouched (#534)") {
    // A modulation wheel moving mid-sequence must not disturb the selection or
    // produce an emission of its own.
    Rig rig(YSE::OBJ::M_RPNIN, kPortArg);

    rig.CC(0, kRpnMsb, 0);
    rig.CC(0, 1, 64); // modulation wheel
    rig.CC(0, kRpnLsb, 2);
    rig.CC(0, 7, 100); // channel volume
    rig.CC(0, kDataMsb, 1);
    rig.Block();

    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[1] == "o1:2");
    CHECK(rig.log[2] == "o0:128");
  }

  // ─── the channel filter and the JSON round trip ───────────────────────────

  TEST_CASE("rpnin: port and channel survive a DumpJSON round trip (#534)") {
    // Read back through behaviour rather than an accessor: what has to survive
    // a save and a load is a patch that still listens to the same port and the
    // same channel — and, unlike Max, still has its channel outlet.
    Rig rig(YSE::OBJ::M_RPNIN, kPortArg + " 3", /*roundTrip=*/true);
    CHECK(rig.object->GetName() == std::string(".rpnin"));
    CHECK(std::string(rig.object->GetParams()) == kPortArg + " 3");
    CHECK(rig.object->GetOutputs() == 3);

    rig.Sequence(0, kRpnMsb, kRpnLsb, 2, 1000); // channel 1 — filtered out
    CHECK(rig.log.empty());

    rig.Sequence(2, kRpnMsb, kRpnLsb, 2, 1000); // channel 3
    REQUIRE(rig.log.size() == 6);
    CHECK(rig.log[3] == "o2:3");
    CHECK(rig.log[4] == "o1:2");
    CHECK(rig.log[5] == "o0:1000");
  }

#endif // YSE_ENABLE_MIDI_DEVICE

  // ─── end to end, in a real patcher ────────────────────────────────────────

  TEST_CASE("rpnout: a real patch writes a real parameter on a real channel (#534)") {
    // The issue's claim at the level a patch makes it: built through
    // `CreateObject` with a creation argument, wired with a real cord, and fed
    // the way a patch feeds it. What comes out is what `.midiout` would hand
    // the device.
    YSE::patcher patch;
    patch.create(2);

    YSE::pHandle* rpn = patch.CreateObject(YSE::OBJ::M_RPNOUT, "2");
    REQUIRE(rpn != nullptr);
    CHECK(rpn->GetInputs() == 2);
    CHECK(rpn->GetOutputs() == 1);

    ListLog log;
    YSE::pHandle logHandle(&log);
    patch.Connect(rpn, 0, &logHandle, 0);

    rpn->SetIntData(1, 0); // parameter 0 — pitch-bend sensitivity
    rpn->SetIntData(0, 1536); // two semitones, no cents: MSB 12, LSB 0

    REQUIRE(log.received.size() == 4);
    CHECK(log.received[0] == Bytes(0xB2, kRpnMsb, 0));
    CHECK(log.received[1] == Bytes(0xB2, kRpnLsb, 0));
    CHECK(log.received[2] == Bytes(0xB2, kDataMsb, 12));
    CHECK(log.received[3] == Bytes(0xB2, kDataLsb, 0));

    patch.DeleteObject(rpn);
  }

  TEST_CASE("nrpnout: the channel survives a DumpJSON / ParseJSON round trip (#534)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::M_NRPNOUT, "7") != nullptr);

    const std::string json = src.DumpJSON();
    CHECK(json.find(".nrpnout") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* obj = loaded.GetHandleFromList(0);
    REQUIRE(obj != nullptr);
    CHECK(obj->GetName() == std::string(".nrpnout"));
    CHECK(std::string(obj->GetParams()) == "7");

    ListLog log;
    YSE::pHandle logHandle(&log);
    loaded.Connect(obj, 0, &logHandle, 0);

    obj->SetIntData(1, 1057);
    obj->SetIntData(0, 300);
    REQUIRE(log.received.size() == 4);
    CHECK(log.received[0] == Bytes(0xB7, kNrpnMsb, 8));
    CHECK(log.received[1] == Bytes(0xB7, kNrpnLsb, 33));
    CHECK(log.received[2] == Bytes(0xB7, kDataMsb, 2));
    CHECK(log.received[3] == Bytes(0xB7, kDataLsb, 44));

    loaded.DeleteObject(obj);
  }

#if YSE_ENABLE_MIDI_DEVICE

  TEST_CASE("rpn: a .rpnout's own bytes are read back by a .rpnin (#534)") {
    // The loop closed. A real `.rpnout` in a real patch formats the sequence,
    // every byte of it goes onto the wire through the hub exactly as `.midiout`
    // would put it there, and a real `.rpnin` in another patch reassembles the
    // parameter and value the sender started from. Neither object knows about
    // the other, which is what makes this a test of the wire format rather than
    // of a shared assumption.
    Rig in(YSE::OBJ::M_RPNIN, kPortArg);

    YSE::patcher patch;
    patch.create(2);
    YSE::pHandle* out = patch.CreateObject(YSE::OBJ::M_RPNOUT, "0");
    REQUIRE(out != nullptr);

    ListLog log;
    YSE::pHandle logHandle(&log);
    patch.Connect(out, 0, &logHandle, 0);

    out->SetIntData(1, 1234);
    out->SetIntData(0, 9999);
    REQUIRE(log.received.size() == 4);

    for (const std::string& message : log.received) {
      REQUIRE(message.size() == 3);
      InHub().Deliver(kTestPort, reinterpret_cast<const unsigned char*>(message.data()), 3);
    }
    in.Block();

    REQUIRE(in.log.size() == 6);
    CHECK(in.log[3] == "o2:1");
    CHECK(in.log[4] == "o1:1234");
    CHECK(in.log[5] == "o0:9999");

    patch.DeleteObject(out);
  }

#endif // YSE_ENABLE_MIDI_DEVICE

} // TEST_SUITE
