#pragma once
#include "../pObject.h"
#include "../../headers/types.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Running average of the numbers received — ``.mean`` (issue #460).
     *
     *  Every number that arrives is folded into an average of everything that
     *  arrived before it, and the new average comes out. A patch uses it to
     *  normalise an input against its own history, to hold a baseline a noisy
     *  controller wanders around, or to drive something slow from something
     *  fast. Where ``.slide`` (#459) forgets the past geometrically — a value
     *  from a hundred steps ago has almost no weight left — this one never
     *  forgets: the thousandth value counts exactly as much as the first.
     *
     *  ### Shape
     *
     *  One inlet, two outlets, as in Max:
     *
     *  - inlet 0 — the stream.
     *  - outlet 0 (float) — the mean.
     *  - outlet 1 (int) — how many numbers it rests on.
     *
     *  The two are sent **right to left**, count first, which is the patcher's
     *  existing convention for a multi-outlet report (``.cartopol`` sends its
     *  pair the same way) and Max's own outlet order. A patch that triggers off
     *  the mean therefore already holds the matching count when it fires, so
     *  the pair can never be read half-updated.
     *
     *  Unlike ``.histo`` (#458) this does *not* collapse to a single list
     *  outlet. There the pair was two ints that only meant something together;
     *  here the mean is the value the patch wants to route straight into a
     *  float inlet, and forcing it through a list would make the common case
     *  the awkward one.
     *
     *  ### Messages on inlet 0
     *
     *  - int / float — added to the running sum; the new mean and count come
     *    out.
     *  - bang — re-sends the stored mean and count without adding anything.
     *    Max: "Sends out the previous output (the stored average value)."
     *  - a list of numbers — Max's list behaviour, verbatim: "The numbers in
     *    the list are added together, the sum is divided by the number of items
     *    in the list, and the mean is sent out. All previously received numbers
     *    are cleared from memory." So a list is a one-shot mean *and* an
     *    erasure, not an append. At most ``MAX_LIST_ITEMS`` items are read; a
     *    longer list is truncated, the way ``.vexpr`` truncates at Max's
     *    ``maxsize``, and the count outlet says so by reporting the truncated
     *    length rather than the length sent.
     *  - ``clear`` — Max's word: "Resets the stored and calculated contents of
     *    the object to zero." Sum and count both go to 0 and **nothing is
     *    emitted**, matching ``.histo``'s ``clear`` and ``.slide``'s ``reset``,
     *    which are also silent. ``reset`` is accepted as a synonym: the
     *    patcher's accumulating objects are split between the two spellings
     *    (Max calls it ``clear`` here and in ``histo``, ``reset`` in ``slide``)
     *    and a patch should not have to remember which object came from which
     *    page.
     *  - anything else — ignored rather than guessed at, as in ``.slide`` and
     *    ``.histo``. Note that a word followed by numbers (``wobble 3``) is a
     *    *list* by the time it gets here, because the shared list reader skips
     *    non-numeric tokens; only a message with no numbers in it at all is
     *    silently dropped.
     *
     *  ### Bang before any input, and after a clear
     *
     *  The mean of nothing is not a number, so it has to be *chosen*. The
     *  object reports **0 with a count of 0**, which is the same answer
     *  ``.histo`` gives for an empty histogram. The alternatives were both
     *  worse: computing 0/0 puts a NaN on the outlet and poisons whatever it
     *  drives, and staying silent leaves a patch that bangs its objects once on
     *  load with no value at all — which in a headless patcher is invisible
     *  rather than merely empty. The count outlet is what distinguishes "the
     *  average is 0" from "there is no average yet", and it is sent first, so
     *  the distinction is available before the 0 arrives.
     *
     *  ### Numerics — why a running sum in double
     *
     *  The obvious implementation, a ``float`` sum divided by a count, fails in
     *  two distinct ways over a long stream, and the second one is fatal:
     *
     *  - it *drifts*: every addition rounds, and for a stream of similar values
     *    the roundings share a sign, so the error grows roughly linearly in the
     *    number of terms rather than cancelling;
     *  - it *stagnates*: once the running sum exceeds ``2^24`` times the
     *    magnitude of a new value, ``sum + x`` rounds straight back to ``sum``
     *    and the value contributes **nothing**. For values around 1 that point
     *    is about 17 million messages — minutes of a busy patcher — after which
     *    the reported mean simply stops responding while the count keeps
     *    climbing, so the average slides towards zero.
     *
     *  This object keeps the sum in ``double`` and the count as an exact
     *  ``I64``. That moves both failure points out of reach:
     *
     *  - **Error bound.** Recursive summation satisfies
     *    ``|Σ̂ − Σ| ≤ (n−1)·u·Σ|xᵢ|`` with ``u = 2⁻⁵³ ≈ 1.11e-16``. Dividing by
     *    the exact count gives ``|mean̂ − mean| ≤ (n−1)·u·mean(|x|)``: the error
     *    is at most ``(n−1)·u`` relative to the average magnitude of the input.
     *    That is 1.1e-10 after a million values and 1.1e-7 after a *billion* —
     *    still inside one ulp of the ``float`` the outlet carries, whose
     *    spacing is 1.19e-7. In other words the mean is correct to the last bit
     *    the patcher can transmit, for any stream a patcher can plausibly
     *    produce.
     *  - **Stagnation.** The same threshold in ``double`` is ``2^53`` times the
     *    new value: about 9e15 messages of similar magnitude, or 285,000 years
     *    at a thousand messages a second.
     *  - **Overflow.** ``double`` tops out at 1.8e308 and a ``float`` input is
     *    at most 3.4e38, so reaching it would take 10^269 messages. The count
     *    overflows first, and ``I64`` gives it 9.2e18 — 292 million years at
     *    the same rate.
     *
     *  The alternative considered was Welford's incremental update,
     *  ``m_k = m_{k−1} + (x_k − m_{k−1})/k``, which the issue suggested. It was
     *  **not** chosen, and the reason is that it does not actually buy accuracy
     *  here. Each step rounds by about ``u·|m_k|``, and that error is attenuated
     *  by the remaining steps by exactly ``∏_{j>k}(1 − 1/j) = k/n``, so the
     *  total is ``u·Σ_k |m_k|·k/n ≈ u·n·|m|/2`` — the *same* ``O(n·u)`` order as
     *  the sum, with a factor of two in its favour and a division per message
     *  against it. Welford's real payoff is the *variance*, where the textbook
     *  ``Σx² − (Σx)²/n`` loses everything to cancellation; this object does not
     *  compute a variance. Set against a tie on accuracy, the running sum wins
     *  on three things that are not ties: it is one addition per message
     *  instead of a subtract, a divide and an add; the count stays an exact
     *  integer, so ``clear`` and the count outlet are trivially right; and a
     *  value's contribution does not depend on *when* it arrived, which makes
     *  the object's behaviour explainable rather than merely correct.
     *
     *  A non-finite input (NaN, ±∞) is folded in as **0** rather than being
     *  refused, the convention ``./``, ``.sqrt``, ``.zmap``, ``.clip`` and
     *  ``.slide`` already use — and here it is also the only safe choice, since
     *  an infinity in the sum would pin the mean at infinity and a following
     *  ``−∞`` would turn it into a NaN that no later input could clear. It is
     *  still *counted*: the count outlet reports how many messages the inlet
     *  accepted, and a stream that disagreed with the message history would be
     *  harder to debug than one that names an unusable input as a zero.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlet, not by
     *  the DSP tick. The emitting step lives in Accumulate()/Report() for the
     *  reason ``.slide`` documents: a hot inlet fires CalculateIfReady() after
     *  *every* message type it accepts, so a Calculate() that emitted would make
     *  ``clear`` emit too, which is the one thing it must not do.
     *
     *  Nothing on any path allocates, locks or blocks. Adding a number is one
     *  compare and one addition; a bang is a division; ``clear`` is two stores;
     *  a list is a bounded walk over at most ``MAX_LIST_ITEMS`` items through
     *  the shared, allocation-free and locale-free ``ExprParseFloatList``. The
     *  outlets carry a float and an int, not a string, so unlike ``.histo`` and
     *  ``.anal`` this object has no ``std::string`` on its message path at all.
     */
    PATCHER_CLASS(gMean, YSE::OBJ::G_MEAN)
    _NO_MESSAGES
    _NO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)
    _BANG_IN(Bang)
    _LIST_IN(SetList)

    _HAS_GUI

    /**
     *  @brief Most items read out of one list message.
     *
     *  Matches ``.vexpr``'s ceiling, which is Max's default ``maxsize``. A list
     *  arrives as text, so reading it needs a fixed destination if it is not to
     *  allocate; a longer list is truncated to its first ``MAX_LIST_ITEMS``
     *  numbers and the count outlet reports the truncated length.
     */
    static constexpr int MAX_LIST_ITEMS = 256;

    /** @brief The current mean; 0 when nothing has been counted. */
    double Mean() const;

    /** @brief How many numbers the current mean rests on. Exact. */
    ::I64 Count() const {
      return count;
    }

    /** @brief The running sum, in the precision it is actually kept in. */
    double Sum() const {
      return sum;
    }

    /**
     *  @brief The count as the outlet reports it: saturating at ``INT_MAX``.
     *
     *  Public because it *is* the documented behaviour of the right outlet
     *  past two billion messages, and driving the object there to assert it is
     *  not an option. The internal count keeps counting past the saturation
     *  point, so the mean itself stays correct.
     */
    static int ReportableCount(::I64 value);

  private:
    // Fold one number into the running sum. Non-finite reads as 0. No emit —
    // the callers decide whether this message is one that reports.
    void Add(float value);

    // Send the count out outlet 1 and the mean out outlet 0, in that order.
    void Report(YSE::THREAD thread);

    // Sum and count back to zero. Silent, as Max's `clear` is.
    void Clear();

    // Running sum of every number counted. Double on purpose — see the class
    // docs; this is the whole numerical argument of the object.
    double sum;

    // How many numbers that sum rests on. Exact, and 64-bit so it cannot wrap
    // before the sum stagnates.
    ::I64 count;
  };
}
}
