#pragma once

#include <cmath>
#include <cstddef>
#include <cstring>
#include "../pListArgs.h"
#include "../pSelector.h"

namespace YSE {
  namespace PATCHER {
    namespace sfcues {

      // Reading the words of ~sfplay's and ~sflist's messages in place
      // (issue #1155): no copy, no allocation on whichever thread sends.

      /** @brief The word of @p text from @p pos: [begin, end), empty at the
       *         end. */
      inline void NextToken(const char* text, std::size_t length, std::size_t pos,
                            std::size_t& begin, std::size_t& end) {
        while (pos < length && IsSelectorSeparator(text[pos]))
          pos++;
        begin = pos;
        while (pos < length && !IsSelectorSeparator(text[pos]))
          pos++;
        end = pos;
      }

      /** @brief Whether [begin, end) of @p text is @p word. */
      inline bool Is(const char* text, std::size_t begin, std::size_t end, const char* word) {
        const std::size_t n = std::strlen(word);
        return end - begin == n && std::strncmp(text + begin, word, n) == 0;
      }

      /** @brief The word at [begin, end) as a finite number. */
      inline bool Number(const char* text, std::size_t begin, std::size_t end, float& out) {
        return begin < end && ReadNumericToken(text + begin, end - begin, out) &&
               std::isfinite(out);
      }

      /**
       *  @brief The arguments of ``preload <cue> [file] [start] [end]
       *         [direction] [speed]``, Max's order: the file is the word
       *         after the cue when it is not a number, and the numbers after
       *         it are the start and end in ms (or 0 to 1 for ``preloadn``),
       *         a direction flag (not 0: the cue is buffered for playing
       *         backward, issue #1288), and a speed.
       */
      struct preloadArgs {
        int cue = 0;
        std::size_t fileBegin = 0; // no file: fileBegin == fileEnd
        std::size_t fileEnd = 0;
        float start = 0.f;
        float end = 0.f;
        float speed = 1.f;
        bool reverse = false;
      };

      /** @brief Read @p text after its first word (which ends at @p pos).
       *         False without a cue number. */
      inline bool ParsePreload(const char* text, std::size_t length, std::size_t pos,
                               preloadArgs& out) {
        std::size_t begin = 0;
        std::size_t end = 0;
        NextToken(text, length, pos, begin, end);
        float number = 0.f;
        if (!Number(text, begin, end, number)) return false;
        out.cue = static_cast<int>(number);
        pos = end;
        NextToken(text, length, pos, begin, end);
        if (begin < end && !Number(text, begin, end, number)) {
          out.fileBegin = begin;
          out.fileEnd = end;
          pos = end;
        }
        float values[4] = {0.f, 0.f, 0.f, 1.f};
        for (float& value : values) {
          NextToken(text, length, pos, begin, end);
          float read = 0.f;
          if (!Number(text, begin, end, read)) break;
          value = read;
          pos = end;
        }
        out.start = values[0];
        out.end = values[1];
        out.reverse = values[2] != 0.f;
        out.speed = values[3];
        return true;
      }

    } // namespace sfcues
  } // namespace PATCHER
} // namespace YSE
