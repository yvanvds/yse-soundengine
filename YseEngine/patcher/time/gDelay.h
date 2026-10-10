#pragma once
#include "../pObject.h"
#include "../time/clockBridge.h"
#include "../time/messageScheduler.h"
#include <atomic>
#include <cstddef>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Holds a bang for a while and then lets it go — ``.delay``
     *         (issue #503).
     *
     *  Max's ``delay``, "delay a bang": "holds a bang for a specified amount of
     *  time before sending it to the next object."
     *
     *  ### The smallest scheduling primitive, and the one that was missing
     *
     *  Before this the patcher could *repeat* on a clock (``.metro``) and it
     *  could play a stored sequence back in time (``.qlist``, ``.mtr``,
     *  ``.seq``), but it could not **defer** anything: there was no way to say
     *  "this, but in 200 ms". Every one of those objects is a store with a clock
     *  bolted on; this one is only the clock. One bang in, one bang out, later.
     *
     *  ### Where the time comes from
     *
     *  The patcher's deferred-message scheduler (#628) — the same mechanism
     *  ``.bondo``, ``.qlist``, ``.mtr`` and ``.seq`` already wait on, and
     *  deliberately not a new one. ``TimerThread``, which the issue suggested
     *  and which backs ``.metro``, is the wrong tool twice over and
     *  ``messageScheduler``'s own header says why: its ``Add`` takes a mutex and
     *  allocates a ``std::function`` (a bang may arrive on the audio callback,
     *  where neither is allowed), and its callback fires *outside* any dispatch
     *  frame, so the released bang would reach downstream objects as an
     *  unrelated stimulus rather than as one logical event. ``.metro`` gets away
     *  with the timer because a metronome's bang genuinely *is* an unrelated
     *  stimulus — nothing caused it. A delayed bang is caused by the bang that
     *  went in, and the whole point of the object is that the causal chain
     *  survives the wait.
     *
     *  So: arming is wait-free and allocation-free, delivery happens at the top
     *  of ``patcherImplementation::Calculate`` inside a fresh
     *  ``messageEventScope``, and the clock is the patcher's block counter —
     *  which stops when the engine does, so a paused patch holds a delayed bang
     *  where it stands.
     *
     *  ### ``delay 0`` defers; it does not fire now — and that is the object
     *
     *  The scheduler's deadline floor is one audio block: a deferred message is
     *  never delivered inside the dispatch that armed it. ``.qlist`` documents
     *  that floor as a feature and ``.seq`` deliberately overrides it (three
     *  bytes of a note-on delivered a block apart are not that note-on). This
     *  object keeps it, and unlike those two it is not a trade-off — it is the
     *  semantics.
     *
     *  In Max, ``delay 0`` is the canonical way to *break out of* the current
     *  message chain: the bang goes to the scheduler and comes back on the next
     *  pass, after everything the sending message caused has finished. A
     *  ``delay 0`` that fired synchronously would not be a delay at all, it
     *  would be ``.bangbang`` — and, wired outlet-back-to-inlet as delay chains
     *  routinely are, it would recurse until the audio thread's stack ran out.
     *  The one-block floor is what makes that patch merely a very fast metronome
     *  instead of a crash, which is exactly the property the floor exists for.
     *  ``.seq`` could override it because its walk is bounded by its own tape;
     *  a delay line fed from its own outlet is bounded by nothing.
     *
     *  ### One bang at a time
     *
     *  Max: "Only one bang at a time can be delayed by ``delay``. If a bang is
     *  already in delay when a new bang is received in the left inlet, the first
     *  bang is forgotten." So a new bang **cancels and re-arms** rather than
     *  queueing a second — ``.bondo``'s one-clock-per-object shape, which is
     *  what a Max ``clock`` gives every one of these objects. ``stop`` cancels
     *  without arming: Max's "stops delay from outputting the bang it is
     *  currently delaying."
     *
     *  ### Two honest edges, and they answer differently
     *
     *  - **No patcher at all.** A standalone object has no scheduler and so no
     *    clock; "later" has no referent, and the only alternatives are *now* or
     *    *never*. It bangs immediately, which is ``.bondo``'s answer to the same
     *    dead end and the one that keeps a standalone object testable rather
     *    than a black hole.
     *  - **The pending set is full.** Here the bang is **dropped**, counted by
     *    ``messageScheduler::Dropped()``, and this is the one place ``.bondo``'s
     *    fallback is deliberately not followed. ``.bondo`` releases immediately
     *    rather than lose a *set of stored values*; this object holds no data,
     *    so an immediate bang would not be salvaging anything — it would be
     *    breaking the object's single guarantee (the bang arrives later, as its
     *    own event) at precisely the moment the patcher is already at its
     *    resource limit, and a self-feeding delay chain would then recurse
     *    unbounded on the audio thread. A lost bang is a bounded failure; that
     *    is not.
     *
     *  ### ``clock <name>`` and a tempo-relative time (issue #705)
     *
     *  Max's ``delay`` speaks the whole Max time-format syntax — note values
     *  (``4nd``), ticks, ``bars.beats.units``, ``samples`` — and its ``list`` /
     *  ``anything`` methods exist only to carry those. When this object was
     *  written milliseconds were the only unit it had, because everything else
     *  is tempo-relative and the patcher had no transport and no bridge to
     *  ``CLOCK::domainClock`` at all. #688 built the bridge
     *  (``PATCHER::clockBridge``), ``.qlist`` and ``.seq`` are its first two
     *  consumers, and this object is the third. Two independent things arrive
     *  with it, exactly as they are independent in Max:
     *
     *  - **``clock <name>``** names the clock a tempo-relative time is measured
     *    on, and **``clock`` alone** takes it away. That is Max's own method,
     *    verbatim: "the word clock, followed by the name of an existing
     *    setclock object, sets the delay object to be controlled by that
     *    setclock object rather than by Max's internal millisecond clock. The
     *    word clock by itself sets the delay object back to using Max's regular
     *    millisecond clock." Unlike ``.qlist``'s and ``.seq``'s, this ``clock``
     *    is a **port** rather than an addition — Max's ``delay`` and ``metro``
     *    are two of the objects ``setclock`` names explicitly.
     *  - **A tempo-relative time** — a note value (``4n``, ``4nd``, ``8nt``) or
     *    a tick count (``1440 ticks``) — is the delay, in beats, in place of
     *    the milliseconds. Both spellings arrive through Max's own ``list`` /
     *    ``anything`` method, which exists to carry them, and the arithmetic is
     *    shared with ``.metro`` in ``time/timeValue.h``.
     *
     *  The unit travels with the *value*, not with the clock, which is Max's
     *  model: a plain number is milliseconds and says so — Max: "the number is
     *  stored as the number of milliseconds" — so a number arriving in either
     *  inlet puts the object back on milliseconds, and a note value puts it back
     *  on beats. ``clock`` only decides *which* clock a beat time is counted on.
     *
     *  ### Three departures from Max, and why
     *
     *  - **A ``clock`` does not scale the milliseconds.** Max's ``setclock`` is
     *    a millisecond clock that can be made to run at another rate, so a
     *    ``delay 500`` under one is still 500 of *its* milliseconds. A
     *    ``CLOCK::domainClock`` has no millisecond scale at all — it is the
     *    running integral of a tempo, and beats are the only thing it counts —
     *    so a millisecond time keeps the patcher's own block clock whether or
     *    not a clock is bound. Binding one adds a unit; it does not reinterpret
     *    the one already there.
     *  - **A tempo-relative time with no clock bound does not fire.** In Max a
     *    note value with no ``setclock`` falls back to the global transport;
     *    this patcher has no transport, so there is nothing to measure a beat
     *    against, and a bang arms nothing and goes nowhere rather than being
     *    silently re-read as milliseconds — which would be a wait of "1" for a
     *    ``4n``. It is the same answer the bridge gives everywhere else: a wait
     *    on a clock that is not there never comes due. Sending ``clock <name>``
     *    afterwards makes the next bang work, so the two messages may arrive in
     *    either order.
     *  - **``bars.beats.units``, ``quantize`` and ``transport`` stay out.** All
     *    three need a bar, a bar needs a meter, and a ``domainClock`` has none.
     *    See ``time/timeValue.h``.
     *
     *  What the beat unit buys is what milliseconds cannot: a delay that
     *  follows the domain's tempo changes and ramps, that stays in step with
     *  every ``YSE::clip`` on that domain, and that holds where it stands when
     *  the domain pauses. A clock named before the host creates it simply does
     *  not come due until it appears; one destroyed under a bound object leaves
     *  a frozen beat (#707), so a bang waiting on it waits for good.
     *
     *  The binding is **not saved** with the patch, for the reason ``.qlist``'s
     *  is not: it is run-time state, and a reloaded patch that re-bound itself
     *  to a clock the host may not have created yet would have two answers to
     *  "what is this object waiting on". The beat *time* is saved, because it is
     *  a time like the millisecond one — it is the second creation argument.
     *
     *  The right inlet is Max's exactly: a number there "changes the delay time
     *  of the next bang received -- it does not modify the time of a bang
     *  currently being delayed". A number in the *left* inlet does both — Max:
     *  "the number is stored ... It then automatically sends a bang message to
     *  itself to start the delay."
     *
     *  ### One departure from the reference page, which disagrees with itself
     *
     *  Max's Arguments section says "if there is no argument, the initial time
     *  interval is 5 milliseconds"; its Attributes table lists ``delaytime`` as
     *  defaulting to ``0 ms``. Those cannot both describe a freshly typed
     *  ``delay``. The argument prose wins, because it is the statement about
     *  *this object's* constructor rather than about the attribute's declared
     *  default, and because it matches what a Max user sees. So a ``.delay``
     *  with no creation argument waits 5 ms.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing, for the reason it does nothing in every
     *  message-driven object here: this one is driven by its inlets and by its
     *  own clock, and a ``Calculate()`` that emitted would fire a bang on every
     *  DSP tick from a stimulus no patch sent. No path allocates, locks or
     *  blocks — arming and cancelling are wait-free, the delay time is one
     *  atomic load, the list handler matches its tokens in place, binding a
     *  clock is the bridge's wait-free claim (the name is resolved on the
     *  background pool, because that lookup takes the clock manager's mutex),
     *  and reading a beat is two acquire loads.
     *
     *  The delivered bang is sent with the tag the scheduler handed over. That
     *  tag is ``T_GUI``, which is the right reading for an outlet send — the
     *  block's own traversal renders whatever the bang caused — and the wrong
     *  one for a *remote* send, which is #690. This object has no remote path of
     *  its own, so like ``.mtr`` and ``.seq`` it passes the tag straight
     *  through; a ``.s`` wired downstream of this outlet meets #690 exactly as
     *  it meets it downstream of any other deferred send, and that is ``.s``'s
     *  to fix.
     */
    PATCHER_CLASS(gDelay, YSE::OBJ::G_DELAY)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    /**
     *  @brief The wait a ``.delay`` with no creation argument gets, in
     *         milliseconds — Max's "if there is no argument, the initial time
     *         interval is 5 milliseconds".
     */
    static constexpr int DEFAULT_DELAY = 5;

    /**
     *  @brief The delay in milliseconds, as the next bang will use it: the
     *         stored parameter with negatives clamped away.
     *
     *  Clamped on read rather than on write, which is ``.metro``'s arrangement
     *  and for ``.metro``'s reason — a live ``SetParams`` re-parse stores
     *  straight into the field from the audio thread and notifies nobody, so a
     *  clamp applied at the inlet would not cover that route.
     */
    int DelayTime() const;

    /**
     *  @brief The tempo-relative delay in beats, or 0 when the delay is
     *         milliseconds (issue #705).
     *
     *  Set by a note value or a tick count and cleared by any plain number,
     *  which is Max's "the number is stored as the number of milliseconds".
     *  Clamped on read like ``DelayTime()``, and for the same reason.
     */
    double DelayBeats() const;

    /** @brief Whether ``clock <name>`` has bound a domain clock for a
     *         tempo-relative time to be measured on (issue #705). False for a
     *         fresh object, after a bare ``clock``, and for a standalone
     *         object, which has no bridge to bind through. */
    bool OnClock() const {
      return binding.load(std::memory_order_relaxed) != 0;
    }

    /** @brief The clock name the object is bound to, or ``""``. The storage
     *         belongs to the patcher's bridge and never changes, so this is
     *         safe from any thread. */
    const char* ClockName() const;

    /** @brief Whether a bang is currently being held. False before the first
     *         one, after ``stop``, and once the bang has gone out. */
    bool IsPending() const;

    // The scheduler coming back with the bang whose wait has elapsed.
    void DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) override;

  private:
    // Max's "holds a bang": cancel whatever is pending — "the first bang is
    // forgotten" — and arm a new one. Bangs immediately when there is no
    // scheduler at all, and drops the bang when the scheduler refuses; see the
    // header on why those two edges answer differently.
    void Start(YSE::THREAD thread);

    // Drop the held bang, if there is one. Max's `stop`.
    void CancelPending();

    // Max's `clock <name>` / bare `clock`, through the patcher's bridge (issue
    // #705). Binds wait-free on whichever thread the message arrived on; a name
    // that does not fit, a bridge that is full, or a standalone object all
    // leave the object where it was, silently, since this may be the audio
    // thread. A bang already in flight keeps the clock it was armed on, the way
    // the cold inlet leaves one alone.
    void SetClock(const char* name, std::size_t length);

    // Max's delay time in milliseconds, and the first creation argument. Read
    // on every arm and written by the inlets and by a live SetParams re-parse,
    // so atomic; unclamped, since DelayTime() is where the range is applied.
    aInt delaytime;

    // The tempo-relative delay in beats, or 0 for milliseconds — the second
    // creation argument (issue #705). Written by the list inlet's note-value
    // and tick spellings, cleared by any plain number, and read on every arm.
    // Atomic and unclamped for delaytime's reasons.
    aFlt delaybeats;

    // The domain clock `clock <name>` bound, or 0 for Max's millisecond clock
    // (issue #705). A patcher-owned binding handle rather than a name: the
    // bridge never releases one, so it stays valid for the life of the patcher
    // and costs nothing to carry. Atomic because a `clock` message and an arm
    // are not on the same thread; relaxed, since neither publishes anything
    // through it. `.qlist`'s arrangement, for `.qlist`'s reason.
    std::atomic<clockBridge::Handle> binding{0};

    // The bang being held, or 0. One clock per object, Max's shape. Atomic
    // because an arriving message and a scheduler delivery are not on the same
    // thread — `.qlist`'s arrangement, for `.qlist`'s reason.
    std::atomic<messageScheduler::Handle> pending{0};
  };

} // namespace PATCHER
} // namespace YSE
