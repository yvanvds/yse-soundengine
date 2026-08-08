#pragma once
#include "../pObject.h"
#include "../pSelector.h"
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Bang the outlet whose selector the input matches — ``.sel``
     *         (issue #465).
     *
     *  Max's ``select``, and with ``.trigger`` the backbone of message dispatch:
     *  one outlet per creation argument plus a rightmost pass-through, so a
     *  stream of numbers or symbols fans out into one branch per value. The
     *  patcher had ``.route``, which matches the *leading symbol of a list* and
     *  forwards the remainder, and ``.split``, which routes by *range*; neither
     *  can branch on a bare number.
     *
     *  ### The shape
     *
     *  Max: "The number of arguments determines the number of outlets in
     *  addition to the rightmost outlet. If there is no argument, there is only
     *  one other outlet, which is assigned the integer number 0." So ``.sel 1 2
     *  3`` has four outlets and a bare ``.sel`` has two, the first of which
     *  matches 0. The outlets are built in the parameter-parse callback, the
     *  pattern ``.route`` and ``.gate`` already use, which is why a live
     *  ``SetParams`` that changes the argument count goes through the
     *  structural-rebuild path of #234 rather than resizing a published object.
     *
     *  ### Exactly one outlet fires
     *
     *  Worth stating because the object *looks* like it might fan out. It does
     *  not: an input either matches — one bang, one outlet — or it does not, and
     *  then it leaves the rightmost outlet. Max settles the only case where the
     *  question could arise, a repeated argument: "If an int is listed multiple
     *  times as an argument, a bang message will be sent out the leftmost outlet
     *  only." So ``.sel 5 5`` bangs outlet 0 and never outlet 1.
     *
     *  That is what spares this object the right-to-left firing order ``.mean``,
     *  ``.cartopol`` and ``.peak`` have to respect — there is never a second
     *  outlet to order against the first. ``.split`` is the same story for the
     *  same reason.
     *
     *  ### What comes out where
     *
     *  A match sends a **bang**, and nothing else: the outlet's position already
     *  carries the value, which is the whole point of the object. The rightmost
     *  outlet passes the input on **unchanged and in its own type** — an int
     *  leaves as an int, a float as a float, a list as the same list, a bang as
     *  a bang — so a chain of ``.sel`` objects can be strung together with the
     *  reject outlet feeding the next one's inlet, exactly as ``.split`` chains
     *  its out-of-range branch.
     *
     *  On a match the rest of a list is **dropped**. Max is explicit — "a bang
     *  from one of its corresponding outlets if the first element in the list
     *  matches" — and the object that keeps the remainder is ``.route``. Only
     *  the first element is ever examined.
     *
     *  ### What matches what
     *
     *  A selector is a **number** or a **symbol**, decided once when the
     *  argument is read, and the two never match each other:
     *
     *  - a number matches a numeric selector by value. ``int`` input is widened
     *    to ``float`` and compared as one, so ``.sel 5`` answers both the int 5
     *    and the float 5.0 — Max's int/float duality is not reproduced here, for
     *    the reason ``.accum``, ``.maximum`` and ``.past`` set out: ``.sel 5``
     *    and ``.sel 5.0`` are the same parameter string in this patcher.
     *  - a symbol matches a symbolic selector by exact text.
     *  - a **bang** matches a selector spelled ``bang``, which is Max's rule
     *    ("The bang message matches a 'bang' symbol in the arguments"). With no
     *    such selector a bang goes out the rightmost outlet, still as a bang.
     *  - a **list** is matched on its first element alone, and that element is a
     *    number if it reads as one and a symbol otherwise. So ``.sel 5`` bangs
     *    for the list ``5 6``, which is what Max does: in Max the first element
     *    of that list *is* an int, and this patcher carries lists as text.
     *
     *  The comparison is **exact**. Max has two attributes here that this port
     *  deliberately does not reproduce: ``matchfloat``, whose default of 0 makes
     *  Max's ``select`` ignore floats altogether, and ``fuzzy``, which widens a
     *  float comparison into a tolerance band. The first has no meaning in a
     *  patcher with a single numeric type — there is no float to single out —
     *  and reproducing it would mean silently dropping half the messages the
     *  object is sent. The second is a real convenience and a real hazard: a
     *  tolerance makes ``.sel`` match values it was not given, and two selectors
     *  closer together than the tolerance would then both be right, with the
     *  leftmost silently winning. A patch that needs it can say so out loud with
     *  a ``.round`` on the way in, which is also where the rounding it wants is
     *  visible. What remains is Max's ``matchfloat 1, fuzzy 0.``.
     *
     *  Exact comparison has one consequence worth knowing: a **computed** float
     *  may miss a selector it looks equal to, because 0.1 + 0.2 is not 0.3 in
     *  binary floating point. That is not a defect of the matcher — the same
     *  ``.==`` would answer the same way — and a ``.round`` upstream is the
     *  fix. A **NaN** matches nothing at all, since a NaN compares unequal to
     *  everything including itself, so it leaves the rightmost outlet; the
     *  patcher's usual "read a non-finite as 0" substitution is deliberately
     *  *not* applied, because it would make a stray NaN bang the outlet a patch
     *  wired for a real 0.
     *
     *  ### The right inlet, and the one deviation from Max
     *
     *  Max: "If there is a single int argument (or if there are no arguments) a
     *  second inlet is created on the right. Numbers received in that inlet are
     *  stored in place of the argument. If there is more than one argument, or
     *  if the only argument is not an int, the right inlet is not created."
     *
     *  Reproduced, with *numeric* in place of *int*: a single numeric selector
     *  gets the inlet, a single symbolic one does not, two or more do not. The
     *  deviation is forced — ``.sel 5`` and ``.sel 5.0`` are the same parameter
     *  string here, so there is no int-ness left to test — and the alternative,
     *  keying the inlet count off whether the argument text happens to contain a
     *  decimal point, would make the object's *shape* depend on spelling that a
     *  JSON round trip is free to normalise.
     *
     *  The inlet is silent, takes an int or a float, and replaces the value of
     *  selector 0. It does not take a symbol: a symbolic selector never has the
     *  inlet in the first place, so there is nothing a symbol could usefully
     *  set. With more than one selector there is no way to change them, which is
     *  Max's answer too — the argument list is the object's shape, and changing
     *  it changes the outlet count.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlet, and a
     *  Calculate() that emitted would bang again for every DSP tick after the
     *  hot inlet fired. The rule ``.route``, ``.past`` and ``.slide`` establish.
     *
     *  No path allocates, locks or blocks. Matching is a bounded walk over at
     *  most ``MAX_SELECTORS`` entries doing a float compare or a length-checked
     *  ``std::string::compare`` against a character range — no ``substr``, no
     *  ``std::to_string``, no locale. Deciding whether the leading token of a
     *  list is a number goes through the shared, allocation-free
     *  ``ReadNumericToken`` in ``pListArgs.h`` — extracted from here when
     *  ``.trigger`` (#466) needed the same yes/no answer for its own creation
     *  arguments. Exactly one ``Send`` follows. The selector table
     *  is built on the control thread by the parameter callbacks and is never
     *  resized afterwards; the only field a message handler writes is the value
     *  of selector 0, from the cold inlet, as a plain float store.
     *
     *  ### Where the matcher lives
     *
     *  In ``SelectorTable`` (``pSelector.h``), shared with ``.route`` and
     *  ``.routepass`` (issue #680). The table, the two match functions and the
     *  leading-token walk were written here and copied into the other two as
     *  they landed; three objects whose only job is to branch have to branch
     *  alike, and three copies is how they stop doing that. What stays here is
     *  the part this object legitimately does differently: the parse loop's
     *  ``MAX_SELECTORS`` ceiling and empty-token skip, the fall back to the
     *  single selector ``0``, the conditional right inlet, and the fact that a
     *  match sends a bang.
     */
    PATCHER_CLASS(gSel, YSE::OBJ::G_SEL)
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
     *  256, the ceiling ``.past``, ``.maximum``, ``.mean`` and ``.vexpr``
     *  already use for a list. Max documents no limit, but every selector costs
     *  an outlet and a patch with more than 256 branches in one box has a
     *  different problem; arguments past the ceiling are dropped.
     */
    static constexpr int MAX_SELECTORS = 256;

    /** @brief How many selectors the object matches against. At least one — a
     *         bare ``.sel`` matches the single number 0. */
    int SelectorCount() const {
      return (int)selectors.Size();
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

    /**
     *  @brief True when the object has Max's settable right inlet.
     *
     *  Exposed because the inlet's *presence* is the visible half of the
     *  single-argument rule, and a test that only sent numbers at it could not
     *  tell "the inlet exists and stores" from "the inlet was never created".
     */
    bool HasValueInlet() const {
      return inputs.size() > 1;
    }

  private:
    // Rebuild the inlets and outlets from the current selector table, docs
    // included. Control thread only: called from the constructor and from the
    // parameter callbacks, all of which run before the object is wired or
    // published.
    void ShapePorts();

    // The creation argument, as tokens. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler.
    std::vector<std::string> selectorArgs;

    // The resolved selectors, and the matcher over them — the shared table
    // `.route` and `.routepass` hold too (#680). Sized by ParseParams() /
    // ClearParams() before the object is published and never resized
    // afterwards, so a handler's walk over it cannot race a reallocation; the
    // cold inlet writes one float into it and touches nothing else.
    SelectorTable selectors;
  };
}
}
