/*
  ==============================================================================

    midiBytes.hpp
    The byte-level primitives of the Standard MIDI File format (issue #698).

    Shared by both readers of that format in the engine, the way
    midiSynthRouting.hpp is shared by both writers into a synth:
      * MIDI file playback — midifileImplementation.cpp
      * the patcher's .seq  — patcher/genericObjects/gSeq.cpp

    Only the *primitives* are shared. The two parsers above them stay separate
    on purpose and issue #698 says why: `MIDI::fileImpl::create()` is handed a
    filesystem path, slurps it with std::ifstream and builds and sorts
    std::vectors, which is the right shape for a document read once off the
    audio thread; `.seq` is handed bytes that already came through the host's
    IO() layer and parses them into fixed-size tables inside a completion the
    audio thread delivers, where none of that is allowed. Merging them would
    have to break one side or the other.

    What is genuinely common is the format itself, and a reader and a writer
    that disagree about a variable-length quantity is the classic MIDI bug —
    so both directions live here, next to each other.

    Real-time: every Read* function and ChannelDataBytes is pure, branch-local
    and allocation-free, and so usable from the audio thread. The Append*
    functions push onto a std::string and therefore allocate whenever it has to
    grow; a caller on the audio thread must reserve first (gSeq does, in its
    constructor).

  ==============================================================================
*/

#ifndef YSE_MIDI_MIDIBYTES_HPP
#define YSE_MIDI_MIDIBYTES_HPP

#include <cstddef>
#include <cstdint>
#include <string>

namespace YSE {
  namespace MIDI {

    // ── the format's own marker bytes ────────────────────────────────────────

    /** Prefix of a meta event: FF type length data... */
    constexpr unsigned char META_PREFIX = 0xFF;
    /** Meta type 2F — the last event of every well-formed MTrk chunk. */
    constexpr unsigned char META_END_OF_TRACK = 0x2F;
    /** Meta type 51 — set tempo, three bytes of microseconds per quarter note. */
    constexpr unsigned char META_TEMPO = 0x51;
    /** System-exclusive message: F0 length data... */
    constexpr unsigned char SYSEX_BEGIN = 0xF0;
    /** The escape / continuation form of the above: F7 length data... */
    constexpr unsigned char SYSEX_ESCAPE = 0xF7;

    // ── reading ──────────────────────────────────────────────────────────────

    /** Big-endian 16-bit read at `at`. The caller keeps the cursor: nothing here
        knows how many bytes are left, so the two bytes must already be known to
        be in range. */
    inline std::uint16_t ReadU16BE(const unsigned char* at) {
      return (std::uint16_t)(((std::uint16_t)at[0] << 8) | (std::uint16_t)at[1]);
    }

    /** Big-endian 32-bit read at `at`, on the same terms as ReadU16BE. */
    inline std::uint32_t ReadU32BE(const unsigned char* at) {
      return ((std::uint32_t)at[0] << 24) | ((std::uint32_t)at[1] << 16) |
             ((std::uint32_t)at[2] << 8) | (std::uint32_t)at[3];
    }

    /**
     *  A variable-length quantity: seven bits per byte, the high bit set on all
     *  but the last. `at` is advanced past the bytes consumed — including on
     *  failure, since the value that was there is unreadable either way.
     *
     *  Four bytes is the format's own limit, so a corrupt file cannot send this
     *  walking off the end looking for a terminator: it stops either at `end` or
     *  after the fourth byte, whichever comes first. `out` is written only on
     *  success.
     */
    inline bool ReadVarLen(const unsigned char* data, std::size_t& at, std::size_t end,
                           std::uint32_t& out) {
      std::uint32_t value = 0;
      for (int i = 0; i < 4; i++) {
        if (at >= end) return false;
        const unsigned char byte = data[at];
        at++;
        value = (value << 7) | (std::uint32_t)(byte & 0x7F);
        if ((byte & 0x80) == 0) {
          out = value;
          return true;
        }
      }
      return false;
    }

    /** How many data bytes follow a channel status byte: two for everything
        except program change (C0) and channel pressure (D0), which take one. */
    inline std::size_t ChannelDataBytes(unsigned char status) {
      const unsigned char kind = status & 0xF0;
      return (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
    }

    // ── writing ──────────────────────────────────────────────────────────────
    //
    // The inverses of the above. These append, so they allocate when `out` has
    // to grow — see the real-time note at the top of the file.

    inline void AppendByte(std::string& out, unsigned char value) {
      out.push_back((char)value);
    }

    inline void AppendU16BE(std::string& out, std::uint16_t value) {
      AppendByte(out, (unsigned char)((value >> 8) & 0xFF));
      AppendByte(out, (unsigned char)(value & 0xFF));
    }

    inline void AppendU32BE(std::string& out, std::uint32_t value) {
      AppendByte(out, (unsigned char)((value >> 24) & 0xFF));
      AppendByte(out, (unsigned char)((value >> 16) & 0xFF));
      AppendByte(out, (unsigned char)((value >> 8) & 0xFF));
      AppendByte(out, (unsigned char)(value & 0xFF));
    }

    /** The other direction of ReadVarLen. Clamped to the four-byte maximum the
        format allows: a value longer than that cannot be spelled at all, and one
        shortened is better than a file no reader will take. */
    inline void AppendVarLen(std::string& out, std::uint32_t value) {
      if (value > 0x0FFFFFFF) value = 0x0FFFFFFF;
      unsigned char buffer[4];
      std::size_t written = 0;
      buffer[written] = (unsigned char)(value & 0x7F);
      written++;
      value >>= 7;
      while (value != 0) {
        buffer[written] = (unsigned char)((value & 0x7F) | 0x80);
        written++;
        value >>= 7;
      }
      while (written > 0) {
        written--;
        AppendByte(out, buffer[written]);
      }
    }

  } // namespace MIDI
} // namespace YSE

#endif // YSE_MIDI_MIDIBYTES_HPP
