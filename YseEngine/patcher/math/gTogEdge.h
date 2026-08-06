#pragma once
#include "../pObject.h"
#include "gChange.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Bang when a value crosses zero in either direction — ``.togedge``
     *         (issue #469).
     *
     *  Max's ``togedge``. A continuous stream carries a level; a patch usually
     *  wants the two *moments* where that level starts and stops mattering.
     *  This object turns the stream into those two events: the left outlet bangs
     *  when the value becomes non-zero and the right outlet bangs when it
     *  returns to zero, and nothing at all comes out in between. It is the clean
     *  way to derive note-on / note-off style gating from a threshold or an
     *  envelope, and in a patcher whose message queue is bounded (#225) the
     *  silence in between is the point rather than a nicety — an envelope
     *  sampled at frame rate would otherwise deliver a message per frame for the
     *  whole time the gate is open.
     *
     *  ### The shared corner with ``.change``
     *
     *  Max's ``change`` (#468) has these two outlets built into it, and this
     *  object is not allowed to disagree with them. The zero test therefore is
     *  not written here at all: ZeroToNonZero() and NonZeroToZero() live in
     *  gChange.h and both objects call them. The rule has one corner that two
     *  hand-written copies would sooner or later drift apart on — ``-0.f ==
     *  0.f`` is true in IEEE-754, so a negative zero counts as zero on *both*
     *  sides, which is right, since it is zero — and a shared predicate is the
     *  only way to be sure that a patch translated from Max sees the same edges
     *  whichever of the two objects it used.
     *
     *  ### The two objects do not agree about the *payload*, and should not
     *
     *  ``change``'s edge outlets send the **int 1**; this object's send a
     *  **bang**. That is not an inconsistency to be tidied away — it is what the
     *  two Max reference pages say, one each:
     *
     *  - ``change``: "If the stored value is 0 and the input is not 0, 1 is sent
     *    out; otherwise nothing is sent out."
     *  - ``togedge``: "Out left outlet: bang, if the stored value is changed
     *    from 0 to not 0."
     *
     *  And the difference is defensible on its own terms. ``change``'s edge
     *  outlets sit beside a *value* outlet, so an int 1 distinguishes "an edge
     *  happened" from "nothing happened" on an outlet that is already carrying
     *  numbers; here there is no value outlet, the outlet's identity carries the
     *  whole meaning, and a bang is the honest message for an event with no
     *  payload. Porting the int here would also break the object's most obvious
     *  use — a bang outlet drives ``.trigger``, a ``.m`` message box or a
     *  ``.counter`` directly, where a stray 1 would be a value nobody asked for.
     *
     *  ### Where the stored value starts, and why there is no creation argument
     *
     *  Max: "Arguments: None." The object starts holding **0**, so the first
     *  non-zero number it ever receives is a rising edge and bangs the left
     *  outlet, while a first 0 is silent because 0 is what it already held.
     *
     *  ``.change`` takes an ``initial`` argument and this one deliberately does
     *  not, which is worth stating because the two are otherwise so close. The
     *  argument on ``.change`` exists to stop a patch *load* from firing the
     *  value outlet for a parameter that has not moved — it declares what the
     *  patch already believes. That reasoning does not transfer: this object has
     *  no value outlet to fire spuriously, only edges, and an edge that has not
     *  happened yet cannot be pre-declared. Inventing an argument Max does not
     *  document would also make ``.togedge 1`` a box whose first genuine 0 bangs
     *  the right outlet for a transition the patch never made. So: no argument,
     *  and ``reset`` (below) covers the one case where a patch really does need
     *  to put the object back to zero without a bang.
     *
     *  ### Only transitions, never levels
     *
     *  Max: "Otherwise, togedge sends no output." Both halves of that matter and
     *  a plausible implementation gets one of them wrong:
     *
     *  - 5 then 9 is **not** an event. Both are non-zero, the stored value
     *    changed, and no edge was crossed. An implementation that reported "the
     *    value changed" rather than "the value crossed zero" would bang the left
     *    outlet on every message of a rising envelope.
     *  - 0 then 0 is not an event either, for the mirror reason.
     *
     *  So an outlet can only fire when the *zero-ness* changes, which also means
     *  the two outlets are mutually exclusive: at most one of them fires per
     *  message, and they must strictly alternate over the object's whole life.
     *  That alternation is the invariant the tests hold this object to, because
     *  it is the property a downstream note-on / note-off pairing depends on —
     *  two note-ons with no note-off between them is a stuck note.
     *
     *  ### The bang inlet, which is not a passthrough
     *
     *  Max gives this object a bang method and it is easily the most surprising
     *  thing about it: "Switches the value stored in togedge from 0 to non-zero,
     *  or vice versa, and reports the change by sending a bang out one of the
     *  outlets." A bang **toggles** the stored value and then reports the
     *  transition it just made itself, so a stream of bangs comes out left,
     *  right, left, right — which is what the description means by "Outlets
     *  alternate output when bangs are received".
     *
     *  This is genuinely useful rather than a curiosity: it is how a patch drives
     *  the same note-on / note-off pair from a single button, and it composes
     *  with the numeric input rather than living beside it, because both go
     *  through the same stored value. A bang after the object was driven to 5 by
     *  a number bangs the *right* outlet and leaves the object holding 0.
     *
     *  A bang is therefore never silent, which makes this object the one member
     *  of the family whose bang always emits. Note that the toggle stores the
     *  literal 1 for its non-zero side; nothing downstream can observe which
     *  non-zero number is held, since only zero-ness is ever tested.
     *
     *  ### Non-finite input is ignored, not read as 0
     *
     *  The convention ``.change`` and ``.sel`` set, and it is load-bearing here
     *  for a sharper reason than usual. The patcher's usual substitution — read
     *  a NaN or an infinity as 0 — would **emit a transition the patch never
     *  sent**: a NaN arriving while the object holds 5 would bang the falling
     *  outlet, and a downstream synthesiser would receive a note-off for a note
     *  that is still being held. Storing it raw is no better, since a NaN is
     *  neither ``== 0.f`` nor usefully ``!= 0.f`` and would leave the object in a
     *  state from which neither outlet could ever fire again.
     *
     *  So a non-finite number is **ignored**: no outlet fires and the stored
     *  value is left exactly as it was. The invariant that falls out is the one
     *  ``.change`` keeps — **the stored value is always finite** — and here it is
     *  what guarantees the strict alternation above can never be broken by a
     *  value the patch did not choose.
     *
     *  ### ``reset``
     *
     *  Not a Max message; the patcher family's word, with the meaning
     *  ``.counter``, ``.accum``, ``.peak``, ``.past`` and ``.change`` already
     *  give it — back to how the object was created, which here is simply a
     *  stored 0. **Silent**, and the silence is the whole reason it exists: a
     *  patch that wants the gate re-armed cannot just send 0, because sending 0
     *  bangs the falling outlet and delivers a note-off the patch did not mean.
     *  ``reset`` is the only way back to the initial state without an event, and
     *  without it there would be none.
     *
     *  Max has no ``set`` and neither does this object. On ``.change``, ``set``
     *  moves a stored value that a *value* outlet is compared against; here the
     *  only thing a silent store could do that ``reset`` does not already do is
     *  arm the object *high*, and a patch that wants that can send a number and
     *  ignore the bang. Inventing the message would mean guessing at a spelling
     *  Max never gave it.
     *
     *  ### What this object does not accept
     *
     *  **A symbol.** Zero-ness is a numeric predicate and a symbol is neither
     *  zero nor non-zero, so there is no honest edge to report. A message that
     *  is neither a word this object knows nor a list beginning with a number is
     *  ignored, as in ``.slide``, ``.mean``, ``.accum``, ``.maximum``, ``.peak``,
     *  ``.past`` and ``.change``.
     *
     *  **A list of numbers**, as such. Max's ``togedge`` has one inlet and no
     *  list method, so Max's list distribution sends the first element to the
     *  ``int`` method and drops the rest. Reproduced: ``5 6`` is the number 5.
     *
     *  ### One numeric type, and a deviation from Max's ``int``
     *
     *  Max documents an ``int`` method only, which means a float reaching Max's
     *  ``togedge`` is truncated first — and so **0.5 would be a zero there**.
     *  That is not reproduced, and this is the one place this object knowingly
     *  departs from the reference. The patcher has one numeric type (the reason
     *  ``.accum``, ``.maximum``, ``.peak``, ``.past`` and ``.change`` all give),
     *  and truncating here would break the object's headline use case outright:
     *  an envelope between 0 and 1 would read as *permanently zero* except at
     *  full scale, so the gate would never open. Zero-ness is tested on the
     *  float as it arrived, which is also exactly what ``.change``'s edge
     *  outlets do — and the two must not disagree.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlet, not by
     *  the DSP tick — and the emitting step lives in Receive(), called only from
     *  the handlers Max documents as emitting. This is the rule the family
     *  establishes: a hot inlet fires CalculateIfReady() after *every* message it
     *  accepts, so a Calculate() that emitted would make ``reset`` emit too — the
     *  one thing it must not do — and would re-bang on every DSP tick, breaking
     *  the strict alternation that is the object's whole contract.
     *
     *  Nothing on any path allocates, locks or blocks. A number is one finiteness
     *  test, two comparisons and at most one SendBang. A message is one
     *  fixed-length string comparison plus at most one number read through the
     *  shared, allocation-free and locale-free ``ReadNumericToken`` into a stack
     *  buffer — no ``substr``, no ``std::stof``, no ``std::to_string``, no
     *  locale. The outlets carry bangs, so there is no ``std::string`` on the
     *  message path at all, and the object holds no container of any kind.
     */
    PATCHER_CLASS(gTogEdge, YSE::OBJ::G_TOGEDGE)
    _NO_MESSAGES
    _NO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)
    _BANG_IN(SetBang)
    _LIST_IN(SetList)

    _HAS_GUI

    /**
     *  @brief The value the next input is compared against. Always finite.
     *
     *  Only its zero-ness is ever tested, but the whole number is exposed
     *  because the tests need to tell "the non-finite input was ignored" from
     *  "it was read as something", and a bare bit could not say which.
     */
    float Stored() const {
      return stored;
    }

    /** @brief Whether the object is currently on the non-zero side — the one bit
     *         of state that actually decides what the next input does. */
    bool IsHigh() const {
      return stored != 0.f;
    }

  private:
    // A number arrived. Report the edge it crossed, if it crossed one, and store
    // it. The only path that emits.
    void Receive(float value, YSE::THREAD thread);

    // The value the next input is compared against. Always finite: the inlet
    // refuses a non-finite number and there is no other way in, so the strict
    // alternation of the two outlets cannot be broken by a value the patch did
    // not choose.
    float stored = 0.f;
  };
}
}
