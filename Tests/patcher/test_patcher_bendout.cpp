// Tests for `.bendout` (issue #532) — the MIDI pitch bend message generator.
//
// What is being pinned:
//
//   - **the bytes**: status 0xE0 on the argument's channel, the fine byte 0,
//     and the value as the coarse byte. That byte order is the whole object —
//     the wire sends fine before coarse, so a generator that put the value
//     first would bend by a semitone-ish nothing and look almost right;
//   - **the 7-bit reading**: 0-127 with 64 at rest, the resolution `.bendin`,
//     `.midiparse` and `.midiformat` already agree on, with the fine byte left
//     to `.xbendout` (#533);
//   - **clamping rather than refusing**, which is what the rest of the sender
//     family does with its data bytes, plus the channel clamp that keeps an
//     out-of-range argument from carrying into the status nibble and turning
//     the packet into a different message;
//   - **the shape it shares with its six siblings**: one hot inlet, one list
//     outlet carrying a raw three-byte string — the form `.midiout` reads —
//     no DSP edge and no block poll;
//   - **the channel argument surviving a DumpJSON / ParseJSON round trip**,
//     read back through the object's behaviour rather than an accessor.
//
// The end-to-end section drives a real `YSE::patcher`: the object built
// through the registry with a real creation argument, a real cord to a real
// inlet, fed by `SetIntData` the way a patch feeds it. That is the level at
// which this issue's claim ("a patch can now produce pitch bend") is either
// true or not — a unit test of the encoding would pass just as happily on an
// object nothing could create or wire.
//
// No audio device and no MIDI hardware required: the object opens nothing, it
// only emits bytes. It is nonetheless compiled behind `#if YSE_WINDOWS` with
// the six senders it belongs to — see mMidiBendOut.h, and issue #746 for the
// sweep that lifts that guard off the whole family.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <vector>

#include "headers/defines.hpp"

#if YSE_WINDOWS

#include "patcher/inlet.h"
#include "patcher/midi/mMidiBendOut.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObject.h"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/parameters.h"
#include "patcher/patcher.hpp"
#include "sinks.hpp"

using YSE::PATCHER::mMidiBendOut;
using YSE::PATCHER::Register;

namespace {

  // The three wire bytes as a std::string, so an expectation reads as the
  // message it is. Built from ints because two of the three bytes have their
  // top bit set or are zero, and a string literal cannot carry either legibly.
  std::string Bytes(int status, int fine, int coarse) {
    std::string s(3, '\0');
    s[0] = (char)status;
    s[1] = (char)fine;
    s[2] = (char)coarse;
    return s;
  }

  // A standalone `.bendout` with a ListSink on its outlet. The sink is declared
  // first so it outlives the object that sends to it — sinks.hpp's teardown
  // rule.
  struct BendRig {
    TestHelpers::ListSink sink;
    mMidiBendOut op;

    explicit BendRig(const std::string& params = "") {
      if (!params.empty()) op.SetParams(params);
      TestHelpers::Wire(op, 0, sink, 0);
    }
    BendRig(const BendRig&) = delete;
    BendRig& operator=(const BendRig&) = delete;

    void Send(int value) {
      op.GetInlet(0)->SetInt(value, YSE::T_GUI);
    }
  };

} // namespace

#endif // YSE_WINDOWS

