#pragma once
#include "headers/defines.hpp"
// Guarded exactly like `.midiinfo`, and for the same reason: with no RtMidi
// backend there are no ports to enumerate and `MIDI::deviceManager` does not
// exist to ask.
#if YSE_ENABLE_MIDI_DEVICE

#include "../../internal/threadPool.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Both directions of the machine's MIDI ports, as one scan saw
     *         them (issue #757).
     *
     *  Fixed size, so it can be copied between threads with a plain assignment
     *  — no allocation, which is what lets the audio thread take delivery of
     *  one. A machine with more than ``PORTS_MAX`` ports has the surplus
     *  dropped rather than growing the table; a name longer than
     *  ``NAME_CAPACITY - 1`` characters is truncated. Both bounds are far above
     *  what real hardware presents.
     */
    struct midiPortSnapshot {
      /** @brief Ports one direction holds. */
      static constexpr int PORTS_MAX = 32;

      /** @brief Longest port name a slot carries, including the terminator. */
      static constexpr std::size_t NAME_CAPACITY = 64;

      /** @brief Index of the input half of ``count`` / ``names``. */
      static constexpr int DIR_INPUT = 0;

      /** @brief Index of the output half. */
      static constexpr int DIR_OUTPUT = 1;

      int count[2] = {0, 0};
      char names[2][PORTS_MAX][NAME_CAPACITY] = {};
    };

    /**
     *  @brief The patcher's bridge to RtMidi's port enumeration — "what MIDI
     *         ports does this machine have *now*?" — askable from a message
     *         handler on any thread (issue #757).
     *
     *  ### Why an object cannot simply re-enumerate
     *
     *  A patcher message handler runs on **whichever thread dispatched the
     *  message**. In-patcher delivery dispatches on ``T_DSP``, and the drains
     *  at the top of ``patcherImplementation::Calculate`` dispatch ``T_GUI``
     *  *from the audio callback*, so a ``refresh`` arriving at ``.midiinfo``
     *  may well be on the audio thread. Everything enumeration needs is
     *  forbidden there: ``RtMidi::getPortCount`` / ``getPortName`` talk to the
     *  platform's MIDI service, return ``std::string``s, take a driver lock,
     *  and construct the backend on first use — and since #757
     *  ``MIDI::deviceManager`` takes a mutex of its own around all of it.
     *
     *  This is the same wall ``clockBridge`` (#688) hit with
     *  ``CLOCK::Manager().lookup`` and ``fileScheduler`` (#683) hit with
     *  ``open()``, and the answer here is the same shape: split the problem
     *  where the lock is.
     *
     *  - **Requesting is wait-free.** ``Request`` is one CAS on the slot's
     *    state word plus a push onto the background pool's lock-free ring. No
     *    allocation, no lock, no syscall.
     *  - **The enumeration is on the background pool.** Each slot owns a
     *    pre-allocated ``INTERNAL::threadPoolJob``, so arming one allocates
     *    nothing.
     *  - **Taking delivery is wait-free.** ``Consume`` copies the finished
     *    snapshot into the caller's own storage with a plain struct assignment
     *    and hands the slot back. Safe from the audio thread, which is where
     *    ``.midiinfo`` takes delivery (its block poll).
     *
     *  ### Why the jobs live here and not in the object
     *
     *  ``~threadPoolJob`` joins, and the #227 epoch reclaimer frees retired
     *  patcher objects **on the background pool itself** — so an object-owned
     *  job would have its destructor spin on the very worker that is already
     *  inside that destructor. ``fileScheduler`` and ``timerBridge`` both state
     *  the trap in their headers; this class is the third answer to it. The
     *  slots belong to a process-lifetime singleton, an object holds only a
     *  ``Handle``, and ``Release`` never joins.
     *
     *  ### The slot lifecycle is what makes it TSan-clean
     *
     *  ::
     *
     *      FREE --Claim--> IDLE --Request--> ARMED --job--> SCANNING
     *        ^                 ^                              |
     *        |   Release       |            Consume           v
     *        +-----------------+--------------------------- READY
     *
     *  The table inside a slot is written **only** in ``SCANNING`` and read
     *  **only** in ``READY``, and the state word's release/acquire pairs order
     *  both crossings. So the writer and the reader can never touch it at the
     *  same time — not "in practice", but by construction, which is what a
     *  sanitizer run demands. A ``Request`` that arrives while a scan is
     *  already in flight sets ``again`` instead of racing it, and ``Consume``
     *  re-arms on the way out, so no refresh is silently lost.
     *
     *  ``Release`` waits out a scan in progress (the one state where a worker
     *  is writing the slot) rather than joining. It cannot deadlock for
     *  ``timerBridge``'s reason: a reclaimer that is itself on the pool worker
     *  cannot also be executing that job.
     */
    class midiPortScanner {
    public:
      /** @brief One owner's slot. 0 is never a live handle; it is what
       *         ``Claim`` returns when the table is full. */
      using Handle = std::uint32_t;

      /**
       *  @brief Owners that can hold a slot at once, process-wide. Generous
       *         rather than tight — a refusal costs an object its rescan — but
       *         still bounded, since each slot carries a full snapshot inline.
       */
      static constexpr std::size_t CAPACITY = 16;

      midiPortScanner();
      ~midiPortScanner();
      midiPortScanner(const midiPortScanner&) = delete;
      midiPortScanner& operator=(const midiPortScanner&) = delete;
      midiPortScanner(midiPortScanner&&) = delete;
      midiPortScanner& operator=(midiPortScanner&&) = delete;

      /**
       *  @brief Ask the platform for both port lists, right here, right now.
       *
       *  **Control thread or the background pool only** — it allocates, takes
       *  ``MIDI::deviceManager``'s mutex and may construct the RtMidi backend.
       *  Public because ``.midiinfo`` takes its first snapshot synchronously in
       *  ``SetParent``, where blocking is free and deferral would leave a
       *  freshly loaded patch reporting an empty machine.
       */
      static void ScanNow(midiPortSnapshot& into);

      /**
       *  @brief Take a slot for the life of an owner. Wait-free; no
       *         allocation. Returns 0 when the table is full (counted in
       *         ``Dropped``).
       */
      Handle Claim();

      /**
       *  @brief Give the slot back. Blocks only while a scan is mid-write into
       *         it, which is bounded by one enumeration. Callers are
       *         destructors, which never run on the audio callback.
       */
      void Release(Handle handle);

      /**
       *  @brief Ask for a rescan. Any thread, including the audio callback;
       *         wait-free.
       *
       *  Returns false for handle 0 or a slot that is not claimed. A request
       *  made while a scan is already in flight (or while a finished one is
       *  still waiting to be consumed) is remembered and re-armed by the next
       *  ``Consume``, so it is deferred rather than dropped.
       */
      bool Request(Handle handle);

      /**
       *  @brief Copy a finished scan into @p into and hand the slot back.
       *         Any thread, including the audio callback; wait-free.
       *
       *  Returns false — leaving @p into untouched — when nothing new has
       *  landed. Also the recovery point for an arm whose push never reached
       *  the pool (a pool that was down, or a full background ring): a slot
       *  still ``ARMED`` with no queued job is re-armed here.
       */
      bool Consume(Handle handle, midiPortSnapshot& into);

      /** @brief Whether @p handle's slot has a scan in flight or a result
       *         waiting. Diagnostics / tests. */
      bool Busy(Handle handle) const;

      /** @brief Claims refused so far (full table). A counter rather than a log
       *         line, for the same reason the rest of the patcher counts
       *         refusals: the refusing thread may be the audio callback. */
      std::uint64_t Dropped() const;

      /**
       *  @brief Block until no scan is still on the background pool.
       *
       *  Control thread only — written so a test can assert on a refresh
       *  without sleeping. Never call it from a background worker.
       */
      void WaitIdle();

    private:
      static constexpr std::uint32_t STATE_FREE = 0;
      static constexpr std::uint32_t STATE_IDLE = 1;
      static constexpr std::uint32_t STATE_ARMED = 2;
      static constexpr std::uint32_t STATE_SCANNING = 3;
      static constexpr std::uint32_t STATE_READY = 4;

      struct Entry;

      // One slot's worth of enumeration. Pre-allocated with the slot, so arming
      // it allocates nothing, and owned by this singleton rather than by the
      // requesting object — see the class notes on why an object-owned job
      // cannot be made safe here.
      //
      // It points *at* its slot rather than carrying an index into
      // ``entries_``: ``unique_ptr::reset`` nulls its pointer **before** it
      // destroys the elements, so a job reaching through ``entries_`` during
      // teardown would index off a null member (issue #706).
      struct scanJob : INTERNAL::threadPoolJob {
        midiPortScanner* owner = nullptr;
        Entry* slot = nullptr;
        void run() override;
      };

      struct Entry {
        std::atomic<std::uint32_t> state{STATE_FREE};
        // Set by a Request that could not arm because one was already in
        // flight; cleared and acted on by Consume. This is what makes "a
        // refresh is never silently lost" true.
        std::atomic<bool> again{false};
        // Written only in SCANNING, read only in READY — see the class notes.
        midiPortSnapshot table;
        // Declared last so it is destroyed *first*: ~threadPoolJob joins, and a
        // job that is still running reads every field above it (issue #706).
        scanJob job;
      };

      // Background pool: enumerate into @p entry's table and publish it.
      void RunSlot(Entry& entry);

      // Push @p entry's job unless it is already queued. Wait-free.
      static void Arm(Entry& entry);

      Entry* Slot(Handle handle);
      const Entry* Slot(Handle handle) const;

      // One heap block, allocated with the scanner and never resized. Held by
      // pointer for the reason clockBridge holds its table that way, and
      // emptied only after ``WaitIdle`` in the destructor, so each Entry's job
      // is joined while its slot still exists.
      std::unique_ptr<Entry[]> entries_;
      std::atomic<std::uint64_t> dropped_{0};
    };

    /** @brief The process-wide scanner. Its slots and jobs outlive every
     *         patcher object, which is the whole point — see
     *         ``midiPortScanner``. */
    midiPortScanner& MidiPortScanner();

  } // namespace PATCHER
} // namespace YSE

#endif // YSE_ENABLE_MIDI_DEVICE
