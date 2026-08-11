// Tests for what `.midiout` reads off its inlet (issue #748) — the two
// spellings of "a list of MIDI bytes" the patcher has, and the fact that both
// of them reach the device.
//
// What is being pinned:
//
//   - **the numeric spelling composes**: `144 60 100`, the shape `.midiformat`
//     (#530) and `.sxformat` (#531) send, is read as the three bytes it names.
//     Before this it was read as raw characters, so wiring `.midiformat` into
//     `.midiout` put the ASCII codes of `1`, `4` and `4` on the wire — the gap
//     the issue records;
//   - **the binary spelling still composes**: the three-character strings the
//     older senders build (`.noteon`, `.bendout`, the `.x*out` family) are
//     still read as raw bytes, unchanged. This is the half that had to not
//     break, and the reason the two are distinguishable at all: a MIDI message
//     begins with a status byte, 0x80 or above, which is never a decimal digit;
//   - **length honesty**: a two-byte message stays two bytes and a 256-byte
//     system-exclusive dump is not cut to three, which is what `midiOut::Raw`
//     used to do to everything it was handed;
//   - **refusal rather than corruption**: a numeric list carrying a number that
//     is not a byte, or more bytes than fit, is dropped whole rather than sent
//     in part or reread as text.
//
// The composition section drives real objects in a real `patcherImplementation`
// — `.midiformat`, `.sxformat` and `.noteon` built through the registry, wired
// with real cords — and runs the reader `.midiout` uses over exactly the text
// that came out of their outlets. That is the level at which "the two objects
// compose" is either true or not; a reader tested only on hand-typed strings
// would pass just as happily on a pair whose spellings still disagreed.
//
// The last section drives `.midiout` itself, which the byte-list sections
// deliberately do not (issue #759). What made that impossible was the object's
// own list handler: it opened a hardware port inline and then handed the bytes
// to RtMidi, so driving it would have played real notes out of a real
// synthesiser on any machine with one. The deferred open changes exactly that —
// the *first* message opens nothing and sends nothing, which is what the
// section asserts — so the object can now be driven end to end without a byte
// ever reaching a device: the messages sent after the port arrives are ones the
// reader refuses. Tests/midi/test_devicemanager.cpp takes the same line with
// `midiOut`. The step no test here covers is therefore still
// `out.Raw(bytes, count)` — one call, whose length handling is covered there.
//
// No audio device and no MIDI hardware required. The byte-list sections carry
// no `#if` — the reader is arithmetic over characters — while the deferred-open
// section sits behind YSE_ENABLE_MIDI_DEVICE with the object it drives.

#include <doctest/doctest.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "headers/defines.hpp"

#include "patcher/midi/pMidiByteList.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObject.h"
#include "patcher/pObjectList.hpp"
#include "patcher/patcher.hpp"
#include "sinks.hpp"

#if YSE_ENABLE_MIDI_DEVICE
#include "midi/device.hpp"
#include "midi/midiDeviceManager.h"
#include "patcher/inlet.h"
#include "patcher/midi/mMidiOut.h"
#include "patcher/midi/midiPortOpener.h"
#include "patcher/patcherImplementation.h"
#endif

using YSE::PATCHER::MIDI_BYTE_LIST_MAX;
using YSE::PATCHER::midiByteList;
using YSE::PATCHER::ReadMidiByteList;

namespace {

  // What `.midiout` would put on the wire for a given list: the result of the
  // reader, and the bytes it produced, as a vector so an expectation reads as
  // the message it is.
  struct Wire {
    midiByteList how = midiByteList::characters;
    std::vector<int> bytes;
  };

  // The whole of `.midiout`'s list handler apart from the device call: read the
  // list, and take the bytes from wherever the reading says they are.
  Wire Send(const std::string& list) {
    Wire wire;
    unsigned char bytes[MIDI_BYTE_LIST_MAX];
    int count = 0;
    wire.how = ReadMidiByteList(list, bytes, MIDI_BYTE_LIST_MAX, count);

    if (wire.how == midiByteList::numeric) {
      for (int i = 0; i < count; i++)
        wire.bytes.push_back((int)bytes[i]);
    } else if (wire.how == midiByteList::characters) {
      // The binary spelling goes to the device as the characters it is.
      for (char c : list)
        wire.bytes.push_back((int)(unsigned char)c);
    }
    return wire;
  }

