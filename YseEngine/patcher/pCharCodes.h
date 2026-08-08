#pragma once
#include <cstddef>

namespace YSE {
  namespace PATCHER {

    /** @brief Highest code point there is — U+10FFFF. */
    constexpr unsigned int MAX_CODE_POINT = 0x10FFFFu;

    /** @brief Most bytes one code point takes in UTF-8. */
    constexpr std::size_t UTF8_MAX_BYTES = 4;

    /**
     *  @brief True when @p code is a character that can be written: inside
     *         Unicode's range and not half of a surrogate pair.
     *
     *  The exact set ``NextCodePoint`` will ever return from a well-formed
     *  sequence, which is what makes the two functions here a round trip: a code
     *  this accepts can be written by ``WriteCodePoint`` and read back by
     *  ``NextCodePoint`` as itself.
     */
    inline bool IsCodePoint(int code) {
      if (code < 0 || (unsigned int)code > MAX_CODE_POINT) return false;
      return code < 0xD800 || code > 0xDFFF;
    }

    /**
     *  @brief Reads the code point starting at @p cursor and advances @p cursor
     *         past it, stopping at @p end.
     *
     *  A well-formed UTF-8 sequence yields the one code point it spells.
     *  Anything that is not one — a stray continuation byte, a sequence
     *  truncated by @p end, an overlong encoding, a surrogate half, or a value
     *  above U+10FFFF — yields the *lead byte's own value*, 0-255, and the
     *  cursor advances by one so decoding resumes at the next byte.
     *
     *  That fallback is deliberate and documented on the objects that use it: a
     *  patcher message is bytes, nothing upstream promises an encoding, and an
     *  object that threw away what it could not decode would silently lose
     *  characters from a patch fed Latin-1 or raw binary. No allocation, no
     *  locale, no failure path.
     *
     *  Written for ``.spell`` (#492) and shared with ``.atoi`` (#493), which has
     *  to read a character exactly as ``.spell`` does or the two would disagree
     *  about what a message spells to.
     */
    inline unsigned int NextCodePoint(const unsigned char* bytes, std::size_t end,
                                      std::size_t& cursor) {
      const unsigned int lead = bytes[cursor];
      if (lead < 0x80u) {
        cursor++;
        return lead;
      }

      // How many continuation bytes the lead announces, and the bits it carries
      // itself. A continuation byte in lead position, and the 5- and 6-byte
      // forms UTF-8 has not permitted since 2003, fall through as their own
      // value.
      std::size_t extra = 0;
      unsigned int code = 0;
      if ((lead & 0xE0u) == 0xC0u) {
        extra = 1;
        code = lead & 0x1Fu;
      } else if ((lead & 0xF0u) == 0xE0u) {
        extra = 2;
        code = lead & 0x0Fu;
      } else if ((lead & 0xF8u) == 0xF0u) {
        extra = 3;
        code = lead & 0x07u;
      } else {
        cursor++;
        return lead;
      }

      if (cursor + extra >= end) {
        cursor++;
        return lead;
      }
      for (std::size_t k = 1; k <= extra; k++) {
        if ((bytes[cursor + k] & 0xC0u) != 0x80u) {
          cursor++;
          return lead;
        }
      }
      for (std::size_t k = 1; k <= extra; k++)
        code = (code << 6) | (bytes[cursor + k] & 0x3Fu);

      // Overlong encodings spell a code point that had a shorter form,
      // surrogates are not characters, and nothing above U+10FFFF exists. Each
      // of the three is a byte sequence that is *not* well-formed UTF-8, so each
      // takes the same route as any other malformed byte.
      static constexpr unsigned int kSmallest[4] = {0u, 0x80u, 0x800u, 0x10000u};
      if (code < kSmallest[extra] || code > MAX_CODE_POINT ||
          (code >= 0xD800u && code <= 0xDFFFu)) {
        cursor++;
        return lead;
      }

      cursor += extra + 1;
      return code;
    }

    /**
     *  @brief Writes @p code to @p out as UTF-8 and returns how many bytes that
     *         took. At most ``UTF8_MAX_BYTES``.
     *
     *  The inverse of ``NextCodePoint``, and written alongside it for that
     *  reason: what one reads the other writes back byte for byte, which is what
     *  makes ``.itoa`` into ``.atoi`` (#493) return the codes that went in. The
     *  caller must have accepted @p code with ``IsCodePoint`` first — a
     *  surrogate or an out-of-range value has no encoding, and inventing one
     *  would produce bytes ``NextCodePoint`` then reads back as something else.
     *
     *  No allocation, no locale, no exception — one bounded series of writes, so
     *  it is safe on whichever thread a message handler runs on.
     */
    inline std::size_t WriteCodePoint(unsigned int code, char* out) {
      if (code < 0x80u) {
        out[0] = (char)code;
        return 1;
      }
      if (code < 0x800u) {
        out[0] = (char)(0xC0u | (code >> 6));
        out[1] = (char)(0x80u | (code & 0x3Fu));
        return 2;
      }
      if (code < 0x10000u) {
        out[0] = (char)(0xE0u | (code >> 12));
        out[1] = (char)(0x80u | ((code >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (code & 0x3Fu));
        return 3;
      }
      out[0] = (char)(0xF0u | (code >> 18));
      out[1] = (char)(0x80u | ((code >> 12) & 0x3Fu));
      out[2] = (char)(0x80u | ((code >> 6) & 0x3Fu));
      out[3] = (char)(0x80u | (code & 0x3Fu));
      return 4;
    }

  } // namespace PATCHER
} // namespace YSE
