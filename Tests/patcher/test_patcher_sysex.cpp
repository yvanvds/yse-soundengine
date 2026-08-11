// Tests for the system-exclusive pair (issue #531) — `.sysexin` and
// `.sxformat`.
//
// What is being pinned:
//
//   - **the template language**: constant bytes, `$i1`-`$i9` placeholders and
//     the inlet count they imply, `sumstart` / `sum`, and the two rules a
//     substituted value follows — negative omits its byte (Max's
//     variable-length messages), above 127 is clamped rather than passed on;
//   - **the checksum**: the Roland byte that makes its region a multiple of
//     128, with and without an explicit region marker, against a hand-computed
//     message;
//   - **the bound**: a template past the maximum is cut *and reported*, which
//     is the acceptance criterion the issue spells out — a dump wrong by its
//     tail must not be discoverable only by listening;
//   - **the filtering**: `.sysexin` passing every byte of a dump and nothing
//     else, across the transport chunks a long dump arrives in, with a clock
//     byte inside it dropped rather than passed on or read as an ending, and a
//     status byte ending an unterminated dump;
//   - **the platform split**: `.sxformat` registered everywhere and `.sysexin`
//     only where there is a port to open.
//
// The end-to-end section drives a real `patcherImplementation`: objects built
// through the registry, wired with real cords, `.sxformat` feeding a real
// `.midiparse` so that "what this object builds is a MIDI message" is checked
// by the object that reads MIDI messages, and `.sysexin` fed by injecting into
// the real `MIDI::inHub` and drained by real `Calculate()` blocks. That is the
// level at which "a patch can now receive and build SysEx" is either true or
// not; a unit test of the builder would pass just as happily on an object
// nothing could wire.
//
// No audio device and no MIDI hardware required. The `.sysexin` half needs the
// MIDI backend and is compiled out with it; the `.sxformat` half is not.

#include <doctest/doctest.h>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "headers/defines.hpp"
#include "patcher/midi/mSysEx.h"
#include "patcher/pHandle.hpp"
#include "patcher/pListArgs.h"
#include "patcher/pObject.h"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"

#if YSE_ENABLE_MIDI_DEVICE
#include "midi/midiInHub.h"
#include "patcher/patcherImplementation.h"
#endif

using YSE::PATCHER::mSxFormat;
using YSE::PATCHER::Register;

namespace {

  // Records every message that arrived, tagged with the outlet it came out of.
  // Order matters as much as content — a dump is a sequence — so a sink that
  // only kept the last value could not test it.
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
      return "sysex_tap";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A standalone `.sxformat` with its template already set and one Tap on the
  // outlet. The template goes on *before* the outlet is wired, mirroring the
  // patcher's own order — ReplaceObjectUnlocked parses params on an object that
  // is not yet published — and it has to, because the inlets are built by the
  // parse.
  struct Rig {
    mSxFormat op;
    Tap tap;
    std::vector<std::string> log;

    explicit Rig(const std::string& tmpl) {
      log.reserve(64);
      op.SetParams(tmpl);
      tap.log = &log;
      tap.tag = "out";
      REQUIRE(tap.ConnectInlet(op.GetOutlet(0), 0));
      op.ConnectOutlet(tap.GetInlet(0), 0);
    }
    ~Rig() {
      op.UnwireFromPeers();
    }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    // Cold inlets only store; use before Fire().
    void Set(int inlet, int value) {
      op.GetInlet(inlet)->SetInt(value, YSE::T_GUI);
    }
    // The hot inlet: stores and sends.
    void Fire(int value) {
      log.clear();
      op.GetInlet(0)->SetInt(value, YSE::T_GUI);
    }
    void Bang() {
      log.clear();
      op.GetInlet(0)->SetBang(YSE::T_GUI);
    }
    void FireList(const std::string& list) {
      log.clear();
      op.GetInlet(0)->SetList(list, YSE::T_GUI);
    }
    // The one message this send produced, without its "out:" tag.
    std::string Message() const {
      REQUIRE(log.size() == 1);
      return log[0].substr(4);
    }
  };

