// Tests for midi/midiBytes.hpp — the standard-MIDI-file byte primitives shared
// by `MIDI::fileImpl` and the patcher's `.seq` (issue #698).
//
// These four readers and three writers existed twice before #698: once as
// anonymous-namespace statics in `midifileImplementation.cpp` and once, with
// different signatures, inside `gSeq.cpp`. The lift merged them onto the
// pointer-and-bound signature, which is the only one that works for both — so
// the risk it carries is not "does it compile" but "does either caller now get
// a *slightly* different answer than its own copy gave it".
//
// This file is the guard against that. Every case pins a property that both
// original copies had, chosen so that the plausible ways of getting the merge
// subtly wrong are each caught by name:
//
//   - the fixed-width reads do **not** advance a cursor. `gSeq`'s copies took a
//     bare pointer; `fileImpl`'s took a vector and a `pos&` and advanced it. The
//     shared form is the non-advancing one and `midifileImplementation.cpp`
//     keeps two thin wrappers that add the `pos += 2` / `pos += 4` back. A
//     shared version that advanced would double-step every header field there.
//   - `ReadVarLen` advances `at` past the bytes it consumed **even when it
//     fails**, which both copies did and which the callers' error paths assume.
//   - `ReadVarLen` stops after four bytes, the format's own limit — the property
//     that lets `.seq` call it on the audio thread over a possibly-corrupt file
//     without an unbounded walk.
//   - `ChannelDataBytes` answers 1 for exactly program change and channel
//     pressure. `fileImpl` returned `int` and `gSeq` `std::size_t`; the shared
//     one returns `std::size_t` and `fileImpl`'s call site was widened to match,
//     so the `nBytes == 2 ? data[pos + 1] : 0` there still selects the same byte.
//   - the writers are the exact inverses of the readers. A reader and a writer
//     that disagree about a variable-length quantity is the classic MIDI bug,
//     and it is the reason both directions live in one header.
//
// Pure header, no engine session, no audio device.

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "midi/midiBytes.hpp"

using YSE::MIDI::AppendByte;
using YSE::MIDI::AppendU16BE;
using YSE::MIDI::AppendU32BE;
using YSE::MIDI::AppendVarLen;
using YSE::MIDI::ChannelDataBytes;
using YSE::MIDI::ReadU16BE;
using YSE::MIDI::ReadU32BE;
using YSE::MIDI::ReadVarLen;

namespace {

  const unsigned char* Bytes(const std::string& s) {
    return (const unsigned char*)s.data();
  }

} // namespace

