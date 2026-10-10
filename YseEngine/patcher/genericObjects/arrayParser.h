#pragma once
#include "../../internal/threadPool.h"
#include "gArray.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief The array family's bridge to JSON parsing — "turn this document
     *         into array elements, and tell me when you have" — askable from a
     *         message handler on any thread (issue #797).
     *
     *  ``dictParser``'s shape (#771), for ``dictParser``'s reason, over the
     *  other store type: a patcher message handler runs on **whichever thread
     *  dispatched the message** — in-patcher delivery dispatches on ``T_DSP``,
     *  and the drains at the top of ``patcherImplementation::Calculate``
     *  dispatch ``T_GUI`` *from the audio callback* — and
     *  ``nlohmann::json::parse`` allocates without bound and (by default)
     *  throws. So the problem is split where the allocation is:
     *
     *  - **Submitting is wait-free.** One CAS on the slot's state word, a
     *    bounded ``memcpy`` of the document text, one lock-free push onto the
     *    background pool's ring. No allocation, no lock, no syscall.
     *  - **The parse is on the background pool.** Each slot owns a
     *    pre-allocated ``INTERNAL::threadPoolJob``, so arming one allocates
     *    nothing. The job parses non-throwing (``allow_exceptions = false``)
     *    and reads the document through ``ArrayFromJson`` into the slot's own
     *    staging store — the one reader the whole type shares, so what this
     *    installs is exactly what a saved patch reloads through
     *    ``gArray::RestoreState``.
     *  - **Taking delivery is wait-free.** ``Consume`` copies the staged
     *    elements into the caller's store with bounded ``assign``s into
     *    pre-reserved strings and hands the slot back. Safe from the audio
     *    thread, which is where ``.array.deserialize`` takes delivery (its
     *    block poll).
     *
     *  The slot lifecycle, its release/acquire ordering, why the jobs live in
     *  this process-lifetime singleton rather than in the objects (the #227
     *  epoch reclaimer frees retired patcher objects on the background pool
     *  itself, and ``~threadPoolJob`` joins), and why a ``Submit`` that finds
     *  a parse in flight refuses rather than queueing (a document is a
     *  payload — remembering the ask without the bytes would re-parse the old
     *  text as the new one) are all ``dictParser``'s, stated in full in its
     *  header. This class is the same five states over the other store; only
     *  what differs is said here:
     *
     *  - **The staging store is an ``arrayStore``** — every element reserved
     *    to its capacity, built by the first ``Claim`` of a slot (the control
     *    thread, since the only caller is an object constructor), so an
     *    unclaimed slot never pays for it.
     *  - **The text buffer is transport-sized, not file-sized.** ``.array``'s
     *    read half has no file route — the dict pair's ``read`` (#840) has no
     *    array counterpart — so a document can only arrive down a cord, and
     *    the cord's bound is the value queue's payload
     *    (``gArrayDeserialize::DOCUMENT_CAPACITY``). 256 bytes per slot is
     *    cheap enough to live inline in the table.
     *  - **A well-formed result is a JSON array**, ``is_array``, where the
     *    dict wants ``is_object`` — reported through ``Consume``'s @p ok, with
     *    the caller's store untouched on false.
     */
    class arrayParser {
    public:
      /** @brief One owner's slot. 0 is never a live handle; it is what
       *         ``Claim`` returns when the table is full. */
      using Handle = std::uint32_t;

      /**
       *  @brief Owners that can hold a slot at once, process-wide —
       *         ``dictParser``'s bound, for its reason: generous rather than
       *         tight (a refusal costs an object its documents), but still
       *         bounded, since each claimed slot carries a full staging store.
       */
      static constexpr std::size_t CAPACITY = 16;

      /**
       *  @brief Longest document a slot carries, including the terminator —
       *         the transport bound (``kValueListCap``), not a file's, because
       *         the array's read half has no file route (see the class notes).
       */
      static constexpr std::size_t TEXT_CAPACITY = 256;

      arrayParser();
      ~arrayParser();
      arrayParser(const arrayParser&) = delete;
      arrayParser& operator=(const arrayParser&) = delete;
      arrayParser(arrayParser&&) = delete;
      arrayParser& operator=(arrayParser&&) = delete;

      /**
       *  @brief Take a slot for the life of an owner. Control thread only —
       *         the first claim of a slot allocates its staging store.
       *         Returns 0 when the table is full (counted in ``Dropped``).
       */
      Handle Claim();

      /**
       *  @brief Give the slot back. Blocks only while a submit or a parse is
       *         mid-write into it, which is bounded by one document. Callers
       *         are destructors, which never run on the audio callback —
       *         and ``Release`` never joins, so the background reclaimer
       *         (where a deleted patcher object's destructor actually runs)
       *         is safe here.
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
       *         thread; wait-free. Also the recovery point for a submit whose
       *         push never reached the pool — ``dictParser::HasResult``'s
       *         arrangement.
       */
      bool HasResult(Handle handle);

      /**
       *  @brief Take a finished parse and hand the slot back. Any thread,
       *         including the audio callback; wait-free.
       *
       *  Returns false — leaving @p into untouched — when nothing has landed.
       *  On true, @p ok says whether the document parsed to a JSON array:
       *  when it did, @p into has been **replaced whole** by the staged
       *  elements (bounded ``assign``s into storage @p into reserved at
       *  construction — no allocation); when it did not, @p into is
       *  untouched, so a bad document never costs an array its contents.
       *
       *  **The caller holds @p into's guard**, exactly as it would for any
       *  other store mutation, and is the slot's owner — one consumer per
       *  handle, which is what lets this be a plain state check rather than a
       *  second claim.
       */
      bool Consume(Handle handle, arrayStore& into, bool& ok);

      /** @brief Whether @p handle's slot has a parse in flight or a result
       *         waiting. Diagnostics / tests. */
      bool Busy(Handle handle) const;

      /** @brief Claims refused so far (full table). A counter rather than a
       *         log line, for the family's reason: the refusing thread may be
       *         the audio callback. */
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
      // requesting object — dictParser's arrangement, for the #706 reason its
      // header states: it points *at* its slot rather than carrying an index,
      // because ``unique_ptr::reset`` nulls its pointer before it destroys
      // the elements.
      struct parseJob : INTERNAL::threadPoolJob {
        arrayParser* owner = nullptr;
        Entry* slot = nullptr;
        void run() override;
      };

      struct Entry {
        std::atomic<std::uint32_t> state{STATE_FREE};
        // Whether the document parsed to a JSON array. Written only in
        // PARSING, read only in READY — the state word orders the crossing.
        bool ok = false;
        // The document. Written only in CLAIMED, read only in PARSING.
        // Inline, unlike dictParser's file-sized buffer: 256 bytes is the
        // transport bound, and a table of sixteen of them is cheaper than
        // sixteen pointers' indirection.
        std::size_t textLength = 0;
        char text[TEXT_CAPACITY] = {};
        // The staged elements. Written only in PARSING, read only in READY.
        // Built by the first Claim of this slot (control thread) rather than
        // with the table, so an unclaimed slot never pays for it.
        std::unique_ptr<arrayStore> staged;
        // Declared last so it is destroyed *first*: ~threadPoolJob joins, and
        // a job that is still running reads every field above it (issue
        // #706).
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
     *         patcher object, which is the whole point — see ``arrayParser``
     *         and ``dictParser``'s header for the full argument. */
    arrayParser& ArrayParser();

  } // namespace PATCHER
} // namespace YSE
