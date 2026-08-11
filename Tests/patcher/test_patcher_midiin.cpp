// Tests for the MIDI input object family (issue #529) — `.midiin`, `.notein`,
// `.ctlin`, `.bendin`, `.pgmin`, `.touchin`, `.polyin` and `.rtin`, plus the
// `MIDI::inHub` transport they all share.
//
// What is being pinned:
//
//   - **the transport**: `MIDI::inHub` hands a message from the device thread
//     to one bounded lock-free queue per subscribed object — fan-out to every
//     listener on a port, isolation between ports, chunking of a message too
//     long for one event, a bounded queue that *drops and counts* rather than
//     growing, and a freed slot that does not hand its leftovers to the next
//     owner;
//   - **the plumbing**: an object with no inlets and no DSP edge still gets
//     `Calculate()` called once per block, which is what
//     `pObject::WantsBlockPoll()` and the GraphState's `pollers` list exist
//     for. Without it the whole family would compile, register, subscribe —
//     and never emit anything;
//   - **the decoding**: each member reads its own fields out of the wire
//     bytes, ignores every message that is not its own, reports channels as
//     1-16 rather than the 0-15 nibble, and fires its outlets right to left;
//   - **the deviations that were chosen rather than inherited**: a note-off
//     reported as velocity 0 (Max), a program change reported as 1-128 (Max),
//     `.bendin` reporting only the coarse byte (Max), and the channel outlet
//     that stays put when a channel argument is given (deliberately *not*
//     Max — see mMidiIn.h);
//   - **the filters**: channel on six members, controller number on `.ctlin`,
//     with the out-of-range spellings falling back to "everything" rather than
//     silencing the object;
//   - **the lifecycle**: a standalone object holds no subscription, joining a
//     patcher takes one, and destroying it gives it back.
//
// The end-to-end section drives a real `patcherImplementation`: objects built
// through the registry, wired with real cords, fed by injecting into the real
// hub, and drained by real `Calculate()` blocks. That is the level at which
// this issue's claim ("the patcher can now receive MIDI") is either true or
// not — a unit test of the decode would pass just as happily on a family
// nothing ever called.
//
// No audio device and no MIDI hardware required: `inHub::Deliver` is the same
// entry point the RtMidi callback uses, so the tests drive the production path
// rather than a parallel one. The tests use port 7 — the last index the hub
// accepts — because a machine with eight MIDI inputs in use is rare enough
// that a real device cannot inject stray traffic into the assertions.

#include <doctest/doctest.h>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "headers/defines.hpp"

#if YSE_ENABLE_MIDI_DEVICE

#include "midi/midiInHub.h"
#include "patcher/graphState.h"
#include "patcher/inlet.h"
#include "patcher/midi/mMidiIn.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"

using YSE::MIDI::inEvent;
using YSE::MIDI::inHub;
using YSE::MIDI::InHub;
using YSE::PATCHER::patcherImplementation;
using YSE::PATCHER::Register;

namespace {

  // The port every test in this file listens to. See the file header for why
  // it is the last one rather than the first.
  constexpr unsigned int kTestPort = 7;

  // Records every int that arrived, tagged with which outlet delivered it, into
  // a log shared by all the taps of one rig. That is what makes "channel first,
  // then velocity, then pitch" an assertion rather than an inference — a sink
  // that only kept the last value per outlet could not tell the order.
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
      return "midiin_tap";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A registry-built MIDI-input object in a real patcher, with one Tap on each
  // of its outlets writing into one shared log.
  //
  // `taps` is a deque-like vector of unique_ptr rather than a vector of Tap so
  // that growing it cannot move a Tap an inlet already points at.
  struct Rig {
    patcherImplementation patch{1, nullptr};
    std::vector<std::string> log;
    std::vector<std::unique_ptr<Tap>> taps;
    std::vector<std::unique_ptr<YSE::pHandle>> tapHandles;
    YSE::pHandle* object = nullptr;

