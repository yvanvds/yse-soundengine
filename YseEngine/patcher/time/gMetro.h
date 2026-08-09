
#pragma once

#include "../pObject.h"
#include "TimerThread.h"
#include "clockBridge.h"
#include "messageScheduler.h"
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Max's ``metro``: "output a bang message at regular intervals",
     *         in milliseconds or in beats on a domain clock (issue #705).
     *
     *  ### Two clocks, and which one a metro runs on
     *
     *  Milliseconds run on ``timerThread`` — a real OS timer, the object's
     *  original and unchanged engine. A metronome's bang genuinely *is* an
     *  unrelated stimulus (nothing caused it), which is what lets this object
     *  use a timer where ``.delay`` may not: see ``gDelay.h`` on why a delayed
     *  bang has to stay inside the patcher's dispatch.
     *
     *  Beats run on the patcher's deferred-message scheduler against a bound
     *  ``CLOCK::domainClock``, because that is the only clock in the process
     *  that knows what a beat is. Which engine a run uses is decided **at the
     *  toggle**, by whether a tempo-relative interval is in force, and does not
     *  change under a running metro — a unit change takes effect at the next
     *  start, the way Max's ``interval`` attribute takes effect at the next
     *  output.
     *
     *  ### ``clock <name>`` and a tempo-relative interval (issue #705)
     *
     *  Max's ``metro`` is one of the objects ``setclock`` names explicitly —
     *  "passed as the argument to a ``clock`` message to numerous objects that
     *  use timing in Max, such as ``metro``, ``line`` and ``pipe``" — so unlike
     *  ``.qlist``'s and ``.seq``'s, this ``clock`` is a **port** and not an
     *  addition. ``clock <name>`` binds the domain clock of that name through
     *  #688's bridge; a bare ``clock`` gives it back, Max's "sets the object
     *  back to using Max's regular millisecond clock".
     *
     *  The interval itself comes in through Max's own ``list`` / ``anything``
     *  method on the right inlet — "a list may be used to specify time in one
     *  of the Max time formats" — as a note value (``4n``, ``4nd``, ``8nt``) or
     *  a tick count (``1440 ticks``), read by ``time/timeValue.h`` and shared
     *  with ``.delay``. Any plain number puts the object back on milliseconds,
     *  Max's own "the number is the time interval, in milliseconds". A
     *  tempo-relative interval with no clock bound does not run at all, for the
     *  reason ``.delay`` does not fire on one: this patcher has no transport,
     *  so there is nothing to measure a beat against, and running the last
     *  millisecond interval instead would be a metronome at a tempo nobody
     *  asked for. ``bars.beats.units``, ``quantize`` and ``transport`` stay out
     *  — all three need a meter, and a ``domainClock`` is a bare beat
     *  accumulator with none.
     *
     *  ### The bang count is read off the clock, never counted from wakeups
     *
     *  This is the one place the obvious implementation is wrong, and a metro
     *  that drifts is a metro that is broken.
     *  ``messageScheduler::ScheduleBangOnClock`` arms relative to the beat the
     *  arm was taken at, but a delivery lands at the first *audio block*
     *  boundary at or after that deadline. Re-arming "one interval from now"
     *  per delivered bang would therefore drop that overshoot **every bang**
     *  and run the metro slow; and once the interval is shorter than a block,
     *  it would cap at one bang per block however fast the domain ran.
     *  ``.seq`` (#704) found this the same way.
     *
     *  So the wakeup is a *polling rate* and the beat position is the time
     *  base. A run keeps the beat its first bang stood on (``beatBase``) and
     *  how many bangs it has emitted (``emitted``); each delivery reads the
     *  clock, takes ``n = floor((beat - beatBase) / interval)``, and emits
     *  ``n - emitted`` bangs — more than one when a wakeup covered several
     *  intervals, none when it arrived early. The next wakeup is armed at the
     *  **absolute** grid point ``beatBase + (n + 1) × interval``, not one
     *  interval from now, so the arm carries no accumulated error either.
     *  Nothing drifts and nothing accumulates, at any tempo and any interval.
     *
     *  A catch-up burst is bounded by ``MAX_CATCHUP``: a domain that jumps a
     *  thousand beats in one block skips the bangs it missed rather than
     *  emitting them all on the audio thread, which is the same trade every
     *  bounded walk in the patcher makes.
     *
     *  ### The edges, and how they answer
     *
     *  - **A clock named before the host creates it.** The metro starts, bangs
     *    once (Max bangs immediately on start) and then waits; the baseline is
     *    taken at the first wakeup that finds a clock, so the run begins when
     *    the clock starts existing. ``messageScheduler``'s ``ResolveBeat``
     *    rule, arrived at for the same reason.
     *  - **A clock at tempo 0** never brings another bang due, and one
     *    destroyed under a bound object leaves a frozen beat (#707), so the
     *    metro holds where it stands. That is the honest reading of "every
     *    quarter note" on a clock that is not moving.
     *  - **A beat interval change while running** rebases the grid at the
     *    current beat rather than jumping the phase, which is #625's rule for
     *    the millisecond path ("rescheduling keeps the phase, so a tempo tweak
     *    does not re-trigger whatever the bang drives"). Both routes that write
     *    the interval are covered: the list inlet re-arms eagerly, and a live
     *    ``SetParams`` re-parse — which stores straight into the field from the
     *    audio thread and notifies nobody — is caught at the next wakeup, which
     *    compares the live interval against the grid's.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlets and by
     *  its own clock. No message or delivery path allocates, locks or blocks —
     *  binding is the bridge's wait-free claim (the name lookup happens on the
     *  background pool, because it takes the clock manager's mutex), arming and
     *  cancelling are wait-free, reading a beat is two acquire loads, and the
     *  grid is guarded by ``.value``'s non-blocking ``busy`` exchange, whose
     *  loser does nothing rather than waiting.
     */
    PATCHER_CLASS(gMetro, YSE::OBJ::G_METRO)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(Toggle)
    _INT_IN(SetIntPeriod)
    _FLOAT_IN(SetFloatPeriod)
    _LIST_IN(ListIn)

    void Bang();

    ~gMetro() override;

    /** @brief Most bangs one delivery emits when a wakeup covered several
     *         intervals. Past this the run skips ahead rather than emptying an
     *         unbounded burst onto the audio thread. */
    static constexpr std::int64_t MAX_CATCHUP = 64;

    /** @brief The tempo-relative interval in beats, or 0 when the interval is
     *         milliseconds (issue #705). Set by a note value or a tick count
     *         and cleared by any plain number, which is Max's "the number is
     *         the time interval, in milliseconds". */
    double PeriodBeats() const;

    /** @brief Whether ``clock <name>`` has bound a domain clock for a
     *         tempo-relative interval to be counted on (issue #705). */
    bool OnClock() const {
      return binding.load(std::memory_order_relaxed) != 0;
    }

    /** @brief The clock name the object is bound to, or ``""``. The storage
     *         belongs to the patcher's bridge and never changes, so this is
     *         safe from any thread. */
    const char* ClockName() const;

    /** @brief Whether this run is on a domain clock rather than on the
     *         millisecond timer. False while the metro is stopped. */
    bool RunningOnClock() const {
      return beatOn.load(std::memory_order_relaxed);
    }

    // The scheduler coming back with a beat wakeup that has come due.
    void DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) override;

  private:
    /**
     *  @brief Non-blocking exclusive access to the beat grid.
     *
     *  ``Held()`` is false when another thread had it — the caller then does
     *  nothing at all. Never waits, never allocates. ``.qlist``'s and
     *  ``.seq``'s ``storeGuard``, for the reason both give: this object is
     *  reachable from the control thread and from a rendering graph alike, a
     *  mutex is out on the second of those, and there is no single writer to
     *  build a seqlock around.
     */
    class storeGuard {
    public:
      explicit storeGuard(std::atomic<bool>& flag)
        : flag_(flag), held_(!flag.exchange(true, std::memory_order_acquire)) {}
      ~storeGuard() {
        if (held_) flag_.store(false, std::memory_order_release);
      }
      storeGuard(const storeGuard&) = delete;
      storeGuard& operator=(const storeGuard&) = delete;
      storeGuard(storeGuard&&) = delete;
      storeGuard& operator=(storeGuard&&) = delete;

      bool Held() const {
        return held_;
      }

    private:
      std::atomic<bool>& flag_;
      bool held_;
    };

    // Push the current `period` onto the running timer, if any. No-op when the
    // metro is stopped or the interval has not moved (issue #625).
    void ApplyPeriod();

    // `period` clamped to the documented 1+ ms range, in the timer's unit.
    timerThread::millisec Interval() const;

    // Max's `clock <name>` / bare `clock`, through the patcher's bridge (issue
    // #705). Binds wait-free on whichever thread the message arrived on; a name
    // that does not fit, a bridge that is full, or a standalone object all
    // leave the object where it was, silently, since this may be the audio
    // thread.
    void SetClock(const char* name, std::size_t length);

    // Stop whichever engine is running, both of them being idempotent to stop.
    void StopRun();

    // Begin a run on the bound domain clock: take the baseline, arm the first
    // wakeup, and bang. `bound` is non-zero and `PeriodBeats()` positive.
    void StartBeats(clockBridge::Handle bound, YSE::THREAD thread);

    // Arm the next wakeup at the *absolute* next grid point rather than one
    // interval from here, which is half of what keeps the run from drifting;
    // the other half is that the delivery reads its bang count off the clock.
    // Guard held. Falls back to a plain interval while the binding has not
    // resolved, there being no beat yet to measure a grid point from.
    void ArmNext(clockBridge::Handle bound);

    // Drop the pending wakeup, if there is one. Guard held.
    void CancelWakeup();

    // Rebase the grid at the current beat and re-arm — a beat interval change
    // under a running metro, which keeps the phase rather than re-triggering.
    void RetimeBeats();

    // Max's interval in milliseconds, and the first creation argument.
    aInt period;

    // The tempo-relative interval in beats, or 0 for milliseconds — the second
    // creation argument (issue #705). Atomic for `period`'s reason: a live
    // SetParams re-parse writes it from the audio thread.
    aFlt periodbeats;

    // The domain clock `clock <name>` bound, or 0 for Max's millisecond clock
    // (issue #705). A patcher-owned binding handle rather than a name: the
    // bridge never releases one, so it stays valid for the life of the patcher.
    // Atomic because a `clock` message and a wakeup are not on the same thread.
    std::atomic<clockBridge::Handle> binding{0};

    // Written by the toggle inlet on the control thread, read by Bang() on the
    // timer thread — atomic so the live reschedule is not a data race. Ids are
    // handed out monotonically and never recycled, so a stale id read here can
    // only miss, never hit the wrong timer.
    std::atomic<timerThread::timerID> id;

    // Whether this run is on the domain clock rather than on the timer. The
    // millisecond run has `id` for the same job; a beat run needs its own flag
    // because a wakeup that finds the metro stopped must not re-arm.
    std::atomic<bool> beatOn{false};

    // The beat grid, guarded: written by the toggle and list inlets on whichever
    // thread dispatched them and by a wakeup on the audio thread.
    std::atomic<bool> busy{false};
    // The beat bang 0 of this run stood on, and whether it has been taken yet —
    // it cannot be while the binding is unresolved, so the first wakeup that
    // finds a clock takes it instead.
    double beatBase = 0.0;
    bool beatBased = false;
    // The interval the current grid is built on. Compared against the live
    // `periodbeats` at every wakeup, which is how the SetParams route reaches a
    // running metro — `Bang()`'s ApplyPeriod, for the beat clock.
    double gridBeats = 0.0;
    // Bangs emitted since `beatBase`, and the index the next grid point is at.
    std::int64_t emitted = 0;
    // The wakeup this object is waiting on, or 0. One clock per object.
    messageScheduler::Handle pending = 0;
  };
}
}
