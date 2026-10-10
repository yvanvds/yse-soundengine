#pragma once
#include "../pObject.h"
#include <cstddef>
#include <limits>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Accumulator with add and multiply — ``.accum`` (issue #461).
     *
     *  A stored number a patch can add into and multiply in place: the
     *  general-purpose register that ``.counter`` only covers the
     *  increment-by-one case of. Running totals, scaling chains, gain staged
     *  over several steps, a value nudged by one part of a patch and scaled by
     *  another — all of it without the feedback cord an equivalent
     *  ``.+``/``.f`` pair would need, which the patcher's message-feedback loop
     *  guard (#236) would refuse anyway. That is the point of the object.
     *
     *  ### Shape — three inlets, as in Max
     *
     *  - inlet 0 (hot) — an int or float **replaces** the stored value and
     *    sends it out; a bang sends the stored value out.
     *  - inlet 1 (cold) — the number is **added** to the stored value, without
     *    triggering output.
     *  - inlet 2 (cold) — the stored value is **multiplied** by the number,
     *    without triggering output.
     *
     *  One float outlet.
     *
     *  Note that this is Max's layout, not the sketch in the issue's Notes
     *  ("a float/int adds; ``mult`` multiplies"). Max's reference page is the
     *  authority for a port, and its left inlet is documented as "Replaces the
     *  value stored in accum, and sends the new value out the outlet" with the
     *  adding done by the middle inlet. The sketch's intent is served all the
     *  same, by the messages below: a patch that prefers one cord and a message
     *  box to three cords writes ``add 5`` and ``mult 2`` instead of wiring
     *  inlets 1 and 2, and gets exactly the same arithmetic.
     *
     *  ### Messages on inlet 0
     *
     *  - ``set <n>`` — stores @em n without emitting. Max's wording: "The word
     *    set, followed by a number, sets the stored value to that number,
     *    without triggering output." This is the silent twin of sending the
     *    bare number.
     *  - ``add <n>`` — adds without emitting; exactly what inlet 1 does.
     *    ``ft1 <n>`` is accepted as a synonym because that is Max's spelling of
     *    the same message ("The message ft1, followed by a number, adds the
     *    number to the stored value without triggering output"), and ``add`` is
     *    offered next to it because ``ft1`` names an inlet index rather than an
     *    operation and reads as noise in a patch.
     *  - ``mult <n>`` — multiplies without emitting; exactly what inlet 2 does,
     *    and the word the issue asked for. Max has no left-inlet spelling of
     *    this one, so this is an addition rather than a port.
     *  - ``reset`` — restores the ``initial`` creation argument, without
     *    emitting. Not a Max message; the patcher family convention, and the
     *    same meaning ``.counter``'s ``reset`` already has here (back to
     *    ``startValue``, not to zero). It is worth having precisely *because*
     *    it is not ``set 0``: after ``.accum 1`` has been multiplied down a
     *    scaling chain, the useful recovery is 1, the identity it started from,
     *    and a patch should not have to repeat the creation argument in a
     *    message box to get back to it.
     *  - ``clear`` is deliberately **not** accepted, although ``.mean`` and
     *    ``.histo`` take it. For them the empty state and zero are the same
     *    thing, so the two words cannot disagree. Here they can — ``clear`` on
     *    an object created as ``.accum 1`` would have to mean either 0 or 1 —
     *    and a message whose meaning depends on the creation argument is worse
     *    than a message that does not exist.
     *  - anything else — ignored rather than guessed at, as in ``.slide`` and
     *    ``.mean``.
     *
     *  ### A bang before any operation
     *
     *  Reports the ``initial`` creation argument, which is 0 for a plain
     *  ``.accum``. It is not silent: a register that has been asked for its
     *  value always has one, and in a headless patcher silence is invisible
     *  rather than merely empty. This is also why the creation argument is
     *  applied through a parse callback rather than left in the parameter
     *  field: ``.accum 5`` must answer 5 to the first bang it receives, not the
     *  0 a constructor default would leave behind.
     *
     *  ### Numeric type
     *
     *  One ``double`` register; the outlet carries a ``float``, as every
     *  numeric outlet in the patcher does.
     *
     *  Max's ``accum`` is really two objects — an int one and a float one,
     *  chosen by whether the creation argument has a decimal point, with the
     *  int variant rounding the result of every multiply back to an integer.
     *  That duality is **not** reproduced. The patcher has no way to say "this
     *  object is integral" in a creation argument (``.accum 5`` and
     *  ``.accum 5.0`` parse to the same float), so the mode would have to be
     *  guessed from the text of the argument — and guessing wrong is
     *  destructive here in a way it is not in Max: an integral ``.accum`` that
     *  truncates after each step turns ``mult 0.5`` twice into 0 instead of a
     *  quarter, silently and irrecoverably. A patch that wants integers puts a
     *  ``.round`` on the outlet, where the truncation is visible.
     *
     *  Keeping the register in ``double`` rather than ``float`` costs nothing
     *  and buys the thing this object needs most: a multiply-accumulate feeds
     *  its own output back into itself, so every rounding error is an input to
     *  the next step. Over a scaling chain of @em k multiplies the relative
     *  error grows as roughly ``k·u``; with ``u = 2⁻⁵³`` that is 1e-13 after a
     *  billion operations, still 29 bits below what the ``float`` on the outlet
     *  can express. In ``float`` (``u = 2⁻²⁴``) the same chain is visibly wrong
     *  after a few thousand steps.
     *
     *  ### The limits — what happens instead of an infinity
     *
     *  A multiply-accumulate diverges *fast*. ``mult 2`` repeated reaches the
     *  top of the ``float`` range in 128 messages; ``mult 10`` in 39. So the
     *  limit is not an exotic case to be documented and forgotten, it is
     *  something a patch will hit within a second of a ``.metro`` being left
     *  running, and what happens there is a design decision rather than an
     *  accident:
     *
     *  **The register saturates at ±``ACCUM_LIMIT`` (``FLT_MAX``,
     *  3.4028235e38) after every operation.** It never becomes ±∞ and never
     *  becomes a NaN.
     *
     *  Saturating loses information — once pinned at the ceiling, a following
     *  ``mult 0.5`` gives half of ``FLT_MAX`` rather than half of what the true
     *  product would have been — and that is accepted, because the two
     *  alternatives are worse:
     *
     *  - **±∞ is absorbing.** ``inf * 0.5`` is still ``inf``, so a single
     *    overflow would make the register permanently useless; nothing a patch
     *    can send afterwards, short of ``set`` or ``reset``, brings it back.
     *    Worse, ``inf`` plus ``-inf`` is a NaN, so one overflow followed by one
     *    subtraction poisons the register *and* everything downstream of it,
     *    since a NaN survives every arithmetic operation that touches it. A
     *    saturated register, by contrast, is still a number: it is wrong in
     *    magnitude but right in sign, arithmetic on it keeps working, and the
     *    very next ``mult 1e-30`` puts the patch back in a usable range.
     *  - **Wrapping** — the other way a register can misbehave at its limit —
     *    is not even available to floating point, and would be the worst of the
     *    three anyway: it turns the largest value into the most negative one,
     *    so a runaway gain would present as a sign flip.
     *
     *  Clamping after *every* operation is also what makes the ``double``
     *  arithmetic itself provably overflow-free, which is the second reason for
     *  it. Both operands are then bounded by ``FLT_MAX``: the largest reachable
     *  sum is ``2·FLT_MAX ≈ 6.8e38`` and the largest reachable product is
     *  ``FLT_MAX² ≈ 1.16e77``, both far below ``double``'s 1.8e308. So no
     *  intermediate can overflow before it is clamped — the saturation is
     *  computed, not caught after the fact.
     *
     *  The bottom of the range is deliberately **not** floored. Zero is a
     *  legitimate value for a register (it is what ``mult 0`` means, and the
     *  identity of the add), so there is nothing to protect against; and
     *  ``double`` reaches down to 1e-308 where the outlet's ``float`` flushes to
     *  0 below 1.4e-45. A chain that multiplies down by 1e-30 and back up by
     *  1e30 therefore recovers its value exactly, even though the outlet
     *  reported 0 in between. That asymmetry — saturate at the top, full range
     *  at the bottom — is not an oversight: an overflow destroys the register,
     *  an underflow only makes the outlet temporarily uninformative.
     *
     *  ### Non-finite operands
     *
     *  Neutralised, per operation: a non-finite number is replaced by the
     *  **identity of the operation it drives** — 0 for an add, 1 for a
     *  multiply — and by 0 where it would *become* the stored value (inlet 0, a
     *  bare number or ``set``), which is the substitution ``./``, ``.sqrt``,
     *  ``.zmap``, ``.clip``, ``.slide`` and ``.mean`` already make.
     *
     *  The multiply is the one that has to differ from the family convention
     *  rather than follow it: folding a NaN in as 0 would zero the register,
     *  and a zeroed register can never be multiplied back to anything. The
     *  identity leaves the value alone instead, which is the only choice that a
     *  later message can recover from — and the same argument as the saturation
     *  above, one level down.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlets, not
     *  by the DSP tick — and the emitting step lives in Emit(), called from the
     *  two inlet-0 handlers that Max documents as emitting. This is the rule
     *  ``.slide`` and ``.mean`` establish: a hot inlet fires
     *  CalculateIfReady() after *every* message type it accepts, so a
     *  Calculate() that emitted would make ``set``, ``add``, ``mult`` and
     *  ``reset`` emit too — the one thing all four must not do.
     *
     *  Nothing on any path allocates, locks or blocks. An operation is one
     *  multiply or add and two compares; a message is a fixed-length prefix
     *  comparison against four words plus one number read through the shared,
     *  allocation-free and locale-free ``ExprParseFloatList``. The outlet
     *  carries a float, so there is no ``std::string`` on the message path at
     *  all.
     */
    PATCHER_CLASS(gAccum, YSE::OBJ::G_ACCUM)
    _NO_MESSAGES
    _NO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)
    _BANG_IN(Bang)
    _LIST_IN(SetList)

    _PARM_PARSE

    _HAS_GUI

    /**
     *  @brief The magnitude the register saturates at: ``FLT_MAX``.
     *
     *  The largest number the float outlet can carry. Every operation clamps
     *  into ``[-ACCUM_LIMIT, ACCUM_LIMIT]``, which keeps the register finite
     *  and keeps the double arithmetic that produces it provably free of
     *  overflow. See "The limits" in the class docs.
     */
    static constexpr double ACCUM_LIMIT = static_cast<double>((std::numeric_limits<float>::max)());

    /**
     *  @brief Clamps @p value into the register's range.
     *
     *  Public because saturation *is* the documented behaviour at the limits,
     *  and asserting it only through the inlets would mean asserting the
     *  interesting cases indirectly. A NaN clamps to 0 — the value it would
     *  have been substituted with on the way in — and an infinity saturates
     *  like any other too-large value, keeping its sign. So the register
     *  cannot hold a non-finite value by any route.
     */
    static double Clamp(double value);

    /** @brief The stored value, in the precision it is actually kept in. */
    double Value() const {
      return current;
    }

    /** @brief The creation argument, and the value ``reset`` returns to. */
    float Initial() const {
      return initial;
    }

  private:
    // The three things the object can do to its register. Each clamps; none
    // emits — the callers decide whether this message is one that reports.
    void Replace(float value);
    void Add(float value);
    void Multiply(float value);

    // Send the stored value out outlet 0.
    void Emit(YSE::THREAD thread);

    // Reads `<word> <number>` when `value` starts with `word`. Returns false
    // (leaving `out` untouched) when the word does not match or no number
    // follows, so an unknown or malformed message falls through to being
    // ignored rather than acting on a zero nobody sent.
    static bool MatchArg(const std::string& value, const char* word, std::size_t wordLength,
                         float& out);

    // The creation argument. A plain float parameter, applied to the register
    // by ParseParams() so a saved `.accum 5` answers 5 to its first bang.
    float initial;

    // The register itself. Double on purpose — see "Numeric type"; the value is
    // always finite and always within ±ACCUM_LIMIT.
    double current;
  };
}
}
