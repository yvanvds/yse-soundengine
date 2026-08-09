#pragma once
#include "../pObject.h"
#include "../time/messageScheduler.h"
#include <atomic>

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
     *  ### What is not ported, and why
     *
     *  Max's ``delay`` speaks the whole Max time-format syntax — notevalues
     *  (``4nd``), ticks, ``bars.beats.units``, ``samples`` — and its ``list`` /
     *  ``anything`` methods exist only to carry those. Milliseconds are the only
     *  unit here, because everything else is tempo-relative and when this object
     *  was written the patcher had no transport and no bridge to
     *  ``CLOCK::domainClock`` at all; the same judgement ``.qlist`` made about
     *  its own ``tempo``. A list whose first item is a plain number therefore
     *  still sets the time — that is the honest millisecond subset of Max's list
     *  method — and anything else does nothing.
     *
     *  #688 has since built the bridge (``PATCHER::clockBridge``, with
     *  ``.qlist``'s ``clock <name>`` as its first consumer), so ``clock`` — Max's
     *  own ``setclock`` vocabulary, and a message this object still ignores —
     *  now has something to name. Adopting it here is filed as **#705**:
     *  ``clock <name>`` plus a beat unit, and the notevalue spellings (``4n``,
     *  ``4nd``, ``8nt``) with it, since those are arithmetic once a beat exists.
     *  ``quantize``, ``transport`` and ``bars.beats.units`` stay out even then —
     *  all three need a bar/meter model, and a ``domainClock`` is a bare beat
     *  accumulator with no meter in it.
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
     *  atomic load, and the list handler matches its leading token in place.
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

    // Max's delay time in milliseconds, and the creation argument. Read on
    // every arm and written by the inlets and by a live SetParams re-parse, so
    // atomic; unclamped, since DelayTime() is where the range is applied.
    aInt delaytime;

    // The bang being held, or 0. One clock per object, Max's shape. Atomic
    // because an arriving message and a scheduler delivery are not on the same
    // thread — `.qlist`'s arrangement, for `.qlist`'s reason.
    std::atomic<messageScheduler::Handle> pending{0};
  };

} // namespace PATCHER
} // namespace YSE
