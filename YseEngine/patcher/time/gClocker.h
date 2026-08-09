
#pragma once

#include "../pObject.h"
#include "TimerThread.h"
#include "clockBridge.h"
#include "messageScheduler.h"
#include "timerBridge.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Max's ``clocker``: "report elapsed time, at regular intervals"
     *         (issues #505 and #725).
     *
     *  ### What it is, next to the objects it sits between
     *
     *  ``.metro`` bangs at an interval and ``.tempo`` counts a subdivision of a
     *  clock. This object emits a **number**, and the number is *how long the
     *  run has been going* — Max's "a metronome that reports the time elapsed
     *  since it was started". That is the difference worth stating, because it
     *  is what removes an object from the patch: an envelope, a ramp or a
     *  progress readout driven by a ``.metro`` needs a ``.counter`` (or a
     *  ``.accum``) behind it to turn ticks into a position, and that counter is
     *  wrong the moment a tick is late or dropped. A ``.clocker`` hands the
     *  position over directly, measured rather than tallied.
     *
     *  ### The elapsed time is measured, never counted
     *
     *  This is the one place the obvious implementation is wrong, and it is
     *  what issue #505 means by "elapsed time should be derived from the same
     *  clock source the rest of the engine uses, not from an independent
     *  wall-clock read".
     *
     *  An OS timer is a *polling rate*, not a time base. ``timerThread`` wakes
     *  its worker, sorts a queue and runs a callback, so a tick nominally due at
     *  50 ms lands at 51 or at 63 depending on what the scheduler was doing;
     *  multiplying a tick count by the interval would report 50 either way and
     *  drift further from the truth on every tick. So a run records the instant
     *  it started and each tick *reads the clock* and subtracts — the same
     *  discipline ``.metro`` (#705) and ``.tempo`` (#512) apply to a beat grid,
     *  arrived at for the same reason.
     *
     *  The clock read is ``std::chrono::steady_clock``, which is not an
     *  independent choice: it is the very clock ``timerThread`` schedules on
     *  (``timerThread::Clock``), the one ``INTERNAL::time`` measures the
     *  engine's ticks with, and the one ``MIDI::midiOutSender`` dates its
     *  outbound events by. Sharing it is what makes the reported number and the
     *  tick that carried it two views of one timeline rather than two clocks
     *  that disagree. A *wall* clock — ``system_clock`` — would have been the
     *  wrong quantity twice over: it is not monotonic, so an NTP correction mid
     *  run would make the elapsed time jump or go backwards.
     *
     *  ### Max's methods, and the one place two of them deliberately differ
     *
     *  - **``int``** — "any non-zero number starts the clocker object. The time
     *    elapsed since clocker was started is sent out the outlet at regular
     *    intervals. 0 stops the clocker object." **``float``** is "same as
     *    int", and is compared against zero rather than cast, so ``0.5`` starts
     *    where ``(int)0.5`` would stop — ``.metro``'s rule (#711).
     *  - **``stop``** — "in left inlet: stops the clocker object", which is
     *    ``int 0`` under another name, so it goes through the one path.
     *  - **``reset``** — "resets the elapsed time to 0 without stopping or
     *    restarting the clock; clocker continues to report the new elapsed time
     *    at the same regular interval." The phase of the tick is therefore
     *    explicitly *not* touched: only the instant the elapsed time is
     *    measured from moves — on either engine, so a reset under a beat run
     *    rewinds both reported numbers and leaves the grid exactly where it
     *    stands. Max adds that it "is meaningless when the clocker is not
     *    running, since it always resets to 0 anyway when stopped", and a
     *    stopped ``.clocker`` reads 0 here for exactly that reason.
     *  - **``bang``** — "if the clocker object is not running, a bang message
     *    will start the count. If the clocker object is running, a bang message
     *    will reset the count."
     *  - **``clock <name>``** — issue #725, and Max's own method: ``setclock``'s
     *    name is "passed as the argument to a ``clock`` message to numerous
     *    objects that use timing in Max". Here it names the YSE domain clock
     *    the beats are counted on, the same message ``.metro`` (#705),
     *    ``.timepoint`` (#507) and ``.tempo`` (#512) take through #688's
     *    ``clockBridge``; a bare ``clock`` gives it back, Max's "sets the object
     *    back to using Max's regular millisecond clock".
     *
     *  ``bang`` and ``int 1`` are *not* the same method here, where on
     *  ``.metro`` they are. Max's page gives ``bang`` a running-object rule of
     *  its own and gives ``int`` none, and the rule it gives is ``reset``'s: a
     *  bang into a running clocker moves the baseline and leaves the tick where
     *  it is. A non-zero ``int`` is read as ``.metro``'s "start", which on a
     *  running object is a re-start — a fresh baseline *and* a re-phased timer —
     *  so a toggle box switched off and on puts the run back on its own grid.
     *  Both zero the reported number; only the int moves the tick.
     *
     *  ### The interval, in milliseconds or in beats (issue #725)
     *
     *  Max's ``interval`` attribute and the creation argument, default **5 ms**
     *  — "if there is no argument, the initial time interval is set to 5
     *  milliseconds". A number in the right inlet sets it, and setting it under
     *  a running clocker **retimes without re-phasing**, which is #625's rule
     *  for ``.metro``'s millisecond path and matters more here than there: the
     *  elapsed time is measured, so a re-phase would show up as a jump in the
     *  reported number rather than only in the tick's timing. Both routes into
     *  the field are covered — the inlet retimes eagerly, and a live
     *  ``SetParams`` re-parse, which stores straight into it from the audio
     *  thread and notifies nobody, is re-asserted by the next tick.
     *
     *  Since #725 the interval may be **tempo-relative** instead: a note value
     *  (``4n``, ``4nd``, ``8nt``) or a tick count (``1440 ticks``) in the right
     *  inlet sets it in beats, read by the shared ``timeValue.h`` so that this
     *  object and ``.metro`` cannot drift apart on the same syntax. Any plain
     *  number puts the interval back on milliseconds, which is Max's "the number
     *  is the time interval, in milliseconds" — a statement about the *unit*.
     *  Reading the tempo-relative form **first** is what closes the trap #505
     *  documented and answered by refusing every such message: Max's tick
     *  spelling begins with a digit, so ``1440 ticks`` taken as a leading number
     *  would silently become 1440 *milliseconds*, wrong by whatever the tempo is
     *  and looking like it worked.
     *
     *  Which engine a run uses is decided **at the toggle**, once, and does not
     *  change under it — ``.metro``'s rule (#705). A tempo-relative interval
     *  with no clock bound does not run at all: falling back to the millisecond
     *  interval would report against a grid nobody asked for, and a later
     *  ``clock <name>`` makes the next start work.
     *
     *  ``bars.beats.units``, ``quantize``, ``autostart``, ``autostarttime`` and
     *  ``defer`` stay out. The first two need a meter, and a
     *  ``CLOCK::domainClock`` is a bare beat accumulator with no bar, no meter
     *  and no downbeat; the other three need machinery the patcher does not
     *  have.
     *
     *  ### What the outlets carry on a bound clock (issue #725)
     *
     *  This is the question #505 left open, and the reason binding a clock here
     *  is not the trivial port of ``.metro``'s route: ``.metro`` emits a bang,
     *  so a beat-relative interval only changes *when* it fires, while this
     *  object emits a number and something has to say whether "elapsed" is then
     *  milliseconds or beats.
     *
     *  The answer is **both, spelled by two outlets**, which is exactly
     *  ``.timer``'s answer to the same question (#506) and is chosen for the
     *  same two reasons. It keeps a unit out of the *mode*: outlet 0 is
     *  milliseconds always, so no patch changes meaning when a clock is bound
     *  and no downstream object has to guess what it is reading. And the two
     *  numbers are genuinely two measurements rather than two spellings of one
     *  — the milliseconds are wall-monotonic and the beats are the clock's, so a
     *  domain that ramps, pauses or is retempoed mid-run makes them diverge, and
     *  the divergence is the information. Dividing one by a BPM figure could not
     *  have produced the other.
     *
     *  - **Outlet 0, milliseconds.** Unchanged by #725, on either engine.
     *  - **Outlet 1, beats since the run started** — or since the last ``reset``
     *    / ``bang``, the baseline being the one the milliseconds use. Sent
     *    *first*, Max's outlets firing right to left, and only when there is a
     *    beat baseline to measure from: nothing at all with no clock bound, with
     *    a clock nothing has created, or on a bridge-less standalone object.
     *
     *  Note what the beats outlet is *not* gated on: the interval. A ``.clocker
     *  200`` sent ``clock main`` reports both numbers, because reporting a
     *  measurement is not the same act as choosing when to report it.
     *
     *  ### The beat grid is anti-drift, and a late wakeup reports once
     *
     *  A beat run is scheduled through ``messageScheduler::ScheduleBangOnClock``
     *  on the bound clock, and it keeps ``.metro``'s and ``.tempo``'s two
     *  halves: the next wakeup is armed at the **absolute** next grid point
     *  ``beatBase + (n + 1) × interval`` rather than one interval from here, and
     *  the grid index is taken as ``floor((beat - beatBase) / interval)`` off
     *  the clock's beat position rather than counted from the wakeups. A
     *  delivery lands at the first audio block boundary at or after its
     *  deadline, so counting deliveries would hand back that overshoot on every
     *  report and run the grid slow.
     *
     *  Where this object parts from those two is what a wakeup that covered
     *  *several* grid points does: it reports **once**. ``.metro`` owes the
     *  patch the bangs it missed and ``.tempo`` owes it the positions, but this
     *  object's number is the time *now* — n copies of it would be n copies of
     *  one reading, and the milliseconds of the reports that were missed are not
     *  recoverable from anywhere. The index still advances to the grid point
     *  actually reached, so nothing accumulates into the next wakeup.
     *
     *  This object never creates a clock and never destroys one: ``.transport``
     *  is the sole creator (#513). A name nothing has claimed simply has no
     *  beats until something claims it, and the run picks the clock up when it
     *  appears — the first report that finds one takes its baseline there.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlets, by its
     *  timer and by the scheduler. A patcher message handler runs on **whichever
     *  thread dispatched the message** — in-patcher delivery dispatches
     *  ``T_DSP``, and the drains at the top of
     *  ``patcherImplementation::Calculate`` dispatch ``T_GUI`` *from the audio
     *  callback* — so a ``.delay`` wired into the left inlet puts a start on the
     *  audio thread. Nothing on any path there allocates, locks or blocks:
     *
     *  - the millisecond timer is armed and disarmed through ``timerBridge``
     *    (#718), whose ``Request*`` front is a handful of atomic stores and a
     *    push onto a pre-allocated lock-free job, with the locking
     *    ``timerThread`` work done on the background pool. Off the audio
     *    callback the ``Apply*`` front does it inline, keeping the
     *    stop-means-stopped handshake. Which of the two, this object decides
     *    from ``patcherImplementation::CallingThread`` (#690) rather than from
     *    the ``THREAD`` tag, because the tag is dispatch semantics and not
     *    thread identity;
     *  - binding a clock is a bounded walk of the bridge's table and a
     *    ``memcpy``, arming and cancelling a beat wakeup are lock-free pushes,
     *    and reading a beat is two acquire loads. The *name lookup* is the one
     *    thing that takes the clock manager's mutex, and it happens on the
     *    background pool;
     *  - the baseline is one 64-bit atomic store and one
     *    ``steady_clock::now()``, which is a ``QueryPerformanceCounter`` on
     *    Windows and a vDSO ``clock_gettime`` on Linux and Android —
     *    ``INTERNAL::time::update`` already reads it from the audio callback
     *    every block (#667);
     *  - the command words are matched against the message in place (a
     *    ``substr`` would allocate) and numbers go through ``pListArgs.h``'s
     *    allocation-free readers.
     *
     *  The one by-name read of ``CLOCK::Manager`` — ``.timer``'s fallback for a
     *  binding that has not resolved yet — is taken only off the audio callback
     *  and only from a message handler, never from a report. It is what lets a
     *  start sent in the same breath as ``clock <name>`` have a beat baseline at
     *  all, and it builds a ``std::string`` to ask with, which is that thread's
     *  to make.
     */
    PATCHER_CLASS(gClocker, YSE::OBJ::G_CLOCKER)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(Toggle)
    _FLOAT_IN(ToggleFloat)
    _BANG_IN(BangIn)
    _LIST_IN(Command)
    _INT_IN(SetIntInterval)
    _FLOAT_IN(SetFloatInterval)

    ~gClocker() override;

    /** @brief What the millisecond timer calls: read the clock, send the
     *         elapsed time. Public because the trampoline is, and because a
     *         test may want one report without waiting for one. */
    void Tick();

    /** @brief The scheduler coming back with a beat wakeup that has come due. */
    void DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) override;

    /** @brief Whether the clocker is running. Created stopped, as Max's is. */
    bool Running() const {
      return running.load(std::memory_order_relaxed);
    }

    /** @brief The reporting interval in milliseconds, floored at the 1 ms the
     *         timer can actually keep. What a run uses only while the interval
     *         is *not* tempo-relative. */
    timerThread::millisec IntervalMs() const;

    /** @brief The tempo-relative interval in beats, or 0 when the interval is
     *         milliseconds (issue #725). Set by a note value or a tick count in
     *         the right inlet and cleared by any plain number. */
    double IntervalBeats() const;

    /** @brief Whether a domain clock is bound (issue #725). */
    bool OnClock() const {
      return binding.load(std::memory_order_relaxed) != 0;
    }

    /** @brief The clock name the object is bound to, or ``""``. The storage
     *         belongs to the patcher's bridge and never changes, so this is
     *         safe from any thread. */
    const char* ClockName() const;

    /** @brief Whether this run is driven by the domain clock's beat grid rather
     *         than by the millisecond timer. False while stopped. */
    bool RunningOnClock() const {
      return beatOn.load(std::memory_order_relaxed);
    }

    /** @brief The number the next report would send on outlet 0: milliseconds
     *         since the run started or since the last ``reset``. **0 while
     *         stopped** — Max's "it always resets to 0 anyway when stopped". */
    std::int64_t Elapsed() const;

    /** @brief The number the next report would send on outlet 1: beats of the
     *         bound clock since the same baseline. False, writing nothing, when
     *         there is no beat to report — which is when outlet 1 stays silent.
     *         Control thread (it may take the clock manager's mutex). */
    bool ElapsedBeats(double& beats) const;

  private:
    /**
     *  @brief Non-blocking exclusive access to the beat grid.
     *
     *  ``Held()`` is false when another thread had it — the caller then does
     *  nothing at all. Never waits, never allocates. ``.metro``'s
     *  ``storeGuard``, for the reason it gives: a message handler may be on the
     *  audio callback, so a mutex is out, and both the control thread and a
     *  delivery write this state, so there is no single writer to build a
     *  seqlock around.
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

    // steady_clock now, in nanoseconds. The engine's monotonic time source —
    // `timerThread::Clock`, `INTERNAL::time` and `MIDI::nowNs` are the same
    // clock — kept in nanoseconds so the millisecond the outlet carries is a
    // rounding of a finer reading rather than a sum of coarse ones.
    static std::int64_t NowNs();

    // What the timer calls. A free function and a context rather than a
    // `std::function`, because building one of those is an allocation and the
    // thread that arms the timer may be the audio callback (issue #718).
    static void TickTrampoline(void* ctx);

    // Whether the handler currently running is on the audio callback, which is
    // the question a `THREAD` tag cannot answer — see the header. False for a
    // standalone object, which has no patcher to ask and is never rendered.
    bool OnAudioThread(YSE::THREAD thread) const;

    // The bound clock's beat position through the bridge alone: two acquire
    // loads, any thread, false while nothing is bound or the binding has not
    // resolved. The only read a report is allowed to make.
    bool ReadBeatFast(double& beat) const;

    // The same, with `.timer`'s by-name fallback for the window between binding
    // a name and the background pool resolving it (#506). Message handlers
    // only: off the audio callback it takes the clock manager's mutex and
    // builds a std::string to ask with, and on it there is simply nothing to
    // report.
    bool ReadBeat(YSE::THREAD thread, double& beat) const;

    // Take both baselines from one instant — the beat first, so the pair a
    // report subtracts belongs to one moment.
    void TakeBaselines(YSE::THREAD thread);

    // Max's start / re-start (fresh baselines and a re-phased grid, on
    // whichever engine the interval selects), his stop, and his reset (fresh
    // baselines, the grid left alone).
    void Start(YSE::THREAD thread);
    void StopRun(YSE::THREAD thread);
    void Reset(YSE::THREAD thread);

    // Arm and disarm the millisecond timer, deferring the `timerThread` call to
    // the background pool when this handler is on the audio callback or inside
    // this object's own tick, and making it inline when it is not (issue #718).
    void StartMillis(YSE::THREAD thread);
    void StopMillis(YSE::THREAD thread);

    // Push the current interval onto a running timer without re-phasing it
    // (issue #625). No-op when the interval has not moved — `timerBridge` drops
    // a request that does not, which is what lets a tick re-assert it for free.
    void ApplyInterval(YSE::THREAD thread);

    // Max's `clock <name>` / bare `clock`, through the patcher's bridge (issue
    // #725). Binds wait-free on whichever thread the message arrived on; a name
    // that does not fit, a bridge that is full, or a standalone object all leave
    // the object where it was, silently, since this may be the audio thread.
    void SetClock(const char* name, std::size_t length, YSE::THREAD thread);

    // Begin a run on the bound domain clock: take the grid's baseline and arm
    // the first wakeup. Nothing is emitted — Max documents an immediate output
    // for `metro` and documents none here.
    void StartBeats(clockBridge::Handle bound);

    // Arm the next wakeup at the *absolute* next grid point rather than one
    // interval from here, which is half of what keeps the grid from drifting;
    // the other half is that the delivery reads its index off the clock. Guard
    // held. Falls back to a plain interval while the binding has not resolved,
    // there being no beat yet to measure a grid point from.
    void ArmNext(clockBridge::Handle bound);

    // Drop the pending wakeup, if there is one. Guard held.
    void CancelWakeup();

    // Take the grid afresh at the current beat and re-arm — a `clock <name>`
    // under a running beat clocker, which moves the grid onto the new clock
    // without touching what the outlets measure from.
    void RebaseGrid();

    // A beat interval change under a running beat clocker: rebase the grid at
    // the current beat rather than jumping the phase, #625's rule.
    void RetimeBeats();

    // One report: the beats (when there is a baseline) and then the
    // milliseconds, both read before either is sent.
    void Report(YSE::THREAD thread);

    // Max's `interval` attribute and the creation argument, in milliseconds.
    // Atomic because a live SetParams re-parse writes it from the audio thread.
    aInt interval;

    // The tempo-relative interval in beats, or 0 for milliseconds — the second
    // creation argument (issue #725). Atomic for `interval`'s reason.
    aFlt intervalbeats;

    // Whether the run is on. The timer's own armed state is the bridge slot's,
    // but a tick that races a stop needs a flag of its own to find, and
    // `Elapsed()` needs one to answer 0 from.
    std::atomic<bool> running{false};

    // The steady_clock instant, in nanoseconds, the elapsed time is measured
    // from: the start, or the last `reset` / `bang` under a running clocker.
    // Meaningless while stopped, which is why `Elapsed()` checks `running`
    // first rather than trusting it.
    std::atomic<std::int64_t> baseNs{0};

    // The beat the same baseline stands at, or NaN when there was no clock to
    // read it from. NaN rather than a flag because there is no beat value that
    // could not legitimately be a baseline, and because the first report that
    // finds a clock adopts it — that is one relaxed store either way.
    std::atomic<double> baseBeat{std::numeric_limits<double>::quiet_NaN()};

    // The domain clock `clock <name>` bound, or 0 for Max's millisecond clock
    // (issue #725). A patcher-owned binding handle rather than a name: the
    // bridge never releases one, so it stays valid for the life of the patcher.
    // Atomic because a `clock` message and a report are not on the same thread.
    std::atomic<clockBridge::Handle> binding{0};

    // This object's slot in the process-wide timer bridge (issue #718), or 0
    // when the table was full. Taken in the constructor and given back in the
    // destructor, and never written in between, so no thread synchronisation is
    // needed to read it. The millisecond run's timer state — armed or not, at
    // which period — lives in that slot rather than here, because the slot has
    // to outlive this object: it is what a reconcile job already on the
    // background pool addresses.
    timerBridge::Handle timerSlot = 0;

    // Whether this run is on the domain clock rather than on the timer. The
    // millisecond run has its bridge slot for the same job; a beat run needs
    // its own flag because a wakeup that finds the clocker stopped must not
    // re-arm.
    std::atomic<bool> beatOn{false};

    // The beat grid, guarded: written by the inlets on whichever thread
    // dispatched them and by a wakeup on the audio thread. The *baselines*
    // above are deliberately outside it — they are single atomics with no
    // multi-field invariant, and a reset must never be dropped for want of a
    // guard.
    std::atomic<bool> busy{false};
    // The beat grid point 0 of this run stood on, and whether it has been taken
    // yet — it cannot be while the binding is unresolved, so the first wakeup
    // that finds a clock takes it instead.
    double beatBase = 0.0;
    bool beatBased = false;
    // The interval the current grid is built on. Compared against the live
    // `IntervalBeats()` at every wakeup, which is how the SetParams route
    // reaches a running clocker.
    double gridBeats = 0.0;
    // Grid points passed since `beatBase`, and the index the next one is at.
    std::int64_t emitted = 0;
    // The wakeup this object is waiting on, or 0. One clock per object.
    messageScheduler::Handle pending = 0;
  };
}
}
