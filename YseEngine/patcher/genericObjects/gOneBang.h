#pragma once
#include "../pObject.h"
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Let exactly one bang through per arming — ``.onebang`` (issue
     *         #470).
     *
     *  Max's ``onebang``. A bang on the left inlet passes only if the gate has
     *  been armed from the right inlet since the last time one passed, and the
     *  act of passing closes it again. Max: "Allows a bang in the left inlet to
     *  pass through ONLY if a bang has been received in the right inlet. After
     *  that, a bang in the left inlet will not get through again until a bang
     *  has been received again in the right inlet."
     *
     *  That is the standard one-shot: fire a sound on the first trigger of a
     *  burst and ignore the rest until something explicitly re-enables it. The
     *  patcher could already *count* triggers (``.counter``) and *compare* them
     *  (``.sel``, ``.past``), but expressing "once, then not again until I say
     *  so" that way takes three boxes and leaves the re-arm implicit in the
     *  numbers; here the re-arm is a patch cord.
     *
     *  ### The whole object is its state machine
     *
     *  One bit — armed or not — and four things move it:
     *
     *  | event                      | armed        | disarmed      |
     *  |----------------------------|--------------|---------------|
     *  | anything on the left inlet | bang out 0, become disarmed | bang out 1 |
     *  | anything on the right inlet| stays armed  | become armed  |
     *  | ``stop`` on the left inlet | become disarmed | stays disarmed |
     *  | the creation argument      | — | — (sets the state at construction) |
     *
     *  Two properties follow that are worth naming because a plausible
     *  implementation misses them:
     *
     *  - **The right inlet is idempotent.** Arming an already-armed gate is not
     *    an error and does not bank a second pass. Max says the left inlet
     *    passes "only if it has received a bang in its right inlet since the
     *    last time it sent out a bang" — a *since*, not a count — so five arms
     *    followed by five triggers is one pass and four rejects, never five
     *    passes. An implementation holding a credit counter instead of a bit
     *    reads the description the same way and behaves differently the moment
     *    a patch arms twice, which a burst of MIDI or a poll loop does all the
     *    time.
     *  - **Nothing is ever swallowed.** Every message the left inlet accepts
     *    produces exactly one bang, on one outlet or the other; the object
     *    gates *where* a bang goes, not *whether* one happens. That is what
     *    makes the right outlet worth wiring — it is the "you were too early"
     *    signal — and it is why the two outlets together are a complete
     *    accounting of the left inlet's traffic.
     *
     *  ### Two outlets, which the object's name does not suggest
     *
     *  Max: "When onebang receives a bang in its left inlet, it sends a bang
     *  out its left outlet only if it has received a bang in its right inlet
     *  since the last time it sent out a bang. **Otherwise, it sends a bang out
     *  its right outlet.**" So a blocked bang is reported rather than dropped,
     *  and reading only the object's summary ("gate bangs using a bang") would
     *  give a one-outlet object that silently loses that half.
     *
     *  Exactly one of the two fires per message, so there is no firing order to
     *  respect here — unlike ``.trigger``, ``.bangbang``, ``.mean`` or
     *  ``.peak``, whose outlets all fire together and therefore right to left.
     *
     *  ### Everything is a bang
     *
     *  Max gives ``int``, ``float`` and ``list`` the same one-line description
     *  ("In either inlet: Same as a bang") and ``anything`` the description "In
     *  either inlet: Converted to bang". The payload is not merely unused, it
     *  is documented as discarded — the same rule ``.bangbang`` (#467) follows,
     *  and for the same reason: what the object emits carries no value, so
     *  there is nothing an input value could turn into.
     *
     *  So a 0 on the left inlet triggers the gate. That is worth stating
     *  because the number reads like a "don't", and this object is not
     *  ``.gate``: a value of 0 does not mean *off* anywhere in it, on either
     *  inlet. A patch that wants a numeric gate wants ``.gate``.
     *
     *  ### ``stop``, and why it is the only word
     *
     *  Max: "In left inlet: Undoes the effect of a bang in the right inlet." It
     *  is the manual disarm — silent, since Max lists only ``bang`` under
     *  Output — and it makes the object's whole state reachable from the two
     *  inlets without waiting for a trigger to consume the arming.
     *
     *  It is also the **only** word this object knows, and deliberately so.
     *  Everywhere else in the patcher family — ``.change``, ``.togedge``,
     *  ``.peak``, ``.past``, ``.counter`` — a symbol is a message the object
     *  would otherwise ignore, so adding ``reset`` to it costs nothing. Here a
     *  symbol is a *trigger*, because Max converts anything to a bang, so every
     *  word this object learns is a bang it silently stops passing. Inventing
     *  ``reset`` would therefore trade a documented behaviour for a
     *  convenience, and it would buy nothing that is not already reachable:
     *  ``stop`` gives the disarmed state and a message on the right inlet gives
     *  the armed one, which between them are the entire state space, creation
     *  argument included.
     *
     *  Bare, as ``.change``'s ``mode`` and ``.past``'s ``clear`` are bare:
     *  ``stop 1`` is not ``stop``, it is a list, and a list is a bang. The
     *  family's rule is that a message word with an argument is a different
     *  message, and here that rule has teeth — the alternative would make the
     *  object's behaviour depend on text after a word it does not read.
     *
     *  ### ``stop`` on the *right* inlet arms, and does not disarm
     *
     *  The one genuinely ambiguous corner. Max scopes the message to the left
     *  inlet ("In left inlet: ..."), while giving ``int``, ``float``, ``list``
     *  and ``anything`` an explicit "In either inlet" — so the reference draws
     *  the distinction deliberately where it means it. Read literally, a
     *  ``stop`` arriving on the right inlet is not the ``stop`` message at all;
     *  it is one more thing converted to a bang, and it arms.
     *
     *  That is the reading taken here, and it is also the one that keeps the
     *  right inlet honest. The reference describes that inlet with no
     *  conditions — "Resets onebang to permit a bang to be sent out the next
     *  time a bang is received in the left inlet" — so a patch is entitled to
     *  treat a cord into it as *the gate is now open*. If one message text out
     *  of all possible texts closed the gate instead, that guarantee would hold
     *  only for messages whose content the patch had inspected, and a ``.m``
     *  box or a ``.route`` remainder feeding the arming inlet could disarm it
     *  by accident. A disarm is available from the left inlet, where the
     *  reference puts it.
     *
     *  ### Where the object starts
     *
     *  Max: "A non-zero argument sets onebang to permit a bang to be sent out
     *  the left outlet the first time a bang is received in the left inlet." So
     *  the state at construction is a documented choice rather than an
     *  accident, and the default — no argument, or a zero one — is
     *  **disarmed**.
     *
     *  Disarmed is the right default independently of the reference, which is
     *  worth noting since a gate object could plausibly start open: the point
     *  of the object is that a pass is *granted*, and an object that started
     *  armed would let a patch load fire one bang nobody armed it for. A patch
     *  that wants the first trigger to pass says so with ``.onebang 1``.
     *
     *  The argument is a flag, so only its **zero-ness** is read, and it is
     *  read from the float as it arrived rather than from a truncated int.
     *  Max's argument is an ``int`` and would make ``onebang 0.5`` a zero, but
     *  ``.onebang 0.5`` and ``.onebang 1`` are both "non-zero" here for the
     *  reason ``.accum``, ``.maximum``, ``.past``, ``.change`` and ``.togedge``
     *  all give: the patcher has one numeric type, and a flag that flipped on
     *  the third decimal place of a number nobody meant as a value would be a
     *  trap rather than a feature.
     *
     *  A token that is not a whole finite number is not an argument, through
     *  the shared strict ``ReadNumericToken``, so ``.onebang wobble`` starts
     *  disarmed. ``.onebang inf`` does too — non-finite is "not a number" in
     *  this family, as ``.change inf`` starting at 0 already establishes —
     *  which is the one place the flag reading and the "non-zero" wording pull
     *  apart, and the family's consistency wins, because the alternative is
     *  that ``ExprParseFloatList``-style leniency turns the symbol ``5abc``
     *  into an arming argument.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlets, and
     *  the rule the family establishes is that a hot inlet fires
     *  CalculateIfReady() after *every* message it accepts — so a Calculate()
     *  that emitted would make ``stop`` emit too, and would re-fire the gate on
     *  every DSP tick, which for a one-shot is the exact failure the object
     *  exists to prevent.
     *
     *  Nothing on any path allocates, locks or blocks. A trigger is one bool
     *  test, one bool store and one ``SendBang``; an arm is one bool store. The
     *  only text path is a fixed-length comparison against ``stop`` — no
     *  ``substr``, no ``std::stof``, no ``std::to_string``, no locale — and the
     *  object holds no container of any kind. The creation argument is read on
     *  the control thread by the parameter callbacks, before the object is
     *  wired or published, and never afterwards.
     */
    PATCHER_CLASS(gOneBang, YSE::OBJ::G_ONEBANG)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(SetBang)
    _INT_IN(SetInt)
    _FLOAT_IN(SetFloat)
    _LIST_IN(SetList)

    _PARM_CLEAR
    _PARM_PARSE

    _HAS_GUI

    /**
     *  @brief Whether the next message on the left inlet will pass.
     *
     *  The whole of the object's state. Exposed because "nothing came out"
     *  cannot distinguish a gate that closed from a message that was dropped,
     *  and because the silent messages (``stop``, and the arming inlet) have no
     *  other observable effect.
     */
    bool IsArmed() const {
      return armed;
    }

    /**
     *  @brief The state the creation argument asks the object to start in.
     *
     *  Separate from IsArmed() so a test can tell "the argument was read" from
     *  "the object happens to be armed", which after any traffic are no longer
     *  the same question.
     */
    bool InitiallyArmed() const {
      return initialArmed;
    }

  private:
    // A message reached the left inlet. Pass it or reject it, and close the
    // gate if it passed. The only path that emits.
    void Trigger(YSE::THREAD thread);

    // The creation argument, verbatim. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler.
    std::string initialArg;

    // The zero-ness of the creation argument. Max: "A non-zero argument sets
    // onebang to permit a bang to be sent out the left outlet the first time a
    // bang is received in the left inlet."
    bool initialArmed = false;

    // The gate. Set from `initialArmed` at construction and by every message
    // afterwards; the one field a handler writes, as a plain bool store.
    bool armed = false;
  };
}
}