  // The bytes of a message as a space-separated decimal list — the shape
  // `.sxformat` sends and the shape a test can write out by hand.
  std::string Bytes(std::initializer_list<int> values) {
    std::string out;
    for (int v : values) {
      if (!out.empty()) out += ' ';
      out += std::to_string(v);
    }
    return out;
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── registration and shape ───────────────────────────────────────────────

  TEST_CASE("sysex: .sxformat is registered on every platform (#531)") {
    // Unguarded on purpose: it opens no device, so a patch that builds a dump
    // into a file must work where there is no MIDI hardware at all.
    auto names = Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::M_SXFORMAT)) found = true;
    }
    CHECK(found);

    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::M_SXFORMAT));
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == std::string(YSE::OBJ::M_SXFORMAT));
    // One inlet with no template, and one outlet. Control-rate: nothing polls
    // it and nothing renders it — a message on the hot inlet is what runs it.
    CHECK(obj->NumInputs() == 1);
    CHECK(obj->NumOutputs() == 1);
    CHECK_FALSE(obj->IsDSPObject());
    CHECK_FALSE(obj->WantsBlockPoll());
  }

  TEST_CASE("sysex: .sysexin is registered exactly where a port exists (#531)") {
    auto names = Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::M_SYSEXIN)) found = true;
    }
#if YSE_ENABLE_MIDI_DEVICE
    CHECK(found);
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::M_SYSEXIN));
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == std::string(YSE::OBJ::M_SYSEXIN));
    // The input family's shape: no inlets at all — its input is the wire — and
    // therefore the block poll, which is the only thing that runs it.
    CHECK(obj->NumInputs() == 0);
    CHECK(obj->NumOutputs() == 1);
    CHECK(obj->WantsBlockPoll());
    CHECK_FALSE(obj->IsDSPObject());
#else
    CHECK_FALSE(found);
