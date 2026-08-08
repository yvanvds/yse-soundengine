#pragma once
#include "pListArgs.h"
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief The separators ``Parameters::Set`` and the list outlets use.
     *
     *  Hand-rolled rather than ``std::isspace``, which reads locale state
     *  another thread may be mutating and is undefined for a negative
     *  ``char``. List handlers run synchronously on whichever thread sent the
     *  message, so that is not a theoretical objection.
     */
    inline bool IsSelectorSeparator(char c) {
      return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    }

    /**
     *  @brief ``"match0"``, ``"match1"``, ... — the documentation label of the
     *         outlet a selector owns.
     *
     *  The sibling of ``OutletLabel`` / ``InletLabel`` in ``pListArgs.h``, for
     *  the objects whose outlets are selectors rather than plain numbered
     *  ports. Routed through ``WriteInt`` rather than ``std::to_string``
     *  because the patcher has one way of turning an int into text and this is
     *  it — control-thread only either way, since ``SetDoc`` is.
     */
    inline std::string MatchOutletLabel(int index) {
      char digits[FORMAT_INT_WIDTH];
      const std::size_t written = WriteInt(index, digits);
      return "match" + std::string(digits, written);
    }

    /**
     *  @brief The selector table ``.sel``, ``.route`` and ``.routepass`` branch
     *         on — one resolved creation argument per entry (issue #680).
     *
     *  ### Why it is one object
     *
     *  Three objects whose only job is to branch must branch the same way, or
     *  a patch that swaps one for another changes meaning silently. Each of
     *  the three used to carry its own byte-for-byte copy of the table, the
     *  two match functions and the leading-token walk; #672, which gave
     *  ``.route`` ``.sel``'s semantics, was the third copy and the reason to
     *  stop at three. The argument is written into each object's own
     *  documentation, so the code is now where the documentation already said
     *  it was.
     *
     *  What is *not* here is the part the three legitimately disagree on: how
     *  the argument list becomes selectors. ``.sel`` and ``.routepass`` skip
     *  empty tokens and stop at their ``MAX_SELECTORS`` ceiling; ``.route``
     *  keeps one entry per token, empty ones included, so an index into its
     *  ``list`` parameter is an index into its outlets. ``.sel`` and
     *  ``.route`` fall back to the single selector ``0`` when the argument
     *  list is empty, which is Max's no-argument case for ``select`` and
     *  ``route``; ``.routepass``, for which Max documents no default, ends up
     *  with none. Those loops stay in the objects. This holds the table and
     *  the matching, which is the part that has to agree.
     *
     *  ### What a selector is
     *
     *  A **number** or a **symbol**, decided once by ``Add`` when the argument
     *  is read, and the two never match each other. A token the strict
     *  ``ReadNumericToken`` accepts as one whole finite number is a numeric
     *  selector and matches by value; anything else is a symbol and matches by
     *  exact text. The comparison is exact — see ``.sel``'s header on why
     *  Max's ``fuzzy`` and ``matchfloat`` attributes are deliberately not
     *  ported, and on the two consequences that follow: a computed float may
     *  miss a selector it looks equal to, and a NaN matches nothing at all.
     *
     *  A repeated selector matches the **leftmost** of its entries, Max's rule
     *  for a repeated argument, which is what every ``Match*`` returning the
     *  first hit rather than the last gives.
     *
     *  ### Real-time behaviour
     *
     *  The table is built on the control thread by the parameter callbacks,
     *  before the object is published, and is never resized afterwards — so a
     *  message handler's walk over it cannot race a reallocation. ``SetValue``
     *  is the only write a handler makes (``.sel``'s cold inlet), and it is a
     *  plain float store into an existing entry.
     *
     *  No ``Match*`` allocates, locks, blocks or reads locale state: each is a
     *  bounded walk doing a float compare or a length-checked
     *  ``std::string::compare`` against a character range. Matching a list's
     *  leading token takes the range in place rather than a ``substr``, and
     *  classifies it through the allocation-free ``ReadNumericToken`` in
     *  ``pListArgs.h``. Removing ``std::to_string`` from this path was half of
     *  #672 and it must not come back: these handlers run on whichever thread
     *  sent the message, which may be the audio thread.
     */
    class SelectorTable {
    public:
      /** @brief Drop every selector. Control thread only. */
      void Clear() {
        selectors.clear();
      }

      /** @brief Reserve room for @p count selectors, so a parse loop that
       *         knows its token count allocates once. Control thread only. */
      void Reserve(std::size_t count) {
        selectors.reserve(count);
      }

      /**
       *  @brief Resolve @p token into a selector and append it.
       *
       *  Numeric when ``ReadNumericToken`` reads the whole token as one finite
       *  number, symbolic otherwise. Strict on purpose, as the rest of the
       *  family is: ``ExprParseFloatList`` would read ``5abc`` as 5 and fold
       *  ``1e999`` to 0, and neither answers "is this selector a number at
       *  all". Control thread only.
       */
      void Add(const std::string& token) {
        float number = 0.f;
        if (ReadNumericToken(token.c_str(), token.size(), number)) {
          selectors.push_back(Selector{token, number, true});
        } else {
          selectors.push_back(Selector{token, 0.f, false});
        }
      }

      /**
       *  @brief Replace the table with the single numeric selector ``0``.
       *
       *  Max's no-argument case for ``select`` and ``route``: "If there is no
       *  argument, there is only one other outlet, which is assigned the
       *  integer number 0." ``.routepass`` does not call this, deliberately —
       *  Max documents no default selector for it, and inventing one would put
       *  a branch in a patch that did not ask for a branch.
       */
      void ResetToZero() {
        selectors.clear();
        selectors.push_back(Selector{"0", 0.f, true});
      }

      /** @brief How many selectors the table holds. */
      std::size_t Size() const {
        return selectors.size();
      }

      /** @brief Whether the table holds no selectors at all. */
      bool Empty() const {
        return selectors.empty();
      }

      /** @brief The text selector @p index was spelled as — the token itself,
       *         numeric or not. @p index must be below ``Size()``; the callers
       *         are outlet-building loops over exactly that range. */
      const std::string& Text(std::size_t index) const {
        return selectors[index].text;
      }

      /** @brief Whether selector @p index is a number rather than a symbol.
       *         False for an index out of range. */
      bool IsNumber(int index) const {
        if (index < 0 || index >= (int)selectors.size()) return false;
        return selectors[index].numeric;
      }

      /** @brief The value of numeric selector @p index, or 0 when the index is
       *         out of range or the selector is a symbol. */
      float Value(int index) const {
        if (index < 0 || index >= (int)selectors.size()) return 0.f;
        if (!selectors[index].numeric) return 0.f;
        return selectors[index].value;
      }

      /** @brief The text of symbolic selector @p index, or "" when the index is
       *         out of range or the selector is a number. */
      std::string SymbolText(int index) const {
        if (index < 0 || index >= (int)selectors.size()) return std::string();
        if (selectors[index].numeric) return std::string();
        return selectors[index].text;
      }

      /**
       *  @brief Replace the value of numeric selector @p index, silently.
       *
       *  ``.sel``'s settable right inlet, and the only write a message handler
       *  makes to the table: a plain float store into an entry that already
       *  exists, so it neither allocates nor races the handler's own walk.
       *  Does nothing for an index out of range.
       */
      void SetValue(std::size_t index, float value) {
        if (index >= selectors.size()) return;
        selectors[index].value = value;
      }

      /**
       *  @brief Index of the numeric selector @p value matches, or -1.
       *
       *  Exact, and exactness is the object — see ``.sel``'s header on why
       *  Max's ``fuzzy`` attribute is not ported. A NaN on either side
       *  compares unequal, which is what sends it out the rightmost outlet.
       *  Leftmost wins, which is Max's rule for a repeated argument.
       */
      int MatchNumber(float value) const {
        for (std::size_t i = 0; i < selectors.size(); i++) {
          if (!selectors[i].numeric) continue;
          if (selectors[i].value == value) return (int)i;
        }
        return -1;
      }

      /**
       *  @brief Index of the symbolic selector whose text is exactly the
       *         @p length characters at @p text, or -1.
       *
       *  Takes a range rather than a ``std::string`` so a list's leading token
       *  can be matched without a ``substr`` on whichever thread the message
       *  arrived on. Leftmost wins.
       */
      int MatchSymbol(const char* text, std::size_t length) const {
        for (std::size_t i = 0; i < selectors.size(); i++) {
          if (selectors[i].numeric) continue;
          if (selectors[i].text.size() != length) continue;
          // Compared against the character range in place: a substr here would
          // allocate on whichever thread the message arrived on.
          if (selectors[i].text.compare(0, length, text, length) == 0) return (int)i;
        }
        return -1;
      }

      /**
       *  @brief Index of the selector a **bang** matches, or -1.
       *
       *  Max: "The bang message matches a 'bang' symbol in the arguments." A
       *  bang is a symbol here and nothing else, so this is ``MatchSymbol``
       *  over the four characters of the word.
       */
      int MatchBang() const {
        return MatchSymbol("bang", 4);
      }

      /**
       *  @brief Index of the selector the **first item** of @p message
       *         matches, or -1, leaving @p tokenEnd just past that item.
       *
       *  Only the first element is ever examined, which is Max's rule for all
       *  three objects: "if the first element in the list matches the object
       *  argument(s)". A leading token that reads as a finite number is a
       *  number — in Max the first element of the list ``5 6`` *is* an int,
       *  and this patcher carries lists as text — and anything else is a
       *  symbol.
       *
       *  @p tokenEnd is written whether or not there is a match, so a caller
       *  that has to strip the item (``.route``) has the offset the remainder
       *  starts at without walking the message twice. A message with no first
       *  element at all matches nothing and leaves @p tokenEnd at the end of
       *  the leading separators.
       *
       *  Allocation-free: the token is matched as a character range in place.
       */
      int MatchLeadingToken(const std::string& message, std::size_t& tokenEnd) const {
        std::size_t begin = 0;
        while (begin < message.size() && IsSelectorSeparator(message[begin]))
          begin++;
        std::size_t end = begin;
        while (end < message.size() && !IsSelectorSeparator(message[end]))
          end++;
        tokenEnd = end;

        if (end <= begin) return -1;

        const std::size_t length = end - begin;
        float number = 0.f;
        if (ReadNumericToken(message.c_str() + begin, length, number)) {
          return MatchNumber(number);
        }
        return MatchSymbol(message.c_str() + begin, length);
      }

      /**
       *  @brief ``MatchLeadingToken`` for callers that do not need the offset
       *         back — ``.sel``, which drops the remainder, and ``.routepass``,
       *         which forwards the message whole.
       *
       *  Wanting that offset is exactly what makes ``.route`` ``.route``.
       */
      int MatchLeadingToken(const std::string& message) const {
        std::size_t tokenEnd = 0;
        return MatchLeadingToken(message, tokenEnd);
      }

    private:
      // One creation argument, resolved once. `numeric` decides which of the
      // two other fields means anything, and a numeric selector never matches
      // a symbol or the other way round.
      struct Selector {
        std::string text;
        float value;
        bool numeric;
      };

      // Sized by the owning object's parameter callbacks on the control thread
      // before the object is published, and never resized afterwards.
      std::vector<Selector> selectors;
    };

  } // namespace PATCHER
} // namespace YSE
