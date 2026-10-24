#pragma once

#include <cstddef>
#include <string>
#include "../pListArgs.h"
#include "../pSelector.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Reads ``synctrig <x>``, the sync threshold message of ``~saw``,
     *         ``~rect`` and ``~tri`` (issue #1055).
     *
     *  True, with the number in @p out, when @p message is ``synctrig``
     *  followed by a number. The text is walked in place: no allocation on
     *  whichever thread delivers the message.
     */
    inline bool ReadSyncTrig(const std::string& message, float& out) {
      const char* text = message.c_str();
      const std::size_t length = message.size();
      std::size_t begin = 0;
      while (begin < length && IsSelectorSeparator(text[begin]))
        begin++;
      std::size_t end = begin;
      while (end < length && !IsSelectorSeparator(text[end]))
        end++;
      if (end - begin != 8 || message.compare(begin, 8, "synctrig") != 0) return false;
      std::size_t arg = end;
      while (arg < length && IsSelectorSeparator(text[arg]))
        arg++;
      std::size_t argEnd = arg;
      while (argEnd < length && !IsSelectorSeparator(text[argEnd]))
        argEnd++;
      return ReadNumericToken(text + arg, argEnd - arg, out);
    }

  } // namespace PATCHER
} // namespace YSE
