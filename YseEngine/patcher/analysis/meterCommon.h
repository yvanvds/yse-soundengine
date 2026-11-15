#pragma once

// Helpers shared by ~meter, ~levelmeter and ~gridmeter (issue #1166):
// message tokens, the update-interval limits Max gives its meters, and the
// number formatting of their GUI values. Internal to those three objects.

#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pSelector.h"

namespace YSE {
  namespace PATCHER {
    namespace meterCommon {

      // Max's meters update every 10 ms to 2 s.
      constexpr float MIN_INTERVAL = 10.f;
      constexpr float MAX_INTERVAL = 2000.f;

      inline float Interval(float ms) {
        if (std::isnan(ms) || ms < MIN_INTERVAL) return MIN_INTERVAL;
        return ms > MAX_INTERVAL ? MAX_INTERVAL : ms;
      }

      // The token starting at or after `from` in `text`, walked in place so
      // nothing allocates on whichever thread delivers the message. `next` is
      // set to where the token ends. Empty when there is none.
      inline std::string_view Token(const std::string& text, std::size_t from, std::size_t& next) {
        const char* chars = text.c_str();
        const std::size_t size = text.size();
        std::size_t begin = from;
        while (begin < size && IsSelectorSeparator(chars[begin]))
          begin++;
        std::size_t end = begin;
        while (end < size && !IsSelectorSeparator(chars[end]))
          end++;
        next = end;
        return std::string_view(chars + begin, end - begin);
      }

      // The number in the token after `from`, false when there is none or it
      // is NaN.
      inline bool Number(const std::string& text, std::size_t from, float& out) {
        std::size_t next = 0;
        const std::string_view token = Token(text, from, next);
        float number = 0.f;
        if (!ReadNumericToken(token.data(), token.size(), number) || std::isnan(number))
          return false;
        out = number;
        return true;
      }

      // A GUI value cell. Host thread.
      inline std::string Cell(float value) {
        char text[kExprValueTextMax];
        const int written = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
        return written > 0 ? std::string(text, static_cast<std::size_t>(written))
                           : std::string("0.");
      }

    } // namespace meterCommon
  } // namespace PATCHER
} // namespace YSE
