#pragma once
#include "../pObject.h"
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Route a complete message by what its first item matches, leaving
     *         that item on the message — ``.routepass`` (issue #483).
     *
     *  Max's ``routepass``, whose one-line summary is "Route a complete
     *  incoming message based on input matching": one outlet per creation
     *  argument plus a rightmost outlet for everything that matched none of
     *  them, and — the whole of the object — it "does not strip off the matched
     *  portion of the message".
     *
     *  ### Why an object exists for this
     *
     *  Stripping is what makes ``route`` a *dispatcher*: it turns ``note 60
     *  100`` into ``60 100`` on the note branch, so the branch works in bare
     *  values and never sees the word that got it there. That is right until
     *  the downstream object needs the tag as well — a ``.s`` that re-tags the
     *  message, a ``.m`` that echoes it, a second ``.routepass`` further down
     *  that has to match on the same word, an outlet wired back into a bank
     *  keyed by name. With a stripping ``route`` the patch has to put the word
     *  back on, which means knowing the word at the far end of the cord, in a
     *  second place, where it can disagree with the first. This object is the
     *  answer to that: the branch is chosen by the tag and the tag stays.
     *
     *  ### Its relatives
     *
     *  ``.sel`` matches the same way and sends a **bang**: the outlet's
     *  position is the whole answer and the value is dropped. Use it to *test*.
     *
     *  ``.route`` is the stripping form — or is meant to be. As this is
     *  written it forwards the whole message too, which is a defect in it and
     *  not a fact about this object (issue #672). When it is fixed the pair is
     *  Max's pair; until then this is the one whose *contract* says the message
     *  arrives intact, which is what a patch can rely on.
     *
     *  ``.split`` routes by numeric *range* rather than by a match, and
     *  ``.gate``/``.switch``/``.router`` route by state the object holds rather
     *  than by anything in the message. Here the message chooses its own
     *  destination and nothing about the object changes as it does.
     *
     *  ### The shape
     *
     *  Max: "The number of arguments determines the number of outlets, in
     *  addition to the rightmost outlet." So ``.routepass note ctl`` has three
     *  outlets, and a bare ``.routepass`` has one — the rightmost — through
     *  which everything passes unchanged. That degenerate object is not useful
     *  and it is also not an error, so it is built rather than papered over
     *  with an invented default selector; Max documents one for ``select`` and
     *  none here, and inventing a ``0`` outlet would put a branch in a patch
     *  that the patch did not ask for.
     *
     *  The outlets are built in the parameter callbacks, the pattern ``.route``,
     *  ``.sel`` and ``.gate`` already use, which is why a live ``SetParams``
     *  that changes the argument count goes through the structural-rebuild path
     *  of #234 rather than resizing a published object.
     *
     *  ### What matches what
     *
     *  The matcher is ``.sel``'s, deliberately — an object whose only job is to
     *  branch must branch the same way as the other object whose only job is to
     *  branch, or a patch that swaps one for the other changes meaning
     *  silently. A selector is a **number** or a **symbol**, decided once when
     *  the argument is read, and the two never match each other:
     *
     *  - a number matches a numeric selector by value, with an ``int`` widened
     *    to a ``float``, so ``.routepass 5`` answers both the int 5 and the
     *    float 5.0. Max's int/float duality is not reproduced, for the reason
     *    ``.sel``, ``.accum`` and ``.past`` set out: ``.routepass 5`` and
     *    ``.routepass 5.0`` are the same parameter string in this patcher.
     *  - a symbol matches a symbolic selector by exact text.
     *  - a **bang** matches a selector spelled ``bang``, ``.sel``'s rule for
     *    Max's "The bang message matches a 'bang' symbol in the arguments".
     *  - a **list** is matched on its first element alone, and that element is
     *    a number if it reads as one and a symbol otherwise — in Max the first
     *    element of ``5 6`` *is* an int, and this patcher carries lists as
     *    text.
     *
     *  The comparison is exact, with the consequences ``.sel`` documents: a
     *  computed float may miss a selector it looks equal to, and a NaN matches
     *  nothing and leaves the rightmost outlet.
     *
     *  A selector repeated in the argument list sends out the **leftmost** of
     *  its outlets only, Max's rule for ``select`` and the only sane reading
     *  here: an object that fired both would fan out, and this one routes.
     *
     *  ### What comes out where
     *
     *  Whatever arrived, in the kind it arrived as and spelled as it was
     *  spelled. A matched list leaves as the same list, first item included; a
     *  matched int as that int; a matched bang as a bang. The rightmost outlet
     *  does the same for everything that matched nothing, so a chain of
     *  ``.routepass`` objects strings together with each reject outlet feeding
     *  the next inlet — and because nothing is consumed, every object in the
     *  chain sees the message the first one saw.
     *
     *  Exactly one outlet fires per input.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlet, and one
     *  that emitted would route again on every DSP tick after the inlet fired —
     *  the rule ``.route``, ``.sel``, ``.trigger`` and ``.router`` establish.
     *
     *  No path allocates, locks or blocks. Matching is a bounded walk over at
     *  most ``MAX_SELECTORS`` entries doing a float compare or a
     *  length-checked ``std::string::compare`` against a character range — no
     *  ``substr``, no ``std::to_string``, no locale — and exactly one ``Send``
     *  follows. The list's leading token is classified by the shared,
     *  allocation-free ``ReadNumericToken`` in ``pListArgs.h``. Forwarding is
     *  by reference: the string a matched list is sent as is the one that
     *  arrived, so the pass-through costs nothing the stripping form would not
     *  also have cost. The selector table is built on the control thread by the
     *  parameter callbacks and is never written by a message handler.
     */
    PATCHER_CLASS(gRoutePass, YSE::OBJ::G_ROUTEPASS)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(SetBang)
    _INT_IN(SetInt)
    _FLOAT_IN(SetFloat)
    _LIST_IN(SetList)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief Most selectors — and so at most ``MAX_SELECTORS + 1`` outlets.
     *
     *  256, the ceiling ``.sel``, ``.trigger``, ``.spray``, ``.funnel``,
     *  ``.decode`` and ``.router`` already use. Max documents no limit, but
     *  every selector costs an outlet; arguments past the ceiling are dropped.
     */
    static constexpr int MAX_SELECTORS = 256;

    /**
     *  @brief How many selectors the object matches against — one fewer than
     *         ``NumOutputs()``, which counts the rightmost outlet too.
     *
     *  Zero for a bare ``.routepass``, which is Max's shape for no arguments
     *  and passes everything through the one outlet it has.
     */
    int SelectorCount() const {
      return (int)selectors.size();
    }

    /** @brief Whether selector @p index is a number rather than a symbol.
     *         False for an index out of range. */
    bool SelectorIsNumber(int index) const;

    /** @brief The value of numeric selector @p index, or 0 when the index is
     *         out of range or the selector is a symbol. */
    float SelectorValue(int index) const;

    /** @brief The text of symbolic selector @p index, or "" when the index is
     *         out of range or the selector is a number. */
    std::string SelectorText(int index) const;

  private:
    // One creation argument, resolved once. `numeric` decides which of the two
    // other fields means anything, and a numeric selector never matches a
    // symbol or the other way round. The same three fields `.sel` resolves its
    // arguments into, because the two objects have to agree on what a selector
    // is.
    struct Selector {
      std::string text;
      float value;
      bool numeric;
    };

    // Index of the selector @p value matches, or -1. Leftmost wins, which is
    // Max's rule for a repeated argument.
    int MatchNumber(float value) const;

    // Index of the symbolic selector whose text is exactly the @p length
    // characters at @p text, or -1. Takes a range rather than a std::string so
    // the leading token of a list can be matched without a substr on whichever
    // thread the message arrived on.
    int MatchSymbol(const char* text, std::size_t length) const;

    // Rebuild the outlets from the current selector table, docs included.
    // Control thread only: called from the constructor and from the parameter
    // callbacks, all of which run before the object is wired or published.
    void ShapePorts();

    // The creation argument, as tokens. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler.
    std::vector<std::string> selectorArgs;

    // The resolved selectors. Sized by ParseParams() / ClearParams() before the
    // object is published and never resized afterwards, so a handler's walk
    // over it cannot race a reallocation; no message handler writes to it at
    // all, since this object has no settable inlet.
    std::vector<Selector> selectors;
  };
}
}
