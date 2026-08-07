#pragma once
#include "../pObject.h"
#include <cstddef>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Reverse the order of a pair of numbers — ``.swap`` (issue #476).
     *
     *  Max's ``swap``, whose one-line summary is "Swap two numbers":
     *  "``swap`` swaps the values of its inlets, preserving right-to-left
     *  ordering."
     *
     *  Two inlets and two outlets, and the whole object is one sentence from the
     *  Max reference: "The number is sent out the right outlet, then the number
     *  in the right inlet is sent out the left outlet." So what arrives on the
     *  left comes back out on the *right*, and what was stored on the right
     *  leaves on the *left* — the pair crosses over on its way through.
     *
     *  ### Why a patch wants a box for this
     *
     *  Every two-inlet object in this patcher is hot on the left and cold on the
     *  right: ``.-``, ``./``, ``.%``, ``.pow``, ``.atan2``, ``.split``,
     *  ``.past`` all fire on inlet 0 and merely store on inlet 1. That fixes the
     *  operand order at the *box*, and a patch whose values arrive the other way
     *  round has to either rewire the two cords — which is exactly the edit that
     *  is hard to read back later, since the crossing is invisible once the
     *  patch is saved — or grow a ``.trigger`` and a cold-inlet dance to reorder
     *  them by hand. ``.swap`` is that reordering as one box: put it in front of
     *  the arithmetic and the cords stay straight while the operands arrive
     *  reversed.
     *
     *  The obvious question is why ``.!-`` and ``.!/`` (#439) exist if this does.
     *  They are the *fused* form for the two operators where reversal is common
     *  enough to be worth its own box, and they cost one object where this costs
     *  two; ``.swap`` is the general answer, and the only one available for
     *  ``.%``, ``.pow``, ``.atan2``, ``.split`` and everything else that has no
     *  reversed twin. Neither makes the other redundant.
     *
     *  ### One object, not two: why there is no ``.fswap``
     *
     *  Max ships ``swap`` and ``fswap``, and the reference is explicit that the
     *  *only* difference between them is a type declaration: "The second
     *  outlet's (and first inlet's) type is int or float depending on whether
     *  you use the name ``swap`` or ``fswap``." Max needs two boxes because a
     *  Max outlet has one fixed atom type, so an object that must be able to
     *  carry a float has to be a different object from one that carries an int.
     *
     *  This patcher has no such constraint. An outlet may be declared
     *  ``OUT_TYPE::ANY`` and an inlet may register an int handler *and* a float
     *  handler, so one box can carry whichever of the two arrived — which is
     *  what ``.trigger`` (#466), ``.route`` (#469), ``.gate``, ``.buddy`` (#475)
     *  and every other forwarding object in the patcher already do. So ``.swap``
     *  forwards each number as the kind it arrived as: an int in comes out an
     *  int, a float in comes out a float, on either side, independently. That is
     *  ``swap``'s behaviour for the values ``swap`` can carry and ``fswap``'s
     *  behaviour for the values ``fswap`` can carry, in one object, and a second
     *  box would differ from this one in nothing at all.
     *
     *  It is worth being precise about why this is *not* the same call
     *  ``.maximum`` / ``.minimum`` (#462) made when they deliberately refused
     *  Max's int/float duality and became float-only. Those objects **compute**:
     *  they compare, and the result is a new number whose type is a guess the
     *  object would have to make, where guessing integral silently rounds. This
     *  object computes nothing — it is a **forwarder**, and the number leaving
     *  is the number that arrived. Widening an int to a float in transit would
     *  therefore be a side effect of the reordering, visible downstream in every
     *  object that distinguishes the two (``.route 1``, ``.sel``, ``.i``,
     *  ``.match``), and inflicted by a box whose entire job was to change
     *  *order*. Preserving the kind is the only reading under which ``.swap``
     *  is transparent.
     *
     *  ### The creation argument
     *
     *  Max: the argument "sets the initial value sent from the left outlet" —
     *  that is, it pre-loads the right inlet's slot, so a bare ``.swap 5``
     *  answers its first input without needing a cord on inlet 1 first. Its
     *  *spelling* decides the kind, which is Max's own rule stated the way this
     *  object can state it: Max says a float argument makes the left outlet a
     *  float outlet, and here ``.swap 5`` starts holding int 5 while
     *  ``.swap 5.`` starts holding float 5. The same int-atom / float-atom test
     *  ``.trigger`` and ``.match`` use (``TokenLooksLikeFloat``) decides it, and
     *  ``Parameters`` stores the argument string verbatim, so the distinction
     *  survives a ``DumpJSON`` / ``ParseJSON`` round trip rather than being
     *  flattened on save.
     *
     *  Max documents one argument; a second and further tokens are ignored, and
     *  a token that is not a whole finite number leaves the default int 0 in
     *  place rather than being read as a partial number.
     *
     *  ### The message grammar
     *
     *  - **int / float on inlet 0** — stored, then the pair is released.
     *  - **int / float on inlet 1** — stored, and nothing is released. The one
     *    cold inlet, as on every other two-inlet object here.
     *  - **bang on inlet 0** — "Swaps and outputs the currently stored numbers."
     *    A replay, not an exchange: it releases the pair without moving either
     *    slot, so two bangs in a row send the same thing twice. (An
     *    implementation that really exchanged the slots would alternate, which
     *    would make ``.swap`` stateful in a way nothing in the reference asks
     *    for.) Before anything has arrived that is int 0 and the argument.
     *  - **list on inlet 0** — "The numbers are stored in ``swap``. The first
     *    number is sent out the right outlet, then the second number is sent out
     *    the left outlet." So a two-number list writes *both* slots and then
     *    releases, which is the only way to set the pair in one message. A
     *    one-number list is the plain number. Each element keeps its own
     *    spelling, so ``1 2.5`` releases an int and a float. Reading stops at
     *    the first token that is not a number, and a message with no number at
     *    the front at all — a bare symbol this object does not know — is ignored
     *    entirely rather than releasing the old pair.
     *
     *  ### The ordering guarantee
     *
     *  Outlet 1 is served first and outlet 0 last, and each send **completes in
     *  full** — the whole subgraph behind it, depth first — before the next one
     *  starts. Max's universal right-to-left rule, the same guarantee
     *  ``.trigger`` (#466), ``.bondo`` (#474) and ``.buddy`` (#475) state and
     *  from the same place: ``outlet::Send*`` walks its target list calling
     *  ``inlet::Set*`` directly, with no queue in between.
     *
     *  It is load-bearing here rather than decorative, because the object exists
     *  to feed a two-inlet box: the idiom is outlet 1 into the *cold* inlet of
     *  the arithmetic and outlet 0 into its *hot* one, so that by the time the
     *  left value lands and fires the operation, the right operand is already in
     *  place. Reverse the order and every result is one input stale — which is
     *  the very bug a patch reached for ``.swap`` to avoid.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing. The object is driven by its inlets, and an
     *  emitting ``Calculate()`` would re-release the pair on every DSP tick from
     *  a stimulus no patch sent — the rule ``.sel``, ``.trigger``, ``.past`` and
     *  ``.bondo`` establish.
     *
     *  Nothing on any path allocates, locks or blocks. Both slots are two ints
     *  and two floats' worth of POD, the list reader walks the message text into
     *  a stack pair through ``ReadNumericToken`` (no allocation, no locale, no
     *  exception), and a release is two ``Send`` calls with no formatting and no
     *  conversion.
     *
     *  The pair is **snapshotted before the first send**, as ``.onebang``,
     *  ``.togedge``, ``.bondo`` and ``.buddy`` settle their state, because the
     *  send path is synchronous and re-entrant: a patch that loops outlet 1 back
     *  into inlet 1 re-enters here *inside* the first ``Send``, and the second
     *  outlet must still carry the value the release began with rather than the
     *  one the loop has just written. Such a cycle is bounded by the
     *  ``kMaxSendDepth`` ceiling in ``outlet.cpp``.
     */
    PATCHER_CLASS(gSwap, YSE::OBJ::G_SWAP)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(SetLeftBang)
    _INT_IN(SetLeftInt)
    _FLOAT_IN(SetLeftFloat)
    _LIST_IN(SetLeftList)

    _INT_IN(SetRightInt)
    _FLOAT_IN(SetRightFloat)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief Which of the two number kinds a slot is holding. */
    enum class Kind {
      INT, ///< an int; releases through ``outlet::SendInt``
      FLOAT, ///< a float; releases through ``outlet::SendFloat``
    };

    /** @brief The kind inlet 0's slot is holding — what outlet 1 will send. */
    Kind LeftKind() const {
      return left.kind;
    }

    /** @brief The kind inlet 1's slot is holding — what outlet 0 will send. */
    Kind RightKind() const {
      return right.kind;
    }

    /** @brief The value of inlet 0's slot when it is an ``INT``, else 0. */
    int LeftInt() const {
      return left.kind == Kind::INT ? left.intValue : 0;
    }

    /** @brief The value of inlet 0's slot when it is a ``FLOAT``, else 0. */
    float LeftFloat() const {
      return left.kind == Kind::FLOAT ? left.floatValue : 0.f;
    }

    /** @brief The value of inlet 1's slot when it is an ``INT``, else 0. */
    int RightInt() const {
      return right.kind == Kind::INT ? right.intValue : 0;
    }

    /** @brief The value of inlet 1's slot when it is a ``FLOAT``, else 0. */
    float RightFloat() const {
      return right.kind == Kind::FLOAT ? right.floatValue : 0.f;
    }

  private:
    // One slot. `kind` decides which of the two value fields means anything;
    // the other is stale and never read.
    struct Value {
      Kind kind = Kind::INT;
      int intValue = 0;
      float floatValue = 0.f;
    };

    // **The object.** Send the crossed-over pair, right outlet first. The only
    // path that emits, so there is exactly one place the firing order lives.
    void Emit(YSE::THREAD thread);

    // One slot out of one outlet, as whichever kind the slot holds.
    void Send(std::size_t index, const Value& value, YSE::THREAD thread);

    // Read one whitespace-delimited token as a number, keeping its int/float
    // spelling. False when the token is not a whole finite number, leaving
    // @p slot untouched.
    static bool ReadValue(const char* text, std::size_t length, Value& slot);

    // Load the right slot from the creation argument. Control thread only:
    // called from the constructor and from the two parameter callbacks, all of
    // which run before the object is wired or published.
    void ApplyArgument();

    // The creation argument, verbatim. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler.
    std::string argument;

    // What inlet 0 last received — leaves outlet 1.
    Value left;

    // What inlet 1 last received, or the creation argument — leaves outlet 0.
    Value right;
  };

} // namespace PATCHER
} // namespace YSE
