#pragma once
#include "../../internal/threadPool.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace YSE {
  namespace CLOCK {
    class domainClock;
  }

  namespace PATCHER {

    /**
     *  @brief The patcher's bridge to the engine's named domain clocks —
     *         "which beat is clock ``<name>`` on?" — answerable from a message
     *         handler on any thread (issue #688).
     *
     *  ### Why an object cannot simply look a clock up
     *
     *  ``CLOCK::Manager().lookup(name)`` walks the manager's canonical list
     *  under ``implementationsMutex``. A patcher message handler runs on
     *  **whichever thread dispatched the message** — in-patcher delivery
     *  dispatches on ``T_DSP``, and the three drains at the top of
     *  ``patcherImplementation::Calculate`` dispatch ``T_GUI`` *from the audio
     *  callback* — so there is no predicate an object can ask to find out that
     *  taking that mutex is safe. This is the same wall ``fileScheduler``
     *  (#683) hit with ``open()``, and the answer here is the same shape.
     *
     *  What is *not* blocked is reading a clock once you hold one:
     *  ``domainClock::beatPosition()`` is a single acquire load of a lock-free
     *  ``atomic<double>`` the audio thread publishes every block. So the bridge
     *  splits the problem exactly where the lock is:
     *
     *  - **Binding is wait-free.** ``Bind`` claims a slot from a fixed table
     *    with at most one CAS attempt per slot, copies the name inline (a
     *    bounded ``memcpy``) and returns a ``Handle``. No allocation, no lock,
     *    no syscall — any handler may bind whatever thread is dispatching it.
     *    A full table refuses (handle 0) rather than blocking, and refusals are
     *    counted (``Dropped``) rather than logged, since the binding thread may
     *    be the audio callback.
     *
     *  - **Resolution is on the background pool.** Each slot owns a
     *    pre-allocated ``INTERNAL::threadPoolJob``, so arming one is a push onto
     *    the pool's lock-free ring. The job does the one thing that needs the
     *    manager's mutex — ``lookup`` — and publishes the resulting
     *    ``domainClock*`` into the slot with a release store.
     *
     *  - **Reading is wait-free.** ``Beat`` is one acquire load of the slot's
     *    clock pointer and one acquire load of the clock's published beat.
     *    ``false`` while the binding is unresolved, which is what a deadline
     *    test on a clock that does not exist has to mean.
     *
     *  ### Bindings are permanent, and that is the point
     *
     *  A handle handed out here stays valid for the life of the patcher: slots
     *  are never freed, so there is no generation counter, no ABA and no way
     *  for a stale handle to address someone else's clock. ``CAPACITY``
     *  *distinct clock names* per patcher is the bound — not one per object, so
     *  every ``.qlist``, ``.seq`` and ``.del`` naming the same clock shares one
     *  slot. ``Bind`` is therefore idempotent by name: the same name always
     *  gives back the same handle.
     *
     *  (Two threads binding the same *new* name at the same instant can each
     *  win a different slot. Harmless — both resolve to the same clock and both
     *  handles behave identically — and not worth a lock to prevent.)
     *
     *  ### A clock that does not exist yet
     *
     *  A binding whose name has no live clock stays unresolved and ``Beat``
     *  keeps answering false, so a wait armed on it never elapses — the only
     *  honest reading of "two beats from now" on a clock that is not running,
     *  and the same one ``messageScheduler`` already gives a paused engine.
     *
     *  ``Poll`` re-arms the resolve job for still-unresolved bindings every
     *  ``RESOLVE_INTERVAL_BLOCKS`` blocks, so a patch that names its clock
     *  before the host creates it starts playing when the clock appears rather
     *  than staying dead. It also recovers a resolve that never ran because the
     *  background ring was full or the pool was down.
     *
     *  ``ResolveBeat`` reports where the clock stood the moment the binding
     *  resolved. It is what lets ``messageScheduler`` give a wait armed *before*
     *  resolution a baseline — the wait starts when the clock starts existing —
     *  without any thread having to write to a slot it does not own.
     *
     *  ### Lifetime
     *
     *  A resolved binding holds a raw ``CLOCK::domainClock*``, exactly as
     *  ``CLIP::transport::bind`` does, and inherits that API's documented
     *  contract: the bound clock must outlive the things bound to it. The
     *  engine frees a clock on the slow pool one manager tick after
     *  ``destroyClock``, so destroying a clock a live patcher is still bound to
     *  is a use-after-free. Session teardown is safe —
     *  ``INTERNAL::global::close()`` clears the clock manager only after the
     *  device is closed and both pools are joined — it is an explicit
     *  ``destroyClock`` under a running engine that is not. Filed as **#707**:
     *  making a binding outlive ``destroyClock`` is a change to the clock
     *  layer's ownership model rather than to this bridge, and it is the same
     *  hazard ``CLIP::transport`` has carried since #250.
     */
    class clockBridge {
    public:
      /**
       *  @brief A bound clock name. 0 is never a live handle; it is what
       *         ``Bind`` returns on refusal and what a deadline uses to mean
       *         "the block clock, not a domain clock".
       */
      using Handle = std::uint32_t;

      /**
       *  @brief Distinct clock names one patcher can bind. Small on purpose:
       *         a patch drives its objects from a handful of named domains, and
       *         objects naming the same clock share a slot.
       */
      static constexpr std::size_t CAPACITY = 8;

      /** @brief Longest clock name a slot carries, including the terminator. */
      static constexpr std::size_t NAME_CAPACITY = 64;

      /**
       *  @brief How often ``Poll`` retries a binding that has not resolved, in
       *         patcher blocks. Long enough that a permanently unknown name
       *         costs nothing measurable, short enough that a clock created
       *         after the patch is picked up in well under a second.
       */
      static constexpr std::uint64_t RESOLVE_INTERVAL_BLOCKS = 64;

      clockBridge();
      ~clockBridge();
      clockBridge(const clockBridge&) = delete;
      clockBridge& operator=(const clockBridge&) = delete;
      clockBridge(clockBridge&&) = delete;
      clockBridge& operator=(clockBridge&&) = delete;

      /**
       *  @brief Bind the domain clock called @p name. Any thread; wait-free; no
       *         allocation.
       *
       *  Returns the handle already held for that name when there is one, a
       *  fresh handle otherwise, and 0 when @p name is empty, longer than
       *  ``NAME_CAPACITY - 1``, or the table is full. Never fails because the
       *  clock does not exist — that is only discovered on the background pool
       *  and reported by ``Beat`` answering false.
       */
      Handle Bind(const char* name, std::size_t length);

      /**
       *  @brief The bound clock's current beat position. Any thread; wait-free.
       *
       *  False for handle 0, an out-of-range handle, or a binding that has not
       *  resolved yet; @p beat is then untouched.
       */
      bool Beat(Handle handle, double& beat) const;

      /**
       *  @brief The beat the bound clock stood at when this binding resolved.
       *
       *  0 while unresolved. Any thread; wait-free. See the class notes: this
       *  is the baseline a wait armed before resolution measures from.
       */
      double ResolveBeat(Handle handle) const;

      /** @brief Whether @p handle names a clock that has been found. */
      bool Resolved(Handle handle) const;

      /** @brief The name @p handle was bound with, or ``""``. The storage is
       *         written once and never changes, so the pointer stays valid for
       *         the life of the bridge. */
      const char* NameOf(Handle handle) const;

      /**
       *  @brief Re-arm resolution for bindings still waiting on their clock.
       *
       *  Audio thread, from the top of Calculate with the patcher's block
       *  count. Rate-limited to one pass every ``RESOLVE_INTERVAL_BLOCKS``
       *  blocks; a pass is a bounded walk of ``CAPACITY`` slots and at most
       *  ``CAPACITY`` lock-free pushes onto the background ring.
       */
      void Poll(std::uint64_t block);

      /** @brief Names bound so far, resolved or not. Diagnostics / tests. */
      std::size_t BoundCount() const;

      /** @brief Binds refused so far (full table, empty or over-long name). A
       *         counter rather than a log line because the binding thread may
       *         be the audio callback. Diagnostics / tests. */
      std::uint64_t Dropped() const;

      /**
       *  @brief Block until no resolve job is still on the background pool.
       *
       *  Control thread only. Does not make an unknown name resolve — it only
       *  makes the *attempt* deterministic, so a test that created its clock
       *  first can assert on the binding without sleeping.
       */
      void WaitIdle();

    private:
      // Slot lifecycle. One atomic word, no generation counter: a binding is
      // never released, so no handle outlives a slot and there is no ABA.
      //
      //   FREE --Bind--> CLAIMED --publish--> BOUND (forever)
      //
      // `name` is written only in CLAIMED and read only once BOUND, so it is
      // never touched concurrently. `clock` and `resolveBeat` are written by
      // the resolve job and read by everyone, so they are atomics.
      static constexpr std::uint32_t STATE_FREE = 0;
      static constexpr std::uint32_t STATE_CLAIMED = 1;
      static constexpr std::uint32_t STATE_BOUND = 2;

      struct Entry;

      // One slot's worth of name resolution. Pre-allocated with the slot, so
      // arming it allocates nothing; it holds only its owner and its own slot,
      // never a pObject, so a concurrent DeleteObject has nothing to dangle on.
      //
      // It points *at* its slot rather than carrying an index into
      // ``entries_``, and that is load-bearing rather than a style choice:
      // ``unique_ptr::reset`` nulls its pointer **before** running the element
      // destructors, so a job that reached through ``entries_`` while
      // ~clockBridge was in progress would index off a null member — and the
      // window is exactly the one the joins below exist to cover, since the
      // job it is waiting for is by definition still running. The slot's own
      // address stays valid until ``operator delete[]``, which is after every
      // join.
      struct resolveJob : INTERNAL::threadPoolJob {
        clockBridge* owner = nullptr;
        Entry* slot = nullptr;
        void run() override;
      };

      struct Entry {
        std::atomic<std::uint32_t> state{STATE_FREE};
        // Published by the resolve job; null until the name is found. The
        // release store on `clock` is the publish, so `resolveBeat` is written
        // first and is visible to anyone who sees a non-null clock.
        std::atomic<CLOCK::domainClock*> clock{nullptr};
        std::atomic<double> resolveBeat{0.0};
        char name[NAME_CAPACITY] = {};
        // Declared last so it is destroyed *first*: ~resolveJob joins, and a
        // job that is still running reads every field above it.
        resolveJob job;
      };

      // Background pool: look @p entry's name up and publish the result.
      void RunSlot(Entry& entry);

      // Push slot @p index's resolve job onto the background pool unless it is
      // already there. Wait-free; safe on the audio callback.
      void ArmResolve(std::size_t index);

      // Whether the `length` characters at `text` are exactly the C string at
      // `stored`. Compared in place rather than through a std::string, since
      // Bind may be running on the audio thread.
      static bool NameIs(const char* stored, const char* text, std::size_t length);

      // One heap block, allocated with the bridge and never resized. Held by
      // pointer for the reason fileScheduler holds its table that way, and
      // declared first because each Entry's job joins in its own destructor and
      // must do so while its slot still exists.
      std::unique_ptr<Entry[]> entries_;
      std::atomic<std::uint64_t> dropped_{0};
      // Audio-thread-only: the block Poll is next allowed to do a pass on.
      std::uint64_t nextPollBlock_ = 0;
    };

  } // namespace PATCHER
} // namespace YSE