    Rig(const char* type, const std::string& args) {
      object = patch.CreateObject(type, args);
      REQUIRE(object != nullptr);
      log.reserve(64);
      for (int i = 0; i < object->GetOutputs(); i++) {
        auto tap = std::make_unique<Tap>();
        tap->log = &log;
        // "o0", "o1", ... — the outlet the value came out of.
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

  // A bare hub subscription with no patcher behind it, for the transport tests.
  // Releases itself, so a failing REQUIRE cannot leak a slot into the next test
  // — the hub is a process-wide singleton and its slots are finite.
  struct Sub {
    inHub::Handle handle = inHub::kNoHandle;

    explicit Sub(unsigned int port) : handle(InHub().Subscribe(port)) {}
    ~Sub() {
      InHub().Unsubscribe(handle);
    }
    Sub(const Sub&) = delete;
    Sub& operator=(const Sub&) = delete;

    // How many events are waiting, drained out in the process.
    int Drain(std::vector<inEvent>* into = nullptr) {
      int count = 0;
      inEvent e;
      while (InHub().TryPop(handle, e)) {
        if (into) into->push_back(e);
        count++;
      }
      return count;
    }
  };

  void Deliver(unsigned int port, std::initializer_list<int> bytes) {
    std::vector<unsigned char> raw;
    raw.reserve(bytes.size());
    for (int b : bytes)
      raw.push_back(static_cast<unsigned char>(b));
    InHub().Deliver(port, raw.data(), raw.size());
  }

  // Every name the family registers, in the order the issue lists them.
  const char* const kFamily[] = {
      YSE::OBJ::M_IN,    YSE::OBJ::M_NOTEIN,  YSE::OBJ::M_CTLIN,  YSE::OBJ::M_BENDIN,
      YSE::OBJ::M_PGMIN, YSE::OBJ::M_TOUCHIN, YSE::OBJ::M_POLYIN, YSE::OBJ::M_RTIN,
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── registration and shape ───────────────────────────────────────────────

  TEST_CASE("midi input: all eight objects are registered and creatable (#529)") {
    auto names = Register().AllNames();
    for (const char* type : kFamily) {
      CAPTURE(type);
      bool found = false;
      for (const auto& name : names) {
        if (name == std::string(type)) found = true;
      }
      CHECK(found);

      std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK(std::string(obj->Type()) == std::string(type));
    }
  }

  TEST_CASE("midi input: the family has no inlets — its input is the wire (#529)") {
    // The reason `WantsBlockPoll()` had to exist at all: with no inlet there is
    // nothing for the graph traversal to push these objects through, and they
    // are not DSP objects either, so without the poll list nothing would ever
    // call Calculate() on them.
    for (const char* type : kFamily) {
      CAPTURE(type);
      std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK(obj->NumInputs() == 0);
      CHECK(obj->NumOutputs() > 0);
      CHECK(obj->WantsBlockPoll());
      // Control-rate, emphatically: reporting these as DSP objects would put a
      // wrong answer into every palette and binding that reads the metadata.
      CHECK_FALSE(obj->IsDSPObject());
    }
  }

  TEST_CASE("midi input: each member declares the outlets its decoding needs (#529)") {
    struct Expected {
      const char* type;
      int outlets;
    };
    const Expected expected[] = {
        {YSE::OBJ::M_IN, 1}, // raw bytes
        {YSE::OBJ::M_RTIN, 1}, // status byte
        {YSE::OBJ::M_NOTEIN, 3}, // pitch, velocity, channel
        {YSE::OBJ::M_CTLIN, 3}, // value, controller, channel
        {YSE::OBJ::M_POLYIN, 3}, // pitch, pressure, channel
        {YSE::OBJ::M_BENDIN, 2}, // bend, channel
        {YSE::OBJ::M_PGMIN, 2}, // program, channel
        {YSE::OBJ::M_TOUCHIN, 2}, // pressure, channel
    };
    for (const Expected& e : expected) {
      CAPTURE(e.type);
      std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(e.type));
      REQUIRE(obj != nullptr);
      CHECK(obj->NumOutputs() == e.outlets);
    }
  }

  // ─── the transport ────────────────────────────────────────────────────────

  TEST_CASE("midi inHub: a delivered message comes back out of the subscription (#529)") {
    Sub sub(kTestPort);
    REQUIRE(sub.handle != inHub::kNoHandle);

    Deliver(kTestPort, {0x90, 60, 100});

    std::vector<inEvent> got;
    REQUIRE(sub.Drain(&got) == 1);
    CHECK(got[0].len == 3);
    CHECK(got[0].bytes[0] == 0x90);
    CHECK(got[0].bytes[1] == 60);
    CHECK(got[0].bytes[2] == 100);
  }

  TEST_CASE("midi inHub: every subscription on a port gets its own copy (#529)") {
    // The normal case rather than a corner: a `.notein` and a `.ctlin` on one
    // keyboard. Each has a queue of its own, so a slow consumer cannot starve
    // the other.
    Sub a(kTestPort);
    Sub b(kTestPort);
    REQUIRE(a.handle != inHub::kNoHandle);
    REQUIRE(b.handle != inHub::kNoHandle);
    CHECK(a.handle != b.handle);

    Deliver(kTestPort, {0xB0, 7, 64});

    CHECK(a.Drain() == 1);
    CHECK(b.Drain() == 1);
  }

  TEST_CASE("midi inHub: a subscription hears only its own port (#529)") {
    Sub seven(7);
    Sub six(6);
    REQUIRE(seven.handle != inHub::kNoHandle);
    REQUIRE(six.handle != inHub::kNoHandle);

    Deliver(7, {0x90, 60, 100});

    CHECK(seven.Drain() == 1);
    CHECK(six.Drain() == 0);
  }

  TEST_CASE("midi inHub: a message too long for one event arrives as ordered chunks (#529)") {
    // A SysEx dump is the real case. The transport must not truncate it and
    // must not reorder it — `.sysexin` (issue #531) is going to read it as a
    // byte stream, and a stream that arrives out of order is not one.
    Sub sub(kTestPort);
    REQUIRE(sub.handle != inHub::kNoHandle);

    std::vector<unsigned char> dump;
    dump.push_back(0xF0);
    for (unsigned char i = 1; i < 19; i++)
      dump.push_back(i);
    dump.push_back(0xF7);
    REQUIRE(dump.size() == 20);
    InHub().Deliver(kTestPort, dump.data(), dump.size());

    std::vector<inEvent> got;
    // 20 bytes over 8-byte events: 8 + 8 + 4.
    REQUIRE(sub.Drain(&got) == 3);
    CHECK(got[0].len == inEvent::kMaxBytes);
    CHECK(got[1].len == inEvent::kMaxBytes);
    CHECK(got[2].len == 4);

    // Reassembled, it is byte for byte what went in.
    std::vector<unsigned char> rebuilt;
    for (const inEvent& e : got) {
      for (std::size_t i = 0; i < e.len; i++)
        rebuilt.push_back(e.bytes[i]);
    }
    CHECK(rebuilt == dump);
  }

  TEST_CASE("midi inHub: a port past the hub's limit is refused rather than silently ignored"
            " (#529)") {
    inHub::Handle refused = InHub().Subscribe(inHub::kMaxPorts);
    CHECK(refused == inHub::kNoHandle);
    // A handle that names nothing must be inert on every entry point rather
    // than indexing something.
    inEvent e;
    CHECK_FALSE(InHub().TryPop(refused, e));
    CHECK(InHub().Dropped(refused) == 0);
    InHub().Unsubscribe(refused); // must not crash
  }

  TEST_CASE("midi inHub: a port's listener slots are finite and are given back (#529)") {
    std::vector<std::unique_ptr<Sub>> subs;
    for (unsigned int i = 0; i < inHub::kMaxSubscriptionsPerPort; i++) {
      subs.push_back(std::make_unique<Sub>(kTestPort));
      CHECK(subs.back()->handle != inHub::kNoHandle);
    }
    // One past the ceiling: refused, and logged rather than growing a table the
    // device thread walks without a lock.
    CHECK(InHub().Subscribe(kTestPort) == inHub::kNoHandle);

    // Give one back and the next caller gets it.
    subs.pop_back();
    Sub reused(kTestPort);
    CHECK(reused.handle != inHub::kNoHandle);
  }

  TEST_CASE("midi inHub: a full queue drops and counts rather than allocating (#529)") {
    // The bound is the whole point: `try_push`, never `push`. The allocating
    // form would malloc on the device thread and grow a queue whose reason for
    // existing is that it cannot.
    Sub sub(kTestPort);
    REQUIRE(sub.handle != inHub::kNoHandle);
    CHECK(InHub().Dropped(sub.handle) == 0);

    const std::size_t overshoot = 5;
    for (std::size_t i = 0; i < inHub::kQueueCapacity + overshoot; i++) {
      Deliver(kTestPort, {0x90, 60, 100});
    }

    CHECK(InHub().Dropped(sub.handle) == overshoot);
    // Exactly the queue's worth survived — nothing beyond it, and nothing lost
    // from within it.
    CHECK(sub.Drain() == static_cast<int>(inHub::kQueueCapacity));
  }

  TEST_CASE("midi inHub: a fresh subscription does not inherit the last owner's queue (#529)") {
    {
      Sub first(kTestPort);
      REQUIRE(first.handle != inHub::kNoHandle);
      Deliver(kTestPort, {0x90, 60, 100});
      Deliver(kTestPort, {0x90, 62, 100});
      // Deliberately not drained: the slot is given back with events still in
      // it, which is exactly what happens when an object is deleted.
    }

    Sub second(kTestPort);
    REQUIRE(second.handle != inHub::kNoHandle);
    CHECK(second.Drain() == 0);
    CHECK(InHub().Dropped(second.handle) == 0);
  }

  // ─── the plumbing: an object with no inlets still runs ────────────────────

  TEST_CASE("midi input: a MIDI-in object is polled every block, having no other way to run"
            " (#529)") {
    Rig rig(YSE::OBJ::M_NOTEIN, "7");
    REQUIRE(rig.object->GetOutputs() == 3);

    // Nothing on the wire, nothing out: the poll is not a bang.
    rig.Block();
    CHECK(rig.log.empty());

    rig.WireAndBlock({0x90, 60, 100});
    CHECK_FALSE(rig.log.empty());

    // And the event is consumed rather than re-emitted every block.
    const std::size_t after = rig.log.size();
    rig.Block();
    rig.Block();
    CHECK(rig.log.size() == after);
  }

  TEST_CASE("midi input: an event that arrives between blocks lands in the next one (#529)") {
    Rig rig(YSE::OBJ::M_NOTEIN, "7");

    // Three notes queued while the audio thread was elsewhere. All three come
    // out in the following block, in arrival order.
    rig.Wire({0x90, 60, 100});
    rig.Wire({0x90, 62, 101});
    rig.Wire({0x90, 64, 102});
    CHECK(rig.log.empty());

    rig.Block();
    REQUIRE(rig.log.size() == 9); // three messages of three outlets each
    CHECK(rig.log[2] == "o0:60");
    CHECK(rig.log[5] == "o0:62");
    CHECK(rig.log[8] == "o0:64");
  }

  TEST_CASE("midi input: the poll list is rebuilt with the graph (#529)") {
    patcherImplementation patch(1, nullptr);
    // A patcher with nothing in it polls nothing.
    patch.Calculate(YSE::T_DSP);

    YSE::pHandle* obj = patch.CreateObject(YSE::OBJ::M_NOTEIN, "7");
    REQUIRE(obj != nullptr);
    patch.Calculate(YSE::T_DSP);

    // Deleting it takes it back out — and, with it, the hub subscription, which
    // is what stops a deleted object from holding a device port open.
    patch.DeleteObject(obj);
    patch.Calculate(YSE::T_DSP);
    CHECK(patch.Objects() == 0);
  }

  // ─── .notein ──────────────────────────────────────────────────────────────

  TEST_CASE("notein: a note-on decodes to pitch, velocity and channel, right to left (#529)") {
    Rig rig(YSE::OBJ::M_NOTEIN, "7");
    // Note-on, wire channel nibble 5 -> reported channel 6.
    rig.WireAndBlock({0x95, 60, 100});

    REQUIRE(rig.log.size() == 3);
    // Right to left, Max's order: whatever the pitch triggers downstream
    // already sees the velocity and channel that came with it.
    CHECK(rig.log[0] == "o2:6");
    CHECK(rig.log[1] == "o1:100");
    CHECK(rig.log[2] == "o0:60");
  }

  TEST_CASE("notein: both spellings of a release report velocity 0 (#529)") {
    // Max's rule, and the reason it matters: hardware sends releases as a
    // note-off *or* as a note-on with velocity 0, and a patch should not have
    // to test for both.
    Rig rig(YSE::OBJ::M_NOTEIN, "7");

    rig.WireAndBlock({0x80, 60, 64}); // note-off with a release velocity
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[1] == "o1:0");
    CHECK(rig.log[2] == "o0:60");

    rig.log.clear();
    rig.WireAndBlock({0x90, 60, 0}); // note-on, velocity 0
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[1] == "o1:0");
    CHECK(rig.log[2] == "o0:60");
  }