TEST_SUITE("midi") {

  TEST_CASE("midiBytes: the fixed-width reads are big-endian and do not move a cursor (#698)") {
    const unsigned char raw[6] = {0x12, 0x34, 0xDE, 0xAD, 0xBE, 0xEF};

    CHECK(ReadU16BE(raw) == 0x1234u);
    CHECK(ReadU16BE(raw + 2) == 0xDEADu);
    CHECK(ReadU32BE(raw + 2) == 0xDEADBEEFu);

    // Reading twice at the same address gives the same answer: there is no
    // hidden cursor, which is what `midifileImplementation.cpp`'s wrappers
    // assume when they do the `pos +=` themselves.
    CHECK(ReadU16BE(raw) == ReadU16BE(raw));

    // The full-width edges, where a sign-extending implementation would show.
    const unsigned char high[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    CHECK(ReadU16BE(high) == 0xFFFFu);
    CHECK(ReadU32BE(high) == 0xFFFFFFFFu);
    const unsigned char zero[4] = {0x00, 0x00, 0x00, 0x00};
    CHECK(ReadU32BE(zero) == 0u);

    // A high bit in the *second* byte is data, not a sign: 0x0080, not 0x80
    // widened. This is what a `char`-typed intermediate would get wrong.
    const unsigned char lowHigh[2] = {0x00, 0x80};
    CHECK(ReadU16BE(lowHigh) == 0x0080u);
  }

  TEST_CASE("midiBytes: a variable-length quantity reads back what it was written as (#698)") {
    // The format's own worked examples (SMF spec table), plus the boundaries
    // where a byte is added.
    const std::uint32_t cases[] = {0x00000000u, 0x00000040u, 0x0000007Fu, 0x00000080u,
                                   0x00002000u, 0x00003FFFu, 0x00004000u, 0x00100000u,
                                   0x001FFFFFu, 0x00200000u, 0x08000000u, 0x0FFFFFFFu};

    for (std::uint32_t value : cases) {
      std::string encoded;
      AppendVarLen(encoded, value);
      CAPTURE(value);
      // Never more than the four bytes the format allows.
      REQUIRE(encoded.size() >= 1);
      REQUIRE(encoded.size() <= 4);

      std::size_t at = 0;
      std::uint32_t out = 0xFFFFFFFFu;
      REQUIRE(ReadVarLen(Bytes(encoded), at, encoded.size(), out));
      CHECK(out == value);
      // The cursor lands exactly past the quantity — the next event's status
      // byte in a real track.
      CHECK(at == encoded.size());
    }
  }

  TEST_CASE("midiBytes: the continuation bit marks every byte but the last (#698)") {
    // Pinned against the spec's own encoding rather than against the round trip
    // above, which would only prove the two halves agree with each other.
    std::string encoded;
    AppendVarLen(encoded, 0x00004000u);
    REQUIRE(encoded.size() == 3);
    CHECK((unsigned char)encoded[0] == 0x81);
    CHECK((unsigned char)encoded[1] == 0x80);
    CHECK((unsigned char)encoded[2] == 0x00);

    encoded.clear();
    AppendVarLen(encoded, 0x0000007Fu);
    REQUIRE(encoded.size() == 1);
    CHECK((unsigned char)encoded[0] == 0x7F);
  }

  TEST_CASE("midiBytes: a value too large to spell is clamped, not wrapped (#698)") {
    // A delta longer than 28 bits cannot be written at all. Clamping keeps the
    // file readable; wrapping would write a *different, plausible* time.
    std::string encoded;
    AppendVarLen(encoded, 0xFFFFFFFFu);
    REQUIRE(encoded.size() == 4);

    std::size_t at = 0;
    std::uint32_t out = 0;
    REQUIRE(ReadVarLen(Bytes(encoded), at, encoded.size(), out));
    CHECK(out == 0x0FFFFFFFu);
  }

  TEST_CASE("midiBytes: ReadVarLen stops at the bound and reports truncation (#698)") {
    // A quantity that runs off the end of the buffer.
    const unsigned char truncated[2] = {0x81, 0x80};
    std::size_t at = 0;
    std::uint32_t out = 0x5A5A5A5Au;
    CHECK_FALSE(ReadVarLen(truncated, at, 2, out));
    // Both original copies advanced past what they consumed even on failure,
    // and both callers' error paths were written against that.
    CHECK(at == 2);
    // `out` is untouched on failure — the caller must not act on a half value.
    CHECK(out == 0x5A5A5A5Au);

    // An empty span fails immediately without reading anything.
    std::size_t empty = 0;
    CHECK_FALSE(ReadVarLen(truncated, empty, 0, out));
    CHECK(empty == 0);

    // The bound is respected even when there are readable bytes past it: this
    // is what keeps a track's parse inside its own chunk.
    const unsigned char spill[3] = {0x81, 0x7F, 0x00};
    std::size_t bounded = 0;
    CHECK_FALSE(ReadVarLen(spill, bounded, 1, out));
    CHECK(bounded == 1);
  }

  TEST_CASE("midiBytes: five continuation bytes are malformed rather than an endless walk (#698)") {
    // The property `.seq` leans on to call this from an audio-thread completion:
    // a corrupt file cannot make the reader hunt for a terminator. Every byte
    // here has its high bit set, so only the four-byte cap can stop it.
    unsigned char runaway[64];
    for (unsigned char& byte : runaway)
      byte = 0xFF;

    std::size_t at = 0;
    std::uint32_t out = 0;
    CHECK_FALSE(ReadVarLen(runaway, at, sizeof(runaway), out));
    // Exactly four bytes consumed — not five, and not the whole buffer.
    CHECK(at == 4);
  }

  TEST_CASE("midiBytes: ReadVarLen reads from where the cursor already is (#698)") {
    // Both parsers call this mid-buffer with a cursor they own, never at 0.
    const unsigned char track[5] = {0x90, 0x3C, 0x64, 0x81, 0x40};
    std::size_t at = 3;
    std::uint32_t out = 0;
    REQUIRE(ReadVarLen(track, at, 5, out));
    CHECK(out == 192u); // 0x81 0x40 -> (1 << 7) | 0x40
    CHECK(at == 5);
  }

  TEST_CASE("midiBytes: only program change and channel pressure take one data byte (#698)") {
    // All seven channel-voice statuses, across every channel nibble, since both
    // parsers pass the whole status byte rather than the nibble.
    for (unsigned char channel = 0; channel < 16; channel++) {
      CAPTURE(channel);
      CHECK(ChannelDataBytes((unsigned char)(0x80 + channel)) == 2); // note off
      CHECK(ChannelDataBytes((unsigned char)(0x90 + channel)) == 2); // note on
      CHECK(ChannelDataBytes((unsigned char)(0xA0 + channel)) == 2); // poly pressure
      CHECK(ChannelDataBytes((unsigned char)(0xB0 + channel)) == 2); // control change
      CHECK(ChannelDataBytes((unsigned char)(0xC0 + channel)) == 1); // program change
      CHECK(ChannelDataBytes((unsigned char)(0xD0 + channel)) == 1); // channel pressure
      CHECK(ChannelDataBytes((unsigned char)(0xE0 + channel)) == 2); // pitch bend
    }
  }

  TEST_CASE("midiBytes: the fixed-width writers are the inverses of the readers (#698)") {
    std::string out;
    AppendU16BE(out, 0xBEEFu);
    AppendU32BE(out, 0x0BADF00Du);
    AppendByte(out, 0x7F);

    REQUIRE(out.size() == 7);
    CHECK(ReadU16BE(Bytes(out)) == 0xBEEFu);
    CHECK(ReadU32BE(Bytes(out) + 2) == 0x0BADF00Du);
    CHECK((unsigned char)out[6] == 0x7F);

    // Big-endian on the wire: the most significant byte is written first. A
    // little-endian writer would still round-trip through these readers if they
    // shared the mistake, so this asserts the byte order directly.
    CHECK((unsigned char)out[0] == 0xBE);
    CHECK((unsigned char)out[1] == 0xEF);
    CHECK((unsigned char)out[2] == 0x0B);
    CHECK((unsigned char)out[5] == 0x0D);
  }

  TEST_CASE("midiBytes: the writers append rather than replace (#698)") {
    // `.seq` builds a whole file into one buffer, so every writer has to leave
    // what is already there alone.
    std::string out = "MThd";
    AppendU32BE(out, 6);
    AppendU16BE(out, 0);
    CHECK(out.compare(0, 4, "MThd") == 0);
    REQUIRE(out.size() == 10);
    CHECK(ReadU32BE(Bytes(out) + 4) == 6u);
  }

  TEST_CASE("midiBytes: the format's marker bytes are the values the spec gives (#698)") {
    // Both parsers compared against these as literals before the lift; a typo in
    // one constant would silently turn a tempo change into an unknown meta.
    CHECK(YSE::MIDI::META_PREFIX == 0xFF);
    CHECK(YSE::MIDI::META_END_OF_TRACK == 0x2F);
    CHECK(YSE::MIDI::META_TEMPO == 0x51);
    CHECK(YSE::MIDI::SYSEX_BEGIN == 0xF0);
    CHECK(YSE::MIDI::SYSEX_ESCAPE == 0xF7);

    // The two sysex forms are status bytes, so they must not be mistaken for
    // channel-voice ones by the running-status test both parsers use.
    CHECK((YSE::MIDI::SYSEX_BEGIN & 0x80) != 0);
    CHECK(YSE::MIDI::SYSEX_BEGIN >= 0xF0);
    CHECK(YSE::MIDI::SYSEX_ESCAPE >= 0xF0);
  }

} // TEST_SUITE("midi")