  // The three wire bytes of a binary message as a std::string — the form the
  // older senders build. Built from ints because a status byte has its top bit
  // set and cannot be written legibly in a string literal.
  std::string Binary(int a, int b, int c) {
    std::string s(3, '\0');
    s[0] = (char)a;
    s[1] = (char)b;
    s[2] = (char)c;
    return s;
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── the numeric spelling ─────────────────────────────────────────────────

  TEST_CASE("midiout: a numeric byte list is read as the bytes it names (#748)") {
    // The bug in one line: `144 60 100` used to reach the device as the eleven
    // characters of that text, starting with the ASCII codes of '1', '4', '4'.
    const Wire note = Send("144 60 100");
    CHECK(note.how == midiByteList::numeric);
    REQUIRE(note.bytes.size() == 3u);
    CHECK(note.bytes[0] == 144);
    CHECK(note.bytes[1] == 60);
    CHECK(note.bytes[2] == 100);
  }

  TEST_CASE("midiout: a two-byte message stays two bytes (#748)") {
    // Program change and channel aftertouch carry one data byte. `.midiformat`
    // emits exactly two numbers for them, and both of them have to arrive —
    // no third byte the patch never wrote.
    const Wire program = Send("192 41");
    CHECK(program.how == midiByteList::numeric);
    REQUIRE(program.bytes.size() == 2u);
    CHECK(program.bytes[0] == 192);
    CHECK(program.bytes[1] == 41);
  }

  TEST_CASE("midiout: a long message is not truncated (#748)") {
    // A system-exclusive dump is as long as it is. The old three-byte `Raw`
    // made it unsendable outright.
    std::string dump = "240 67";
    for (int i = 0; i < 61; i++)
      dump += " 16";
    dump += " 247";

    const Wire wire = Send(dump);
    CHECK(wire.how == midiByteList::numeric);
    REQUIRE(wire.bytes.size() == 64u);
    CHECK(wire.bytes[0] == 240);
    CHECK(wire.bytes[1] == 67);
    CHECK(wire.bytes[63] == 247);
  }

  TEST_CASE("midiout: the byte-list limit is `.sxformat`'s template limit (#748)") {
    // Nothing the patcher itself can build is refused for length: `.sxformat`
    // caps its template at 256 tokens, and that is the size of the buffer here.
    std::string full;
    for (int i = 0; i < MIDI_BYTE_LIST_MAX; i++) {
      if (i > 0) full += " ";
      full += "16";
    }
    const Wire fits = Send(full);
    CHECK(fits.how == midiByteList::numeric);
    CHECK(fits.bytes.size() == (std::size_t)MIDI_BYTE_LIST_MAX);

    // One more than fits is refused whole rather than sent short: half a dump
    // is worse at the receiving device than none.
    const Wire over = Send(full + " 16");
    CHECK(over.how == midiByteList::refused);
    CHECK(over.bytes.empty());
  }

  TEST_CASE("midiout: a value outside 0-255 is not a byte and the message is dropped (#748)") {
    for (const char* list : {"144 60 300", "144 -1 100", "144 60 100 99999"}) {
      CAPTURE(list);
      const Wire wire = Send(list);
      CHECK(wire.how == midiByteList::refused);
      CHECK(wire.bytes.empty());
    }
    // The boundaries themselves are bytes.
    CHECK(Send("0 0 0").how == midiByteList::numeric);
    CHECK(Send("255 255 255").how == midiByteList::numeric);
  }

  TEST_CASE("midiout: a single number is a one-byte message (#748)") {
    // System real time is one byte: 248 is a clock, 250 a start. `.midiformat`
    // sends them through its raw inlet exactly like this.
    const Wire clock = Send("248");
    CHECK(clock.how == midiByteList::numeric);
    REQUIRE(clock.bytes.size() == 1u);
    CHECK(clock.bytes[0] == 248);
  }

  TEST_CASE("midiout: extra separators do not change the message (#748)") {
    const Wire padded = Send("  144   60  100  ");
    CHECK(padded.how == midiByteList::numeric);
    REQUIRE(padded.bytes.size() == 3u);
    CHECK(padded.bytes[0] == 144);
    CHECK(padded.bytes[2] == 100);
  }

  // ─── the binary spelling ──────────────────────────────────────────────────

  TEST_CASE("midiout: a binary three-byte message is still read as raw bytes (#748)") {
    // What the older senders build. It cannot be read as a numeric list: the
    // status byte is 0x90, which is not a digit — the property the whole
    // distinction rests on.
    const Wire note = Send(Binary(0x90, 60, 100));
    CHECK(note.how == midiByteList::characters);
    REQUIRE(note.bytes.size() == 3u);
    CHECK(note.bytes[0] == 0x90);
    CHECK(note.bytes[1] == 60);
    CHECK(note.bytes[2] == 100);
  }

  TEST_CASE("midiout: every status byte a sender can build stays binary (#748)") {
    // The full set: note off, note on, poly pressure, control change, program
    // change, channel pressure, pitch bend — on every one of the 16 channels,
    // since the channel nibble is what the sender adds to the status. A single
    // one of these reading as a digit would be a message silently rewritten.
    for (int status = 0x80; status <= 0xEF; status++) {
      CAPTURE(status);
      // Data bytes chosen to be digits, which is the hostile case: if the
      // status byte did not disqualify the string, `48 49` would be read as
      // numbers.
      const Wire wire = Send(Binary(status, '0', '1'));
      CHECK(wire.how == midiByteList::characters);
      REQUIRE(wire.bytes.size() == 3u);
      CHECK(wire.bytes[0] == status);
    }
  }

  TEST_CASE("midiout: a binary message carrying a zero byte survives (#748)") {
    // `.bendout` sends the fine byte as 0, and `.programchange` pads with one.
    // A reader that stopped at a NUL would cut both messages short.
    const Wire bend = Send(Binary(0xE0, 0, 96));
    CHECK(bend.how == midiByteList::characters);
    REQUIRE(bend.bytes.size() == 3u);
    CHECK(bend.bytes[1] == 0);
    CHECK(bend.bytes[2] == 96);
  }

  TEST_CASE("midiout: text that is not a byte list at all is left as characters (#748)") {
    for (const char* list : {"hello", "144 hello 100", "144. 60 100", ""}) {
      CAPTURE(list);
      CHECK(Send(list).how == midiByteList::characters);
    }
  }

  // ─── the composition, through real objects in a real patcher ──────────────

  TEST_CASE("midiout: what .midiformat sends is what .midiout would put on the wire (#748)") {
    // The claim of the issue, at the level a patch would make it: the producer
    // built through the registry, wired with a real cord, and its real outlet
    // text run through the real reader.
    YSE::patcher patch;
    patch.create(2);

    YSE::pHandle* format = patch.CreateObject(YSE::OBJ::M_FORMAT, "");
    REQUIRE(format != nullptr);

    TestHelpers::ListSink sink;
    auto handle = std::make_unique<YSE::pHandle>(&sink);
    patch.Connect(format, 0, handle.get(), 0);

    format->SetIntData(6, 1); // channel 1

    // Note on: the message the issue is named after.
    format->SetListData(0, "60 100");
    REQUIRE(sink.gotList);
    {
      const Wire wire = Send(sink.received);
      CHECK(wire.how == midiByteList::numeric);
      REQUIRE(wire.bytes.size() == 3u);
      CHECK(wire.bytes[0] == 0x90);
      CHECK(wire.bytes[1] == 60);
      CHECK(wire.bytes[2] == 100);
    }

    // Program change: two bytes out of `.midiformat`, two bytes on the wire.
    format->SetIntData(3, 42);
    {
      const Wire wire = Send(sink.received);
      CHECK(wire.how == midiByteList::numeric);
      REQUIRE(wire.bytes.size() == 2u);
      CHECK(wire.bytes[0] == 0xC0);
      CHECK(wire.bytes[1] == 41); // 1-128 in, 0-127 on the wire
    }

    // And the raw passthrough, which is how system traffic gets back out.
    format->SetListData(7, "240 67 16 247");
    {
      const Wire wire = Send(sink.received);
      CHECK(wire.how == midiByteList::numeric);
      REQUIRE(wire.bytes.size() == 4u);
      CHECK(wire.bytes[0] == 240);
      CHECK(wire.bytes[3] == 247);
    }

    patch.DeleteObject(format);
  }

  TEST_CASE("midiout: a .sxformat dump reaches the wire entire (#748)") {
    // `.sxformat`'s outlet documentation already says "send it to '.midiout'".
    // It could not be true before: the message left as a numeric list and
    // arrived as three bytes of text.
    YSE::patcher patch;
    patch.create(2);

    YSE::pHandle* sx = patch.CreateObject(YSE::OBJ::M_SXFORMAT, "240 67 16 $i1 sum 247");
    REQUIRE(sx != nullptr);

    TestHelpers::ListSink sink;
    auto handle = std::make_unique<YSE::pHandle>(&sink);
    patch.Connect(sx, 0, handle.get(), 0);

    sx->SetIntData(0, 64);
    REQUIRE(sink.gotList);

    const Wire wire = Send(sink.received);
    CHECK(wire.how == midiByteList::numeric);
    REQUIRE(wire.bytes.size() == 6u);
    CHECK(wire.bytes[0] == 240);
    CHECK(wire.bytes[5] == 247);
    // The checksum byte makes the region a multiple of 128 — the byte a Roland
    // device rejects the message for getting wrong, and the one a three-byte
    // send would have thrown away.
    CHECK(((wire.bytes[1] + wire.bytes[2] + wire.bytes[3] + wire.bytes[4]) % 128) == 0);

    patch.DeleteObject(sx);
  }

  TEST_CASE("midiout: what .noteon sends is still read as raw bytes (#748)") {
    // The other half of the acceptance: the six older senders drive `.midiout`
    // exactly as they did. Their objects are built on every platform since
    // issue #746 lifted the family's `#if YSE_WINDOWS`.
    YSE::patcher patch;
    patch.create(2);

    YSE::pHandle* noteon = patch.CreateObject(YSE::OBJ::M_NOTEON, "");
    REQUIRE(noteon != nullptr);

    TestHelpers::ListSink sink;
    auto handle = std::make_unique<YSE::pHandle>(&sink);
    patch.Connect(noteon, 0, handle.get(), 0);

    noteon->SetIntData(1, 100); // velocity
    noteon->SetIntData(0, 60); // pitch, and the trigger
    REQUIRE(sink.gotList);
    CHECK(sink.received.size() == 3u);

    const Wire wire = Send(sink.received);
    CHECK(wire.how == midiByteList::characters);
    REQUIRE(wire.bytes.size() == 3u);
    CHECK(wire.bytes[0] == 0x90);
    CHECK(wire.bytes[1] == 60);
    CHECK(wire.bytes[2] == 100);

    patch.DeleteObject(noteon);
  }

  // ─── the deferred port open (#759) ────────────────────────────────────────

#if YSE_ENABLE_MIDI_DEVICE

  // A list the reader refuses (300 is not a byte), so it reaches no device.
  // What it *does* do is run the handler's port gate, which is how a test
  // collects a finished open without putting a note on the wire.
  static const char* const kRefusedList = "144 60 300";

  TEST_CASE("midiout: the first message opens nothing on the thread that sent it (#759)") {
    // The load-bearing assertion of the whole fix. A list handler runs on
    // whichever thread dispatched the message and in-patcher delivery
    // dispatches on T_DSP, so a handler that opened inline would be
    // constructing an RtMidiOut, calling openPort — a driver call that can
    // block — inserting into a std::map and taking MIDI::deviceManager's mutex
    // from the audio callback.
    using YSE::PATCHER::MidiPortOpener;
    using YSE::PATCHER::mMidiOut;

    mMidiOut object;
    CHECK_FALSE(object.PortSettled());
    CHECK_FALSE(object.PortOpen());
    CHECK_FALSE(object.OpenInFlight());

    object.GetInlet(0)->SetList("144 60 100", YSE::T_DSP);

    // The handler came back with no port and nothing sent. Both are checked on
    // the thread that sent the message, where they cannot race the pool: only a
    // handler ever settles the object, and the drop is already counted.
    CHECK_FALSE(object.PortSettled());
    CHECK(object.Deferred() == 1);
    // And the work is somewhere else — in flight on the pool, or already
    // waiting there to be collected.
    CHECK(object.OpenInFlight());

    MidiPortOpener().WaitIdle();
  }

  TEST_CASE("midiout: the port opens on the background pool and the next message finds it (#759)") {
    // The other half of the acceptance: deferring must still end with an open
    // port, or the object would simply never send anything again.
    using YSE::PATCHER::MidiPortOpener;
    using YSE::PATCHER::mMidiOut;

    mMidiOut object;
    const std::uint64_t before = MidiPortOpener().Opened();

    object.GetInlet(0)->SetList("144 60 100", YSE::T_DSP);
    MidiPortOpener().WaitIdle();

    // The open ran, and it ran on the pool: the count only moves there.
    CHECK(MidiPortOpener().Opened() >= before + 1);

    // Collected by the next message through the same gate. A refused list, so
    // this test opens a device without ever sending it a note.
    object.GetInlet(0)->SetList(kRefusedList, YSE::T_DSP);
    CHECK(object.PortSettled());
    CHECK_FALSE(object.OpenInFlight());
    // A refusal is not a deferral: the port was there, the bytes were not.
    CHECK(object.Deferred() == 1);

    // "Settled" is the attempt, not its success. On a machine with an output
    // port, port 0 is now open; on CI, where there is none, the attempt failed
    // and the object stays closed — which is exactly what an inline open that
    // threw always left behind.
    if (YSE::MIDI::DeviceManager().getNumMidiOutDevices() > 0) {
      CHECK(object.PortOpen());
    } else {
      CHECK_FALSE(object.PortOpen());
    }

    // And it is asked for once: a settled object goes straight to the send.
    const std::uint64_t opened = MidiPortOpener().Opened();
    object.GetInlet(0)->SetList(kRefusedList, YSE::T_DSP);
    CHECK(MidiPortOpener().Opened() == opened);
  }

  TEST_CASE("midiout: a message arriving down a real cord defers the open (#759)") {
    // The same claim at the level a patch makes it: a registry-built
    // `.midiformat` wired to `.midiout` with a real cord, so the message is
    // delivered by the patcher's own dispatch rather than by a test poking an
    // inlet. This is the shape the issue describes — the first list a patch
    // sends is what used to open the device.
    using YSE::PATCHER::MidiPortOpener;
    using YSE::PATCHER::mMidiOut;
    using YSE::PATCHER::patcherImplementation;

    patcherImplementation patch{1, nullptr};
    YSE::pHandle* format = patch.CreateObject(YSE::OBJ::M_FORMAT, "");
    REQUIRE(format != nullptr);

    mMidiOut object;
    auto handle = std::make_unique<YSE::pHandle>(&object);
    patch.Connect(format, 0, handle.get(), 0);

    format->SetIntData(6, 1); // channel 1
    const std::uint64_t before = MidiPortOpener().Opened();

    // Note on, straight down the cord into `.midiout`'s list inlet.
    format->SetListData(0, "60 100");

    CHECK_FALSE(object.PortSettled());
    CHECK(object.Deferred() == 1);
    CHECK(object.OpenInFlight());

    MidiPortOpener().WaitIdle();
    CHECK(MidiPortOpener().Opened() >= before + 1);
  }

  TEST_CASE("midiout: a control message waits for the port too (#759)") {
    // `allnotesoff` and the rest reach into the same `midiOut`, so they go
    // through the same gate — not for symmetry but because the gate's acquire
    // load is what makes the port the pool opened visible on this thread.
    using YSE::PATCHER::MidiPortOpener;
    using YSE::PATCHER::mMidiOut;

    mMidiOut object;
    object.SetMessage("allnotesoff", 0.f);
    CHECK_FALSE(object.PortSettled());
    CHECK(object.Deferred() == 1);
    CHECK(object.OpenInFlight());

    MidiPortOpener().WaitIdle();
  }

  // ─── the opener the deferral runs on ──────────────────────────────────────

  TEST_CASE("midiPortOpener: a claimed slot round-trips an open (#759)") {
    using YSE::PATCHER::midiPortOpener;

    midiPortOpener opener;
    const midiPortOpener::Handle handle = opener.Claim();
    REQUIRE(handle != 0);

    YSE::midiOut target;
    // Nothing asked for, nothing to collect.
    CHECK_FALSE(opener.Consume(handle));
    CHECK_FALSE(opener.Busy(handle));

    const std::uint64_t before = opener.Opened();
    CHECK(opener.Request(handle, &target, 0));
    opener.WaitIdle();
    CHECK(opener.Opened() == before + 1);
    CHECK(opener.Settled(handle));
    CHECK(opener.Busy(handle)); // a result is waiting

    CHECK(opener.Consume(handle));
    CHECK_FALSE(opener.Busy(handle));
    // Collecting twice takes nothing: the slot went back to idle with the first.
    CHECK_FALSE(opener.Consume(handle));

    opener.Release(handle);
  }

  TEST_CASE("midiPortOpener: asking again while an open is in flight costs nothing (#759)") {
    // `.midiout` asks on every message until the port arrives, so a repeat ask
    // has to be harmless — and must not re-point the slot at another target
    // while a worker is reading it.
    using YSE::PATCHER::midiPortOpener;

    midiPortOpener opener;
    const midiPortOpener::Handle handle = opener.Claim();
    REQUIRE(handle != 0);

    YSE::midiOut target;
    const std::uint64_t before = opener.Opened();
    CHECK(opener.Request(handle, &target, 0));
    CHECK(opener.Request(handle, &target, 0));
    CHECK(opener.Request(handle, &target, 0));
    opener.WaitIdle();

    // Three asks, one open.
    CHECK(opener.Opened() == before + 1);
    CHECK(opener.Consume(handle));

    opener.Release(handle);
  }

  TEST_CASE("midiPortOpener: a request on no slot is refused rather than ignored (#759)") {
    using YSE::PATCHER::midiPortOpener;

    midiPortOpener opener;
    YSE::midiOut target;

    // Handle 0 is what a full table hands back, and every call has to stay safe
    // on it: `.midiout` keeps asking either way.
    CHECK_FALSE(opener.Request(0, &target, 0));
    CHECK_FALSE(opener.Request(static_cast<midiPortOpener::Handle>(midiPortOpener::CAPACITY + 1),
                               &target, 0));
    CHECK_FALSE(opener.Consume(0));
    CHECK_FALSE(opener.Busy(0));
    CHECK_FALSE(opener.Settled(0));
    opener.Release(0); // no-op, not a crash

    // A claimed slot with nowhere to open into is refused rather than armed.
    const midiPortOpener::Handle handle = opener.Claim();
    REQUIRE(handle != 0);
    CHECK_FALSE(opener.Request(handle, nullptr, 0));
    CHECK_FALSE(opener.Busy(handle));

    // A released slot stops answering too.
    opener.Release(handle);
    CHECK_FALSE(opener.Request(handle, &target, 0));
  }

  TEST_CASE("midiPortOpener: the table is bounded and refusals are counted (#759)") {
    using YSE::PATCHER::midiPortOpener;

    midiPortOpener opener;
    std::vector<midiPortOpener::Handle> held;
    for (std::size_t i = 0; i < midiPortOpener::CAPACITY; i++) {
      const midiPortOpener::Handle handle = opener.Claim();
      CHECK(handle != 0);
      held.push_back(handle);
    }

    CHECK(opener.Dropped() == 0);
    CHECK(opener.Claim() == 0);
    CHECK(opener.Dropped() == 1);

    // Giving one back makes the table usable again — slots are held for the
    // life of an owner, not of the process.
    opener.Release(held.back());
    held.pop_back();
    const midiPortOpener::Handle reused = opener.Claim();
    CHECK(reused != 0);
    held.push_back(reused);

    for (auto handle : held)
      opener.Release(handle);
  }

#endif // YSE_ENABLE_MIDI_DEVICE

} // TEST_SUITE
