#pragma once
#include "../../internal/threadPool.h"
#include "TimerThread.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief The bridge from a patcher message handler to ``timerThread`` —
     *         "run this every N milliseconds", askable from a handler on any
     *         thread (issue #718).
     *
     *  ### Why an object cannot simply arm a timer
     *
     *  A patcher message handler runs on **whichever thread dispatched the
     *  message**. In-patcher delivery dispatches on ``T_DSP``, and the drains at
     *  the top of ``patcherImplementation::Calculate`` dispatch ``T_GUI`` *from
     *  the audio callback*, so a ``.delay`` wired into a ``.metro``'s left inlet
     *  puts that metro's toggle on the audio thread. Everything
     *  ``timerThread`` offers is forbidden there:
     *
     *  - ``Add`` takes ``sync`` and inserts into an ``unordered_map`` and a
     *    ``multiset`` — one lock and two node allocations — plus whatever the
     *    caller's ``std::function`` costs to build;
     *  - ``SetPeriod`` takes the same mutex;
     *  - ``ClearTimer`` takes it too, and its ``destroyImpl`` **blocks** on a
     *    condition variable until an in-flight callback returns on the timer
     *    worker.
     *
     *  This is the same wall ``clockBridge`` (#688) hit with
     *  ``CLOCK::Manager().lookup`` and ``fileScheduler`` (#683) hit with
     *  ``open()``, and the answer here is the same shape: split the problem
     *  where the lock is.
     *
     *  - **Requesting is wait-free.** ``RequestStart`` / ``RequestStop`` /
     *    ``RequestPeriod`` write a handful of atomics into a slot that already
     *    exists and push a pre-allocated job onto the background pool's
     *    lock-free ring. No allocation, no lock, no syscall.
     *  - **The timer work is on the background pool.** The job holds the slot's
     *    mutex and reconciles *wanted* state against *armed* state with the
     *    plain blocking ``timerThread`` calls, on the one thread that may make
     *    them.
     *  - **Off the audio callback nothing is deferred at all.** ``ApplyStart``
     *    and friends do the same reconcile synchronously on the calling thread,
     *    so a control-thread toggle keeps the exact timing and the exact
     *    stop-means-stopped handshake it has always had. Which of the two a
     *    caller uses is *its* decision, taken from
     *    ``patcherImplementation::CallingThread`` (#690) rather than from a
     *    ``THREAD`` tag, because the tag is dispatch semantics and not thread
     *    identity.
     *
     *  ### Wanted state, not a command queue
     *
     *  A slot carries what the owner *wants* — running or not, at which period,
     *  since which start — and the reconciler drives the timer to match. Two
     *  toggles that arrive before the pool runs collapse into the final one
     *  instead of queueing two pieces of work, and a reconcile that races a new
     *  request simply runs again. Nothing about a millisecond metronome needs
     *  the intermediate states to be replayed.
     *
     *  ``startSeq`` is what separates "start" from "still running": it is
     *  bumped by every start *and* every stop, so a restart while running is a
     *  new sequence and therefore a genuine re-phase — Max's bang method
     *  restarting a metro "from the moment we triggered it". A bare period
     *  change does not bump it, so it retimes rather than re-triggers (#625).
     *
     *  ### Lifetime: a slot outlives its owner, and the job is never joined
     *
     *  The slot table belongs to a process-lifetime singleton, so no object ever
     *  owns a ``threadPoolJob``. That is deliberate and it is the reason this
     *  table is not a member of the object that uses it: the #227 epoch
     *  reclaimer frees retired patcher objects **on the background pool
     *  itself**, and ``~threadPoolJob`` joins, so an object-owned job would have
     *  its destructor spin on the single background worker that is already
     *  inside that destructor. ``fileScheduler``'s header states the same trap.
     *
     *  ``Release`` therefore never joins. It takes the slot's mutex, performs
     *  the blocking ``ClearTimer`` handshake — the caller is a destructor, never
     *  the audio thread — and marks the slot free. A job still queued for that
     *  slot finds it free when it eventually runs and does nothing; a job
     *  already *running* holds the mutex, so ``Release`` waits for it, which
     *  cannot deadlock because a reclaimer that is itself on the pool worker
     *  cannot also be executing that job.
     */
    class timerBridge {
    public:
      /**
       *  @brief One owner's timer. 0 is never a live handle; it is what
       *         ``Claim`` returns when the table is full.
       */
      using Handle = std::uint32_t;

      /**
       *  @brief What the timer calls, and what it is called with. A function
       *         pointer and a context rather than a ``std::function``, so a
       *         request never has to build a callable — building one is one of
       *         the allocations this class exists to keep off the audio thread.
       */
      using Callback = void (*)(void*);

      /**
       *  @brief Owners that can hold a slot at once, process-wide. Far above
       *         any realistic count of running ``.metro`` objects; a refusal
       *         costs the owner its millisecond clock, so the bound is generous
       *         rather than tight.
       */
      static constexpr std::size_t CAPACITY = 256;

      timerBridge();
      ~timerBridge();
      timerBridge(const timerBridge&) = delete;
      timerBridge& operator=(const timerBridge&) = delete;
      timerBridge(timerBridge&&) = delete;
      timerBridge& operator=(timerBridge&&) = delete;

      /**
       *  @brief Take a slot for the life of an owner. Wait-free; no allocation.
       *
       *  Returns 0 when the table is full (counted in ``Dropped``). Called from
       *  an object's constructor, so the control thread in practice, but it is
       *  a bounded CAS walk and safe from anywhere.
       */
      Handle Claim(Callback fn, void* ctx);

      /**
       *  @brief Stop the slot's timer and give the slot back.
       *
       *  **Blocks**: it waits out an in-flight callback exactly as
       *  ``timerThread::ClearTimer`` does, which is the handshake that lets an
       *  owner be destroyed while its timer is firing. Callers are destructors,
       *  which never run on the audio callback.
       */
      void Release(Handle handle);

      /** @name Wait-free requests — any thread, including the audio callback.
       *
       *  Each writes the slot's wanted state and arms the background job. The
       *  timer is not touched here, so the change lands a pool hop later; the
       *  owner's own immediate output (a metro's bang on start) is not deferred
       *  with it and stays synchronous.
       *  @{
       */
      /** Run every @p periodMs milliseconds, phase starting now. */
      void RequestStart(Handle handle, timerThread::millisec periodMs);
      /** Stop. Idempotent, and does *not* wait for a callback in flight. */
      void RequestStop(Handle handle);
      /** Retime a running timer without re-phasing it (issue #625). */
      void RequestPeriod(Handle handle, timerThread::millisec periodMs);
      /** @} */

      /** @name Synchronous equivalents — every thread but the audio callback.
       *
       *  Same wanted-state writes, reconciled inline before returning, so the
       *  timer is armed (or demonstrably stopped) by the time the call is done.
       *  @{
       */
      void ApplyStart(Handle handle, timerThread::millisec periodMs);
      void ApplyStop(Handle handle);
      void ApplyPeriod(Handle handle, timerThread::millisec periodMs);
      /** @} */

      /** @brief Claims refused so far. A counter rather than a log line,
       *         because the refusing thread may be the audio callback. */
      std::uint64_t Dropped() const;

      /**
       *  @brief Block until no reconcile job is still on the background pool.
       *
       *  Control thread only — written so a test can assert on a request made
       *  from the audio-thread path without sleeping. Never call it from a
       *  background worker.
       */
      void WaitIdle();

    private:
      // Slot lifecycle. One atomic word; no generation counter is needed
      // because a Handle is only ever held by the owner that claimed it and is
      // dropped in that owner's destructor.
      //
      //   FREE --Claim--> CLAIMED --publish--> BUSY --Release--> FREE
      static constexpr std::uint32_t STATE_FREE = 0;
      static constexpr std::uint32_t STATE_CLAIMED = 1;
      static constexpr std::uint32_t STATE_BUSY = 2;

      struct Entry;

      // One slot's worth of timer work. Pre-allocated with the slot, so arming
      // it allocates nothing, and owned by this singleton rather than by the
      // requesting object — see the class notes on why an object-owned job
      // cannot be made safe here.
      struct reconcileJob : INTERNAL::threadPoolJob {
        timerBridge* owner = nullptr;
        Entry* slot = nullptr;
        void run() override;
      };

      struct Entry {
        std::atomic<std::uint32_t> state{STATE_FREE};

        // Wanted state, written by the owner on whichever thread dispatched it
        // and read by the reconciler. `startSeq` is the publisher: it is stored
        // last with a release and read first with an acquire, so a reconcile
        // that sees a new sequence also sees the `wantOn` / `wantPeriod` that
        // belong to it.
        std::atomic<bool> wantOn{false};
        std::atomic<std::int64_t> wantPeriod{1};
        std::atomic<std::uint64_t> startSeq{0};

        // Bumped by every request; the reconciler compares it across its own
        // work to notice a request that arrived while it was running. Together
        // with `pending` this is what keeps a coalesced request from being lost
        // in the window where the job is running but no longer queueable.
        std::atomic<std::uint64_t> reqSeq{0};
        // False when a push onto the pool would be needed; set by the requester
        // that performs the push and cleared by the job.
        std::atomic<bool> pending{false};

        // Written once while CLAIMED, read by the reconciler only once BUSY.
        Callback fn = nullptr;
        void* ctx = nullptr;

        // Armed state. Reconciler and Release only, both under `mtx`. Mutable
        // so the const diagnostic reader below can take it; nothing on the
        // audio-callback path ever does — that is the whole point of the class.
        mutable std::mutex mtx;
        timerThread::timerID armedId = timerThread::noTimer;
        std::uint64_t armedSeq = 0;
        std::int64_t armedPeriod = 0;

        // Declared last so it is destroyed first: ~threadPoolJob joins, and a
        // job that is still running reads every field above it — clockBridge's
        // and fileScheduler's rule (#706), for the same reason.
        reconcileJob job;
      };

      // Drive `e`'s timer to its wanted state. Blocking; the pool or a
      // synchronous caller, never the audio thread. `e.mtx` held.
      void Reconcile(Entry& e);
      // The loop the job runs: reconcile, then look again for a request that
      // landed while it was working. See the class notes.
      void RunSlot(Entry& e);
      // Push `e`'s job unless a push is already outstanding. Wait-free.
      static void ArmJob(Entry& e);
      // The wanted-state writes shared by the Request* and Apply* fronts. Kept
      // apart because a period change must not touch `wantOn`: reading it and
      // storing it back would let a period change that straddles a stop put the
      // metro back on. Both are wait-free.
      static void WantRun(Entry& e, bool on);
      static void WantStart(Entry& e, timerThread::millisec periodMs);
      // Returns false when the interval already is what is being asked for, in
      // which case there is nothing to reconcile and no job to arm. That is
      // what lets `.metro` re-assert its period on every single tick for free.
      static bool WantPeriod(Entry& e, timerThread::millisec periodMs);

      Entry* Slot(Handle handle);
      const Entry* Slot(Handle handle) const;

      // One heap block, allocated with the bridge and never resized. Held by
      // pointer for the reason clockBridge holds its table that way, and
      // declared first because each Entry's job joins in its own destructor and
      // must do so while its slot still exists.
      std::unique_ptr<Entry[]> entries_;
      std::atomic<std::uint64_t> dropped_{0};
    };

    /** @brief The process-wide bridge. Its slots and jobs outlive every patcher
     *         object, which is the whole point — see ``timerBridge``. */
    timerBridge& TimerBridge();

  } // namespace PATCHER
} // namespace YSE
