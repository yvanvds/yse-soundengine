#pragma once
#include "../../headers/enums.hpp"
#include "../../internal/threadPool.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace YSE {
  namespace PATCHER {

    class pObject;
    struct GraphState;

    /** @brief Which half of the file surface a request is — ``read`` or
     *         ``write``. */
    enum class FILE_OP : std::uint8_t {
      READ,
      WRITE,
    };

    /**
     *  @brief One finished file request, as ``pObject::DeliverFileResult``
     *         receives it (issue #683).
     *
     *  ``tag`` is whatever the requesting object passed at request time — a
     *  private discriminator, so an object with more than one kind of file
     *  request (``.seq``'s MIDI file against its text form, say) can tell them
     *  apart. ``ok`` is false for every failure alike: no such file, a file
     *  larger than a slot holds, a write while the host's read-only virtual
     *  file system is installed, or a background pool that never ran the job.
     *
     *  ``bytes`` points into scheduler-owned storage and is only meaningful for
     *  a successful ``READ``. It is valid for the duration of the callback and
     *  not one instruction longer: the slot is released the moment the callback
     *  returns, so a consumer parses out of it rather than keeping the pointer.
     */
    struct fileResult {
      int tag;
      FILE_OP op;
      bool ok;
      const char* bytes;
      std::size_t byteCount;
    };

    /**
     *  @brief The patcher's file-I/O scheduler — "read this file, and tell me
     *         when you have" — delivered inside the patcher's own dispatch
     *         (issue #683).
     *
     *  ### Why a patcher object cannot simply open a file
     *
     *  A patcher message handler runs on **whichever thread dispatched the
     *  message**. In-patcher delivery dispatches on ``T_DSP``, and ``THREAD``
     *  is a *dispatch-semantics* tag rather than a thread identity —
     *  ``messageScheduler::DeliverDue`` is itself called from the audio
     *  callback carrying ``T_GUI``. There is no predicate an object can ask to
     *  find out that it is not on the audio callback, so a ``read`` handler
     *  that opened a file would block that callback in exactly the cases that
     *  matter. This is why ``read`` and ``write`` were left inert on ``.coll``
     *  (#494), ``.textfile`` (#499), ``.qlist`` (#500), ``.mtr`` (#501) and
     *  ``.seq`` (#502), and it is the whole reason this class exists.
     *
     *  ### The shape
     *
     *  Exactly ``messageScheduler``'s, because it answers the same question
     *  from the same constraints:
     *
     *  - **Requesting is wait-free.** ``RequestRead`` / ``RequestWrite`` claim
     *    a slot from a fixed-capacity table with at most one CAS attempt per
     *    slot — a bounded, lock-free walk, no allocation, no syscall — so any
     *    message handler may ask for a file no matter which thread is
     *    dispatching it. A full table refuses (returns false) rather than
     *    blocking; refusals are counted (``Dropped``) rather than logged,
     *    since the requesting thread may be the audio callback. The payload of
     *    a write is *copied* into the slot at request time (a bounded
     *    ``memcpy``, no allocation), because the requesting object may be gone
     *    long before the bytes reach the disk.
     *
     *  - **The disk work is on the background pool.** Each slot owns a
     *    pre-allocated ``INTERNAL::threadPoolJob``, so arming one is a push
     *    onto the pool's lock-free ring —
     *    ``poolClass::background``, whose documented purpose is already "file
     *    loading". Nothing is allocated to submit and nothing is joined to
     *    submit.
     *
     *  - **The job never touches the requesting object.** It reads and writes
     *    only its own slot, which the scheduler owns and which outlives every
     *    object in the patcher. That is what makes a ``DeleteObject`` racing a
     *    read in flight safe *by construction* rather than by timing: there is
     *    no pointer for the job to dangle on. (An object-owned job could not be
     *    made safe cheaply — the epoch reclaimer frees retired objects on the
     *    background pool itself, so a destructor joining its own still-queued
     *    job would wait on the one worker thread that is inside that
     *    destructor.)
     *
     *  - **Completion is a real dispatch frame.** ``DeliverComplete`` runs on
     *    the audio thread at the top of ``patcherImplementation::Calculate``,
     *    right after the deferred-message drain, and wraps each callback in a
     *    ``messageEventScope`` — so whatever a finished read goes on to cause
     *    shares one fresh logical-event id (#471), exactly as if the file had
     *    been the stimulus. The target is re-resolved against the block's
     *    pinned ``GraphState`` by pointer *and* construction-time id
     *    (``pObject::GetID``), the same check ``messageScheduler`` uses: an
     *    object deleted or structurally replaced between request and
     *    completion is simply absent from the snapshot and its result is
     *    dropped, and a recycled allocation at the same address cannot
     *    impersonate it.
     *
     *  Note that ``DeliverComplete`` dispatches with ``T_GUI``, matching the
     *  value drain and the deferred-message drain beside it. That tag is read
     *  as "the caller is the control thread" by ``patcherImplementation::
     *  PassBang`` / ``PassData``, which is the pre-existing bug filed as #690;
     *  a consumer whose completion path sends to a named receiver should pass
     *  ``T_DSP`` for that remote half the way ``.qlist`` already does, until
     *  #690 is fixed at the patcher level.
     *
     *  ### It is bounded, and lazily built
     *
     *  ``CAPACITY`` requests in flight per patcher, paths up to
     *  ``PATH_CAPACITY`` characters, payloads up to ``BYTES_CAPACITY``, all
     *  stored inline in slots allocated in one block. A file larger than a slot
     *  is **refused whole** rather than truncated — half a collection is a
     *  different collection — and so is a write payload that does not fit.
     *
     *  That table is half a megabyte, which is why a patcher does not build one
     *  until an object that can read or write files joins it: a file-capable
     *  object calls ``pObject::EnableFileIO`` from its ``SetParent`` override
     *  (control thread), and ``pObject::FileIO`` returns null until it has.
     *
     *  ### Time, and what happens when the engine is not running
     *
     *  A completed request is delivered by the patcher's next block, so — like
     *  every other deferred path here — results land only while the patcher
     *  renders. A patcher that never renders holds its completions and
     *  eventually refuses new requests once the table fills. ``WaitIdle`` is
     *  the control-thread escape hatch tests use to make the background half
     *  deterministic; it does not deliver anything, because delivery is the
     *  audio thread's job.
     *
     *  ### The recipe for a consuming object
     *
     *  Four issues are queued on this — #687 (``.textfile``), #689
     *  (``.qlist``), #691 (``.mtr``) and #692 (``.seq``) — and all four do the
     *  same five things:
     *
     *  1. override ``SetParent`` to call ``pObject::SetParent`` and then
     *     ``EnableFileIO()``;
     *  2. from the ``read`` / ``write`` message handler, call
     *     ``FileIO()->RequestRead(this, tag, path, len)`` or
     *     ``RequestWrite(this, tag, path, len, bytes, count)``, giving up
     *     quietly when ``FileIO()`` is null (a standalone object has no
     *     patcher and so no plumbing);
     *  3. build a write payload into a buffer **reserved at construction** —
     *     the request path may be the audio thread, so it may not allocate;
     *  4. override ``DeliverFileResult`` to parse ``result.bytes`` into the
     *     object's own fixed store, still without allocating, and to fire
     *     whatever "file read" outlet the object has;
     *  5. **append** that outlet at the end of the object's existing ones
     *     rather than inserting it in Max's position, so no saved patch's cords
     *     shift. This is ``.coll``'s rule and the whole family follows it.
     */
    class fileScheduler {
    public:
      /**
       *  @brief Most requests in flight at once, patcher-wide. Small on
       *         purpose: each slot carries its payload inline, and the
       *         background pool runs them one at a time anyway.
       */
      static constexpr std::size_t CAPACITY = 4;

      /** @brief Longest path a slot carries, including the terminator. */
      static constexpr std::size_t PATH_CAPACITY = 512;

      /**
       *  @brief Largest file a slot reads or writes — 128 KiB.
       *
       *  Chosen to clear the largest thing the first consumer can hold: a full
       *  ``.coll`` of 256 entries at its maximum address and message lengths
       *  serialises to just under 81 KiB. A file bigger than this is refused
       *  rather than truncated.
       */
      static constexpr std::size_t BYTES_CAPACITY = 131072;

      fileScheduler();
      ~fileScheduler();
      fileScheduler(const fileScheduler&) = delete;
      fileScheduler& operator=(const fileScheduler&) = delete;
      fileScheduler(fileScheduler&&) = delete;
      fileScheduler& operator=(fileScheduler&&) = delete;

      /**
       *  @brief Ask for the contents of @p path. Any thread; wait-free; no
       *         allocation.
       *
       *  Returns false when the table is full, the path is empty or longer
       *  than ``PATH_CAPACITY - 1``, or @p target is null — never because the
       *  file is missing, which is only discovered on the background pool and
       *  reported as a completion with ``ok == false``.
       */
      bool RequestRead(pObject* target, int tag, const char* path, std::size_t pathLength);

      /**
       *  @brief Write @p byteCount bytes to @p path. Same contract as
       *         ``RequestRead``, plus a refusal when the payload is larger than
       *         ``BYTES_CAPACITY``.
       *
       *  The bytes are copied into the slot before this returns, so the caller
       *  may reuse its buffer immediately. Writing through the host's ``IO()``
       *  virtual file system is not possible — that layer exposes open, read
       *  and seek callbacks but no write — so a write while it is active
       *  completes with ``ok == false``.
       */
      bool RequestWrite(pObject* target, int tag, const char* path, std::size_t pathLength,
                        const char* bytes, std::size_t byteCount);

      /**
       *  @brief Hand every finished request to its target, each inside its own
       *         ``messageEventScope``.
       *
       *  Audio thread only, from the top of Calculate with the block's
       *  GraphState pinned. Results whose target is absent from @p graph
       *  (deleted, replaced, or @p graph is null) are dropped. @p thread is
       *  forwarded to ``pObject::DeliverFileResult``.
       */
      void DeliverComplete(const GraphState* graph, THREAD thread);

      /** @brief Requests made and not yet delivered. Diagnostics / tests. */
      std::size_t PendingCount() const;

      /**
       *  @brief Requests refused so far (full table, over-long path, over-large
       *         payload). A counter rather than a log line because the
       *         refusing thread may be the audio callback. Diagnostics /
       *         tests.
       */
      std::uint64_t Dropped() const;

      /**
       *  @brief Block until no request is still waiting on the background pool.
       *
       *  Control thread only — it spins. Delivers nothing: after this returns,
       *  every request has either finished or been abandoned, and the next
       *  ``DeliverComplete`` hands the results out. Written so a test can
       *  assert on a file round trip without sleeping.
       */
      void WaitIdle();

    private:
      // Slot lifecycle. One atomic word, no generation counter: a request
      // cannot be cancelled, so no handle outlives a slot and there is no ABA
      // to guard against.
      //
      //   FREE --Request*--> CLAIMED --publish--> ARMED --job--> RUNNING
      //        <--DeliverComplete-- COMPLETE <--job--/
      //
      // Payload fields are written only in CLAIMED, read only in RUNNING, and
      // read again only in COMPLETE, so they are never touched concurrently.
      static constexpr std::uint32_t STATE_FREE = 0;
      static constexpr std::uint32_t STATE_CLAIMED = 1;
      static constexpr std::uint32_t STATE_ARMED = 2;
      static constexpr std::uint32_t STATE_RUNNING = 3;
      static constexpr std::uint32_t STATE_COMPLETE = 4;

      struct Entry;

      // One slot's worth of disk work. Pre-allocated with the slot, so arming
      // it allocates nothing; it holds only its owner and its index, never a
      // pObject, which is what makes it immune to a concurrent DeleteObject.
      struct fileJob : INTERNAL::threadPoolJob {
        fileScheduler* owner = nullptr;
        std::size_t index = 0;
        void run() override;
      };

      struct Entry {
        std::atomic<std::uint32_t> state{STATE_FREE};
        fileJob job;
        pObject* target = nullptr;
        unsigned int targetId = 0;
        int tag = 0;
        FILE_OP op = FILE_OP::READ;
        bool ok = false;
        std::uint32_t byteCount = 0;
        char path[PATH_CAPACITY] = {};
        char bytes[BYTES_CAPACITY] = {};
      };

      // Shared claim path behind both Request* fronts.
      bool Arm(pObject* target, int tag, FILE_OP op, const char* path, std::size_t pathLength,
               const char* bytes, std::size_t byteCount);

      // Background pool: do slot @p index's I/O and publish the outcome.
      void RunSlot(std::size_t index);

      // The two halves of the disk work. Background pool only — both block.
      static bool ReadSlot(Entry& entry);
      static bool WriteSlot(Entry& entry);

      // One heap block, allocated with the scheduler and never resized. Held by
      // pointer rather than inline so patcherImplementation does not grow by
      // half a megabyte; declared before nothing else, because each Entry's job
      // joins in its own destructor and must do so while its slot still exists.
      std::unique_ptr<Entry[]> entries_;
      std::atomic<std::uint64_t> dropped_{0};
    };

  } // namespace PATCHER
} // namespace YSE
