#pragma once
// Reading a MIDI message off a patcher list (issue #748).
//
// The patcher has, for historical reasons, two spellings of "a list of MIDI
// bytes", and `.midiout` is the object both of them arrive at:
//
//   * **binary** — the older senders (`.noteon`, `.noteoff`, `.controlchange`,
//     `.polypressure`, `.channelpressure`, `.programchange`, `.bendout` and
//     the `.x*out` family) build a three-character `std::string` whose
//     *characters are the bytes*;
//   * **numeric text** — `.midiformat` (#530), `.sxformat` (#531) and `.seq`
//     spell a byte as its decimal number, `144 60 100`, which is what a list
//     is everywhere else in the patcher and the only spelling that survives a
//     byte of 0x20 or 0x00 intact.
//
// Rather than pick one and break the patches built on the other, `.midiout`
// tells them apart, and this is the test. It is unambiguous in practice: every
// message the binary senders build starts with a status byte, which is 0x80 or
// above and therefore never a decimal digit, so a binary message can never
// read as a numeric list — nor a numeric list as a binary message, its first
// character being a digit.
//
// Real-time: one walk of the characters through `ReadIntArgAt`, into a buffer
// the caller owns. No allocation, no locale, no exception — a list handler runs
// on whichever thread sent the message, routinely the audio callback.
#include "../pListArgs.h"

#include <cstddef>
#include <string>

namespace YSE {
  namespace PATCHER {

    /** @brief Bytes of one MIDI message read off a list.
     *
     *  Sized after the longest message the patcher itself can build, which is
     *  `.sxformat`'s 256-token template (`mSysEx.h`'s `MAX_BYTES`), so nothing
     *  a patch can assemble is refused for length. */
    constexpr int MIDI_BYTE_LIST_MAX = 256;

    /** @brief Largest value a MIDI byte can have. */
    constexpr int MIDI_BYTE_VALUE_MAX = 255;

    /** @brief What ``ReadMidiByteList`` made of the text it was given. */
    enum class midiByteList {
      /** Not the numeric spelling — the caller should read the text as raw
          characters, which is what the binary senders send. */
      characters,
      /** A numeric byte list; the bytes are in the caller's buffer. */
      numeric,
      /** The numeric spelling, but not a message: a number outside 0-255, or
          more bytes than the buffer holds. Refused rather than reread as
          characters (its digits are not bytes) and rather than sent in part
          (half a system-exclusive dump is worse at the device than none). */
      refused
    };

    /**
     *  @brief Reads @p text as a numeric MIDI byte list into @p out.
     *
     *  The whole of @p text has to be whitespace-separated decimal integers,
     *  at least one of them, and nothing else — a single non-numeric token
     *  makes it ``characters``, which is how a binary three-byte message
     *  (whose first character is a status byte, never a digit) is recognised.
     *  @p count is the number of bytes written, and is 0 for every result but
     *  ``numeric``.
     *
     *  @param out    caller's buffer, at least @p max bytes.
     *  @param max    its size; ``MIDI_BYTE_LIST_MAX`` unless there is a reason.
     */
    inline midiByteList ReadMidiByteList(const std::string& text, unsigned char* out, int max,
                                         int& count) {
      count = 0;
      std::size_t cursor = 0;
      int number = 0;
      bool refused = false;

      while (ReadIntArgAt(text, cursor, number)) {
        if (number < 0 || number > MIDI_BYTE_VALUE_MAX || count >= max) {
          // Keep reading rather than stopping, so that the cursor still reaches
          // the end of the text and a list that is numeric all through is not
          // mistaken for one with a symbol in it.
          refused = true;
          continue;
        }
        out[count] = static_cast<unsigned char>(number);
        count++;
      }

      while (cursor < text.size() && (text[cursor] == ' ' || text[cursor] == '\t'))
        cursor++;

      // Something the integer reader could not take: a symbol, or a byte with
      // its top bit set. Either way this is not the numeric spelling.
      if (cursor != text.size() || (count == 0 && !refused)) {
        count = 0;
        return midiByteList::characters;
      }

      if (refused) {
        count = 0;
        return midiByteList::refused;
      }
      return midiByteList::numeric;
    }

  } // namespace PATCHER
} // namespace YSE