#endif
  }

  // ─── .sxformat: the template ──────────────────────────────────────────────

  TEST_CASE("sxformat: a template of constants sends those bytes (#531)") {
    Rig rig("240 67 16 0 247");
    rig.Bang();
    CHECK(rig.Message() == Bytes({240, 67, 16, 0, 247}));
    CHECK(rig.op.TemplateSize() == 5);
    CHECK(rig.op.CompileError().empty());
  }

  TEST_CASE("sxformat: an object with no template sends nothing (#531)") {
    Rig rig("");
    CHECK(rig.op.TemplateSize() == 0);
    rig.Bang();
    CHECK(rig.log.empty());
  }

  TEST_CASE("sxformat: a $i placeholder takes the value of its inlet (#531)") {
    Rig rig("240 67 $i1 247");
    rig.Fire(9);
    CHECK(rig.Message() == Bytes({240, 67, 9, 247}));
    // And again with the stored value — a bang re-sends what the inlets hold.
    rig.Bang();
    CHECK(rig.Message() == Bytes({240, 67, 9, 247}));
  }

  TEST_CASE("sxformat: the object has one inlet per placeholder, and no more (#531)") {
    CHECK(Rig("240 247").op.NumInputs() == 1);
    CHECK(Rig("240 $i1 247").op.NumInputs() == 1);
    CHECK(Rig("240 $i3 247").op.NumInputs() == 3);
    CHECK(Rig("240 $i2 $i9 247").op.NumInputs() == 9);
    // An unused index still gets its inlet, so the ones to its right keep their
    // numbering — the same rule `.expr` follows.
    CHECK(Rig("240 $i4 247").op.NumInputs() == 4);
  }

  TEST_CASE("sxformat: only the leftmost inlet sends; the others store (#531)") {
    Rig rig("240 $i1 $i2 $i3 247");
    rig.log.clear();
    rig.Set(1, 11);
    rig.Set(2, 22);
    CHECK(rig.log.empty()); // cold: stored, nothing sent

    rig.Fire(1);
    CHECK(rig.Message() == Bytes({240, 1, 11, 22, 247}));
  }

  TEST_CASE("sxformat: a list on the hot inlet fills the placeholders left to right (#531)") {
    Rig rig("240 $i1 $i2 $i3 247");
    rig.FireList("1 2 3");
    CHECK(rig.Message() == Bytes({240, 1, 2, 3, 247}));

    // A short list leaves the placeholders it does not reach as they were.
    rig.FireList("7 8");
    CHECK(rig.Message() == Bytes({240, 7, 8, 3, 247}));
  }

  TEST_CASE("sxformat: a placeholder never given a value stands for 0 (#531)") {
    Rig rig("240 $i1 $i2 247");
    rig.Bang();
    CHECK(rig.Message() == Bytes({240, 0, 0, 247}));
  }

  TEST_CASE("sxformat: a negative value leaves its byte out entirely (#531)") {
    // Max's rule, and the reason it exists: one template covers both the short
    // form of a message and the long one.
    Rig rig("240 67 $i1 $i2 247");
    rig.Set(1, 5);
    rig.Fire(-1);
    CHECK(rig.Message() == Bytes({240, 67, 5, 247}));

    rig.Fire(3);
    CHECK(rig.Message() == Bytes({240, 67, 3, 5, 247}));
  }

  TEST_CASE("sxformat: a value above 127 is clamped, not passed through (#531)") {
    // A byte with its top bit set inside a dump is a status byte to the
    // receiving device and would end the message early, so passing it on would
    // corrupt the stream rather than merely misreport a number.
    Rig rig("240 $i1 247");
    rig.Fire(200);
    CHECK(rig.Message() == Bytes({240, 127, 247}));
  }

  // ─── .sxformat: the checksum ──────────────────────────────────────────────

  TEST_CASE("sxformat: sum emits the Roland checksum of the message so far (#531)") {
    // Hand-computed: the region is everything after the leading 240, so
    // 67 + 16 + 4 = 87, and the checksum is the byte that makes the total a
    // multiple of 128 — 128 - 87 = 41.
    Rig rig("240 67 16 $i1 sum 247");
    rig.Fire(4);
    CHECK(rig.Message() == Bytes({240, 67, 16, 4, 41, 247}));
    CHECK((67 + 16 + 4 + 41) % 128 == 0);
  }

  TEST_CASE("sxformat: sumstart moves where the checksum region begins (#531)") {
    // Only the two bytes after the marker count: 64 + 12 = 76, so the checksum
    // is 128 - 76 = 52. Without the marker the header bytes would be in the sum
    // and the answer would be a different byte, which is the whole point of it.
    Rig rig("240 65 16 sumstart 64 $i1 sum 247");
    rig.Fire(12);
    CHECK(rig.Message() == Bytes({240, 65, 16, 64, 12, 52, 247}));
    CHECK((64 + 12 + 52) % 128 == 0);
  }

  TEST_CASE("sxformat: a region that is already a multiple of 128 checksums to 0 (#531)") {
    // The edge of the formula: 128 - 0 is 128, which is not a data byte, so it
    // has to come back as 0 rather than as a status byte in the middle of a
    // dump.
    Rig rig("240 sumstart 64 64 sum 247");
    rig.Bang();
    CHECK(rig.Message() == Bytes({240, 64, 64, 0, 247}));
  }

  TEST_CASE("sxformat: sumstart emits no byte of its own (#531)") {
    Rig rig("240 sumstart 1 247");
    rig.Bang();
    CHECK(rig.Message() == Bytes({240, 1, 247}));
    // Three bytes out of four tokens.
    CHECK(rig.op.TemplateSize() == 4);
  }

  // ─── .sxformat: the bound, and malformed templates ────────────────────────

  TEST_CASE("sxformat: a template past the maximum is cut and reported (#531)") {
    // The issue's acceptance criterion: over-length is *reported*, not silently
    // truncated. The report is what a patch author sees; the cut is what keeps
    // the send path allocation-free.
    std::string tmpl = "240";
    for (int i = 0; i < mSxFormat::MAX_BYTES + 10; i++) {
      tmpl += " 1";
    }
    Rig rig(tmpl);
    CHECK(rig.op.TemplateSize() == mSxFormat::MAX_BYTES);
    CHECK_FALSE(rig.op.CompileError().empty());

    rig.Bang();
    // Still a well-formed list of exactly the bytes that fit.
    std::size_t count = 1;
    const std::string message = rig.Message();
    for (char c : message) {
      if (c == ' ') count++;
    }
    CHECK(count == (std::size_t)mSxFormat::MAX_BYTES);
  }

  TEST_CASE("sxformat: a token that is not a byte is reported and skipped (#531)") {
    // Skipped rather than guessed at: reading an unknown token as a byte would
    // put a number the patch never wrote into the middle of a dump.
    Rig rig("240 wobble 12 247");
    CHECK_FALSE(rig.op.CompileError().empty());
    CHECK(rig.op.TemplateSize() == 3);
    rig.Bang();
    CHECK(rig.Message() == Bytes({240, 12, 247}));
  }

  TEST_CASE("sxformat: a number outside 0-255 is not a byte either (#531)") {
    Rig rig("240 300 -5 247");
    CHECK_FALSE(rig.op.CompileError().empty());
    rig.Bang();
    CHECK(rig.Message() == Bytes({240, 247}));
  }

  TEST_CASE("sxformat: $i0 and $i10 are not placeholders (#531)") {
    // `$i0` would be an inlet that does not exist and `$i10` would silently
    // become `$i1`, which is worse than being refused.
    Rig zero("240 $i0 247");
    CHECK_FALSE(zero.op.CompileError().empty());
    CHECK(zero.op.NumInputs() == 1);

    Rig ten("240 $i10 247");
    CHECK_FALSE(ten.op.CompileError().empty());
    CHECK(ten.op.NumInputs() == 1);
  }

  TEST_CASE("sxformat: a re-parse rebuilds rather than patching the live object (#531)") {
    // The template decides the inlet count, so a live SetParams cannot be a
    // scalar write — the patcher has to take the structural-replacement route
    // of issue #234.
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::M_SXFORMAT));
    REQUIRE(obj != nullptr);
    CHECK(obj->ParamsNeedRebuild());
  }

  // ─── .sxformat, end to end in a real patcher ──────────────────────────────

  TEST_CASE("sxformat: what it builds is read back as SysEx by .midiparse (#531)") {
    // The claim at the level a patch makes it: a real cord from a real
    // `.sxformat` into the object that decodes MIDI, which reports system
    // exclusive on its rightmost outlet. If the bytes were not a MIDI message,
    // this is where it would show.
    YSE::patcher patch;
    patch.create(2);

    YSE::pHandle* format =
        patch.CreateObject(YSE::OBJ::M_SXFORMAT, "240 65 16 sumstart 64 $i1 sum 247");
    YSE::pHandle* parse = patch.CreateObject(YSE::OBJ::M_PARSE, "");
    REQUIRE(format != nullptr);
    REQUIRE(parse != nullptr);
    CHECK(format->GetInputs() == 1);
    patch.Connect(format, 0, parse, 0);

    std::vector<std::string> log;
    log.reserve(16);
    Tap tap;
    tap.log = &log;
    tap.tag = "raw";
    YSE::pHandle tapHandle(&tap);
    // Outlet 7 of `.midiparse` is everything its decoding does not cover, which
    // is where a system-exclusive dump comes out.
    patch.Connect(parse, 7, &tapHandle, 0);

    format->SetIntData(0, 12);
    REQUIRE(log.size() == 1);
    CHECK(log[0] == "raw:" + Bytes({240, 65, 16, 64, 12, 52, 247}));

    patch.DeleteObject(format);
    patch.DeleteObject(parse);
  }

  TEST_CASE("sxformat: the template survives a DumpJSON / ParseJSON round trip (#531)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::M_SXFORMAT, "240 67 $i1 sum 247") != nullptr);

    const std::string json = src.DumpJSON();
    CHECK(json.find(".sxformat") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* obj = loaded.GetHandleFromList(0);
    REQUIRE(obj != nullptr);
    CHECK(obj->GetName() == std::string(".sxformat"));

    // Read the template back through the object's behaviour rather than an
    // accessor: what has to survive is a patch that still builds the same
    // message. 67 + 5 = 72, so the checksum is 128 - 72 = 56.
    std::vector<std::string> log;
    log.reserve(8);
    Tap tap;
    tap.log = &log;
    tap.tag = "out";
    YSE::pHandle tapHandle(&tap);
    loaded.Connect(obj, 0, &tapHandle, 0);

    obj->SetIntData(0, 5);
    REQUIRE(log.size() == 1);
    CHECK(log[0] == "out:" + Bytes({240, 67, 5, 56, 247}));

    loaded.DeleteObject(obj);
  }

#if YSE_ENABLE_MIDI_DEVICE

  // ─── .sysexin ─────────────────────────────────────────────────────────────

  // The port every `.sysexin` test listens to: the last index the hub accepts,
  // because a machine with eight MIDI inputs in use is rare enough that a real
  // device cannot inject stray traffic into the assertions. Same choice, and
  // same reason, as the input family's tests (#529).
  const unsigned int kTestPort = 7;

  // A registry-built `.sysexin` in a real patcher with a Tap on its outlet, fed
  // through the real hub and drained by real blocks.
  struct SysExRig {
    YSE::PATCHER::patcherImplementation patch{1, nullptr};
    std::vector<std::string> log;
    Tap tap;
    std::unique_ptr<YSE::pHandle> tapHandle;
    YSE::pHandle* object = nullptr;

    SysExRig() {
      object = patch.CreateObject(YSE::OBJ::M_SYSEXIN, "7");
      REQUIRE(object != nullptr);
      log.reserve(64);
      tap.log = &log;
      tap.tag = "b";
      tapHandle = std::make_unique<YSE::pHandle>(&tap);
      patch.Connect(object, 0, tapHandle.get(), 0);
    }

    // One message on the wire, exactly as the RtMidi callback delivers it.
    void Wire(std::initializer_list<int> bytes) {
      std::vector<unsigned char> raw;
      raw.reserve(bytes.size());
      for (int b : bytes)
        raw.push_back(static_cast<unsigned char>(b));
      YSE::MIDI::InHub().Deliver(kTestPort, raw.data(), raw.size());
    }
    void Block() {
      patch.Calculate(YSE::T_DSP);
    }
    void WireAndBlock(std::initializer_list<int> bytes) {
      Wire(bytes);
      Block();
    }
    // Everything the outlet has sent, as one space-separated list of bytes.
    std::string Bytes() const {
      std::string out;
      for (const auto& entry : log) {
        if (!out.empty()) out += ' ';
        out += entry.substr(2);
      }
      return out;
    }
  };

  TEST_CASE("sysexin: a dump arrives whole, from its 240 to its 247 (#531)") {
    SysExRig rig;
    rig.WireAndBlock({240, 67, 0, 9, 1, 2, 3, 247});
    CHECK(rig.Bytes() == "240 67 0 9 1 2 3 247");
    CHECK_FALSE(rig.log.empty());
  }

  TEST_CASE("sysexin: a dump longer than one transport event loses no bytes (#531)") {
    // The transport carries `inEvent::kMaxBytes` at a time and splits anything
    // longer into consecutive chunks (#529). A dump is the message that is
    // always longer, which is why the object's open-message flag has to outlive
    // one Receive() call.
    std::vector<int> message;
    message.push_back(240);
    message.push_back(67);
    for (int i = 0; i < 40; i++) {
      message.push_back(i);
    }
    message.push_back(247);

    SysExRig rig;
    std::vector<unsigned char> raw;
    raw.reserve(message.size());
    for (int b : message)
      raw.push_back(static_cast<unsigned char>(b));
    YSE::MIDI::InHub().Deliver(kTestPort, raw.data(), raw.size());
    rig.Block();

    REQUIRE(rig.log.size() == message.size());
    for (std::size_t i = 0; i < message.size(); i++) {
      CAPTURE(i);
      CHECK(rig.log[i] == "b:" + std::to_string(message[i]));
    }
  }

  TEST_CASE("sysexin: notes, controllers and clock never reach the outlet (#531)") {
    // The whole difference between this object and `.midiin`.
    SysExRig rig;
    rig.WireAndBlock({144, 60, 100});
    rig.WireAndBlock({176, 7, 90});
    rig.WireAndBlock({248});
    rig.WireAndBlock({192, 4});
    CHECK(rig.log.empty());
    CHECK_FALSE(rig.object == nullptr);
  }

  TEST_CASE("sysexin: a real-time byte inside a dump is dropped and does not end it (#531)") {
    // A clock may legally appear between any two bytes of anything. It belongs
    // to `.rtin`, and the dump it interrupted carries on where it left off.
    SysExRig rig;
    rig.Wire({240, 67, 0});
    rig.Wire({248});
    rig.Wire({1, 2, 247});
    rig.Block();
    CHECK(rig.Bytes() == "240 67 0 1 2 247");
  }

  TEST_CASE("sysexin: a status byte ends an unterminated dump (#531)") {
    // Hardware interrupted mid-transfer stops sending and starts something
    // else. A state machine waiting for an EOX that is never coming would read
    // every later note as voice data — this is the case that proves it does
    // not.
    SysExRig rig;
    rig.Wire({240, 67, 0, 1});
    rig.Wire({144, 60, 100});
    rig.Wire({60, 0});
    rig.Block();
    CHECK(rig.Bytes() == "240 67 0 1");

    // And the next real dump still arrives.
    rig.log.clear();
    rig.WireAndBlock({240, 65, 247});
    CHECK(rig.Bytes() == "240 65 247");
  }

  TEST_CASE("sysexin: a restarted dump begins again rather than being ignored (#531)") {
    SysExRig rig;
    rig.Wire({240, 67, 0});
    rig.Wire({240, 65, 1, 247});
    rig.Block();
    CHECK(rig.Bytes() == "240 67 0 240 65 1 247");
  }

  TEST_CASE("sysexin: bytes outside a message are ignored (#531)") {
    // A patch that starts listening in the middle of a dump has no message
    // open, and a data byte with no 240 in front of it is not part of one.
    SysExRig rig;
    rig.Wire({1, 2, 3});
    rig.Wire({247});
    rig.Block();
    CHECK(rig.log.empty());
  }

  TEST_CASE("sysexin: an event that arrives between blocks lands in the next one (#531)") {
    // The block-poll contract of #529, which is the only thing that runs this
    // object at all.
    SysExRig rig;
    rig.Block();
    CHECK(rig.log.empty());

    rig.Wire({240, 67, 247});
    CHECK(rig.log.empty()); // nothing until the block that drains it
    rig.Block();
    CHECK(rig.Bytes() == "240 67 247");
  }

  TEST_CASE("sysexin: the port argument decides which device is listened to (#531)") {
    YSE::PATCHER::patcherImplementation patch{1, nullptr};
    YSE::pHandle* object = patch.CreateObject(YSE::OBJ::M_SYSEXIN, "3");
    REQUIRE(object != nullptr);

    std::vector<std::string> log;
    Tap tap;
    tap.log = &log;
    tap.tag = "b";
    YSE::pHandle tapHandle(&tap);
    patch.Connect(object, 0, &tapHandle, 0);

    // Delivered to a port this object is not listening to.
    std::vector<unsigned char> raw{240, 67, 247};
    YSE::MIDI::InHub().Deliver(kTestPort, raw.data(), raw.size());
    patch.Calculate(YSE::T_DSP);
    CHECK(log.empty());

    // And to the one it is.
    YSE::MIDI::InHub().Deliver(3, raw.data(), raw.size());
    patch.Calculate(YSE::T_DSP);
    CHECK(log.size() == 3);

    patch.DeleteObject(object);
  }

  TEST_CASE("sysexin: the port argument survives a DumpJSON / ParseJSON round trip (#531)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::M_SYSEXIN, "5") != nullptr);

    const std::string json = src.DumpJSON();
    CHECK(json.find(".sysexin") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* obj = loaded.GetHandleFromList(0);
    REQUIRE(obj != nullptr);
    CHECK(obj->GetName() == std::string(".sysexin"));
    CHECK(std::string(obj->GetParams()) == "5");

    loaded.DeleteObject(obj);
  }

  // ─── the pair, end to end ─────────────────────────────────────────────────

  TEST_CASE("sysex: a request built by .sxformat is the message .sysexin reports (#531)") {
    // The issue's use case in one test: a patch builds a dump request and the
    // reply comes back on the port. Standing in for the hardware is the hub
    // itself, fed the very bytes `.sxformat` produced — so what is checked is
    // that the two halves agree on what a system-exclusive message is.
    Rig builder("240 67 16 sumstart 32 $i1 sum 247");
    builder.Fire(6);
    const std::string built = builder.Message();
    CHECK(built == Bytes({240, 67, 16, 32, 6, 90, 247}));

    std::vector<unsigned char> raw;
    std::size_t cursor = 0;
    int number = 0;
    while (YSE::PATCHER::ReadIntArgAt(built, cursor, number)) {
      raw.push_back(static_cast<unsigned char>(number));
    }
    REQUIRE(raw.size() == 7);

    SysExRig listener;
    YSE::MIDI::InHub().Deliver(kTestPort, raw.data(), raw.size());
    listener.Block();
    CHECK(listener.Bytes() == built);
  }

#endif // YSE_ENABLE_MIDI_DEVICE

} // TEST_SUITE
