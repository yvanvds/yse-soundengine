#pragma once
#include "../../internal/threadPool.h"
#include "../io/fileScheduler.h"
#include "gDict.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief The patcher's bridge to JSON parsing — "turn this document into
     *         dictionary rows, and tell me when you have" — askable from a
     *         message handler on any thread (issue #771).
     *
     *  ### Why an object cannot simply parse
     *
     *  A patcher message handler runs on **whichever thread dispatched the
     *  message**. In-patcher delivery dispatches on ``T_DSP``, and the drains
     *  at the top of ``patcherImplementation::Calculate`` dispatch ``T_GUI``
     *  *from the audio callback*, so a document arriving at
     *  ``.dict.deserialize`` may well be on the audio thread — and
     *  ``nlohmann::json::parse`` allocates without bound and (by default)
     *  throws. This is the same wall ``fileScheduler`` (#683) hit with
     *  ``open()``, ``clockBridge`` (#688) hit with the clock manager's mutex
     *  and ``midiPortScanner`` (#757) hit with RtMidi's enumeration, and the
     *  answer here is the same shape: split the problem where the allocation
     *  is.
     *
     *  - **Submitting is wait-free.** ``Submit`` is one CAS on the slot's
     *    state word, a bounded ``memcpy`` of the document text, and a push
     *    onto the background pool's lock-free ring. No allocation, no lock,
     *    no syscall.
     *  - **The parse is on the background pool.** Each slot owns a
     *    pre-allocated ``INTERNAL::threadPoolJob``, so arming one allocates
     *    nothing. The job parses non-throwing (``allow_exceptions = false``)
     *    and flattens the document through ``DictFromJson`` into the slot's
     *    own staging store — the one flattener the whole family shares, so
     *    what this reads back is exactly what ``DictToJson`` and
     *    ``.dict.serialize`` spell.
     *  - **Taking delivery is wait-free.** ``Consume`` copies the staged rows
     *    into the caller's store with bounded ``assign``s into pre-reserved
     *    strings and hands the slot back. Safe from the audio thread, which
     *    is where ``.dict.deserialize`` takes delivery (its block poll).
     *
     *  ### Why the jobs live here and not in the object
     *
     *  ``~threadPoolJob`` joins, and the #227 epoch reclaimer frees retired
     *  patcher objects **on the background pool itself** — so an object-owned
     *  job would have its destructor spin on the very worker that is already
     *  inside that destructor. ``fileScheduler``, ``timerBridge`` and
     *  ``midiPortScanner`` all state the trap in their headers; this class is
     *  the fourth answer to it. The slots belong to a process-lifetime
     *  singleton, an object holds only a ``Handle``, and ``Release`` never
     *  joins.
     *
     *  ### The slot lifecycle is what makes it TSan-clean
     *
     *  ::
     *
     *      FREE --Claim--> IDLE --Submit--> CLAIMED --publish--> ARMED
     *        ^                ^                                    |
     *        |    Release     |            Consume        job     v
     *        +----------------+----------- READY <--- PARSING <---+
     *
     *  The document text is written **only** in ``CLAIMED`` and read **only**
     *  in ``PARSING``; the staging store is written **only** in ``PARSING``
     *  and read **only** in ``READY``; and the state word's release/acquire
     *  pairs order every crossing. So a writer and a reader can never touch
     *  the same bytes at the same time — not "in practice", but by
     *  construction, which is what a sanitizer run demands. A ``Submit`` that
     *  finds a parse already in flight refuses (the caller counts it) rather
     *  than racing it: a document is a payload, so there is no ``again``
     *  latch to defer one into — remembering the ask without the bytes would
     *  re-parse the *old* document and call it the new one.
     *
     *  ``Release`` waits out the two transient states (a ``Submit``
     *  mid-``memcpy`` on another thread, a parse mid-flatten on the worker)
     *  rather than joining. It cannot deadlock, for ``timerBridge``'s reason:
     *  a reclaimer that is itself on the pool worker cannot also be executing
     *  that slot's job.
     *
     *  ### The memory, and when it is paid
     *
     *  A staging store is a full ``dictStore`` — every row reserved to its
     *  capacity, roughly 100 KiB — and the text buffer is file-sized
     *  (``TEXT_CAPACITY``, 128 KiB, so the file route #840 never refuses a
     *  document the ``fileScheduler`` already accepted) — so neither is
     *  allocated with the table. ``Claim`` builds both the first time a slot
     *  is claimed (the constructor is the only caller, so this is the control
     *  thread), which keeps the resident cost proportional to how many
     *  ``.dict.deserialize`` objects have ever existed at once rather than to
     *  ``CAPACITY``. ``fileScheduler`` defers its half-megabyte table for the
     *  same reason.
     */
    class dictParser {
    public:
      /** @brief One owner's slot. 0 is never a live handle; it is what
       *         ``Claim`` returns when the table is full. */
      using Handle = std::uint32_t;

      /**
       *  @brief Owners that can hold a slot at once, process-wide.
       *         ``midiPortScanner``'s bound, for its reason: generous rather
       *         than tight — a refusal costs an object its documents — but
       *         still bounded, since each claimed slot carries a full staging
       *         store.
       */
      static constexpr std::size_t CAPACITY = 16;

      /**
       *  @brief Longest document a slot carries, including the terminator —
       *         the largest file a ``fileScheduler`` slot reads, plus one.
       *
       *  Sized for the file route (issue #840): a ``read <file>`` on
       *  ``.dict.deserialize`` hands a whole file's bytes to ``Submit``, and
       *  refusing a document the scheduler already accepted would make the
       *  slot the narrower of the two bounds for no reason. The *inlet* route
       *  keeps its own 255-character transport bound — enforced by the
       *  consumer, not here, because it is the value queue's bound rather
       *  than the parser's. The buffer is heap-allocated by the first
       *  ``Claim`` of a slot, beside the staging store, so an unclaimed slot
       *  still costs nothing.
       */
      static constexpr std::size_t TEXT_CAPACITY = fileScheduler::BYTES_CAPACITY + 1;

      dictParser();
      ~dictParser();
      dictParser(const dictParser&) = delete;
      dictParser& operator=(const dictParser&) = delete;
      dictParser(dictParser&&) = delete;
      dictParser& operator=(dictParser&&) = delete;

      /**
       *  @brief Take a slot for the life of an owner. Control thread only —
       *         the first claim of a slot allocates its staging store.
       *         Returns 0 when the table is full (counted in ``Dropped``).
       */
      Handle Claim();

      /**
       *  @brief Give the slot back. Blocks only while a submit or a parse is
       *         mid-write into it, which is bounded by one document. Callers
       *         are destructors, which never run on the audio callback.
       */
      void Release(Handle handle);

      /**
       *  @brief Hand a document to the parser. Any thread, including the
       *         audio callback; wait-free; no allocation.
       *
       *  The @p length characters at @p text are copied into the slot before
       *  this returns, so the caller may reuse its buffer immediately.
       *  Returns false — and the caller counts the refusal — for handle 0, a
       *  slot that is not idle (a parse in flight, or a result not yet
       *  consumed), or a document longer than ``TEXT_CAPACITY - 1``
       *  characters. Never fails because the document is malformed — that is
       *  only discovered on the background pool and reported by ``Consume``.
       */
      bool Submit(Handle handle, const char* text, std::size_t length);

      /**
       *  @brief Whether a finished parse is waiting to be consumed. Any
       *         thread; wait-free.
       *
       *  Also the recovery point for a submit whose push never reached the
       *  pool (a pool that was down, or a full background ring): a slot still
       *  ``ARMED`` with no queued job is re-armed here — one atomic load in
       *  the common case, which is what makes this affordable on the audio
       *  thread once per block.
       */
      bool HasResult(Handle handle);

      /**
       *  @brief Take a finished parse and hand the slot back. Any thread,
       *         including the audio callback; wait-free.
       *
       *  Returns false — leaving @p into untouched — when nothing has
       *  landed. On true, @p ok says whether the document parsed to a JSON
       *  object: when it did, @p into has been **replaced whole** by the
       *  flattened rows (bounded ``assign``s into storage @p into reserved
       *  at construction — no allocation); when it did not, @p into is
       *  untouched, so a bad document never costs a dictionary its contents.
       *
       *  **The caller holds @p into's guard**, exactly as it would for any
       *  other store mutation, and is the slot's owner — one consumer per
       *  handle, which is what lets this be a plain state check rather than
       *  a second claim.
       */
      bool Consume(Handle handle, dictStore& into, bool& ok);

      /** @brief Whether @p handle's slot has a parse in flight or a result
       *         waiting. Diagnostics / tests. */
      bool Busy(Handle handle) const;

      /** @brief Claims refused so far (full table). A counter rather than a
       *         log line, for the same reason the rest of the patcher counts
       *         refusals: the refusing thread may be the audio callback. */
      std::uint64_t Dropped() const;

      /**
       *  @brief Block until no parse is still on the background pool.
       *
       *  Control thread only — written so a test can assert on a document
       *  without sleeping. Never call it from a background worker.
       */
      void WaitIdle();

    private:
      static constexpr std::uint32_t STATE_FREE = 0;
      static constexpr std::uint32_t STATE_IDLE = 1;
      static constexpr std::uint32_t STATE_CLAIMED = 2;
      static constexpr std::uint32_t STATE_ARMED = 3;
      static constexpr std::uint32_t STATE_PARSING = 4;
      static constexpr std::uint32_t STATE_READY = 5;

      struct Entry;

      // One slot's worth of parsing. Pre-allocated with the slot, so arming
      // it allocates nothing, and owned by this singleton rather than by the
      // requesting object — see the class notes on why an object-owned job
      // cannot be made safe here.
      //
      // It points *at* its slot rather than carrying an index into
      // ``entries_``: ``unique_ptr::reset`` nulls its pointer **before** it
      // destroys the elements, so a job reaching through ``entries_`` during
      // teardown would index off a null member (issue #706).
      struct parseJob : INTERNAL::threadPoolJob {
        dictParser* owner = nullptr;
        Entry* slot = nullptr;
        void run() override;
      };

      struct Entry {
        std::atomic<std::uint32_t> state{STATE_FREE};
        // Whether the document parsed to a JSON object. Written only in
        // PARSING, read only in READY — the state word orders the crossing.
        bool ok = false;
        // The document. Written only in CLAIMED, read only in PARSING.
        // Heap-held and built by the first Claim of this slot (control
        // thread), like `staged` below: at TEXT_CAPACITY it is a file-sized
        // buffer (issue #840), so an unclaimed slot must not carry it inline.
        std::size_t textLength = 0;
        std::unique_ptr<char[]> text;
        // The flattened rows. Written only in PARSING, read only in READY.
        // Built by the first Claim of this slot (control thread) rather than
        // with the table — see the class notes on the memory.
        std::unique_ptr<dictStore> staged;
        // Declared last so it is destroyed *first*: ~threadPoolJob joins, and
        // a job that is still running reads every field above it (issue #706).
        parseJob job;
      };

      // Background pool: parse @p entry's document into its staging store and
      // publish the outcome.
      void RunSlot(Entry& entry);

      // Push @p entry's job unless it is already queued. Wait-free.
      static void Arm(Entry& entry);

      Entry* Slot(Handle handle);
      const Entry* Slot(Handle handle) const;

      // One heap block, allocated with the parser and never resized. Held by
      // pointer for the reason clockBridge holds its table that way, and
      // emptied only after ``WaitIdle`` in the destructor, so each Entry's
      // job is joined while its slot still exists.
      std::unique_ptr<Entry[]> entries_;
      std::atomic<std::uint64_t> dropped_{0};
    };

    /** @brief The process-wide parser. Its slots and jobs outlive every
     *         patcher object, which is the whole point — see ``dictParser``. */
    dictParser& DictParser();

  } // namespace PATCHER
} // namespace YSE