TEST_SUITE("patcher") {

#if YSE_WINDOWS

  // ─── registration and shape ─────────────────────────────────────────────

  TEST_CASE("bendout: the object is registered and creatable (#532)") {
    auto names = Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::M_BENDOUT)) found = true;
    }
    CHECK(found);

    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::M_BENDOUT));
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == std::string(YSE::OBJ::M_BENDOUT));
  }

  TEST_CASE("bendout: one hot inlet, one list outlet, like its six siblings (#532)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::M_BENDOUT));
    REQUIRE(obj != nullptr);

    CHECK(obj->NumInputs() == 1);
    CHECK(obj->NumOutputs() == 1);
    // A message formatter: nothing renders it and nothing polls it, unlike the
    // MIDI *input* family of #529.
    CHECK_FALSE(obj->IsDSPObject());
    CHECK_FALSE(obj->WantsBlockPoll());
  }

  TEST_CASE("bendout: it documents itself (#532)") {
    // test_doc_coverage.cpp makes this claim over every registered object; it
    // is repeated here so a metadata regression names `.bendout` directly.
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::M_BENDOUT));
    REQUIRE(obj != nullptr);

    CHECK_FALSE(obj->GetDescription().empty());
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MIDI);
    CHECK_FALSE(obj->GetInlet(0)->GetDocLabel().empty());
    CHECK_FALSE(obj->GetOutlet(0)->GetDocLabel().empty());
    REQUIRE(obj->GetParamDocs().size() == 1);
    CHECK(obj->GetParamDocs()[0].name == "channel");
  }

  // ─── the message ────────────────────────────────────────────────────────

  TEST_CASE("bendout: a value emits status, fine byte 0, then the value (#532)") {
    // The byte order is the object. The wire sends the fine byte first, and
    // this is the 7-bit half of the pair, so it is 0 and the value rides in
    // the coarse byte where `.bendin` and `.midiparse` read it.
    BendRig rig;
    rig.Send(100);

    REQUIRE(rig.sink.gotList);
    CHECK(rig.sink.received == Bytes(0xE0, 0, 100));
  }

  TEST_CASE("bendout: 64 is the wheel at rest and 0 and 127 are its ends (#532)") {
    BendRig rig;

    rig.Send(64);
    CHECK(rig.sink.received == Bytes(0xE0, 0, 64));

    rig.Send(0);
    CHECK(rig.sink.received == Bytes(0xE0, 0, 0));

    rig.Send(127);
    CHECK(rig.sink.received == Bytes(0xE0, 0, 127));
  }

  TEST_CASE("bendout: every value fires, including the same one twice (#532)") {
    // The inlet is hot: it stores *and* sends. A patch sweeping a wheel sends
    // a stream of values, and one that repeated a value would drop a step if
    // the object only emitted on change.
    BendRig rig;
    int hits = 0;
    // Count through a fresh sink so the flag can be reset between sends.
    for (int i = 0; i < 3; i++) {
      rig.sink.gotList = false;
      rig.Send(70);
      if (rig.sink.gotList) hits++;
    }
    CHECK(hits == 3);
  }

  TEST_CASE("bendout: values outside 0-127 are clamped, not refused (#532)") {
    // The family's rule: a patch scaling a controller into a wider range bends
    // to the extremes instead of falling silent or emitting a byte with its
    // top bit set, which the receiving device would read as a status byte.
    BendRig rig;

    rig.Send(-40);
    CHECK(rig.sink.received == Bytes(0xE0, 0, 0));

    rig.Send(9000);
    CHECK(rig.sink.received == Bytes(0xE0, 0, 127));
  }

  // ─── the channel argument ───────────────────────────────────────────────

  TEST_CASE("bendout: the channel argument lands in the status nibble (#532)") {
    // 0-based, as on the other six senders.
    BendRig rig("5");
    rig.Send(64);
    CHECK(rig.sink.received == Bytes(0xE5, 0, 64));
  }

  TEST_CASE("bendout: a channel outside 0-15 cannot corrupt the status byte (#532)") {
    // 0xE0 + 16 is 0xF0 — a system-exclusive start, not a bend at all. A bend
    // arriving as some other message is worse than a bend on the wrong
    // channel, so the channel is clamped at send time.
    {
      BendRig rig("16");
      rig.Send(64);
      CHECK(rig.sink.received == Bytes(0xEF, 0, 64));
    }
    {
      BendRig rig("-3");
      rig.Send(64);
      CHECK(rig.sink.received == Bytes(0xE0, 0, 64));
    }
  }

  // ─── end to end, in a real patcher ──────────────────────────────────────

  TEST_CASE("bendout: a real patch bends a real channel (#532)") {
    // The issue's claim at the level a patch makes it: the object built
    // through `CreateObject` with a creation argument, wired with a real cord,
    // and fed the way a patch feeds it. The bytes that come out are what
    // `.midiout` would hand the device.
    YSE::patcher patch;
    patch.create(2);

    YSE::pHandle* bend = patch.CreateObject(YSE::OBJ::M_BENDOUT, "2");
    REQUIRE(bend != nullptr);
    CHECK(bend->GetInputs() == 1);
    CHECK(bend->GetOutputs() == 1);

    TestHelpers::ListSink sink;
    YSE::pHandle sinkHandle(&sink);
    patch.Connect(bend, 0, &sinkHandle, 0);

    bend->SetIntData(0, 96);
    REQUIRE(sink.gotList);
    CHECK(sink.received == Bytes(0xE2, 0, 96));

    patch.DeleteObject(bend);
  }

  TEST_CASE("bendout: the channel survives a DumpJSON / ParseJSON round trip (#532)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::M_BENDOUT, "7") != nullptr);

    const std::string json = src.DumpJSON();
    CHECK(json.find(".bendout") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* obj = loaded.GetHandleFromList(0);
    REQUIRE(obj != nullptr);
    CHECK(obj->GetName() == std::string(".bendout"));
    CHECK(std::string(obj->GetParams()) == "7");

    // Read the channel back through the object's behaviour rather than an
    // accessor: what has to survive a save and a load is a patch that still
    // bends the same channel.
    TestHelpers::ListSink sink;
    YSE::pHandle sinkHandle(&sink);
    loaded.Connect(obj, 0, &sinkHandle, 0);

    obj->SetIntData(0, 33);
    REQUIRE(sink.gotList);
    CHECK(sink.received == Bytes(0xE7, 0, 33));

    loaded.DeleteObject(obj);
  }

#endif // YSE_WINDOWS

} // TEST_SUITE
