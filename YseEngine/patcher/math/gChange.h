#pragma once
#include "../pObject.h"
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief True when @p previous was zero and @p current is not — the rising
     *         half of Max's zero/non-zero edge report.
     *
     *  Two lines, and a free function rather than a member because ``.change``
     *  is not the only object that owes this exact answer: ``change``'s middle
     *  and right outlets *are* ``togedge``'s two outlets (#469), and the rule
     *  has one non-obvious corner that the two must not drift apart on. ``-0.f
     *  == 0.f`` is true in IEEE-754, so a negative zero counts as zero on both
     *  sides — which is right, since it *is* zero, and which a hand-written
     *  ``previous != 0`` in two separate files would sooner or later disagree
     *  about.
     *
     *  Both take finite arguments. A NaN would answer false to ``== 0.f`` and
     *  false to ``!= 0.f``'s negation in ways that read as a transition, which
     *  is one of the reasons ``.change`` refuses a non-finite input outright
     *  rather than storing it.
     */
    inline bool ZeroToNonZero(float previous, float current) {
      return previous == 0.f && current != 0.f;
    }

    /** @brief True when @p previous was non-zero and @p current is zero — the
     *         falling half. See ZeroToNonZero(). */
    inline bool NonZeroToZero(float previous, float current) {
      return previous != 0.f && current == 0.f;
    }

    /**
     *  @brief Pass a number on only when it differs from the last one —
     *         ``.change`` (issue #468).
     *
     *  Max's ``change``. A control source that polls at frame rate sends the
     *  same number over and over; the parameter downstream only needs to hear
     *  about the ones that actually moved. This object is the filter in
     *  between, and in a patcher whose message queue is bounded (#225) that is
     *  not merely tidiness — a slider held still can otherwise fill the queue
     *  with a value nothing downstream will do anything with.
     *
     *  ### The object looks trivial and is not
     *
     *  "Emit when the input differs from the last input" leaves four questions
     *  unanswered, and Max answers three of them explicitly.
     *
     *  #### 1. What the *first* value does
     *
     *  There is no previous value to differ from, so the object has to start
     *  somewhere, and Max says where: "Sets the initial value for comparison to
     *  incoming numbers. If there is no argument, the initial value is 0."
     *
     *  So the stored value starts at the creation argument, **not** at "nothing
     *  received yet", and the consequence is the one thing about this object
     *  that surprises everybody once: a bare ``.change`` sent 0 as its very
     *  first message emits **nothing**, because 0 is what it already held. Sent
     *  5 first, it emits 5. ``.change 5`` is the mirror image — 5 is swallowed
     *  and 0 comes out.
     *
     *  The alternative, "the first value always passes because there was no
     *  previous one", was rejected because it is not Max and because it makes
     *  the creation argument meaningless: the whole point of ``.change 5`` is
     *  to declare what the patch already believes the value to be, so that
     *  loading a patch does not fire an event for a parameter that has not
     *  moved. An object that always emitted its first input could not express
     *  that, and the argument would be decoration.
     *
     *  #### 2. Float equality is exact
     *
     *  ``==``, with no tolerance, for the reason ``.sel`` (#465) sets out from
     *  the other side. A tolerance in a *matcher* makes it match values it was
     *  not given; a tolerance in a *suppressor* is worse, because it makes the
     *  object swallow values that genuinely differ — and swallowing is
     *  invisible. A patch whose parameter stopped updating at the fourth
     *  decimal place has no symptom to trace back to a fuzzy comparison in a
     *  box that is documented as passing changes on.
     *
     *  The cost of exactness is the familiar one: two computations that "should"
     *  give the same number may not, because 0.1 + 0.2 is not 0.3 in binary
     *  floating point, and this object will dutifully report the difference. A
     *  ``.round`` on the way in is the fix, and it has the merit of putting the
     *  patch's actual tolerance somewhere a reader can see it.
     *
     *  Negative zero is *not* a change from zero: ``-0.f == 0.f`` is true in
     *  IEEE-754, and they are the same number.
     *
     *  #### 3. Non-finite input is refused, not read as 0
     *
     *  The patcher's usual substitution — read a NaN or an infinity as 0 — is
     *  wrong here for exactly the reason it is wrong for ``.sel``, and this
     *  object gives it a third answer that neither of the obvious two can give.
     *
     *  - **Reading it as 0** would emit a 0 the patch never sent and store it,
     *    so the next genuine 0 would then be silently swallowed. A comparator
     *    that invents a value corrupts the very state it exists to track.
     *  - **Storing it raw** is worse. A NaN compares unequal to everything
     *    *including itself*, so a stream of NaNs would emit on every single
     *    message — the exact repetition-flood this object exists to stop — and
     *    a stored NaN would then make the next real value look "different" no
     *    matter what it was.
     *
     *  So a non-finite number is **ignored**: nothing is emitted, no outlet
     *  fires, and the stored value is left exactly as it was. That is the only
     *  option under which "``.change`` suppresses repetitions" stays true for
     *  every possible input, and it keeps a load-bearing invariant: **the
     *  stored value is always finite**, whether it came from the creation
     *  argument, from ``set``, or from the inlet. The zero/non-zero outlets
     *  need that invariant too — a NaN is neither zero nor usefully non-zero,
     *  so there is no honest edge for them to report.
     *
     *  The creation argument gets the same treatment for free: tokens are read
     *  through the shared ``ReadNumericToken``, which refuses a non-finite
     *  token, so ``.change inf`` is a ``.change`` whose argument was not a
     *  number and which therefore starts at 0.
     *
     *  #### 4. ``set`` must not emit, or it is useless
     *
     *  Max: "Replaces the stored value without triggering output." That is the
     *  whole message. If ``set 5`` emitted, it would be a spelling of sending 5
     *  at the inlet and would have no reason to exist; what makes it useful is
     *  precisely that it moves the object's idea of "current" without telling
     *  anybody, so that a patch can re-synchronise the filter with a value that
     *  reached the parameter by some other route. Neither the value outlet nor
     *  the two edge outlets fire.
     *
     *  ### The three outlets, and their overlap with ``.togedge``
     *
     *  Max gives ``change`` three outlets and only the first is what the
     *  object's one-line summary describes:
     *
     *  - outlet 0 (float) — the number, when it differs from the stored one.
     *  - outlet 1 (int) — "If the stored value is 0 and the input is not 0, 1
     *    is sent out; otherwise nothing is sent out."
     *  - outlet 2 (int) — "If the stored value is not 0 and the input is 0, 1
     *    is sent out; otherwise nothing is sent out."
     *
     *  The last two are ``togedge`` (#469) built into the object, and they are
     *  ported because Max ships them: a patch translated from Max would
     *  otherwise silently lose two cords. Note that "the stored value" means
     *  the value held *before* this input, so both are evaluated against the
     *  previous state and then the state is replaced.
     *
     *  Neither edge outlet can fire on a repetition — 0 after 0 is not a rising
     *  edge and 5 after 5 is not a falling one — so they are a strict subset of
     *  the events outlet 0 reports in the default mode. They are *not*
     *  suppressed in the ``+`` / ``-`` modes below: Max's Output section is not
     *  mode-qualified, and the zero-crossing question is orthogonal to which
     *  comparison the value outlet is making.
     *
     *  The three fire **right to left**, the order ``.mean``, ``.cartopol`` and
     *  ``.peak`` already use, so whatever the value on outlet 0 triggers
     *  downstream already sees the matching edge reports rather than the
     *  previous message's. The stored value is settled before any of them, so
     *  nothing reached from an outlet can observe the object half-updated.
     *
     *  ### The modes
     *
     *  Max's second creation argument and its ``mode`` message, which turn the
     *  object from a difference detector into a direction detector:
     *
     *  - default — send the number when it differs.
     *  - ``+`` — send **1** when the number is greater than the previous one.
     *  - ``-`` — send **-1** when the number is less than the previous one.
     *
     *  ``mode +``, ``mode -`` and a bare ``mode`` (back to the default) switch
     *  it live. The stored value is replaced by every number received in every
     *  mode — it is "the previously received number", not "the last number that
     *  was emitted" — so a ``.change +`` fed 5, 3, 4 emits nothing, nothing,
     *  then 1, because 4 beats the 3 it actually received and not the 5 it
     *  last reported on.
     *
     *  Outlet 0 stays a *float* outlet in all three modes, carrying 1 and -1 as
     *  floats: an outlet's type is part of the object's shape and cannot depend
     *  on a message that arrives later.
     *
     *  A ``mode`` with a flag this object does not know keeps the mode it had,
     *  which is the answer ``.peak`` gives a malformed ``set``: a message that
     *  cannot be understood should not silently reconfigure the object.
     *
     *  ### ``reset``
     *
     *  Not a Max message; the patcher family's word, with the meaning
     *  ``.counter``, ``.accum``, ``.peak`` and ``.past`` already give it — back
     *  to how the object was created, which here is the ``initial`` argument
     *  *and* the creation mode. Silent. Without it there is no way back to the
     *  creation argument after a ``set`` or a ``mode``, and a patch should not
     *  have to repeat its own creation argument in a message box.
     *
     *  ### What this object does not accept
     *
     *  **A bang.** Max documents none, and there is no honest answer: outlet
     *  0's meaning is "this number is different from the last one", so a bang
     *  would have to either fabricate a change or do nothing. The stored value
     *  is reachable for a GUI through GetGuiValue() and for a patch through the
     *  object it is filtering.
     *
     *  **A symbol.** The issue's notes ask for symbol input; Max's ``change``
     *  has no symbol or ``anything`` method, and the deviation would not be a
     *  small one. Two of the three outlets are numeric predicates — a symbol is
     *  neither zero nor non-zero — and two of the three modes are *orderings*,
     *  which no set of symbols has. An object that suppressed repeated symbols
     *  would be a different object with one outlet and no modes, and inventing
     *  it inside this one would leave two outlets and two modes permanently
     *  undefined. So a message that is neither a word this object knows nor a
     *  list beginning with a number is ignored, as in ``.slide``, ``.mean``,
     *  ``.accum``, ``.maximum``, ``.peak`` and ``.past``.
     *
     *  **A list of numbers**, as such. Max's ``change`` has one inlet and no
     *  list method, so Max's list distribution sends the first element to the
     *  ``int`` / ``float`` method and complains about the rest. Reproduced:
     *  ``5 6`` is the number 5 and the 6 is dropped. Guessing at anything else
     *  — a reduction, a two-element idiom like ``.peak``'s — would invent
     *  behaviour Max does not have.
     *
     *  ### One numeric type
     *
     *  Everything is a float and outlet 0 is a float outlet. Max's
     *  int-unless-the-creation-argument-has-a-decimal-point duality is not
     *  reproduced, for the reason ``.accum``, ``.maximum``, ``.peak`` and
     *  ``.past`` set out: ``.change 5`` and ``.change 5.0`` are the same
     *  parameter string in this patcher, so the mode would have to be guessed
     *  from the text of an argument a JSON round trip is free to normalise —
     *  and guessing integral would round the values this object passes through,
     *  turning a change filter into a change-and-quantise filter. A patch that
     *  wants integers puts a ``.round`` on the outlet. The two edge outlets
     *  *are* int outlets, because 1 is the only value they can carry.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlet, not by
     *  the DSP tick — and the emitting step lives in Receive(), called only
     *  from the handlers Max documents as emitting. This is the rule ``.slide``,
     *  ``.mean``, ``.accum``, ``.maximum``, ``.peak`` and ``.past`` establish: a
     *  hot inlet fires CalculateIfReady() after *every* message type it
     *  accepts, so a Calculate() that emitted would make ``set``, ``mode`` and
     *  ``reset`` emit too — the one thing all three must not do — and would
     *  re-send the last value on every DSP tick, which is precisely the
     *  repetition flood this object exists to prevent.
     *
     *  Nothing on any path allocates, locks or blocks. A number is one
     *  finiteness test, three comparisons and at most three Sends. A message is
     *  a fixed-length prefix comparison against four words plus at most one
     *  number read through the shared, allocation-free and locale-free
     *  ``ReadNumericToken`` into a stack buffer — no ``substr``, no
     *  ``std::stof``, no ``std::to_string``, no locale. The outlets carry a
     *  float and two ints, so there is no ``std::string`` on the message path
     *  at all. The only ``std::vector`` in the object is the parameter token
     *  list, which is touched on the control thread by ClearParams() /
     *  ParseParams() and never read by a handler.
     */
    PATCHER_CLASS(gChange, YSE::OBJ::G_CHANGE)
    _NO_MESSAGES
    _NO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)
    _LIST_IN(SetList)

    _PARM_CLEAR
    _PARM_PARSE

    _HAS_GUI

    /** @brief Which comparison outlet 0 reports on — Max's ``mode``. */
    enum class Mode {
      DIFFERENT, ///< Max's default: send the number when it differs.
      GREATER, ///< Max's ``+``: send 1 when the number is greater.
      LESS, ///< Max's ``-``: send -1 when the number is less.
    };

    /**
     *  @brief The value the next input is compared against. Always finite.
     *
     *  Exposed because the stored value *is* the object, and asserting it only
     *  through the outlet would leave "swallowed because it was a repetition"
     *  and "swallowed because the stored value was wrong" indistinguishable —
     *  which is the whole failure mode a suppressor has.
     */
    float Stored() const {
      return stored;
    }

    /** @brief The mode in force, which ``mode`` changes and ``reset`` restores. */
    Mode CurrentMode() const {
      return mode;
    }

    /** @brief The creation argument: where the stored value starts and what
     *         ``reset`` returns to. Always finite. */
    float Initial() const {
      return initial;
    }

    /** @brief The creation mode, which ``reset`` returns to. */
    Mode InitialMode() const {
      return initialMode;
    }

  private:
    // A number arrived. Update the edge reports and the stored value, and emit
    // whatever the current mode says. The only path that emits.
    void Receive(float value, YSE::THREAD thread);

    // Read `mode`'s flag out of the characters at @p text. Returns false when
    // the flag is one this object does not know, in which case the mode is left
    // alone.
    static bool ReadMode(const char* text, std::size_t length, Mode& out);

    // The creation arguments, as tokens. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler.
    std::vector<std::string> args;

    // The value the next input is compared against. Always finite: the inlet
    // refuses a non-finite number and ReadNumericToken refuses a non-finite
    // token, so there is no path that can put one here.
    float stored;

    // Which comparison outlet 0 is making.
    Mode mode;

    // What the object was created with, and what `reset` returns to.
    float initial;
    Mode initialMode;
  };
}
}
