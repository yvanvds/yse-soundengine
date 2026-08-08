#pragma once
#include "../pObject.h"
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Route a message by what its first item matches, **consuming that
     *         item** — Max's ``route`` (issue #672).
     *
     *  Max: "If the first item of the message is the same as one of the
     *  arguments of route, the rest of the message is sent out the outlet that
     *  corresponds to that argument. If the first item does not match any of
     *  the arguments, the entire message is passed out the rightmost outlet."
     *  And, for the case where the selector was the whole message: "If the
     *  first item of a message matches one of the arguments, but the message
     *  has no additional items, bang is sent out the specified outlet."
     *
     *  ### The stripping is the object
     *
     *  Until #672 this object forwarded the *whole* message out the matched
     *  outlet, which is Max's ``routepass``, not Max's ``route`` — and the
     *  engine's own documentation had described the Max behaviour all along
     *  (``.sel``: "a list is matched on its first element alone and the rest is
     *  dropped — ``.route`` is the object that keeps the remainder"). Code and
     *  docs disagreed, and the code was the odd one out.
     *
     *  Stripping is what makes this a *dispatcher*: ``note 60 100`` becomes
     *  ``60 100`` on the note branch, so the branch works in bare values and
     *  never sees the word that got it there. The pass-through reading is not
     *  lost — it is ``.routepass`` (#483), whose contract guarantees it — so a
     *  patch that relied on the old behaviour swaps the object for that one.
     *
     *  ### What leaves a matched outlet
     *
     *  Whatever is left once the leading token is taken off, in the kind that
     *  remainder *is* — the patcher carries a list as text, so the kind has to
     *  be read back out of the text the way Max's parser reads an atom:
     *
     *  - ``.route foo`` + ``foo 1 2`` → the list ``1 2``.
     *  - ``.route foo`` + ``foo 1`` → the **int** 1, not a one-element list;
     *    ``foo 1.5`` → the **float** 1.5, decided by the spelling through the
     *    shared ``TokenLooksLikeFloat``, the same test ``.trigger`` and
     *    ``.match`` use.
     *  - ``.route foo`` + ``foo bar`` → the list ``bar``: a lone symbol has no
     *    type of its own in this patcher.
     *  - ``.route foo`` + ``foo`` → a **bang**. Nothing is left, and Max says
     *    so explicitly.
     *  - ``.route 5`` + the int 5 or the float 5.0 → a **bang**, for the same
     *    reason: the number *was* the message.
     *  - a bang matching a selector spelled ``bang`` → a bang; there is nothing
     *    in a bang to strip.
     *
     *  Anything matching no selector leaves the rightmost outlet whole and in
     *  its own type, so a chain of ``.route`` objects strings together with
     *  each fall-through feeding the next inlet.
     *
     *  ### What matches what
     *
     *  ``.sel``'s matcher, shared with ``.routepass`` — three objects whose
     *  only job is to branch must branch alike. A selector is a **number** or a
     *  **symbol**, decided once when the argument is read, and the two never
     *  match each other: a number matches by value with an int widened to a
     *  float, so ``.route 5`` answers both the int 5 and the float 5.0; a
     *  symbol matches by exact text; a list is matched on its first element
     *  alone, which is a number if it reads as one and a symbol otherwise.
     *
     *  The float half of that is the second defect #672 names. The object used
     *  to compare ``std::to_string(value)`` against the selector text, and
     *  ``std::to_string(5.f)`` is ``"5.000000"`` — a spelling no hand-written
     *  selector ever has, so ``.route 5`` could not match the float 5.0 at all
     *  and ``.route 5.0`` could not either. ``std::to_string`` also allocates
     *  and reads locale state on a handler that runs on whichever thread sent
     *  the message; it is gone from every path here.
     *
     *  The comparison is exact, with the consequences ``.sel`` documents: a
     *  computed float may miss a selector it looks equal to, and a NaN matches
     *  nothing and leaves the rightmost outlet. A selector repeated in the
     *  argument list uses the leftmost of its outlets only.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing. Matching is a bounded walk doing a float
     *  compare or a length-checked string compare against a character range —
     *  no ``substr``, no ``std::to_string``, no locale. The one cost stripping
     *  has that pass-through does not is the remainder itself: a remainder of
     *  two or more items is a new string, which is one allocation, and a short
     *  one fits small-string optimisation and costs none. A remainder that is a
     *  single number, or none at all, allocates nothing — those leave as an
     *  int, a float or a bang.
     */
    PATCHER_CLASS(gRoute, YSE::OBJ::G_ROUTE)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(SetIntValue)
    _FLOAT_IN(SetFloatValue)
    _BANG_IN(SetBangValue)
    _LIST_IN(SetListValue)

    _PARM_CLEAR
    _PARM_PARSE

  private:
    // One creation argument, resolved once. `numeric` decides which of the two
    // other fields means anything, and a numeric selector never matches a
    // symbol or the other way round. The same three fields `.sel` and
    // `.routepass` resolve their arguments into, because the three objects have
    // to agree on what a selector is.
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

    // Sends what is left of @p value from @p offset — the character just past
    // the matched leading token — out outlet @p index, in the kind that
    // remainder is: a bang when nothing is left, an int or a float when it is a
    // single number, and a list otherwise.
    void SendRemainder(int index, const std::string& value, std::size_t offset, YSE::THREAD thread);

    // The creation arguments, as tokens. One outlet each, plus the
    // fall-through, and the index of a token is the index of its outlet.
    std::vector<std::string> list;

    // `list` resolved into selectors, one entry per token so the indices stay
    // parallel. Built by ParseParams() on the control thread before the object
    // is published; no message handler writes to it.
    std::vector<Selector> selectors;
  };
}
}
