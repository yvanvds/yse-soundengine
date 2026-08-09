
#pragma once

#include "../pObject.h"
#include "TimerThread.h"
#include "timerBridge.h"
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Max's ``clocker``: "report elapsed time, at regular intervals"
     *         (issue #505).
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
     *    measured from moves. Max adds that it "is meaningless when the clocker
     *    is not running, since it always resets to 0 anyway when stopped", and
     *    a stopped ``.clocker`` reads 0 here for exactly that reason.
     *  - **``bang``** — "if the clocker object is not running, a bang message
     *    will start the count. If the clocker object is running, a bang message
     *    will reset the count."
     *
     *  The last two are why ``bang`` and ``int 1`` are *not* the same method
     *  here, where on ``.metro`` they are. Max's page gives ``bang`` a
     *  running-object rule of its own and gives ``int`` none, and the rule it
     *  gives is ``reset``'s: a bang into a running clocker moves the baseline
     *  and leaves the tick where it is. A non-zero ``int`` is read as
     *  ``.metro``'s "start", which on a running object is a re-start — a fresh
     *  baseline *and* a re-phased timer — so a toggle box switched off and on
     *  puts the run back on its own grid. Both zero the reported number; only
     *  the int moves the tick.
     *
     *  ### The interval
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
     *  ``clock <name>`` and Max's tempo-relative intervals (a note value, a tick
     *  count) stay **out**, which is why the interval inlet insists on the whole
     *  message being one number rather than reading the number it *starts*
     *  with: Max's tick spelling begins with a digit, so ``1440 ticks`` taken as
     *  a leading number would silently become 1440 **milliseconds** — an
     *  interval nobody asked for, wrong by whatever the tempo is, and looking
     *  like it worked. ``.metro`` documents the same trap (#705) and answers it
     *  by reading the tempo-relative form first; this object, having no clock to
     *  read it against, answers it by doing nothing.
     *
     *  That exclusion is a scope line rather than an oversight:
     *  issue #505 asks for the millisecond object, and a tempo-relative
     *  ``.clocker`` raises a question a millisecond one does not — whether
     *  "elapsed" is then milliseconds or beats — that Max answers with a
     *  transport this patcher does not have. ``.metro``'s #705 route is where
     *  that work would go. Max's ``quantize``, ``autostart``, ``autostarttime``
     *  and ``defer`` attributes stay out with it.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlets and by
     *  its timer. A patcher message handler runs on **whichever thread
     *  dispatched the message** — in-patcher delivery dispatches ``T_DSP``, and
     *  the drains at the top of ``patcherImplementation::Calculate`` dispatch
     *  ``T_GUI`` *from the audio callback* — so a ``.delay`` wired into the left
     *  inlet puts a start on the audio thread. Nothing on any path there
     *  allocates, locks or blocks:
     *
     *  - the timer is armed and disarmed through ``timerBridge`` (#718), whose
     *    ``Request*`` front is a handful of atomic stores and a push onto a
     *    pre-allocated lock-free job, with the locking ``timerThread`` work done
     *    on the background pool. Off the audio callback the ``Apply*`` front
     *    does it inline, keeping the stop-means-stopped handshake. Which of the
     *    two, this object decides from ``patcherImplementation::CallingThread``
     *    (#690) rather than from the ``THREAD`` tag, because the tag is dispatch
     *    semantics and not thread identity;
     *  - the baseline is one 64-bit atomic store and one
     *    ``steady_clock::now()``, which is a ``QueryPerformanceCounter`` on
     *    Windows and a vDSO ``clock_gettime`` on Linux and Android —
     *    ``INTERNAL::time::update`` already reads it from the audio callback
     *    every block (#667);
     *  - the command words are matched against the message in place (a
     *    ``substr`` would allocate) and numbers go through ``pListArgs.h``'s
     *    allocation-free readers.
     *
     *  A run needs no grid guard, unlike ``.metro``'s and ``.tempo``'s: the
     *  whole of this object's run state is two atomics that are written
     *  independently, so there is no multi-field invariant for two threads to
     *  tear.
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

    /** @brief What the timer calls: read the clock, send the elapsed
     *         milliseconds. Public because the trampoline is, and because a
     *         test may want one tick without waiting for one. */
    void Tick();

    /** @brief Whether the clocker is running. Created stopped, as Max's is. */
    bool Running() const {
      return running.load(std::memory_order_relaxed);
    }

    /** @brief The reporting interval in milliseconds, floored at the 1 ms the
     *         timer can actually keep. */
    timerThread::millisec IntervalMs() const;

    /** @brief The number the next tick would send: milliseconds since the run
     *         started or since the last ``reset``. **0 while stopped** — Max's
     *         "it always resets to 0 anyway when stopped". */
    std::int64_t Elapsed() const;

  private:
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

    // Max's start / re-start (a fresh baseline and a re-phased tick), his stop,
    // and his reset (a fresh baseline, the tick left alone).
    void Start(YSE::THREAD thread);
    void Stop(YSE::THREAD thread);
    void Reset();

    // Push the current interval onto a running timer without re-phasing it
    // (issue #625). No-op when the interval has not moved — `timerBridge` drops
    // a request that does not, which is what lets a tick re-assert it for free.
    void ApplyInterval(YSE::THREAD thread);

    // Max's `interval` attribute and the creation argument, in milliseconds.
    // Atomic because a live SetParams re-parse writes it from the audio thread.
    aInt interval;

    // Whether the run is on. The timer's own armed state is the bridge slot's,
    // but a tick that races a stop needs a flag of its own to find, and
    // `Elapsed()` needs one to answer 0 from.
    std::atomic<bool> running{false};

    // The steady_clock instant, in nanoseconds, the elapsed time is measured
    // from: the start, or the last `reset` / `bang` under a running clocker.
    // Meaningless while stopped, which is why `Elapsed()` checks `running`
    // first rather than trusting it.
    std::atomic<std::int64_t> baseNs{0};

    // This object's slot in the process-wide timer bridge (issue #718), or 0
    // when the table was full. Taken in the constructor and given back in the
    // destructor, and never written in between, so no thread synchronisation is
    // needed to read it. The run's timer state — armed or not, at which period
    // — lives in that slot rather than here, because the slot has to outlive
    // this object: it is what a reconcile job already on the background pool
    // addresses.
    timerBridge::Handle timerSlot = 0;
  };
}
}
