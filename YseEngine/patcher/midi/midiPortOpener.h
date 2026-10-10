#pragma once
#include "headers/defines.hpp"
// Guarded exactly like `.midiout` and `midiPortScanner`, and for the same
// reason: with no RtMidi backend there is no port to open and
// `MIDI::deviceManager` does not exist to ask.
#if YSE_ENABLE_MIDI_DEVICE

#include "../../internal/threadPool.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace YSE {

  /// @cond INTERNAL
  class midiOut;
  /// @endcond

  namespace PATCHER {

    /**
     *  @brief The patcher's bridge to opening a MIDI **output** port — "give
     *         this `midiOut` the device at index N" — askable from a message
     *         handler on any thread (issue #759).
     *
     *  ### Why an object cannot simply open its own port
     *
     *  A patcher message handler runs on **whichever thread dispatched the
     *  message**. In-patcher delivery dispatches on ``T_DSP``, and the drains
     *  at the top of ``patcherImplementation::Calculate`` dispatch ``T_GUI``
     *  *from the audio callback*, so the first list a patch sends to
     *  ``.midiout`` routinely arrives on the audio thread. Opening is the one
     *  thing that may not happen there: ``deviceManager::getMidiOutPort``
     *  constructs an ``RtMidiOut``, calls ``openPort`` — a driver call that can
     *  block for as long as the platform's MIDI service takes — inserts into a
     *  ``std::map``, and since #757 takes ``MIDI::deviceManager``'s mutex on
     *  top of all that.
     *
     *  Moving the open to ``SetParent`` is not the answer: ``.midiout`` opens
     *  lazily on purpose, so a patch loads on a machine whose devices are not
     *  the ones it was written on. So the open stays where it is and moves
     *  *thread*, which is the shape ``fileScheduler`` (#683), ``clockBridge``
     *  (#688), ``timerBridge`` (#718) and ``midiPortScanner`` (#757) all take.
     *
     *  - **Asking is wait-free.** ``Request`` is one CAS on the slot's state
     *    word plus a push onto the background pool's lock-free ring. No
     *    allocation, no lock, no syscall, no driver call.
     *  - **The open is on the background pool.** Each slot owns a pre-allocated
     *    ``INTERNAL::threadPoolJob``, so arming one allocates nothing.
     *  - **Taking delivery is wait-free.** ``Consume`` is one acquire load and
     *    one store. It is what publishes the port the pool opened to the thread
     *    that will send on it — the acquire pairs with the job's release store,
     *    so the pointer ``midiOut::create`` wrote is visible without a lock.
     *
     *  Messages that arrive before the open lands have no device to go to and
     *  are dropped, which is exactly what already happened to every message
     *  when the open failed.
     *
     *  ### Why the jobs live here and not in the object
     *
     *  ``~threadPoolJob`` joins, and the #227 epoch reclaimer frees retired
     *  patcher objects **on the background pool itself** — so an object-owned
     *  job would have its destructor spin on the very worker that is already
     *  inside that destructor. ``fileScheduler``, ``timerBridge`` and
     *  ``midiPortScanner`` all state the trap; this class is the fourth answer
     *  to it. The slots belong to a process-lifetime singleton, an object holds
     *  only a ``Handle``, and ``Release`` never joins.
     *
     *  ### The slot lifecycle is what makes it TSan-clean
     *
     *  ::
     *
     *      FREE --Claim--> IDLE --Request--> SETUP --> ARMED --job--> OPENING
     *        ^                 ^                                        |
     *        |    Release      |               Consume                  v
     *        +-----------------+-------------------------------------- READY
     *
     *  ``target`` and ``port`` are written **only** in ``SETUP``, which is the
     *  one state no job acts on: the slot is claimed but not yet armed, so
     *  nothing on the pool can be reading it. They are read **only** in
     *  ``OPENING``, and the state word's release/acquire pairs order both
     *  crossings — so writer and reader can never touch them at once by
     *  construction rather than by timing, which is what a sanitizer run
     *  demands.
     *
     *  ``Release`` waits out an open in progress (the one state where a worker
     *  is reaching into the owner's ``midiOut``) rather than joining. It cannot
     *  deadlock for ``timerBridge``'s reason: a reclaimer that is itself on a
     *  pool worker cannot also be executing that job.
     */
    class midiPortOpener {
    public:
      /** @brief One owner's slot. 0 is never a live handle; it is what
       *         ``Claim`` returns when the table is full. */
      using Handle = std::uint32_t;

      /**
       *  @brief Owners that can hold a slot at once, process-wide. Far above
       *         the number of `.midiout` boxes a patch has, and a slot is a few
       *         words rather than a snapshot, so this can be generous — a
       *         refusal costs an object its port for good.
       */
      static constexpr std::size_t CAPACITY = 64;

      midiPortOpener();
      ~midiPortOpener();
      midiPortOpener(const midiPortOpener&) = delete;
      midiPortOpener& operator=(const midiPortOpener&) = delete;
      midiPortOpener(midiPortOpener&&) = delete;
      midiPortOpener& operator=(midiPortOpener&&) = delete;

      /**
       *  @brief Take a slot for the life of an owner. Wait-free; no
       *         allocation. Returns 0 when the table is full (counted in
       *         ``Dropped``).
       */
      Handle Claim();

      /**
       *  @brief Give the slot back. Blocks only while an open is mid-flight
       *         into it, which is bounded by one ``openPort``. Callers are
       *         destructors, which never run on the audio callback.
       */
      void Release(Handle handle);

      /**
       *  @brief Ask for @p into to be given the output port at index @p port.
       *         Any thread, including the audio callback; wait-free.
       *
       *  Returns false for handle 0, a slot that is not claimed, or a null
       *  target. Asking again while an open is already in flight is harmless
       *  and costs one atomic load — which is what lets a caller simply ask on
       *  every message until the port arrives, and what recovers an arm whose
       *  push never reached the pool (a pool that was down, or a full
       *  background ring).
       */
      bool Request(Handle handle, YSE::midiOut* into, unsigned int port);

      /**
       *  @brief Collect a finished open and hand the slot back. Any thread,
       *         including the audio callback; wait-free.
       *
       *  Returns false when nothing has landed. A true return is the *attempt*
       *  having finished, not a device having been found: a port index that
       *  does not exist on this machine leaves the owner's ``midiOut`` closed,
       *  which is the same outcome a failed inline open always had.
       */
      bool Consume(Handle handle);

      /** @brief Whether @p handle's slot has an open in flight or a finished
       *         one waiting to be collected. Diagnostics / tests. */
      bool Busy(Handle handle) const;

      /** @brief Whether @p handle's open has finished and is waiting for its
       *         owner to collect it. Diagnostics / tests. */
      bool Settled(Handle handle) const;

      /** @brief Claims refused so far (full table). A counter rather than a log
       *         line, for the same reason the rest of the patcher counts
       *         refusals: the refusing thread may be the audio callback. */
      std::uint64_t Dropped() const;

      /** @brief Open attempts completed on the background pool. Monotonic;
       *         diagnostics and tests — it is what "the open did not happen on
       *         the dispatching thread" is asserted against. */
      std::uint64_t Opened() const;

      /**
       *  @brief Block until no open is still on the background pool.
       *
       *  Control thread only — written so a test can assert on a deferred open
       *  without sleeping. Never call it from a background worker.
       */
      void WaitIdle();

    private:
      static constexpr std::uint32_t STATE_FREE = 0;
      static constexpr std::uint32_t STATE_IDLE = 1;
      static constexpr std::uint32_t STATE_SETUP = 2;
      static constexpr std::uint32_t STATE_ARMED = 3;
      static constexpr std::uint32_t STATE_OPENING = 4;
      static constexpr std::uint32_t STATE_READY = 5;

      struct Entry;

      // One slot's worth of opening. Pre-allocated with the slot, so arming it
      // allocates nothing, and owned by this singleton rather than by the
      // requesting object — see the class notes on why an object-owned job
      // cannot be made safe here.
      //
      // It points *at* its slot rather than carrying an index into
      // ``entries_``: ``unique_ptr::reset`` nulls its pointer **before** it
      // destroys the elements, so a job reaching through ``entries_`` during
      // teardown would index off a null member (issue #706).
      struct openJob : INTERNAL::threadPoolJob {
        midiPortOpener* owner = nullptr;
        Entry* slot = nullptr;
        void run() override;
      };

      struct Entry {
        std::atomic<std::uint32_t> state{STATE_FREE};
        // Written only in SETUP, read only in OPENING — see the class notes.
        YSE::midiOut* target = nullptr;
        unsigned int port = 0;
        // Declared last so it is destroyed *first*: ~threadPoolJob joins, and a
        // job that is still running reads every field above it (issue #706).
        openJob job;
      };

      // Background pool: open @p entry's port into its target and publish it.
      void RunSlot(Entry& entry);

      // Push @p entry's job unless it is already queued. Wait-free.
      static void Arm(Entry& entry);

      Entry* Slot(Handle handle);
      const Entry* Slot(Handle handle) const;

      // One heap block, allocated with the opener and never resized. Held by
      // pointer for the reason clockBridge holds its table that way, and
      // emptied only after ``WaitIdle`` in the destructor, so each Entry's job
      // is joined while its slot still exists.
      std::unique_ptr<Entry[]> entries_;
      std::atomic<std::uint64_t> dropped_{0};
      std::atomic<std::uint64_t> opened_{0};
    };

    /** @brief The process-wide opener. Its slots and jobs outlive every
     *         patcher object, which is the whole point — see
     *         ``midiPortOpener``. */
    midiPortOpener& MidiPortOpener();

  } // namespace PATCHER
} // namespace YSE

#endif // YSE_ENABLE_MIDI_DEVICE
