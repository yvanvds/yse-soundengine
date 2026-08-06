#pragma once
#include <cstddef>
#include <string>
#include "../headers/types.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Reads a decimal integer out of @p text starting at @p offset, and
     *         advances @p offset past it.
     *
     *  Returns false when there is no integer there, leaving @p out and
     *  @p offset untouched; saturates at the ``int`` limits rather than
     *  wrapping on overflow.
     *
     *  Hand-rolled on purpose. List handlers run synchronously on whichever
     *  thread sent the message, so ``seed 42`` may well be parsed on the audio
     *  thread. ``std::stoi`` constructs a ``std::string`` for its argument (an
     *  allocation) and throws on malformed input; ``strtol`` reads locale
     *  state, which another thread may be mutating. This touches neither, has
     *  no failure path that throws, and runs in time linear in the digits.
     *
     *  Extracted from ``.drunk`` (#453) when ``.urn`` (#454) needed the same
     *  reader; the rest of the patcher's random family parses its ``seed`` /
     *  ``clear`` arguments through here too.
     */
    inline bool ReadIntArgAt(const std::string& text, std::size_t& offset, int& out) {
      std::size_t i = offset;
      while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
        i++;

      bool negative = false;
      if (i < text.size() && (text[i] == '-' || text[i] == '+')) {
        negative = (text[i] == '-');
        i++;
      }
      if (i >= text.size() || text[i] < '0' || text[i] > '9') return false;

      I64 value = 0;
      bool saturated = false;
      while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        if (!saturated) {
          value = (value * 10) + (text[i] - '0');
          if (value > 0x7FFFFFFFLL) {
            value = 0x7FFFFFFFLL;
            saturated = true;
          }
        }
        // Keep consuming digits even once the value has saturated, so that the
        // cursor a multi-value read hands on still points *past* the number
        // rather than into the middle of it.
        i++;
      }
      out = static_cast<int>(negative ? -value : value);
      offset = i;
      return true;
    }

    /** @brief ``ReadIntArgAt`` for callers that do not need the cursor back. */
    inline bool ReadIntArg(const std::string& text, std::size_t offset, int& out) {
      return ReadIntArgAt(text, offset, out);
    }

    /**
     *  @brief Reads up to @p max whitespace-separated integers out of @p text,
     *         starting at @p offset, into @p out. Returns how many were read.
     *
     *  Stops at the first thing that is not an integer, so a caller can tell a
     *  three-number list apart from a two-number one — and from a longer one, by
     *  asking for one more than it wants. Same real-time properties as
     *  ``ReadIntArgAt``: no allocation, no locale, no exception, and time linear
     *  in the characters consumed.
     *
     *  Added for ``.prob`` (#456), whose transition entries arrive as the list
     *  ``<from> <to> <weight>``; ``.anal`` (#457) reads the same shape.
     */
    inline int ReadIntList(const std::string& text, std::size_t offset, int* out, int max) {
      int count = 0;
      std::size_t cursor = offset;
      while (count < max && ReadIntArgAt(text, cursor, out[count]))
        count++;
      return count;
    }

  } // namespace PATCHER
} // namespace YSE
