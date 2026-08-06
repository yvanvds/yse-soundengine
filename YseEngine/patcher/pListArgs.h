#pragma once
#include <cstddef>
#include <string>
#include "../headers/types.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Reads a decimal integer out of @p text starting at @p offset.
     *
     *  Returns false when there is no integer there, leaving @p out untouched;
     *  saturates at the ``int`` limits rather than wrapping on overflow.
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
    inline bool ReadIntArg(const std::string& text, std::size_t offset, int& out) {
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
      while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        value = (value * 10) + (text[i] - '0');
        if (value > 0x7FFFFFFFLL) {
          value = 0x7FFFFFFFLL;
          break;
        }
        i++;
      }
      out = static_cast<int>(negative ? -value : value);
      return true;
    }

  } // namespace PATCHER
} // namespace YSE
