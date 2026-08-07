#pragma once
#include "../../headers/enums.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace YSE {
  namespace PATCHER {

    class pObject;
    struct GraphState;

    /**
     *  @brief What kind of payload a deferred message carries — one per
     *         ``outlet::Send*`` / ``inlet::Set*`` control-message pair.
     */
    enum class DEFERRED_KIND : std::uint8_t {
      BANG,
      INT,
      FLOAT,
      LIST,
    };

    /**
     *  @brief One deferred message, as ``pObject::DeliverDeferred`` receives it
     *         (issue #628).
     *
     *  ``tag`` is whatever the scheduling object passed at arm time — a private
     *  discriminator so an object with more than one kind of pending send can
     *  tell them apart. ``text`` refers to scheduler-owned scratch and is only
     *  meaningful for ``LIST``; it must be consumed inside the callback, not
     *  stored by reference.
     */
    struct deferredMessage {
      int tag;
      DEFERRED_KIND kind;
      int intValue;
      float floatValue;
      const std::string& text;
    };

    /**
     *  @brief The patcher's deferred-message scheduler — "send this, but
     *         later", delivered inside the patcher's own dispatch (issue #628).
     *
     *  The patcher had no way for an object to defer a message. ``TimerThread``
     *  is the wrong tool twice over: its ``Add`` takes a mutex and allocates a
     *  ``std::function`` (a message handler may be running on the audio thread,
     *  where neither is allowed), and its callback fires on the timer thread,
     *  *outside* any dispatch frame — so a deferred send would reach downstream
     *  objects as a pile of unrelated stimuli, one logical event per message,
     *  instead of the single event the deferral stands in for.
     *
     *  This class is the honest answer, and it is exactly the shape issue #628
     *  asks for:
     *
     *  - **Arming is wait-free.** ``Schedule*`` claims a slot from a
     *    fixed-capacity table with at most one CAS attempt per slot — a bounded
     *    walk, no lock, no allocation, no syscall — so any message handler may
     *    arm a deferral no matter which thread is dispatching it. A full table
     *    refuses (handle 0) rather than blocks; the caller decides whether to
     *    fall back to an immediate send or drop.
     *
     *  - **Delivery is a real dispatch frame.** ``DeliverDue`` runs on the
     *    audio thread at the top of ``patcherImplementation::Calculate`` — the
     *    same place the #225 value queue drains — and wraps each delivery in a
     *    ``messageEventScope``, so everything one deferred message goes on to
     *    cause shares one fresh logical-event id (``CurrentMessageEvent``,
     *    issue #471). A ``.bondo`` release deferred here still reads as *one*
     *    event to a downstream ``.next``, which is what a timer-thread delivery
     *    would silently have broken.
     *
     *  - **It is bounded.** ``CAPACITY`` pending messages per patcher, text
     *    payloads up to ``TEXT_CAPACITY`` characters, everything stored inline
     *    in slots allocated with the patcher. A patch cannot queue unbounded
     *    work; refusals are counted (``Dropped``) rather than logged, since the
     *    arming thread may be the audio callback.
     *
     *  ### The timeline is the block clock
     *
     *  Deadlines are quantised to audio blocks: ``delayMs`` is converted to
     *  blocks of ``STANDARD_BUFFERSIZE`` samples at the live ``SAMPLERATE`` at
     *  arm time, with a floor of one block — a deferred message is *never*
     *  delivered inside the dispatch that armed it, because "later, as its own
     *  event" is the semantic and the floor is what guarantees it. The clock is
     *  the owning patcher's block counter, so time only advances while the
     *  patcher renders; a paused engine holds every pending message, which is
     *  the only meaning "100 ms from now" can have on a clock that is not
     *  running.
     *
     *  ### Lifetime safety across live edits
     *
     *  A pending message holds a ``pObject*`` armed possibly long before it is
     *  due — far outside the two-block grace the #227 reclaimer proves for
     *  in-flight snapshots — so the pointer is never trusted by itself.
     *  Delivery re-resolves the target against the block's pinned GraphState:
     *  the slot's pointer *and* the object's construction-time id
     *  (``pObject::GetID``) must both match an object in the snapshot, or the
     *  message is dropped. An object deleted (or structurally replaced, #234)
     *  between arm and due is simply absent from the snapshot, and the id
     *  check keeps a recycled allocation at the same address from impersonating
     *  it.
     *
     *  ### Ordering
     *
     *  Messages due in the same block deliver in arm order (a per-arm sequence
     *  number, sorted with a fixed-capacity insertion sort — no allocation).
     *  Each delivery completes in full, whole subgraph behind it, before the
     *  next starts — the same depth-first rule every synchronous send obeys.
     *  A delivery that arms a *new* deferral cannot run in the same drain: its
     *  deadline floor is one block ahead.
     *
     *  ### Cancellation
     *
     *  ``Schedule*`` returns a ``Handle`` (0 = refused). ``Cancel`` unarm's a
     *  still-pending message; a stale handle — already delivered, already
     *  cancelled, slot since reused — fails the generation check and does
     *  nothing. This is what gives ``.bondo`` (and ``.del`` after it, #503)
     *  Max's one-clock-per-object reschedule: cancel the pending release, arm
     *  a new one.
     */
    class messageScheduler {
    public:
      /**
       *  @brief A pending message, for ``Cancel`` / ``Pending``. 0 is never a
       *         live handle; it is what ``Schedule*`` returns on refusal.
       */
      using Handle = std::uint64_t;

      /**
       *  @brief Most messages that can be pending at once, patcher-wide. Fixed
       *         so the slot table is one allocation with the patcher and the
       *         audio thread's drain is a bounded walk.
       */
      static constexpr std::size_t CAPACITY = 128;

      /**
       *  @brief Longest LIST payload a slot carries inline — the same bound the
       *         #225 value queue accepts. Longer text refuses the arm rather
       *         than truncating silently or allocating.
       */
      static constexpr std::size_t TEXT_CAPACITY = 256;

      /**
       *  @param blockClock The owning patcher's monotonic block counter
       *         (``audioBlock_``); deadlines are measured against it. The
       *         reference must outlive the scheduler, which member ordering in
       *         patcherImplementation guarantees.
       */
      explicit messageScheduler(const std::atomic<std::uint64_t>& blockClock);

      /**
       *  @brief Arm a deferred message. Any thread; wait-free; no allocation.
       *
       *  ``delayMs`` is converted to blocks at the live SAMPLERATE with a floor
       *  of one block (so 0 still defers to the next dispatch). Returns 0 when
       *  the pending set is full.
       */
      Handle ScheduleBang(pObject* target, int tag, int delayMs);
      Handle ScheduleInt(pObject* target, int tag, int delayMs, int value);
      Handle ScheduleFloat(pObject* target, int tag, int delayMs, float value);
      /** Text longer than TEXT_CAPACITY - 1 characters refuses the arm. */
      Handle ScheduleList(pObject* target, int tag, int delayMs, const char* text,
                          std::size_t length);

      /**
       *  @brief Unarm a pending message. Any thread; wait-free. Returns true
       *         when the handle was still pending; false for one already
       *         delivered, already cancelled, or never issued.
       */
      bool Cancel(Handle handle);

      /** @brief True while the handle's message is armed and undelivered. */
      bool Pending(Handle handle) const;

      /**
       *  @brief Deliver every armed message whose deadline has passed, in arm
       *         order, each inside its own ``messageEventScope``.
       *
       *  Audio thread only, from the top of Calculate with the block's
       *  GraphState pinned. Due messages whose target is absent from ``graph``
       *  (deleted, replaced, or ``graph`` is null) are dropped. ``thread`` is
       *  forwarded to the target's ``DeliverDeferred`` — T_GUI at the Calculate
       *  call site, matching the #225 value drain: the block's own traversal
       *  renders whatever the delivery caused.
       */
      void DeliverDue(const GraphState* graph, THREAD thread);

      /** @brief Armed, undelivered messages right now. Diagnostics / tests. */
      std::size_t PendingCount() const;

      /**
       *  @brief Arms refused so far (full pending set or over-long text).
       *         A counter rather than a log line because the refusing thread
       *         may be the audio callback. Diagnostics / tests.
       */
      std::uint64_t Dropped() const;

      /**
       *  @brief The deadline a delay of ``delayMs`` gets, in blocks from now:
       *         ceil at the live SAMPLERATE over STANDARD_BUFFERSIZE blocks,
       *         floored at one block. Exposed so tests assert the real
       *         conversion instead of re-deriving it.
       */
      static std::uint64_t BlocksForMillis(int delayMs);

    private:
      // Slot lifecycle, packed with a generation into one atomic so a claim, a
      // cancel and a delivery can each move a slot with a single CAS that no
      // stale handle or stale scan can satisfy.
      //
      //   FREE --Schedule*--> CLAIMED --publish--> ARMED --DeliverDue--> FIRING --> FREE
      //                                                 \--Cancel-----> FREE
      //
      // Payload fields are written only in CLAIMED and read only after the
      // ARMED->FIRING CAS, so they are never touched concurrently. dueBlock and
      // seq are additionally read *speculatively* by the drain's scan (between
      // seeing ARMED and winning the CAS the slot may be cancelled and
      // re-armed), so those two are atomics; a lost race is caught by the CAS
      // and the stale values discarded.
      static constexpr std::uint64_t STATE_FREE = 0;
      static constexpr std::uint64_t STATE_CLAIMED = 1;
      static constexpr std::uint64_t STATE_ARMED = 2;
      static constexpr std::uint64_t STATE_FIRING = 3;
      static constexpr std::uint64_t STATE_MASK = 3;

      struct Entry {
        // Low 2 bits: state. Remaining bits: generation, bumped once per claim
        // — the ABA guard behind Handle validity.
        std::atomic<std::uint64_t> stateGen{STATE_FREE};
        std::atomic<std::uint64_t> dueBlock{0};
        std::atomic<std::uint64_t> seq{0};
        pObject* target = nullptr;
        unsigned int targetId = 0;
        int tag = 0;
        DEFERRED_KIND kind = DEFERRED_KIND::BANG;
        int intValue = 0;
        float floatValue = 0.f;
        std::uint32_t textLength = 0;
        char text[TEXT_CAPACITY];
      };

      // Shared arm path behind the four Schedule* fronts.
      Handle Arm(pObject* target, int tag, int delayMs, DEFERRED_KIND kind, int intValue,
                 float floatValue, const char* text, std::size_t length);

      Entry entries_[CAPACITY];
      const std::atomic<std::uint64_t>& clock_;
      // Arm-order tickets; what "in arm order" is sorted by.
      std::atomic<std::uint64_t> nextSeq_{1};
      std::atomic<std::uint64_t> dropped_{0};
      // Audio-thread-only delivery scratch for LIST payloads, reserved to
      // TEXT_CAPACITY at construction so no delivery allocates.
      std::string listScratch_;
    };

  } // namespace PATCHER
} // namespace YSE
