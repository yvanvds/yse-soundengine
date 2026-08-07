#pragma once
#include "../pObject.h"
#include "../math/gExprEval.h"
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Most elements a ``.match`` pattern may hold.
     *
     *  Max documents no limit, and this object has to state one because the
     *  pattern, the window of received values and the outgoing list are all
     *  fixed-capacity storage sized once at construction — that is what buys
     *  the "no allocation on a message path" guarantee. 256 is the ceiling
     *  ``.sel``, ``.trigger``, ``.past`` and ``.vexpr`` already use for a list,
     *  and a sequence longer than 256 values is a different problem than a
     *  matcher box. Elements past the ceiling are dropped.
     */
    constexpr int kMatchMaxPattern = 256;

    /**
     *  @brief Detect a sequence of values as it arrives — ``.match``
     *         (issue #472).
     *
     *  Max's ``match``: "watches an incoming stream of ints, floats, symbols,
     *  lists, or messages, and outputs the stream after it has met the
     *  specification of its arguments." The creation arguments are the pattern,
     *  and when the values arriving at the inlet spell it out the whole
     *  sequence leaves the outlet as one list.
     *
     *  This is pattern recognition **over time** rather than over a single
     *  value, which is the thing the patcher had no way to express: ``.sel``
     *  and ``.split`` answer one number at a time and ``.change`` and
     *  ``.togedge`` compare a number with the one before it, so recognising a
     *  played motif, a controller gesture or a trigger sequence meant a chain
     *  of counters and gates whose state was implicit in the wiring. Here it is
     *  a creation argument.
     *
     *  ### The wildcard
     *
     *  Max: "The word ``nn`` can be used as a wild card that will match any
     *  number." So ``.match 1 nn 3`` fires on 1, 5, 3 and on 1, -0.5, 3 — and
     *  what comes out is **what arrived**, ``1 5 3``, not the pattern. That is
     *  the whole reason the object emits a list rather than a bang: the
     *  wildcard positions carry the information the patch was listening for.
     *
     *  ``nn`` matches any *number*, and nothing else — not a symbol, not a
     *  non-finite value. See "What counts as a number" below.
     *
     *  ### A sliding window, not a reset-on-mismatch counter
     *
     *  The rule that separates a working matcher from a plausible one, and the
     *  one Max's reference leaves unstated. A mismatch does **not** throw away
     *  the values that caused it, because those values may be the start of the
     *  next candidate. Against ``.match 1 2 3`` the stream
     *
     *      1  2  1  2  3
     *
     *  **matches**: the third value ends one candidate and begins another at
     *  the same moment. An implementation that walks a cursor forward and
     *  resets it to 0 on a mismatch reports nothing here — it consumes the
     *  second 1 as a failed third element, then the second 2 as a failed first
     *  element, and by the time the 3 arrives it is back at the beginning. That
     *  is not an exotic input; it is what any repeated gesture looks like.
     *
     *  So the object keeps the **last N values received**, N being the pattern
     *  length, in a ring buffer, and after every value asks whether that window
     *  now spells the pattern. A match therefore ends wherever it ends, and
     *  every candidate starting position is tested at once rather than one
     *  chosen in advance. The cost is a bounded walk of at most N comparisons
     *  per value, with no allocation and no branch on history.
     *
     *  Fixed-capacity is what makes this legal on a message path: the window is
     *  ``kMatchMaxPattern`` slots, sized at construction and never grown, and
     *  the outgoing list is written into a string reserved at construction.
     *
     *  ### Matches do not overlap
     *
     *  On a match the window is emptied, so the values that were consumed
     *  cannot also serve as the beginning of the next one. ``.match 1 1 1`` fed
     *  five 1s fires **once**, on the third, not three times; ``.match nn nn``
     *  fed 1, 2, 3 sends ``1 2`` and then waits for a second value. That is the
     *  same erasure Max's ``clear`` performs by hand — "causes match to forget
     *  all numbers it has received up to that time" — and it is what makes the
     *  outlet a stream of *events* rather than of every window that happens to
     *  fit.
     *
     *  ### What counts as a number
     *
     *  A value that is not a number occupies a position in the stream and
     *  matches nothing there, so it breaks any candidate it lands in. Three
     *  kinds of input are not numbers:
     *
     *  - a **symbol** — a token in a list message that does not read as a
     *    number in its entirety. The strict shared ``ReadNumericToken`` decides
     *    that, so ``5abc`` is a symbol rather than the number 5, which is why
     *    ``ExprParseFloatList`` (which skips what it cannot read) is not used
     *    on this path.
     *  - a **non-finite number** — an ``inf`` or a ``nan``, from either the
     *    text or the float inlet. The patcher's usual "read a non-finite as 0"
     *    substitution is deliberately not applied, for the reason ``.sel``
     *    gives: it would make a stray NaN complete a sequence a patch wired for
     *    a real 0. A NaN also compares unequal to everything including itself,
     *    so it could never match a literal in any case — what this rule settles
     *    is that it does not match the **wildcard** either.
     *  - an element of the pattern the object cannot look for, which is the
     *    mirror of the same rule; see below.
     *
     *  The comparison against a literal is **exact**, as ``.sel``'s and
     *  ``.change``'s are. A computed float may therefore miss a literal it
     *  looks equal to, because 0.1 + 0.2 is not 0.3 in binary floating point,
     *  and a ``.round`` upstream is the fix. An int is widened to a float and
     *  compared as one, so ``.match 5`` answers both the int 5 and the float
     *  5.0 — this patcher has a single numeric type and ``.match 5`` and
     *  ``.match 5.0`` are the same parameter string.
     *
     *  ### An argument that is neither a number nor ``nn``
     *
     *  Kept, as an element **nothing can match**, so the object goes silent.
     *  The alternative — dropping it — is worse in the way that matters here:
     *  the pattern's *length* is half of what the object means, so silently
     *  turning ``.match 1 NN 3`` (an uppercase typo) into ``.match 1 3`` would
     *  leave a box that fires on a sequence the patch never asked for and never
     *  fires on the one it did. A dead element fires on nothing, which is a
     *  symptom rather than a wrong answer.
     *
     *  A pattern of zero elements is the same case: a bare ``.match``, or a
     *  ``set`` with no list, has no sequence to detect and stays silent for
     *  good. It is not "matches everything immediately", which is the only
     *  other reading and would put an endless stream of empty lists on the
     *  outlet.
     *
     *  ### The two messages
     *
     *  ``clear`` empties the window — Max: "causes match to forget all numbers
     *  it has received up to that time" — without touching the pattern, and
     *  emits nothing. ``set <list>`` replaces the pattern, accepting ``nn`` in
     *  the same way the creation arguments do, and empties the window too,
     *  since a partial candidate against the old pattern means nothing against
     *  the new one.
     *
     *  ``set`` deliberately does **not** rewrite the creation argument, so a
     *  ``DumpJSON`` after one saves the pattern the object was *created* with —
     *  the same split ``.peak``'s ``set`` and its ``initial`` argument have,
     *  and the same one Max has between a typed-in argument and a message.
     *
     *  There is no ``bang`` inlet. Max routes bang through ``anything``, which
     *  "performs the same as ``list``", and a bang is a list of no values — it
     *  advances nothing, and an inlet that accepted it would only be a way of
     *  spelling "do nothing". A message carrying no number cannot advance a
     *  sequence, which is the rule ``.past`` states for the same reason.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing. The object is driven by its inlet, and an
     *  emitting Calculate() would report a sequence once per DSP block from a
     *  stimulus no patch sent — the rule ``.sel``, ``.next`` and ``.slide``
     *  establish.
     *
     *  Nothing on any message path allocates, locks or blocks. A value is one
     *  store into a fixed ring buffer and at most ``N`` float comparisons; the
     *  outgoing list is built with ``ExprFormatValue`` (no locale, no
     *  ``snprintf``, no ``std::to_string``) into a string reserved at
     *  construction; ``clear`` and ``set`` are matched with the shared
     *  ``MatchWord`` and read with the shared ``ReadNumericToken``, neither of
     *  which allocates. The window is emptied *before* the send, as
     *  ``.onebang``, ``.togedge`` and ``.next`` settle their state before
     *  theirs, because the send path is synchronous and re-entrant: a patch
     *  that loops the outlet back into the inlet re-enters here inside the
     *  ``SendList``, and must find the object already describing the sequence
     *  that is being reported.
     *
     *  ``Progress()`` is the one method that is *not* cheap — it is a
     *  worst-case ``O(N²)`` walk — and it is a control-thread accessor for the
     *  GUI and the tests, never called from a message handler.
     */
    PATCHER_CLASS(gMatch, YSE::OBJ::G_MATCH)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(SetInt)
    _FLOAT_IN(SetFloat)
    _LIST_IN(SetList)

    _PARM_CLEAR
    _PARM_PARSE

    _HAS_GUI

    /** @brief How many elements the pattern holds. 0 for a bare ``.match``,
     *         which never matches. At most ``kMatchMaxPattern``. */
    int PatternLength() const {
      return patternLength;
    }

    /** @brief Whether element @p index is the ``nn`` wildcard, which matches
     *         any number. False for an index out of range. */
    bool ElementIsWildcard(int index) const;

    /** @brief Whether element @p index is a literal number. False for the
     *         wildcard, for an unmatchable element, and out of range. */
    bool ElementIsLiteral(int index) const;

    /** @brief The value of literal element @p index, or 0 when the index is
     *         out of range or the element is not a literal. */
    float ElementValue(int index) const;

    /** @brief How many values the window is holding, 0 to ``PatternLength()``.
     *         Reset to 0 by a match, by ``clear`` and by ``set``. */
    int Held() const {
      return filled;
    }

    /**
     *  @brief How far into the pattern the stream currently is: the length of
     *         the longest **suffix** of what has been received that is also a
     *         **prefix** of the pattern.
     *
     *  The object's progress made observable, and the sliding window's
     *  restart property with it: against ``.match 1 2 3`` the stream 1, 2
     *  reports 2 and a third value of 1 reports **1**, not 0, because that 1
     *  is already the beginning of the next candidate.
     *
     *  Always strictly less than ``PatternLength()``: a full-length prefix
     *  match would have emitted and emptied the window.
     *
     *  Control-thread only — a worst-case ``O(N²)`` walk, computed on demand
     *  rather than maintained per value, so that nothing on the message path
     *  pays for it.
     */
    int Progress() const;

  private:
    // What one position of the pattern will accept.
    // NEVER is first so that the zero-initialised array below starts out full
    // of elements nothing can match, rather than full of literal zeroes that a
    // stray read past `patternLength` could match against.
    enum class Element {
      NEVER, // an argument that is neither a number nor `nn` — see the header
      LITERAL, // exactly this number
      WILDCARD, // Max's `nn`: any number, and only a number
    };

    // One received value. `number` is false for a symbol or a non-finite
    // value, which occupies its position in the stream and matches nothing.
    struct Slot {
      ExprValue value;
      bool number = false;
    };

    // Classify one pattern token. The single place `nn` is spelled.
    static Element ReadElement(const char* text, std::size_t length, float& out);

    // True when @p slot satisfies pattern element @p index.
    bool Accepts(int index, const Slot& slot) const;

    // The @p back-th most recent slot, 1 being the newest. Caller guarantees
    // 1 <= back <= filled.
    const Slot& Recent(int back) const;

    // Take one value into the window and report a completed sequence. The only
    // path that emits.
    void Push(const ExprValue& value, bool number, YSE::THREAD thread);

    // True when the window is full and spells the pattern.
    bool WindowMatches() const;

    // Send the window as a list and forget it. Only called when it matches.
    void Emit(YSE::THREAD thread);

    // Max's `clear`: forget every value received, keep the pattern.
    void Forget();

    // Replace the pattern from the tokens of @p text starting at @p offset,
    // and forget the window. Allocation-free: writes into the fixed array.
    void ReadPattern(const char* text, std::size_t length, std::size_t offset);

    // The creation argument, as tokens. Control thread only: written by
    // Parameters::Set, read by ParseParams(). A `set` message deliberately
    // does not touch it — see the header.
    std::vector<std::string> patternArgs;

    // The pattern. Fixed capacity so that `set` can replace it on whichever
    // thread the message arrived on without allocating.
    Element patternKind[kMatchMaxPattern] = {};
    float patternValue[kMatchMaxPattern] = {};
    int patternLength = 0;

    // The last `filled` values received, oldest first from `head`. `head` is
    // the next slot to write, so it is also the oldest once the ring is full.
    Slot window[kMatchMaxPattern];
    int head = 0;
    int filled = 0;

    // The outgoing list. Reserved at construction to the longest one the
    // object can produce, so building it never allocates.
    std::string result;
  };
}
}
