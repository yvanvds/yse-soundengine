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
// What is deliberately *not* driven here is `.midiout` itself. Its list handler
// opens a hardware port on the first message it receives and then hands the
// bytes to RtMidi, so an end-to-end test of the object would (a) observe
// nothing on a machine with no MIDI device and (b) play real notes out of a
// real synthesiser on a machine that has one. Tests/midi/test_devicemanager.cpp
// takes the same line with `midiOut` for the same reason: every send is driven,
// none of them with a port open. The step this file cannot cover is therefore
// `out.Raw(bytes, count)` — one call, whose length handling is covered there.
//
// No audio device and no MIDI hardware required, and no MIDI backend either:
// the reader is arithmetic over characters and carries no `#if`.

#include <doctest/doctest.h>
#include <cstddef>
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

#if YSE_WINDOWS

  TEST_CASE("midiout: what .noteon sends is still read as raw bytes (#748)") {
    // The other half of the acceptance: the six older senders drive `.midiout`
    // exactly as they did. Their objects are compiled behind `#if YSE_WINDOWS`
    // — see issue #746 for the sweep that lifts that guard.
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

#endif // YSE_WINDOWS

} // TEST_SUITE