  TEST_CASE("notein: messages that are not notes are ignored (#529)") {
    Rig rig(YSE::OBJ::M_NOTEIN, "7");
    rig.Wire({0xB0, 7, 64}); // control change
    rig.Wire({0xE0, 0, 96}); // pitch bend
    rig.Wire({0xF8}); // timing clock
    rig.Block();
    CHECK(rig.log.empty());
  }

  TEST_CASE("notein: the channel argument filters, and keeps its outlet (#529)") {
    // Deliberately not Max, which drops the channel outlet here. See mMidiIn.h:
    // an object whose outlet count depends on its arguments has no stable
    // outlet numbering, so a patch that gained an argument would silently
    // re-point every cord leaving the box.
    Rig rig(YSE::OBJ::M_NOTEIN, "7 3");
    CHECK(rig.object->GetOutputs() == 3);

    rig.WireAndBlock({0x90, 60, 100}); // channel 1 — rejected
    CHECK(rig.log.empty());

    rig.WireAndBlock({0x92, 64, 111}); // channel 3 — accepted
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "o2:3");
    CHECK(rig.log[2] == "o0:64");
  }

  TEST_CASE("notein: an out-of-range channel argument means omni, not silence (#529)") {
    // A stray number silencing the object for good would be the worst possible
    // reading of it: the patch would look wired and do nothing.
    Rig rig(YSE::OBJ::M_NOTEIN, "7 99");
    rig.WireAndBlock({0x90, 60, 100});
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "o2:1");
  }

  // ─── .ctlin ───────────────────────────────────────────────────────────────

  TEST_CASE("ctlin: value, controller number and channel, right to left (#529)") {
    Rig rig(YSE::OBJ::M_CTLIN, "7");
    rig.WireAndBlock({0xB1, 7, 64}); // channel 2, controller 7, value 64

    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "o2:2");
    CHECK(rig.log[1] == "o1:7");
    CHECK(rig.log[2] == "o0:64");
  }

  TEST_CASE("ctlin: the controller argument filters, and defaults to every controller (#529)") {
    Rig any(YSE::OBJ::M_CTLIN, "7");
    any.WireAndBlock({0xB0, 0, 12}); // controller 0 — Bank Select MSB
    REQUIRE(any.log.size() == 3);
    CHECK(any.log[1] == "o1:0");

    // -1 is the wildcard rather than 0, precisely because controller 0 is a
    // real controller a patch may want on its own.
    Rig only(YSE::OBJ::M_CTLIN, "7 0 0");
    only.WireAndBlock({0xB0, 7, 64}); // controller 7 — rejected
    CHECK(only.log.empty());
    only.WireAndBlock({0xB0, 0, 12}); // controller 0 — accepted
    REQUIRE(only.log.size() == 3);
    CHECK(only.log[1] == "o1:0");
    CHECK(only.log[2] == "o0:12");
  }

  TEST_CASE("ctlin: an out-of-range controller argument means every controller (#529)") {
    Rig rig(YSE::OBJ::M_CTLIN, "7 0 200");
    rig.WireAndBlock({0xB0, 7, 64});
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[1] == "o1:7");
  }

  // ─── .bendin / .pgmin / .touchin / .polyin ────────────────────────────────

  TEST_CASE("bendin: reports the coarse byte, centred at 64 (#529)") {
    // Max's 7-bit reading of a 14-bit message, and the reason `.xbendin`
    // (issue #533) is a separate object rather than a mode of this one.
    Rig rig(YSE::OBJ::M_BENDIN, "7");
    // Wire order is LSB then MSB; centre is LSB 0, MSB 64.
    rig.WireAndBlock({0xE0, 0x00, 64});
    REQUIRE(rig.log.size() == 2);
    CHECK(rig.log[0] == "o1:1");
    CHECK(rig.log[1] == "o0:64");

    rig.log.clear();
    // Full up: LSB 127, MSB 127. The fine byte is deliberately not read.
    rig.WireAndBlock({0xE0, 0x7F, 127});
    REQUIRE(rig.log.size() == 2);
    CHECK(rig.log[1] == "o0:127");
  }

  TEST_CASE("pgmin: reports 1-128, the numbering the hardware displays (#529)") {
    Rig rig(YSE::OBJ::M_PGMIN, "7");
    rig.WireAndBlock({0xC3, 0}); // channel 4, wire program 0
    REQUIRE(rig.log.size() == 2);
    CHECK(rig.log[0] == "o1:4");
    CHECK(rig.log[1] == "o0:1");

    rig.log.clear();
    rig.WireAndBlock({0xC0, 127});
    REQUIRE(rig.log.size() == 2);
    CHECK(rig.log[1] == "o0:128");
  }

  TEST_CASE("touchin: reports one pressure for the whole channel (#529)") {
    Rig rig(YSE::OBJ::M_TOUCHIN, "7");
    rig.WireAndBlock({0xD5, 90});
    REQUIRE(rig.log.size() == 2);
    CHECK(rig.log[0] == "o1:6");
    CHECK(rig.log[1] == "o0:90");
  }

  TEST_CASE("polyin: reports pitch and pressure per held key (#529)") {
    Rig rig(YSE::OBJ::M_POLYIN, "7");
    rig.WireAndBlock({0xA2, 64, 77});
    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "o2:3");
    CHECK(rig.log[1] == "o1:77");
    CHECK(rig.log[2] == "o0:64");
  }

  // ─── .midiin and .rtin ────────────────────────────────────────────────────

  TEST_CASE("midiin: every byte leaves the outlet, in order and uninterpreted (#529)") {
    Rig rig(YSE::OBJ::M_IN, "7");
    rig.WireAndBlock({0x90, 60, 100});

    REQUIRE(rig.log.size() == 3);
    // The status byte keeps its wire value — 144, not "channel 1". Decoding is
    // the other seven objects' job.
    CHECK(rig.log[0] == "o0:144");
    CHECK(rig.log[1] == "o0:60");
    CHECK(rig.log[2] == "o0:100");
  }

  TEST_CASE("midiin: a long message comes out whole across its chunks (#529)") {
    Rig rig(YSE::OBJ::M_IN, "7");
    std::vector<unsigned char> dump;
    dump.push_back(0xF0);
    for (unsigned char i = 1; i < 11; i++)
      dump.push_back(i);
    dump.push_back(0xF7);
    InHub().Deliver(kTestPort, dump.data(), dump.size());
    rig.Block();

    REQUIRE(rig.log.size() == dump.size());
    CHECK(rig.log.front() == "o0:240");
    CHECK(rig.log.back() == "o0:247");
  }

  TEST_CASE("rtin: only the real-time bytes, and nothing else (#529)") {
    Rig rig(YSE::OBJ::M_RTIN, "7");

    rig.Wire({0xF8}); // timing clock
    rig.Wire({0x90, 60, 100}); // a note — not this object's business
    rig.Wire({0xFA}); // start
    rig.Wire({0xFC}); // stop
    rig.Block();

    REQUIRE(rig.log.size() == 3);
    CHECK(rig.log[0] == "o0:248");
    CHECK(rig.log[1] == "o0:250");
    CHECK(rig.log[2] == "o0:252");
  }

  // ─── lifecycle ────────────────────────────────────────────────────────────

  TEST_CASE("midi input: a standalone object holds no subscription (#529)") {
    // It would open a device nothing is ever going to drain: nothing calls
    // Calculate() on an object outside a patcher.
    YSE::PATCHER::mNoteIn obj;
    CHECK(obj.Subscription() == inHub::kNoHandle);
    CHECK(obj.Port() == 0);

    // And draining it is inert rather than a null dereference.
    obj.Calculate(YSE::T_GUI);
    CHECK(obj.Subscription() == inHub::kNoHandle);
  }

  TEST_CASE("midi input: joining a patcher takes a subscription, leaving gives it back (#529)") {
    // Driven on the object directly rather than through a pHandle, because the
    // handle deliberately does not expose the object — and because the point
    // being made is about SetParent, which is the exact call
    // CreateObjectUnlocked makes and the one moment a device may be opened.
    patcherImplementation patch(1, nullptr);
    {
      YSE::PATCHER::mNoteIn obj;
      obj.SetParams("7");
      // Not yet joined: no port is opened for an object nothing will drain.
      CHECK(obj.Subscription() == inHub::kNoHandle);

      obj.SetParent(&patch);
      CHECK(obj.Subscription() != inHub::kNoHandle);
      CHECK(obj.Port() == kTestPort);

      // Re-parenting re-takes it rather than leaking the old one.
      const inHub::Handle first = obj.Subscription();
      obj.SetParent(&patch);
      CHECK(obj.Subscription() != inHub::kNoHandle);
      CHECK(obj.Subscription() == first); // the slot it just gave back

      // And leaving a patcher gives it up entirely.
      obj.SetParent(nullptr);
      CHECK(obj.Subscription() == inHub::kNoHandle);
    }

    // The slot is free again: a fresh subscription on that port gets one, which
    // it could not if the destroyed object still held all of them.
    Sub after(kTestPort);
    CHECK(after.handle != inHub::kNoHandle);
  }

  TEST_CASE("midi input: the port argument decides which device is listened to (#529)") {
    Rig seven(YSE::OBJ::M_NOTEIN, "7");
    // A note on port 6 must not reach an object listening to port 7.
    Deliver(6, {0x90, 60, 100});
    seven.Block();
    CHECK(seven.log.empty());

    seven.WireAndBlock({0x90, 60, 100});
    CHECK(seven.log.size() == 3);
  }

  TEST_CASE("midi input: the default port is 0 (#529)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::M_NOTEIN));
    REQUIRE(obj != nullptr);
    auto* midi = static_cast<YSE::PATCHER::mMidiInBase*>(obj.get());
    CHECK(midi->Port() == 0);
  }

  // ─── serialisation ────────────────────────────────────────────────────────

  TEST_CASE("midi input: the arguments survive a DumpJSON / ParseJSON round trip (#529)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::M_CTLIN, "7 3 74") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".ctlin") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".ctlin");
    CHECK(std::string(copy->GetParams()) == "7 3 74");
    CHECK(copy->GetOutputs() == 3);
  }

  TEST_CASE("midi input: every member round trips its own arguments (#529)") {
    for (const char* type : kFamily) {
      CAPTURE(type);
      YSE::patcher src;
      src.create(2);
      REQUIRE(src.CreateObject(type, "7") != nullptr);
      const std::string json = src.DumpJSON();

      YSE::patcher loaded;
      loaded.create(2);
      loaded.ParseJSON(json);
      REQUIRE(loaded.Objects() == 1);
      YSE::pHandle* copy = loaded.GetHandleFromList(0);
      REQUIRE(copy != nullptr);
      CHECK(std::string(copy->Type()) == std::string(type));
      CHECK(std::string(copy->GetParams()) == "7");
    }
  }

} // TEST_SUITE

#endif // YSE_ENABLE_MIDI_DEVICE
