#pragma once
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string>
#include "../headers/types.hpp"

namespace YSE {
  namespace PATCHER {

    /** @brief Longest token ``ReadNumericToken`` will read as a number. */
    constexpr std::size_t NUMBER_TEXT_MAX = 63;

    /**
     *  @brief True when the whole of the @p length characters at @p text is one
     *         finite number, which is then written to @p out.
     *
     *  The strict counterpart of ``ExprParseFloatList``, and the difference is
     *  the point: this answers *"is this token a number?"*, where the expression
     *  reader answers *"give me the numbers in this text"*. The expression
     *  reader **skips** what it cannot read, so ``5abc`` comes back as 5, and it
     *  folds a non-finite result to **0**, so ``1e999`` comes back as a plain
     *  zero. Both are right for a list of thresholds and wrong for deciding what
     *  kind of thing a creation argument is: the first turns the symbol ``5abc``
     *  into the number 5, and the second turns ``inf`` into a value that
     *  collides with every real 0 a patch sends.
     *
     *  So a token that is only *partly* a number is not a number, and neither is
     *  one that reads as a NaN or an infinity (including by overflow) — those
     *  are more useful as the symbols they were typed as.
     *
     *  Written for ``.sel`` (#465) and shared with ``.trigger`` (#466), which
     *  needs the same yes/no answer to tell a format letter from a constant.
     *  Real-time properties match the rest of this header: one bounded copy into
     *  a stack buffer and one ``strtof`` — no allocation, no exception, and the
     *  same locale exposure ``ExprParseFloatList`` already has.
     */
    inline bool ReadNumericToken(const char* text, std::size_t length, float& out) {
      if (length == 0 || length > NUMBER_TEXT_MAX) return false;

      char buffer[NUMBER_TEXT_MAX + 1];
      for (std::size_t i = 0; i < length; i++)
        buffer[i] = text[i];
      buffer[length] = '\0';

      char* end = nullptr;
      const float parsed = std::strtof(buffer, &end);

      // The number has to *be* the token: strtof stops at the first character it
      // cannot use, so without this "5abc" and "5e" would both read as 5.
      if (end != buffer + length) return false;

      // strtof also accepts "inf" and "nan", and overflows to infinity.
      if (!std::isfinite(parsed)) return false;

      out = parsed;
      return true;
    }

    /** @brief ``ReadNumericToken`` over a whole ``std::string`` token. */
    inline bool ReadNumericToken(const std::string& token, float& out) {
      return ReadNumericToken(token.c_str(), token.size(), out);
    }

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

    /**
     *  @brief True when @p text begins with the message word @p word, leaving
     *         @p argOffset at the first character after it.
     *
     *  The fiddly half of reading a ``<word> <number>`` message, and the half
     *  that is easy to get subtly wrong. The word has to *end* where it ends:
     *  a bare prefix comparison accepts ``address 5`` as ``add 5``, because
     *  every reader in this family steps over tokens it cannot parse and would
     *  quietly skip the leftover ``ress`` before taking the 5. So a separator
     *  is required after the word — which also means the bare word on its own
     *  does not match, since a message word with no argument is not the same
     *  message.
     *
     *  Deliberately stops at the word rather than going on to read the number:
     *  the argument's type differs per caller. Pair it with ``ReadIntArgAt``
     *  for an int argument (``seed 42``) or with ``ExprParseFloatList`` for a
     *  float one, both of which skip the leading whitespace themselves. No
     *  allocation, no locale, no exception — the same real-time properties as
     *  the readers above, because list handlers run on whichever thread sent
     *  the message.
     *
     *  Added for ``.peak`` / ``.trough`` (#463), whose ``set <n>`` reseeds the
     *  running extreme without emitting.
     */
    inline bool MatchWord(const std::string& text, const char* word, std::size_t wordLength,
                          std::size_t& argOffset) {
      // Comparing the length first keeps compare() from being asked about a
      // range the string does not have, and rejects the bare word in one go.
      if (text.size() <= wordLength) return false;
      if (text.compare(0, wordLength, word) != 0) return false;

      const char separator = text[wordLength];
      if (separator != ' ' && separator != '\t') return false;

      argOffset = wordLength + 1;
      return true;
    }

    /** @brief Most values ``FormatIntList`` will write; longer lists are cut. */
    constexpr int FORMAT_LIST_MAX = 8;

    // Widest a 32-bit int prints is 11 characters ("-2147483648"), and every
    // value but the first also costs one separator.
    constexpr int FORMAT_INT_WIDTH = 11;

    /**
     *  @brief Writes @p value as decimal into @p out and returns how many
     *         characters that took. At most ``FORMAT_INT_WIDTH``.
     *
     *  The write half of ``ReadIntArgAt``, and hand-rolled for the same reason:
     *  it neither allocates nor reads locale state, so it is safe on whichever
     *  thread a message handler happens to run on.
     */
    inline std::size_t WriteInt(int value, char* out) {
      char digits[FORMAT_INT_WIDTH];
      int n = 0;
      // Take the magnitude in 64 bits: negating INT_MIN as an int is undefined,
      // and its magnitude is not representable as one.
      I64 magnitude = value;
      const bool negative = magnitude < 0;
      if (negative) magnitude = -magnitude;
      do {
        digits[n++] = static_cast<char>('0' + (magnitude % 10));
        magnitude /= 10;
      } while (magnitude != 0);

      std::size_t written = 0;
      if (negative) out[written++] = '-';
      while (n > 0)
        out[written++] = digits[--n];
      return written;
    }

    /**
     *  @brief ``"out0"``, ``"out1"``, ... — the documentation label of outlet
     *         @p index on an object whose outlets are built from its arguments.
     *
     *  Only objects whose outlet *count* is a creation argument need this;
     *  everywhere else the label is a literal in the constructor. Routed
     *  through ``WriteInt`` rather than ``std::to_string`` because the patcher
     *  has one way of turning an int into text and this is it — control-thread
     *  only either way, since ``SetDoc`` is.
     *
     *  Written for ``.trigger`` (#466) and shared with ``.bangbang`` (#467),
     *  which labels its outlets the same way for the same reason.
     */
    inline std::string OutletLabel(int index) {
      char digits[FORMAT_INT_WIDTH];
      const std::size_t written = WriteInt(index, digits);
      return "out" + std::string(digits, written);
    }

    /**
     *  @brief Formats the first @p count of @p values as a space-separated
     *         list — the text form the patcher's list outlets carry.
     *
     *  Built once into a stack buffer and handed to ``std::string`` in one go,
     *  rather than concatenating ``std::to_string`` results, which materialises
     *  a temporary per term. A short list (the three-number transition entry
     *  ``.anal`` and ``.prob`` trade in) fits small-string optimisation and so
     *  costs no allocation at all; a long one costs exactly one. Still not a
     *  path for the audio callback — a list outlet hands a ``std::string`` on —
     *  but it is the cheapest honest way to build one.
     *
     *  Added for ``.anal`` (#457), which emits a list per number it receives;
     *  ``.histo`` (#458) emits the same shape with two values.
     */
    inline std::string FormatIntList(const int* values, int count) {
      if (count > FORMAT_LIST_MAX) count = FORMAT_LIST_MAX;
      char buffer[FORMAT_LIST_MAX * (FORMAT_INT_WIDTH + 1)];
      std::size_t length = 0;
      for (int i = 0; i < count; ++i) {
        if (i > 0) buffer[length++] = ' ';
        length += WriteInt(values[i], buffer + length);
      }
      return std::string(buffer, length);
    }

  } // namespace PATCHER
} // namespace YSE
