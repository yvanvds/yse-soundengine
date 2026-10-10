#pragma once
#include "../pObject.h"
#include "gExtremum.h"
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Bangs the moment a value crosses a threshold — ``.past``
     *         (issue #464).
     *
     *  Edge detection on continuous control data. An envelope follower that
     *  climbs past a level, a controller that reaches the top of its useful
     *  range, a distance that finally gets close enough: the interesting event
     *  is the *crossing*, once, not the thousand messages that follow while the
     *  value stays up there. Feeding the same stream into a ``.>=`` and a
     *  ``.sel 1`` would fire on every block; this object fires on the edge and
     *  then goes quiet until the value comes back down.
     *
     *  ### The rule, and the half of it that is easy to get wrong
     *
     *  Max states it in two halves, in two different places on the reference
     *  page, and only both together define the object:
     *
     *  - **fire** when every number is *at or above* its threshold, having not
     *    been so before — "If all of the arguments are equaled or exceeded by
     *    the numbers received in the inlet, past sends out a bang."
     *  - **re-arm** only when a number "goes back below (**is less than**) its
     *    argument".
     *
     *  The two are exact complements, which is what makes the object a plain
     *  latch on the predicate ``value >= threshold`` rather than a hysteresis
     *  band: it fires on the rising edge of that predicate and nothing else.
     *  Two consequences that a naive implementation gets wrong, and that the
     *  tests pin:
     *
     *  - **A value exactly at the threshold has crossed it.** ``.past 5`` fed 5
     *    bangs. Max's `int` / `float` message entry says "greater than", but its
     *    Arguments section ("Triggers output when the number is met or
     *    exceeded"), its ``set`` message ("the numbers which must be equaled or
     *    exceeded") and its Output section ("If all of the arguments are equaled
     *    or exceeded") all say *at or above*, and only the inclusive reading
     *    makes the re-arm rule fit: with a strict ``>`` the object could latch
     *    on a value it never fired for.
     *  - **A value exactly at the threshold does not re-arm it.** Coming back
     *    down to exactly 5 leaves ``.past 5`` still latched, because "goes back
     *    below" means strictly below. So the sequence 10, 5, 10 bangs once, not
     *    twice. This is the one asymmetry in the object and it is deliberate:
     *    equality belongs to the "past it" side of the line, on the way up and
     *    on the way down alike.
     *
     *  ### Where the object starts
     *
     *  Un-latched, so a value that is *already* above the threshold when the
     *  very first message arrives bangs immediately. Nothing has met the
     *  threshold yet, so the first thing that does is a crossing. This is
     *  exactly the state ``clear`` restores — Max: "Causes past to forget
     *  previously received input, readying it to send a bang message again" —
     *  and a freshly created object has no previously received input to
     *  remember. The alternative, treating creation as "assume we start above",
     *  would make a patch that loads with its controller already up silently
     *  miss the first event it exists to catch.
     *
     *  ### More than one threshold
     *
     *  The creation argument is a *list*, and so is the input: the numbers are
     *  compared element-wise against the corresponding thresholds and the bang
     *  is the **conjunction** — "If all of the numbers in the list are greater
     *  than or equal to the corresponding arguments, a bang is sent out". So
     *  ``.past 60 100`` is "both of these are up", the AND gate of two
     *  thresholds in one box.
     *
     *  Each element keeps its own met/not-met flag, which is what Max's re-arm
     *  wording describes ("the number that equaled or exceeded its argument goes
     *  back below its argument"). Three details follow from that and are worth
     *  stating because none is obvious:
     *
     *  - a list **shorter** than the threshold list updates only the elements it
     *    supplies; the rest keep the flags they had. Partial updates are
     *    therefore possible, and a plain int or float is simply the one-element
     *    case — which is also how Max routes a number when the object has one
     *    argument.
     *  - numbers **past** the last threshold are ignored: there is nothing for
     *    them to be compared against.
     *  - the conjunction is over the flags, not over the current message, so the
     *    bang means "all thresholds are now met" rather than "this message met
     *    them all at once".
     *
     *  At most ``MAX_THRESHOLDS`` thresholds, the ceiling ``.maximum``,
     *  ``.mean`` and ``.vexpr`` already use for a list.
     *
     *  ### ``clear``, ``set`` and ``reset``
     *
     *  - ``clear`` — Max's, and the only documented way back: drop every met
     *    flag so the next crossing bangs again. Silent, and it leaves the
     *    thresholds alone.
     *  - ``set <numbers>`` — Max's, "sets the numbers which must be equaled or
     *    exceeded". It replaces the thresholds and **does not re-arm**, which is
     *    the decision this object has to make that Max does not document.
     *
     *    Re-arming on every ``set`` was rejected, and the reason is the whole
     *    point of the message: a patch that drives the threshold from another
     *    part of the patch sends ``set`` continuously, and an object that
     *    re-armed on each one would bang for every message above the line —
     *    which is a ``.>=``, not a ``.past``. So a threshold change carries the
     *    latch across, and a patch that wants a fresh start says so with the
     *    word that means exactly that.
     *
     *    Elements that survive a ``set`` keep their flags; elements the new list
     *    adds start un-met, since nothing has been compared against them yet.
     *    The latch is then re-derived from the flags, so growing the list can
     *    legitimately un-latch the conjunction — that is the flags being
     *    honest, not a special case.
     *  - ``reset`` — not a Max message; the patcher family's word, with the
     *    meaning ``.counter``, ``.accum`` and ``.peak`` already give it: back to
     *    how the object was created. Here that is both halves at once — the
     *    creation-argument thresholds *and* a cleared latch — which is
     *    unambiguous precisely because it is the whole state. Without it there
     *    is no way back to the creation argument after a ``set``, and a patch
     *    should not have to repeat its own creation argument in a message box.
     *
     *  All three are silent. A message that is neither a word this object knows
     *  nor a list containing a number is ignored rather than guessed at, as in
     *  ``.slide``, ``.mean``, ``.accum``, ``.maximum`` and ``.peak``.
     *
     *  ### No bang inlet, and one inlet rather than two
     *
     *  A bang is deliberately not accepted. The outlet's only meaning is "the
     *  threshold was just crossed", so a bang in would have to either fabricate
     *  a crossing or do nothing; Max documents neither, and the honest answer is
     *  that a message carrying no number cannot cross anything.
     *
     *  Max gives the object one inlet and so does this port, even though a cold
     *  threshold inlet is the patcher's usual idiom. The threshold here is a
     *  *list* whose length is part of the object's shape, and a float inlet can
     *  only ever set the first element of it — so the inlet would be a worse
     *  spelling of ``set`` that silently does the wrong thing for every
     *  multi-threshold object. ``set`` takes a whole list and needs no
     *  qualification.
     *
     *  ### One numeric type
     *
     *  Everything is a float. Max's int-unless-the-creation-argument-has-a-
     *  decimal-point duality is not reproduced, for the reason ``.accum`` and
     *  ``.maximum`` set out at length: ``.past 5`` and ``.past 5.0`` are the
     *  same parameter string in this patcher. It matters less here than
     *  elsewhere — the object emits a bang, so nothing it sends could be rounded
     *  — but a rounded *threshold* would move the line a patch asked for.
     *
     *  ### Non-finite input
     *
     *  Read as 0 wherever it arrives, on the input side and in the thresholds
     *  alike, through the shared ``ExtremumSanitize``. The convention ``./``,
     *  ``.sqrt``, ``.zmap``, ``.clip``, ``.slide``, ``.mean``, ``.accum``,
     *  ``.maximum`` and ``.peak`` all follow.
     *
     *  On the **threshold** side it is load-bearing rather than tidy. A NaN
     *  compares false against everything, so a NaN threshold could never be met
     *  and the object would go permanently mute — and in a multi-threshold
     *  object it would mute the *conjunction* while every other element carried
     *  on working, which is a fault with no symptom to trace. A ``+inf``
     *  threshold does the same, and a ``-inf`` one is met by every number there
     *  is, including the ones a patch sends to bring the value back down, so
     *  its element could never re-arm.
     *
     *  On the **input** side the substitution buys consistency rather than
     *  safety, and it is worth being plain about that: an unfiltered NaN would
     *  read as "not met" against every threshold, while a NaN read as 0 reads as
     *  not met against a positive threshold and as met against a non-positive
     *  one. Neither is dangerous; the second is simply the answer the rest of
     *  the patcher gives a NaN it was handed, and an object that disagreed with
     *  its neighbours about what a stray NaN means would be the surprise.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlet, not by
     *  the DSP tick — and the emitting step lives in Compare(), called from the
     *  handlers that Max documents as emitting. This is the rule ``.slide``,
     *  ``.mean``, ``.accum``, ``.maximum`` and ``.peak`` establish: a hot inlet
     *  fires CalculateIfReady() after *every* message type it accepts, so a
     *  Calculate() that emitted would make ``clear``, ``set`` and ``reset`` bang
     *  too — the one thing all three must not do.
     *
     *  Nothing on any path allocates, locks or blocks. A message is a
     *  fixed-length prefix comparison against three words plus at most
     *  ``MAX_THRESHOLDS`` numbers read through the shared, allocation-free and
     *  locale-free ``ExprParseFloatList`` into a stack array, then a bounded
     *  walk over the flags and at most one Send. The outlet carries a bang, so
     *  there is no ``std::string`` on the message path at all. The only
     *  ``std::vector`` in the object is the parameter token list, which is
     *  touched on the control thread by ClearParams() / ParseParams() and never
     *  read by a handler.
     */
    PATCHER_CLASS(gPast, YSE::OBJ::G_PAST)
    _NO_MESSAGES
    _NO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)
    _LIST_IN(SetList)

    _PARM_CLEAR
    _PARM_PARSE

    _HAS_GUI

    /**
     *  @brief Most thresholds — and most numbers read out of one message.
     *
     *  256, the ceiling ``.maximum``, ``.mean`` and ``.vexpr`` already use for
     *  a list. Max documents no limit for ``past``, but a list arrives as text
     *  and reading it needs a fixed destination if it is not to allocate; a
     *  longer list is truncated to its first ``MAX_THRESHOLDS`` numbers.
     */
    static constexpr int MAX_THRESHOLDS = 256;

    /** @brief How many thresholds the object is currently watching. At least
     *         one — a bare ``.past`` watches a single threshold of 0. */
    int ThresholdCount() const {
      return count;
    }

    /** @brief Threshold @p index, or 0 when the index is out of range. */
    float Threshold(int index) const {
      if (index < 0 || index >= count) return 0.f;
      return thresholds[index];
    }

    /**
     *  @brief True when the object has fired and is waiting for a value to drop
     *         back below a threshold.
     *
     *  Exposed because the latch *is* the object: asserting the edge behaviour
     *  only through the outlet would leave "fired but should not have" and
     *  "should have fired but the latch was already set" indistinguishable.
     */
    bool Latched() const {
      return latched;
    }

    /** @brief Whether element @p index is currently at or above its threshold. */
    bool Met(int index) const {
      if (index < 0 || index >= count) return false;
      return met[index];
    }

  private:
    // Compare @p values against the first @p valueCount thresholds, update the
    // flags, and bang if that just completed the conjunction. The only path
    // that emits.
    void Compare(const float* values, int valueCount, YSE::THREAD thread);

    // Every element at or above its threshold — the predicate the latch tracks.
    bool AllMet() const;

    // Drop every flag, so the next completed conjunction is a crossing again.
    // Max's `clear`; the thresholds are not touched.
    void Rearm();

    // Install @p newCount thresholds, keeping the flags of the elements that
    // survive and re-deriving the latch. `set` and the parameter parse.
    void Install(const float* values, int newCount);

    // The creation argument, as tokens. Control thread only: written by
    // Parameters::Set and read by ParseParams(), never by a message handler.
    std::vector<std::string> thresholdArgs;

    // The thresholds in force, and the ones `reset` returns to. Both always
    // hold `count` / `initialCount` finite numbers, since every path in goes
    // through ExtremumSanitize.
    float thresholds[MAX_THRESHOLDS];
    float initialThresholds[MAX_THRESHOLDS];
    int count;
    int initialCount;

    // Per element: has this one been at or above its threshold since the last
    // time it dropped below? Max's re-arm rule is written per element, and this
    // is that sentence as state.
    bool met[MAX_THRESHOLDS];

    // What AllMet() said last time, which is what turns "the conjunction holds"
    // into "the conjunction just became true".
    bool latched;
  };
}
}
