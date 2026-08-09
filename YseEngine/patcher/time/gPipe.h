#pragma once
#include "../pObject.h"
#include "../time/messageScheduler.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Delays numbers, lists and symbols — ``.pipe`` (issue #504).
     *
     *  Max's ``pipe``, "delay numbers, lists, or symbols": "pipe delays a
     *  number, list, or symbol by a specified amount of time."
     *
     *  ### What it is that ``.delay`` is not
     *
     *  ``.delay`` (#503) defers a *bang*, and it defers exactly one: Max's "if
     *  a bang is already in delay when a new bang is received in the left
     *  inlet, the first bang is forgotten." That is the right shape for a
     *  trigger and the wrong one for data. This object carries a **payload**
     *  and it **queues**: every value that arrives gets its own wait, and a
     *  second value does not cancel the first. Ten numbers into a ``.pipe 500``
     *  are ten numbers out, 500 ms later, in the order they went in. That one
     *  difference is the whole object — echoes, delayed parameter changes, and
     *  every "do this to that value in N ms" pattern need the queue, and none
     *  of them can be built out of ``.delay`` without one ``.delay`` per value.
     *
     *  ### Where the time comes from
     *
     *  The patcher's deferred-message scheduler (#628), for exactly the reasons
     *  ``.delay``'s header sets out and which apply here word for word:
     *  ``TimerThread``'s ``Add`` takes a mutex and allocates a
     *  ``std::function`` (a value may arrive on the audio callback, where
     *  neither is allowed), and its callback fires *outside* any dispatch
     *  frame, so the released value would reach downstream objects as an
     *  unrelated stimulus rather than as one logical event. A delayed value is
     *  *caused* by the value that went in, and the point of the object is that
     *  the causal chain survives the wait. So: arming is wait-free and
     *  allocation-free, delivery happens at the top of
     *  ``patcherImplementation::Calculate`` inside a fresh
     *  ``messageEventScope``, and the clock is the patcher's block counter —
     *  which stops when the engine does, so a paused patch holds every pending
     *  value where it stands.
     *
     *  A delay of 0 still defers to the next block rather than firing now,
     *  which is the scheduler's one-block deadline floor and, as on ``.delay``,
     *  is the semantics rather than a rounding: in Max ``pipe 0`` is how a
     *  patch breaks out of the current message chain, and a synchronous one
     *  wired back into its own inlet would recurse until the audio thread's
     *  stack ran out.
     *
     *  ### The pending set, its capacity, and what a full one does
     *
     *  ``CAPACITY`` values may be in flight per object, each carrying up to
     *  ``TEXT_CAPACITY`` characters of text, all of it allocated with the
     *  object. That is the bound the issue asks for and it is a real one: a
     *  patch cannot make a ``.pipe`` queue unbounded work, and nothing on the
     *  arming path allocates however many values arrive. Beyond the object's
     *  own ceiling sits the patcher's: the scheduler holds
     *  ``messageScheduler::CAPACITY`` pending messages for *every* object
     *  together, so a ``.pipe`` competes with ``.delay``, ``.qlist``, ``.mtr``
     *  and ``.seq`` for that budget and can be refused by it while its own
     *  slots are free.
     *
     *  A refused value is **dropped and counted** — ``Dropped()`` — rather than
     *  emitted immediately or logged. Both alternatives were considered and
     *  both are worse:
     *
     *  - *Emitting immediately* is what ``.bondo`` does when the scheduler
     *    refuses, and it is right there because ``.bondo`` would otherwise lose
     *    a stored set. Here it would break the object's single guarantee — the
     *    value arrives later, as its own event — at precisely the moment the
     *    patcher is at its resource limit, and a ``.pipe`` fed from its own
     *    outlet (a delay line, the idiom this object exists for) would then
     *    recurse unbounded on the audio thread. A lost value is a bounded
     *    failure; that is not.
     *  - *Logging* it would be a string format and an allocation on whichever
     *    thread the value arrived on, and that thread is routinely the audio
     *    callback (a value queued from inside ``Calculate``) or the timer
     *    worker (a ``.metro`` driving the pipe). The project's real-time rule
     *    forbids both, and every other bounded structure in the patcher answers
     *    the same way — ``messageScheduler::Dropped``, which "is a counter
     *    rather than a log line because the refusing thread may be the audio
     *    callback". ``Dropped()`` is the reporting surface, readable from any
     *    thread and from the C API's parameter surface, and it is monotonic so
     *    a host can poll it.
     *
     *  ### ``clear`` and ``flush``, and why neither cancels the scheduler slot
     *
     *  Max's two commands: ``clear`` "removes all delayed items without
     *  outputting them", ``flush`` "outputs all delayed items immediately". A
     *  flush emits in arrival order, which is the order a patch put them in and
     *  the only order that makes a flushed echo tail sound like the echo tail.
     *
     *  Both work by taking the object's slot away from the pending message,
     *  which is what makes them correct without a lock. A slot's life is
     *  ``FREE -> CLAIMED -> ARMED -> FIRING -> FREE``, one atomic CAS per
     *  transition, and the deferred message carries the slot index *and* the
     *  slot's generation in its scheduler tag. Delivery only happens if that
     *  CAS wins, so a value taken by ``clear`` or ``flush`` cannot also be
     *  delivered, and a slot re-armed in between cannot be mistaken for the one
     *  the message was armed on. The scheduler's own ``Cancel`` is asked as
     *  well, best effort, purely to give the patcher-wide budget back early; a
     *  cancel that loses its race leaves a message that comes due, finds the
     *  slot gone, and sends nothing.
     *
     *  ### Two honest edges
     *
     *  - **No patcher at all.** A standalone object has no scheduler and so no
     *    clock; "later" has no referent, and the only alternatives are *now* or
     *    *never*. It sends the value straight through, which is ``.delay``'s
     *    and ``.bondo``'s answer to the same dead end and the one that keeps a
     *    standalone object testable rather than a black hole.
     *  - **Text longer than ``TEXT_CAPACITY``.** Refused and counted, like a
     *    full pending set and for the scheduler's reason: the alternative is an
     *    allocation on the audio thread.
     *
     *  ### Three departures from Max, and why
     *
     *  - **One data inlet, not a settable number of them.** Max's ``pipe`` builds
     *    one inlet/outlet pair per creation argument and spreads an incoming
     *    list across them, so ``pipe 0 0 500`` delays a *pair* of atoms. That
     *    shape exists in Max because a Max message is a list of atoms and pipe
     *    has to take it apart to hold it. Here a list is already one value that
     *    travels whole — the patcher's list type *is* its text — so a ``.pipe``
     *    delays the list as a list and hands it on intact, and a patch that
     *    wants it spread across cords uses ``.spray``, ``.bondo`` or ``.route``
     *    downstream, unchanged by having been delayed. Building N ports to take
     *    a list apart and put it back together would cost a pending *tuple* per
     *    queued value instead of a pending value, for a rearrangement the
     *    patcher already has objects for.
     *  - **A single-token numeric list arrives as the number it spells.** A
     *    ``.m 5`` wired into this inlet is a list message carrying "5", and a
     *    ``.pipe`` that re-emitted it as a list would not reach the ``.i`` on
     *    the far side — an int inlet does not take a list here, there being no
     *    coercion in this patcher. So the leading-token test ``.bondo`` and
     *    ``.trigger`` already use decides it: one token that is wholly a number
     *    is stored as an int or a float by its spelling, and everything else is
     *    stored as text. The value that comes out is the value that went in,
     *    which is the property that matters and the one Max's atom types give
     *    him for free.
     *  - **No tempo-relative time and no ``clock`` message.** ``.delay`` learned
     *    Max's note values and tick counts in #705, against a domain clock bound
     *    through ``PATCHER::clockBridge``. This object speaks milliseconds only,
     *    so rather than silently misread the syntax it does not have — ``1440
     *    ticks`` read as a leading number would become 1440 ms, which is how
     *    ``.clocker`` got it wrong first — the time inlet *refuses* anything
     *    ``timeValue.h`` recognises as a beat time and leaves the delay where it
     *    was.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing, for the reason it does nothing in every
     *  message-driven object here: this one is driven by its inlets and by the
     *  scheduler, and a ``Calculate()`` that emitted would send a value on every
     *  DSP tick from a stimulus no patch sent. No path allocates, locks or
     *  blocks: arming is a bounded walk of the slot table with one CAS per slot
     *  tried, the payload is written into storage the constructor reserved, the
     *  delay time is one atomic load, ``clear`` and ``flush`` are the same
     *  bounded walk plus a fixed-capacity insertion sort over a stack array, and
     *  delivery is one CAS and one send.
     */
    PATCHER_CLASS(gPipe, YSE::OBJ::G_PIPE)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    /**
     *  @brief Values one ``.pipe`` may hold at once — the bounded, pre-allocated
     *         pending set issue #504 asks for.
     *
     *  Half the patcher-wide ``messageScheduler::CAPACITY``, deliberately: one
     *  object should be able to fill a long echo tail without being able to
     *  starve every other deferring object in the patch on its own.
     */
    static constexpr std::size_t CAPACITY = 64;

    /**
     *  @brief Longest text payload a slot carries inline — the same bound the
     *         scheduler and the #225 value queue accept. Longer text refuses the
     *         value rather than truncating it or allocating.
     */
    static constexpr std::size_t TEXT_CAPACITY = 256;

    /**
     *  @brief The delay Max gives a ``pipe`` with no creation argument, in
     *         milliseconds. Max's ``delaytime`` attribute: "default 0".
     */
    static constexpr int DEFAULT_DELAY = 0;

    /**
     *  @brief The delay in milliseconds, as the next value to arrive will use
     *         it: the stored parameter with negatives and NaN clamped away.
     *
     *  Clamped on read rather than on write, which is ``.metro``'s and
     *  ``.delay``'s arrangement and for their reason — a live ``SetParams``
     *  re-parse stores straight into the field from the audio thread and
     *  notifies nobody, so a clamp applied at the inlet would not cover that
     *  route.
     */
    int DelayTime() const;

    /** @brief Values waiting to come out right now. Diagnostics / tests. */
    std::size_t PendingCount() const {
      return pendingCount.load(std::memory_order_relaxed);
    }

    /**
     *  @brief Values refused so far: the object's pending set was full, the
     *         patcher's scheduler was full, or the text was longer than
     *         ``TEXT_CAPACITY``.
     *
     *  Monotonic, readable from any thread, and the object's overflow report —
     *  a counter rather than a log line because the refusing thread may be the
     *  audio callback. See the class notes.
     */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    // The scheduler coming back with a value whose wait has elapsed.
    void DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) override;

  private:
    // What a slot is holding. There is no NONE: a slot only holds anything at
    // all between its claim and its delivery.
    enum class Held : std::uint8_t {
      INT,
      FLOAT,
      LIST,
    };

    // Slot lifecycle, packed with a generation into one atomic so a claim, a
    // flush and a delivery can each move a slot with a single CAS that no stale
    // scheduler tag can satisfy. The scheduler's own Entry does exactly this,
    // for exactly this reason.
    //
    //   FREE --arrival--> CLAIMED --publish--> ARMED --delivery--> FIRING --> FREE
    //                                                \--clear/flush--> FIRING --> FREE
    static constexpr std::uint64_t STATE_FREE = 0;
    static constexpr std::uint64_t STATE_CLAIMED = 1;
    static constexpr std::uint64_t STATE_ARMED = 2;
    static constexpr std::uint64_t STATE_FIRING = 3;
    static constexpr std::uint64_t STATE_MASK = 3;

    // How the slot index and the slot generation share the scheduler's `int`
    // tag. Eight bits of index cover CAPACITY with room to spare; the rest is
    // generation, kept to 22 bits so a tag is always a positive int.
    static constexpr int TAG_INDEX_BITS = 8;
    static constexpr int TAG_INDEX_MASK = 0xFF;
    static constexpr std::uint64_t TAG_GEN_MASK = 0x3FFFFF;

    struct Slot {
      // Low 2 bits: state. Remaining bits: generation, bumped once per claim —
      // the ABA guard behind a scheduler tag's validity.
      std::atomic<std::uint64_t> stateGen{STATE_FREE};
      // The scheduler message this slot is waiting on, for `clear` / `flush` to
      // hand the patcher-wide budget back early. Best effort by construction —
      // it is stored *after* the slot is published, because publishing has to
      // happen before the message is armed or a delivery could race ahead of
      // it — so a stale or missing handle costs a pending message that comes
      // due and finds nothing to do, never a wrong send.
      std::atomic<messageScheduler::Handle> handle{0};
      // Arrival order, which is the order `flush` emits in. Written under
      // CLAIMED and read after the ARMED->FIRING CAS, like the payload, so it
      // needs no atomicity of its own.
      std::uint64_t seq = 0;
      Held held = Held::INT;
      int intValue = 0;
      float floatValue = 0.f;
      // Reserved to TEXT_CAPACITY by the constructor, so storing text costs no
      // allocation.
      std::string text;
    };

    // Queue one arriving value, or drop it when there is no room. Sends it
    // straight out when there is no patcher to defer into.
    void Enqueue(Held kind, int intValue, float floatValue, const char* text, std::size_t length,
                 YSE::THREAD thread);

    // Max's `clear` (emit = false) and `flush` (emit = true): take every
    // pending value away from the scheduler, and either drop it or send it in
    // arrival order.
    void Release(bool emit, YSE::THREAD thread);

    // Send what a slot holds, or send a value directly for a standalone object.
    void Emit(const Slot& slot, YSE::THREAD thread);
    void EmitDirect(Held kind, int intValue, float floatValue, const char* text, std::size_t length,
                    YSE::THREAD thread);

    // Max's delay time in milliseconds, and the creation argument. Read on
    // every arrival and written by the right inlet and by a live SetParams
    // re-parse, so atomic; unclamped, since DelayTime() is where the range is
    // applied.
    aInt delaytime;

    Slot slots[CAPACITY];
    // Arrival tickets; what `flush` sorts by.
    std::atomic<std::uint64_t> nextSeq{1};
    std::atomic<std::size_t> pendingCount{0};
    std::atomic<std::uint64_t> dropped{0};
  };

} // namespace PATCHER
} // namespace YSE
