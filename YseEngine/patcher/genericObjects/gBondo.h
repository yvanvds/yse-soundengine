#pragma once
#include "../pObject.h"
#include "../time/messageScheduler.h"
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Hold one value per inlet and release the whole set together —
     *         ``.bondo`` (issue #474).
     *
     *  Max's ``bondo``, whose one-line summary is "Synchronize a group of
     *  messages": "Synchronizes and outputs a set of inputs when any input is
     *  received."
     *
     *  The problem it solves is *incoherence in transit*. A patch that has to
     *  change three related parameters at once — a filter's frequency, its Q
     *  and its gain; a grain's position, length and pitch — sends them down
     *  three cords, and whatever is downstream sees the new frequency against
     *  the old Q for as long as it takes the second cord to be served. That
     *  gap is inaudible in a diagram and very audible in a patch. ``.bondo``
     *  closes it: the three values are held, and the moment any one of them
     *  changes **all three** go out, so the reader is never handed a mixture
     *  of the old set and the new one.
     *
     *  ### Every inlet is hot, and that is the object
     *
     *  This is the rule to get right, and it is the reverse of almost every
     *  other multi-inlet object in the patcher. ``.+``, ``.sel``, ``.split``
     *  and ``.counter`` all have one hot inlet and cold ones behind it;
     *  ``.bondo`` has **no cold inlets at all**. Max says it once per method
     *  and says it the same way each time — "In any inlet: The input is stored
     *  in the location corresponding to that inlet, and causes anything
     *  previously stored to be sent out its corresponding outlet."
     *
     *  So the object is not "collect, then release on a trigger". It is
     *  "release on *every* change, including the one that just arrived". A
     *  patch does not have to know which of its three parameters moved, and
     *  does not have to route a bang alongside them; whichever one moves
     *  carries the other two out with it. That is why the object is worth a
     *  box: the alternative is a ``.trigger`` fan-out per parameter into a
     *  cold-inlet arrangement per reader, rebuilt every time a parameter is
     *  added.
     *
     *  Its sibling ``buddy`` — which Max lists under See Also and which this
     *  patcher does not have yet — is the opposite policy: wait until *all*
     *  inlets have something, then release once. ``.bondo`` never waits.
     *
     *  ### The two ways to store without releasing
     *
     *  ``set <anything>`` stores into the receiving inlet and emits nothing:
     *  "The word ``set``, followed by any message, stores the input in the
     *  location corresponding to that inlet without triggering any output."
     *  It is how a patch loads a set of values and then releases them in one
     *  go, and it is the only way to write an inlet quietly, since there are
     *  no cold inlets to do it with. Here ``set`` performs *exactly* the store
     *  the same message without the word would have performed — same slot,
     *  same list distribution, same number-or-text decision — and suppresses
     *  only the output. One store path, one rule.
     *
     *  ``bang`` is the other half: "Send all stored messages." It stores
     *  nothing and releases everything, in any inlet. So ``set`` three values
     *  and ``bang`` is the manual mode, and it composes with the automatic one
     *  rather than replacing it.
     *
     *  ### What an inlet that has never been written holds
     *
     *  Max: "If no message has yet been received in a particular inlet, 0 is
     *  sent out of the corresponding outlet." A fresh ``.bondo 3`` banged once
     *  sends 0, 0, 0 — it does not stay silent, and it does not emit a partial
     *  set. That matters for the coherence argument: a reader wired to all
     *  three outlets is guaranteed a complete set on every release, from the
     *  very first one, so it never has to hold a "have I seen this one yet?"
     *  flag of its own.
     *
     *  ### Stored values persist; they are not consumed
     *
     *  A release does not empty the object. Max's wording is that an input
     *  "causes anything previously stored to be sent out" — *previously
     *  stored*, still stored afterwards — and the 0 substitution is documented
     *  only for an inlet that has received nothing *yet*, never for one whose
     *  value has already been sent once. So banging twice in a row sends the
     *  same set twice, and changing one inlet re-sends the other two unchanged.
     *  A consuming object would emit 0s on the second bang, which would make
     *  the set incoherent in exactly the way this object exists to prevent.
     *
     *  ### The ordering guarantee
     *
     *  Outlet *n-1* is served first and outlet 0 last, and each send
     *  **completes in full** — the whole subgraph behind it, depth first —
     *  before the next one starts. Max's universal right-to-left rule, and the
     *  same guarantee ``.trigger`` (#466), ``.bangbang`` (#467) and ``.uzi``
     *  (#473) state, coming from the same place: ``outlet::Send*`` walks its
     *  target list calling ``inlet::Set*`` directly, with no queue in between.
     *
     *  It is load-bearing here for the same reason it is on ``.trigger``. The
     *  idiom is to wire outlet 0 into whatever *acts* and outlets 1..n-1 into
     *  whatever *stores*, so that by the time the leftmost value arrives every
     *  other value of the set is already in place. Reverse the order and the
     *  actor fires against a half-updated set — which is the bug the whole
     *  object is here to prevent, one level further downstream.
     *
     *  ### One release is one logical event
     *
     *  The whole set leaves inside the call frame of the one ``inlet::Set*``
     *  that triggered it, so ``CurrentMessageEvent()`` (see ``inlet.h``, #471)
     *  hands every message of a release the same id. ``.bondo`` into ``.next``
     *  therefore gives one bang out the separated outlet and the rest out the
     *  continued one, however many outlets there are — a release reads as one
     *  event, which is what "synchronize" has to mean for a downstream object
     *  that counts events. Pinned in the tests against a real ``.next`` rather
     *  than against the clock directly.
     *
     *  ### Lists, and the one place this patcher cannot follow Max exactly
     *
     *  Max distinguishes a ``list`` (leading atom is a number) from an
     *  ``anything`` (leading atom is a symbol), and treats them differently: a
     *  list is *spread across* the outlets — "The first element in the list is
     *  sent out the outlet which corresponds to the inlet which received the
     *  list and each subsequent element in the list is sent out each subsequent
     *  outlet" — while an ``anything`` is stored whole in the one slot.
     *
     *  This patcher has no symbol message; text travels as a list message
     *  carrying a string, as ``.sel``, ``.route``, ``.match`` and ``.trigger``
     *  already read symbols. Rather than invent a rule, the leading token
     *  decides, which is *Max's own* test for which method a message reaches:
     *  a message whose first token reads as a whole finite number is a list and
     *  is distributed; anything else is Max's ``anything`` and is stored whole.
     *  Each distributed token is stored as an int when it is spelled as one, a
     *  float when it carries a ``.`` or an exponent — the same int-atom /
     *  float-atom test ``.trigger`` and ``.match`` use — and as text otherwise.
     *  Elements past the last outlet are dropped, as Max drops them.
     *
     *  Max's third creation argument, the symbol ``n``, turns distribution off:
     *  "``bondo`` is able to synchronize lists which arrive in different
     *  inlets", storing and outputting a whole list per outlet. That argument
     *  is supported and is the mode to reach for whenever an inlet's value is
     *  itself a list.
     *
     *  ### The delay argument
     *
     *  Max's optional second creation argument defers a message-triggered
     *  release by that many milliseconds; Max is explicit that a bang is the
     *  exception — "output will be immediate if triggered by a bang". Honoured
     *  here since #628 through the patcher's deferred-message scheduler
     *  (``messageScheduler``), which is what makes the deferral *possible* at
     *  all: arming is wait-free (allowed on a message handler that may be the
     *  audio thread), and delivery happens inside a real dispatch frame on the
     *  patcher's own thread, so a deferred release is still **one logical
     *  event** — the guarantee above keeps holding for exactly the patches
     *  that asked for a delay, where a timer-thread hack would have broken it.
     *
     *  One release is pending at a time, which is the shape a Max ``clock``
     *  gives the original: a message-triggered release while one is pending
     *  *reschedules* it (delay measured from the newest message) rather than
     *  queuing a second, so a burst of stores collapses into one deferred
     *  release of the final set. A bang neither waits nor disturbs the pending
     *  release: it releases immediately, and a rescheduled release still fires
     *  at its own time. The values released are the ones held **when the
     *  release fires**, not when it was armed — the slots are the object's
     *  state and the deferral defers the *release*, not a snapshot.
     *
     *  Two honest fallbacks release immediately instead, both observable and
     *  neither silent about the argument (``RequestedDelay()`` still reports
     *  it): a standalone object outside any patcher has no dispatch to defer
     *  into, and a patcher whose pending set is full (the scheduler is bounded
     *  by design) releases now rather than dropping the set on the floor.
     *
     *  The deferral is quantised to audio blocks on the patcher's block clock —
     *  see ``messageScheduler`` for the timeline contract.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing. The object is driven by its inlets, and an
     *  emitting ``Calculate()`` would re-release the whole set on every DSP tick
     *  from a stimulus no patch sent — the rule ``.sel``, ``.trigger`` and
     *  ``.past`` establish.
     *
     *  Nothing on any path allocates, locks or blocks. **Storage is fixed
     *  capacity**: the slot table is sized once, on the control thread, by the
     *  parameter callbacks, before the object is wired or published, and never
     *  resized afterwards — at most ``MAX_PORTS`` (256) slots, the ceiling
     *  ``.sel``, ``.trigger`` and ``.mean`` already use, and an argument asking
     *  for more is clamped. Each slot's text buffer is reserved to
     *  ``TEXT_CAPACITY`` (256 characters) at the same time, so storing a symbol
     *  or a list up to that length is a copy into an existing buffer; a longer
     *  one costs one reallocation, which is the same bound ``.regexp`` accepts
     *  on its input buffer. Storing a number touches no buffer at all, and a
     *  release is a reverse walk over the table doing one ``Send`` per slot with
     *  no formatting and no conversion.
     *
     *  Every slot is settled before the release begins, as ``.onebang``,
     *  ``.togedge`` and ``.next`` settle theirs, because the send path is
     *  synchronous and re-entrant: a patch that loops an outlet back into an
     *  inlet re-enters here *inside* the ``Send``, and must find the object
     *  already describing the set that is being released. Such a cycle is
     *  bounded by the ``kMaxSendDepth`` ceiling in ``outlet.cpp``, as it is for
     *  ``.trigger`` and ``.bangbang``, which have the same shape.
     */
    PATCHER_CLASS(gBondo, YSE::OBJ::G_BONDO)
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
     *  256, the ceiling ``.sel``, ``.trigger``, ``.past``, ``.mean`` and
     *  ``.vexpr`` already use. Max documents no limit; a larger creation
     *  argument is clamped rather than honoured, which is what makes the slot
     *  table a fixed allocation.
     */
    static constexpr int MAX_PORTS = 256;

    /** @brief Fewest ports the object will build. */
    static constexpr int MIN_PORTS = 1;

    /** @brief Ports with no creation argument — Max's default of 2. */
    static constexpr int DEFAULT_PORTS = 2;

    /**
     *  @brief Characters of text one slot can hold without reallocating.
     *
     *  The per-slot buffer is reserved to this at construction, so storing a
     *  symbol or a list this long or shorter is a copy into memory the object
     *  already owns. Longer text is still stored — correctness never depends on
     *  the bound — at the cost of one reallocation on the storing thread.
     */
    static constexpr std::size_t TEXT_CAPACITY = 256;

    /** @brief What one slot is holding. */
    enum class Held {
      NONE, ///< nothing has reached this inlet yet; releases as int 0
      INT, ///< an int
      FLOAT, ///< a float
      LIST, ///< text — a list, or Max's ``anything``
    };

    /** @brief How many inlet/outlet pairs the object has. At least one. */
    int PortCount() const {
      return (int)slots.size();
    }

    /** @brief What slot @p index is holding. ``NONE`` for an index out of range. */
    Held HeldKind(int index) const;

    /** @brief The value of an ``INT`` slot, else 0. */
    int HeldInt(int index) const;

    /** @brief The value of a ``FLOAT`` slot, else 0. */
    float HeldFloat(int index) const;

    /** @brief The text of a ``LIST`` slot, else "". */
    std::string HeldText(int index) const;

    /**
     *  @brief True when Max's ``n`` creation argument was given: a list is
     *         stored whole in the receiving slot rather than spread across the
     *         slots to its right.
     */
    bool WholeLists() const {
      return wholeLists;
    }

    /**
     *  @brief The delay creation argument, in milliseconds, as the patch typed
     *         it — 0 when none was given.
     *
     *  Honoured since #628 for a message-triggered release inside a patcher
     *  (a bang still releases immediately, as Max's does); see the header for
     *  the reschedule semantics and the two immediate-release fallbacks.
     */
    int RequestedDelay() const {
      return requestedDelay;
    }

    /**
     *  @brief Deferred-release delivery (issue #628): the scheduler calling
     *         back when a delayed release comes due. Emits the whole current
     *         set, exactly as the immediate path does.
     */
    void DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) override;

  private:
    // One inlet/outlet pair. `held` decides which of the value fields means
    // anything; a NONE slot uses none of them and releases as int 0.
    struct Slot {
      Held held = Held::NONE;
      int intValue = 0;
      float floatValue = 0.f;
      std::string text; // LIST only; reserved to TEXT_CAPACITY when shaped
    };

    // **The object.** Send every slot, right to left. The only path that
    // emits, so there is exactly one place the firing order is decided.
    void EmitAll(YSE::THREAD thread);

    // A message-triggered release: immediate without a delay argument, else
    // deferred through the patcher's scheduler (#628) — rescheduling the
    // pending release, Max's one-clock shape. Falls back to the immediate
    // release for a standalone object or a full pending set; see the header.
    void ReleaseOrDefer(YSE::THREAD thread);

    // Store one message into the slot table starting at `inlet`, without
    // emitting. The whole of Max's storage rule, shared by the plain path and
    // by `set`, which is why the two cannot drift apart. `offset` is where the
    // payload starts, past the word `set` when there was one.
    void StoreText(const std::string& text, std::size_t offset, int inlet);

    // Spread whitespace-separated tokens across the slots from `first`
    // rightwards, one token per slot, dropping what does not fit.
    void DistributeTokens(const std::string& text, std::size_t offset, int first);

    // Store one token into slot `index`, as an int, a float or as text.
    void StoreToken(const char* token, std::size_t length, int index);

    // Rebuild the inlets, outlets and slot table from the current port count,
    // docs included. Control thread only: called from the constructor and from
    // the parameter callbacks, all of which run before the object is wired or
    // published.
    void ShapePorts();

    // The creation arguments, as tokens. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler.
    std::vector<std::string> creationArgs;

    // The held set. Sized by ParseParams() / ClearParams() before the object is
    // published and never resized by a message handler, so the release's walk
    // over it cannot race a structural change.
    std::vector<Slot> slots;

    // Max's `n` flag. Control thread only, as the arguments are.
    bool wholeLists = false;

    // Max's delay argument. 0 releases synchronously; a positive value defers a
    // message-triggered release through the patcher's scheduler (#628). Written
    // by ShapePorts before the object is wired or published, like the rest of
    // the argument state, so message handlers read it unsynchronised safely.
    int requestedDelay = 0;

    // The one pending deferred release, or 0 when none (Max's single clock). A
    // new message-triggered release cancels this and arms a fresh one; the
    // delivery callback clears it. Touched only from message handlers and the
    // scheduler's delivery — the same dispatch context the slot table already
    // relies on.
    messageScheduler::Handle pendingRelease = 0;
  };
}
}
