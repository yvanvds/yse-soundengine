
#pragma once

#include "../pObject.h"
#include "TimerThread.h"
#include "clockBridge.h"
#include "messageScheduler.h"
#include "timerBridge.h"
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
     *  original engine, reached since #718 through ``timerBridge`` so that
     *  arming and disarming it is safe from a handler that turns out to be on
     *  the audio callback. A metronome's bang genuinely *is* an unrelated
     *  stimulus (nothing caused it), which is what lets this object use a timer
     *  where ``.delay`` may not: see ``gDelay.h`` on why a delayed bang has to
     *  stay inside the patcher's dispatch.
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
     *  ### Starting and stopping: Max's four left-inlet methods (issue #711)
     *
     *  Max's reference page gives four ways to start or stop a ``metro`` from
     *  the left inlet. Until #711 this object had one of them.
     *
     *  - **``int``** — "any number other than 0 starts the metro object. At
     *    regular intervals, metro sends a bang out the outlet. 0 stops metro."
     *    The one that was already here.
     *  - **``bang``** — "in left inlet: starts the metro object". The same
     *    sentence as ``int``'s, so the same method. What "starts" means for an
     *    object that is *already running* is not on the reference page; Max's
     *    own tutorial is what settles it — "when a metro receives a bang, the
     *    metro will 're-start' itself and begin scheduling subsequent bang
     *    messages from the moment we triggered it", and "the button forces the
     *    metro objects to restart *in sync*" (Max Basic Tutorial 4: Metro and
     *    Toggle). So a bang re-phases a running metro rather than being
     *    ignored, and re-phasing several of them from one button is what the
     *    method is *for*.
     *  - **``float``** — "performs the same function as int". Issue #711 counted
     *    three left-inlet methods and left this one out; the reference page
     *    lists it, so it is here, and Max wins. It is deliberately **not** a
     *    cast to ``int``: ``0.5`` is "a number other than 0" and therefore
     *    *starts* the metro, where ``(int)0.5`` would stop it. ``.delay``
     *    already reads its own left inlet's float as Max's "same function as
     *    int"; this object simply never did.
     *  - **``stop``** — "in left inlet: stops metro", which is ``int 0`` under
     *    another name, exactly as it is on ``.delay``. Left inlet only, so a
     *    ``stop`` arriving on the interval inlet stays what it always was — a
     *    word that is not a time value, and therefore nothing.
     *
     *  ### What a mid-run start or stop does to the beat grid (issue #711)
     *
     *  All four go through the one ``Toggle`` path, which is what keeps them
     *  from having to answer this question four times over.
     *
     *  - **Starting** — bang, or a non-zero ``int``/``float`` — stops first and
     *    then starts, on either engine. On the domain clock that means the
     *    armed wakeup is cancelled and a **new ``beatBase``** is taken at the
     *    beat the message landed on, ``emitted`` goes back to 0, and the next
     *    wakeup is armed at ``beatBase + interval``. The grid is re-phased to
     *    now, which is precisely the tutorial's "from the moment we triggered
     *    it" — and it is why one button banging several metros puts them in
     *    step: they take the same baseline off the same clock in one dispatch.
     *  - **Stopping** — ``stop``, or a zero ``int``/``float`` — clears
     *    ``beatOn`` and cancels the armed wakeup, and emits nothing.
     *    ``beatBase`` and ``emitted`` are left where they stand rather than
     *    zeroed: they mean nothing while stopped, the next start overwrites
     *    both, and the only thing that could read them in between is a wakeup
     *    that lost the race to the cancel — which checks ``beatOn`` and stops.
     *
     *  Neither disturbs the two anti-drift disciplines below, because neither
     *  *edits* a running grid: a start replaces it whole and a stop retires it.
     *  Only an interval change edits one, and that is ``RetimeBeats``.
     *
     *  ### The start bang stays synchronous, and that is safe here
     *
     *  ``.delay`` keeps ``messageScheduler``'s one-block deadline floor partly
     *  because a ``delay 0`` wired outlet-to-inlet would otherwise recurse until
     *  the stack ran out (``gDelay.h``). A ``bang`` method makes that same patch
     *  drawable here with a single cord — this outlet into this inlet — so the
     *  question has to be asked, and the answer is not to defer the start bang.
     *  Max documents it as immediate ("bang is sent immediately when metro is
     *  started"), and a metronome whose first tick arrived a block late would be
     *  a different object. What makes it safe is that ``outlet::Send*`` has
     *  carried a thread-local send-depth ceiling since #236 for exactly this
     *  shape of cycle: the recursion stops at 64 frames, and each nested frame's
     *  ``StopRun`` has already retired the timer or wakeup its caller armed, so
     *  the metro comes out of it holding one of them rather than sixty-four. The
     *  cycle also predates this issue — a ``[t 1]`` in the loop reaches
     *  ``Toggle`` today — so #711 shortens a bounded cycle rather than opening
     *  an unbounded one.
     *
     *  What that reasoning missed, and issue #721 is, is the *other* end of the
     *  same cord. The bang that closes the cycle at run time is not the start
     *  bang on the control thread but ``Bang()`` on the **timer worker, from
     *  inside the timer's own callback**, and the ``StopRun`` it reaches was
     *  therefore asking ``timerThread::ClearTimer`` to retire the id whose
     *  callback was on its own stack. ``destroyImpl`` waits for a completion
     *  only the calling thread can deliver, and it waits holding the bridge
     *  slot's mutex, so the one timer worker in the process was gone for good
     *  and took every other ``.metro`` — and the next thread to touch this one —
     *  with it. The depth ceiling cannot help there: the cycle is one frame deep
     *  and blocks rather than recursing, and a depth counter knows how deep a
     *  send is, not whose callback the frame at the bottom belongs to.
     *
     *  ``Bang()`` therefore marks its frame in a thread-local —
     *  ``patcherImplementation``'s render-frame marker (#690) for the sibling
     *  question, and a mark that travels with the *thread*, so a cycle closed
     *  through a ``.t`` is covered exactly as the single cord is — and a start
     *  or stop that finds its own mark **records** rather than performs.
     *
     *  Recording, and not merely swapping the blocking route for the wait-free
     *  one, is what makes the answer correct rather than only unhung. A restart
     *  from inside the callback needs no work at all: the timer being restarted
     *  is the one that is firing, and the worker reschedules it at ``next +
     *  period`` the instant the callback returns, which is the tutorial's "from
     *  the moment we triggered it" measured from the tick instead of from the
     *  end of everything the tick set off. Publishing "running" would be worse
     *  than redundant — a stop landing from another thread while the cycle
     *  unwound would be overwritten, and the metro the user switched off would
     *  come back. A stop is the one thing that must reach the bridge, since the
     *  worker's own reschedule has to be undone, and it is the safe direction to
     *  publish late: a stop request can only ever stop. ``Bang()`` issues it once
     *  when the send has unwound, at the bounded cost #718 already documents for
     *  a stop that cannot wait — the disarm lands a pool hop later, so one more
     *  tick may come out. An ordinary self-banging tick reaches the bridge
     *  never.
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
     *  its own clock. On the **beat** engine no message or delivery path
     *  allocates, locks or blocks — binding is the bridge's wait-free claim (the
     *  name lookup happens on the background pool, because it takes the clock
     *  manager's mutex), arming and cancelling are wait-free, reading a beat is
     *  two acquire loads, and the grid is guarded by ``.value``'s non-blocking
     *  ``busy`` exchange, whose loser does nothing rather than waiting.
     *
     *  The **millisecond** engine was not clean until #718. Every one of
     *  ``timerThread``'s entry points is forbidden on the audio callback —
     *  ``Add`` takes a mutex and allocates two container nodes plus the caller's
     *  ``std::function``, ``SetPeriod`` takes the same mutex, and
     *  ``ClearTimer`` blocks on a condition variable until an in-flight callback
     *  returns — and every one of them was reached straight from a message
     *  handler, which runs on whichever thread dispatched: a ``.delay`` wired
     *  into this left inlet is enough, since its delivery is the first thing
     *  ``patcherImplementation::Calculate`` does. The fault is as old as the
     *  object; #711 only added three more spellings of a toggle ``int`` has
     *  always had.
     *
     *  The engine is unchanged — a millisecond metro is still a real OS timer,
     *  still ticking while the engine is paused, still at millisecond and not
     *  block resolution. What changed is who calls ``timerThread``:
     *  ``timerBridge`` holds one slot per metro, a start / stop / retime writes
     *  wanted state into it, and the reconcile happens **on the background pool
     *  when the handler is on the audio callback and inline when it is not**.
     *  Which of the two, this object decides for itself from
     *  ``patcherImplementation::CallingThread`` (#690) rather than from the
     *  ``THREAD`` tag, because the tag is dispatch semantics and not thread
     *  identity — and the tag it was handed travels on unaltered. So a toggle
     *  from the control thread keeps exactly the timing and the exact
     *  stop-means-stopped handshake it always had, and one from the audio
     *  callback costs a pool hop before the *second* bang; the first is Max's
     *  immediate one and is never deferred.
     */
    PATCHER_CLASS(gMetro, YSE::OBJ::G_METRO)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(Toggle)
    _BANG_IN(BangIn)
    _FLOAT_IN(ToggleFloat)
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

    // What the timer calls. A free function and a context rather than a
    // `std::function` because building one of those is an allocation, and the
    // thread that asks for a timer may be the audio callback (issue #718).
    static void BangTrampoline(void* ctx);

    // Whether the handler currently running is on the audio callback, which is
    // the question a `THREAD` tag cannot answer — see the header. False for a
    // standalone object, which has no patcher to ask and is never rendered.
    bool OnAudioThread(YSE::THREAD thread) const;

    // Start / stop the millisecond timer, deferring the `timerThread` call to
    // the background pool when this handler is on the audio callback and making
    // it inline when it is not (issue #718).
    void StartMillis(timerThread::millisec ms, YSE::THREAD thread);
    void StopMillis(YSE::THREAD thread);

    // Push the current `period` onto the running timer, if any. No-op when the
    // interval has not moved (issue #625). `thread` picks the mechanism the way
    // StartMillis does.
    void ApplyPeriod(YSE::THREAD thread);

    // `period` clamped to the documented 1+ ms range, in the timer's unit.
    timerThread::millisec Interval() const;

    // Max's `clock <name>` / bare `clock`, through the patcher's bridge (issue
    // #705). Binds wait-free on whichever thread the message arrived on; a name
    // that does not fit, a bridge that is full, or a standalone object all
    // leave the object where it was, silently, since this may be the audio
    // thread.
    void SetClock(const char* name, std::size_t length);

    // Stop whichever engine is running, both of them being idempotent to stop.
    void StopRun(YSE::THREAD thread);

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

    // This object's slot in the process-wide timer bridge (issue #718), or 0
    // when the table was full. Taken in the constructor and given back in the
    // destructor, and never written in between, so no thread synchronisation is
    // needed to read it. The millisecond run's whole state — armed or not, at
    // which period, since which start — lives in that slot rather than here,
    // because the slot has to outlive this object: it is what a reconcile job
    // already on the background pool addresses.
    timerBridge::Handle timerSlot = 0;

    // Whether this run is on the domain clock rather than on the timer. The
    // millisecond run has its bridge slot for the same job; a beat run needs its
    // own flag because a wakeup that finds the metro stopped must not re-arm.
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
