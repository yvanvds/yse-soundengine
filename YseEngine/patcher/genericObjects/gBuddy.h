#pragma once
#include "../pObject.h"
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Wait until every inlet has data, then release the whole set once —
     *         ``.buddy`` (issue #475).
     *
     *  Max's ``buddy``, whose one-line summary is "Synchronize arriving data":
     *  "Outputs incoming data after something has been received in all inlets."
     *
     *  The rendezvous object. Several sources produce parts of one event at
     *  their own pace — a note number from one branch, a velocity computed by
     *  another, a duration polled from a third — and nothing downstream can act
     *  until all of them have arrived. Without this object a patch either fires
     *  on whichever part lands last and hopes the others are current, or grows a
     *  hand-built "have I seen each one yet" flag per source with a manual
     *  reset. ``.buddy`` *is* that flag set, and the reset is automatic: when the
     *  last missing part arrives, the whole set goes out and the object goes
     *  back to empty.
     *
     *  ### The contrast with ``.bondo``, which is the thing to get right
     *
     *  Max lists ``bondo`` first under See Also, and the two objects look
     *  identical from the outside — N inlets, N outlets, values released right
     *  to left — while having *opposite* release policies. Getting them the
     *  wrong way round produces a patch that is subtly, intermittently wrong
     *  rather than broken, so it is worth stating the difference three ways:
     *
     *  - ``.bondo`` releases on **any** input; ``.buddy`` releases only once
     *    **every** inlet has one.
     *  - ``.bondo``'s slots **persist** — a release re-sends what it sent last
     *    time for the inlets that did not move. ``.buddy``'s slots are
     *    **consumed** — Max: "then waits until data has arrived again in all
     *    inlets" — so nothing is ever sent twice.
     *  - ``.bondo`` never emits a stale-free guarantee about *arrival*, only
     *    about *coherence*: its unwritten inlets release int 0. ``.buddy`` never
     *    emits anything until it has been given a real value for every inlet, so
     *    a reader is guaranteed that every element of the set was actually sent
     *    by the patch.
     *
     *  Use ``.bondo`` to keep a set of parameters coherent while any of them is
     *  edited; use ``.buddy`` to assemble one event out of parts that arrive
     *  separately.
     *
     *  ### Every inlet is hot, and none of them is a trigger
     *
     *  Max says "In any inlet" on every method, so there are no cold inlets
     *  here either — but unlike ``.bondo`` an arrival is not a release, it is a
     *  *vote*. The release is a property of the set, not of the message that
     *  completed it, which is why there is no trigger inlet and no ``bang``
     *  meaning "send now": a partial set has nothing complete to send.
     *
     *  A second value arriving at an inlet that already has one simply replaces
     *  it. Max stores one value per "location", and the newest is the one the
     *  release will carry — a queue would make the object hold arbitrarily many
     *  values (an allocation on a message path) and would pair a fresh reading
     *  from one source with a stale one from another, which is exactly what the
     *  object exists to prevent.
     *
     *  ### ``bang`` is the number 0
     *
     *  Max: "bang: In any inlet: Same as sending the number 0." So a bang is not
     *  a trigger and not a query — it is a **value**, and the inlet it lands in
     *  counts as having received data. That is what lets a source that has
     *  nothing to say but "I am ready" take part in the rendezvous, and it is
     *  the whole of the object's bang behaviour: a bang into the last empty
     *  inlet releases the set, with int 0 leaving that outlet.
     *
     *  ### ``clear``, and why it is left-inlet only
     *
     *  Max: "clear: In left inlet: Deletes all values stored in the inlets." The
     *  escape hatch for a rendezvous that will never complete, because one
     *  source stopped sending. It empties every slot and emits nothing.
     *
     *  The scoping to the left inlet is Max's and is kept literally: in any
     *  other inlet the word is an ordinary symbol and is *stored* there, filling
     *  that slot like any other value. That reads as a trap and is the right
     *  reading — those inlets are fed by other objects' outlets, and a message
     *  whose text happened to be ``clear`` silently wiping the object's state
     *  from a data inlet would be far worse than the same message being stored
     *  as the symbol it is. The word is matched **bare**, as ``.uzi``'s
     *  ``pause`` and ``.change``'s ``mode`` are, so ``clear 1`` is a list and a
     *  list is data.
     *
     *  There is no ``set``. ``.bondo`` needs one because it has no cold inlet to
     *  write quietly through; here a store that did *not* count towards the
     *  rendezvous would be a store that can never be released, and one that did
     *  count would just be the plain message.
     *
     *  ### What comes out is what went in
     *
     *  Max: "buddy sends the received messages out their corresponding outlets."
     *  A slot holds whichever of int, float and text last arrived, and releases
     *  it **as itself** — the outlets are ``ANY`` for that reason. Unlike
     *  ``.bondo`` a list is *not* spread across the outlets to the right: Max
     *  documents spreading for ``bondo`` and documents nothing of the kind here,
     *  where ``list`` and ``anything`` share one sentence with ``int`` and
     *  ``float``. So a list occupies exactly the one inlet it arrived at and
     *  leaves the matching outlet verbatim, which is also what makes ``.buddy``
     *  usable as a rendezvous for *list-valued* sources without a mode flag.
     *
     *  ### The ordering guarantee
     *
     *  Max: "When a data has arrived in each inlet, it is sent out the outlets,
     *  in order from right to left." Outlet *n-1* is served first and outlet 0
     *  last, and each send **completes in full** — the whole subgraph behind it,
     *  depth first — before the next one starts. The same guarantee ``.trigger``
     *  (#466), ``.bangbang`` (#467), ``.uzi`` (#473) and ``.bondo`` (#474) state,
     *  from the same place: ``outlet::Send*`` walks its target list calling
     *  ``inlet::Set*`` directly, with no queue in between.
     *
     *  It is load-bearing for the same idiom: wire outlet 0 into whatever *acts*
     *  and outlets 1..n-1 into whatever *stores*, and the actor is guaranteed
     *  that the rest of the assembled event is already in place.
     *
     *  ### One release is one logical event
     *
     *  The set leaves inside the call frame of the one ``inlet::Set*`` that
     *  completed it, so ``CurrentMessageEvent()`` (see ``inlet.h``, #471) hands
     *  every message of a release the same id. ``.buddy`` into ``.next``
     *  therefore gives one bang out the separated outlet and the rest out the
     *  continued one, however many outlets there are. Pinned in the tests
     *  against a real ``.next`` rather than against the clock directly.
     *
     *  ### The object empties itself *before* it sends
     *
     *  Not tidiness — correctness. The send path is synchronous and re-entrant:
     *  a patch that loops an outlet back into an inlet re-enters here *inside*
     *  the ``Send``, and if the slots were still full at that moment the
     *  re-entrant store would find a complete set and release again, and again,
     *  until the ``kMaxSendDepth`` ceiling in ``outlet.cpp`` cut it off. Emptied
     *  first, the same patch stores one value into an otherwise empty object and
     *  stops, which is what Max's "then waits until data has arrived again in
     *  all inlets" describes. ``.onebang``, ``.togedge``, ``.next`` and
     *  ``.match`` settle their state before sending for the same reason.
     *
     *  The values still have to survive the emptying, so they are moved into a
     *  second fixed-size table and sent from there. The move is a **swap** of
     *  the two slots' text buffers rather than a copy, so it cannot allocate
     *  however long the text is — both buffers are reserved at construction and
     *  simply change hands.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing. The object is driven by its inlets, and an
     *  emitting ``Calculate()`` would release on a DSP tick from a stimulus no
     *  patch sent — the rule ``.sel``, ``.trigger``, ``.past`` and ``.bondo``
     *  establish.
     *
     *  Nothing on any path allocates, locks or blocks. **Storage is fixed
     *  capacity**: both slot tables are sized once, on the control thread, by
     *  the parameter callbacks, before the object is wired or published, and
     *  never resized afterwards — at most ``MAX_PORTS`` (256) slots, the ceiling
     *  ``.sel``, ``.trigger``, ``.mean`` and ``.bondo`` already use, and an
     *  argument asking for more is clamped. Each slot's text buffer is reserved
     *  to ``TEXT_CAPACITY`` (256 characters) at the same time, so storing a
     *  symbol or a list up to that length is a copy into an existing buffer; a
     *  longer one costs one reallocation, the same bound ``.bondo`` and
     *  ``.regexp`` accept. Storing a number touches no buffer at all, and a
     *  release is a swap per slot followed by a reverse walk doing one ``Send``
     *  per slot, with no formatting and no conversion.
     */
    PATCHER_CLASS(gBuddy, YSE::OBJ::G_BUDDY)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(SetBang)
    _INT_IN(SetInt)
    _FLOAT_IN(SetFloat)
    _LIST_IN(SetList)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief Most inlet/outlet pairs the object will build.
     *
     *  256, the ceiling ``.sel``, ``.trigger``, ``.mean`` and ``.bondo`` already
     *  use. Max documents no limit; a larger creation argument is clamped rather
     *  than honoured, which is what makes the slot tables a fixed allocation.
     */
    static constexpr int MAX_PORTS = 256;

    /** @brief Fewest ports the object will build. */
    static constexpr int MIN_PORTS = 1;

    /**
     *  @brief Ports with no creation argument.
     *
     *  Max: "If there is no argument, there are two inlets and two outlets."
     */
    static constexpr int DEFAULT_PORTS = 2;

    /**
     *  @brief Characters of text one slot can hold without reallocating.
     *
     *  Both tables' buffers are reserved to this at construction, so storing a
     *  symbol or a list this long or shorter is a copy into memory the object
     *  already owns. Longer text is still stored — correctness never depends on
     *  the bound — at the cost of one reallocation on the storing thread.
     */
    static constexpr std::size_t TEXT_CAPACITY = 256;

    /** @brief What one slot is holding. */
    enum class Held {
      NONE, ///< nothing has arrived at this inlet since the last release
      INT, ///< an int (a bang counts as int 0)
      FLOAT, ///< a float
      LIST, ///< text — a list, or Max's ``anything``
    };

    /** @brief How many inlet/outlet pairs the object has. At least one. */
    int PortCount() const {
      return (int)slots.size();
    }

    /**
     *  @brief How many inlets are still holding a value waiting to be released.
     *
     *  0 immediately after a release or a ``clear``, and never equal to
     *  ``PortCount()`` between messages — reaching it *is* the release.
     */
    int FilledCount() const {
      return filled;
    }

    /** @brief What slot @p index is holding. ``NONE`` for an index out of range. */
    Held HeldKind(int index) const;

    /** @brief The value of an ``INT`` slot, else 0. */
    int HeldInt(int index) const;

    /** @brief The value of a ``FLOAT`` slot, else 0. */
    float HeldFloat(int index) const;

    /** @brief The text of a ``LIST`` slot, else "". */
    std::string HeldText(int index) const;

  private:
    // One inlet/outlet pair. `held` decides which of the value fields means
    // anything; a NONE slot is an inlet the rendezvous is still waiting for.
    struct Slot {
      Held held = Held::NONE;
      int intValue = 0;
      float floatValue = 0.f;
      std::string text; // LIST only; reserved to TEXT_CAPACITY when shaped
    };

    // Record one arrival in `inlet` and release when that completes the set.
    // The single path every message kind funnels into, so the rendezvous rule
    // is decided in exactly one place.
    void Arrive(int inlet, Held kind, int intValue, float floatValue, const std::string* text,
                YSE::THREAD thread);

    // **The object.** Hand the whole set to `outgoing`, empty the slots, then
    // send right to left. Emptying first is what stops a patch that loops an
    // outlet back into an inlet from re-releasing inside its own Send.
    void Release(YSE::THREAD thread);

    // Rebuild the inlets, outlets and both slot tables from the current port
    // count, docs included. Control thread only: called from the constructor and
    // from the parameter callbacks, all of which run before the object is wired
    // or published.
    void ShapePorts();

    // The creation arguments, as tokens. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler.
    std::vector<std::string> creationArgs;

    // The set being assembled. Sized by ParseParams() / ClearParams() before the
    // object is published and never resized by a message handler, so a release's
    // walk over it cannot race a structural change.
    std::vector<Slot> slots;

    // The set being released. Same size as `slots`, and only ever touched
    // inside Release() — the values live here for exactly as long as the send
    // takes, which is what lets `slots` be emptied before the first Send goes
    // out. Its text buffers are reserved alongside the others, and are swapped
    // with them rather than assigned, so a release cannot allocate.
    std::vector<Slot> outgoing;

    // How many slots are not NONE. Kept incrementally rather than rescanned, so
    // a message costs one compare instead of a walk over 256 slots. Reset to 0
    // by Release() and by `clear`.
    int filled = 0;
  };
}
}
