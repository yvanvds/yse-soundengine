
#pragma once

#include "../pObject.h"
#include "clockBridge.h"
#include "messageScheduler.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Max's ``tempo``: "produces metronomic output controllable in beats
     *         per minute, and with specifiable whole-note divisions" — here, a
     *         **counting** metronome on a named domain clock (issue #512).
     *
     *  ### What it is, next to the three objects it sits between
     *
     *  ``.metro`` bangs at an interval, ``.delay`` bangs once after one, and
     *  ``.timepoint`` bangs when a clock arrives somewhere. This object is the
     *  fourth shape: it emits a **number**, at a musical subdivision, and the
     *  number is *where in the cycle the tick is*. That running count is the
     *  whole difference. A bang says "now"; a count says "now, and it is the
     *  third sixteenth", which is what a ``.sel``, a ``.route`` or a ``.coll``
     *  downstream needs to make a pattern rather than a pulse. It is the
     *  rhythmic backbone the issue asks for: one ``.tempo`` and a table is a
     *  step sequencer.
     *
     *  The unit is Max's: **``4 × multiplier ÷ division`` beats**, division
     *  being a fraction of a whole note. The defaults ``1`` and ``16`` are a
     *  sixteenth note — a quarter of a beat — and the count cycles ``0`` to
     *  ``division - 1``, which is Max's "cycles continuously from 0 to
     *  (rhythmic value − 1)".
     *
     *  ### The tempo is the clock's, and that is the point of the object
     *
     *  Issue #512 asks for this to bind a ``CLOCK::domainClock`` "rather than
     *  reimplementing tempo", and the consequence is worth stating plainly:
     *  there is no millisecond engine in here at all. ``.metro`` has two clocks
     *  and picks one at the toggle; ``.tempo`` has one, because a tempo in BPM
     *  and a note value are meaningless without a beat and the domain clock is
     *  the only thing in the process that has one. So:
     *
     *  - **Timing is read off the clock's beat position.** A tempo change, a
     *    ``requestTempo`` ramp, a pause and a negative tempo are all inherited
     *    for free, and two ``.tempo`` objects on one clock — a sixteenth-note
     *    one and a triplet-eighth one — stay in exact relation to each other and
     *    to every clip on that domain. That is what "keeps polytemporal
     *    relationships exact" means.
     *  - **``tempo`` is written *onto* the clock**, not implemented here.
     *    Starting the metronome asserts the object's BPM on its clock, which is
     *    Max's object exactly: a ``tempo`` is a metronome you set the BPM on.
     *    The engine's model is rampable and **never clamped**, so Max's 5–300
     *    range is gone: 0 pauses, and a negative tempo runs the domain
     *    backwards. ``division`` keeps Max's 1–96 and ``multiplier`` its floor
     *    of 1, both being a *shape* of the grid rather than a tempo.
     *
     *  Two hands on one lever is therefore literal here, as it is on
     *  ``.transport``: a ``.tempo main 120`` and a ``.transport main 90`` write
     *  the same clock, and the last write wins. Two ``.tempo`` objects meant as
     *  two subdivisions of one pulse should carry the same BPM — or, better, be
     *  left at the tempo a ``.transport`` sets, since a ``.tempo`` writes only
     *  when it is started or explicitly told.
     *
     *  ### The ownership contract, inherited whole from #513
     *
     *  **This object never creates a clock and never destroys one.**
     *  ``.transport`` is the sole creator, in ``SetParent``, first registration
     *  wins, and teardown belongs to the host. A ``.tempo`` binds a name and
     *  counts on it. A name nothing has claimed never comes due — no ticks, and
     *  no tempo written either, because writing a tempo at a clock that does not
     *  exist is not a tempo — and ``clockBridge::Poll`` picks the binding up
     *  within ``clockBridge::RESOLVE_INTERVAL_BLOCKS`` blocks of a
     *  ``.transport`` or the host finally making it, at which point the run
     *  begins. That is also why a metronome starting must not be able to bring a
     *  clock into being: every object bound to that name would then be sitting
     *  on a clock nobody started.
     *
     *  **Stopping never writes the clock.** This is the one place ``.tempo``
     *  parts company with ``.transport``, and it is deliberate: a
     *  ``.transport``'s stop *is* a tempo 0, because stopping the transport is
     *  what it is for, while a metronome switching off must not stop every clip
     *  on the domain with it. So ``stop`` retires this object's grid and nothing
     *  else, and the clock runs on for whatever else is counting on it.
     *
     *  ### The count is read off the clock, never counted from wakeups
     *
     *  ``.metro``'s anti-drift discipline (#705), and for its reasons.
     *  ``messageScheduler::ScheduleBangOnClock`` arms relative to the beat the
     *  arm was taken at, but a delivery lands at the first *audio block*
     *  boundary at or after that deadline. Re-arming "one unit from now" per
     *  delivery would drop that overshoot on every tick and run the metronome
     *  slow, and once the unit is shorter than a block it would cap at one tick
     *  per block however fast the domain ran.
     *
     *  So a run keeps the beat tick 0 stood on (``beatBase``) and how many ticks
     *  it has emitted (``emitted``); each delivery reads the clock, takes
     *  ``n = floor((beat - beatBase) / unit)``, and emits ``n - emitted``
     *  numbers, arming the next wakeup at the **absolute** grid point
     *  ``beatBase + (n + 1) × unit``. Nothing drifts and nothing accumulates.
     *
     *  A catch-up burst is bounded by ``MAX_CATCHUP``. The *count* is not: a
     *  domain that jumped a hundred units in one block advances the cycle by all
     *  hundred and emits only the last ``MAX_CATCHUP`` of them, because the
     *  count is a position in the bar and a position that skipped its way there
     *  is still the right position. Emitting the burst but landing on the wrong
     *  step would be the worse of the two errors.
     *
     *  ### The edges, and how they answer
     *
     *  - **A clock named before anything creates it.** Nothing is emitted and
     *    the baseline is taken at the first wakeup that finds a clock, so the
     *    run begins when the clock starts existing. ``messageScheduler``'s
     *    ``ResolveBeat`` rule.
     *  - **A clock at tempo 0**, or one destroyed under a bound object (#707,
     *    which leaves a frozen beat), never brings the next unit due, so the
     *    count holds where it stands. That is the honest reading of "every
     *    sixteenth" on a clock that is not moving.
     *  - **A multiplier or division change while running** rebases the grid at
     *    the current beat rather than jumping the phase — #625's rule for
     *    ``.metro``'s millisecond path, carried over — and the count carries on
     *    from where it was rather than restarting, wrapped into the new cycle if
     *    that shrank under it. Both routes that write them are covered: the
     *    inlets re-arm eagerly, and a live ``SetParams`` re-parse — which stores
     *    straight into the field from the audio thread and notifies nobody — is
     *    caught at the next wakeup, which compares the live unit against the
     *    grid's.
     *  - **Bars, beats and units, ``quantize`` and Max's ``transport``
     *    attribute** stay out, as they do on ``.metro``, ``.delay``,
     *    ``.transport`` and ``.timepoint``: all three need a meter, and a
     *    ``domainClock`` is a bare beat accumulator with none.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlets and by
     *  its clock. A patcher message handler runs on **whichever thread
     *  dispatched the message** — in-patcher delivery dispatches ``T_DSP``, and
     *  the drains at the top of ``patcherImplementation::Calculate`` dispatch
     *  ``T_GUI`` *from the audio callback* — so a ``.delay`` wired into the left
     *  inlet puts a start on the audio thread. Nothing on any path there
     *  allocates, locks or blocks: binding and arming are wait-free, reading a
     *  beat is two acquire loads, the command words are matched against the
     *  message in place (a ``substr`` would allocate) and the numbers go through
     *  ``pListArgs.h``'s allocation-free readers.
     *
     *  The one call that would block is the tempo write, and it splits the way
     *  ``.transport``'s does, decided from ``patcherImplementation::CallingThread``
     *  (#690) rather than from the ``THREAD`` tag, because the tag is dispatch
     *  semantics and not thread identity. Off the audio callback
     *  ``CLOCK::Manager().setTempo`` is called by name, inline, which needs no
     *  resolved binding — so a start in the same breath as ``CreateObject``
     *  retempos the clock immediately. On it, ``clockBridge::RequestTempo`` is
     *  three atomic stores the clock consumes on its next block.
     *
     *  The grid is guarded by ``.metro``'s non-blocking ``busy`` exchange, whose
     *  loser does nothing rather than waiting: this object is reachable from the
     *  control thread and from a rendering graph alike, a mutex is out on the
     *  second of those, and there is no single writer to build a seqlock around.
     */
    PATCHER_CLASS(gTempo, YSE::OBJ::G_TEMPO)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(ToggleInt)
    _FLOAT_IN(ToggleFloat)
    _BANG_IN(BangIn)
    _LIST_IN(Command)
    _INT_IN(SetIntTempo)
    _FLOAT_IN(SetFloatTempo)
    _INT_IN(SetIntMultiplier)
    _FLOAT_IN(SetFloatMultiplier)
    _INT_IN(SetIntDivision)
    _FLOAT_IN(SetFloatDivision)

    /** @brief Control thread. Bind the named clock — never create it. See the
     *         ownership contract in the class notes. */
    void SetParent(pObject* parent) override;

    /** @brief Most numbers one delivery emits when a wakeup covered several
     *         units. Past this the run skips ahead — advancing the count over
     *         every unit it skipped — rather than emptying an unbounded burst
     *         onto the audio thread. */
    static constexpr std::int64_t MAX_CATCHUP = 64;

    /** @brief Max's documented ceiling on the rhythmic value: "output division
     *         of whole note (1–96)". A shape of the grid rather than a tempo,
     *         so unlike the BPM it keeps Max's range. */
    static constexpr int MAX_DIVISION = 96;

    /** @brief The clock this metronome counts on, or ``""``. The storage
     *         belongs to the patcher's bridge and never changes, so this is safe
     *         from any thread — and it is the *live* name, so it follows a
     *         ``clock <name>`` where the creation parameter does not. */
    const char* ClockName() const;

    /** @brief Whether a ``clockBridge`` slot was taken for the name. False for
     *         a nameless or unparented object, for a bare ``clock``, and for a
     *         bridge that was full. */
    bool Bound() const {
      return binding.load(std::memory_order_relaxed) != 0;
    }

    /** @brief Whether the metronome is running. A ``.tempo`` is created
     *         stopped, exactly as Max's is and as a ``.metro`` is. */
    bool Running() const {
      return running.load(std::memory_order_relaxed);
    }

    /** @brief The BPM this object asserts on its clock when it is started. Not
     *         a reading of the clock: a host or a ``.transport`` that retempos
     *         the same clock behind its back does not change it. */
    float WantedTempo() const {
      return tempo.load(std::memory_order_relaxed);
    }

    /** @brief Max's beat multiplier, floored at 1. Higher values slow the
     *         output proportionally. */
    int Multiplier() const;

    /** @brief Max's rhythmic value — the whole-note division — clamped to
     *         1–96. Also the length of the output cycle. */
    int Division() const;

    /** @brief The interval between outputs, in beats: Max's
     *         ``4 × multiplier ÷ division``. 1 for a quarter note, 0.25 for the
     *         default sixteenth. */
    double UnitBeats() const;

    /** @brief The number last sent out, which is where in the cycle the run
     *         stands. 0 before the first tick of a run. */
    int Count() const {
      return (int)count.load(std::memory_order_relaxed);
    }

    // The scheduler coming back with a wakeup that has come due.
    void DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) override;

  private:
    /**
     *  @brief Non-blocking exclusive access to the beat grid.
     *
     *  ``Held()`` is false when another thread had it — the caller then does
     *  nothing at all. Never waits, never allocates. ``.metro``'s ``storeGuard``,
     *  for the reason it gives: a message handler may be on the audio callback,
     *  so a mutex is out, and both the control thread and a delivery write this
     *  state, so there is no single writer to build a seqlock around.
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

    // Whether the handler currently running is on the audio callback — the
    // question a `THREAD` tag cannot answer (#690). False for a standalone
    // object, which has no patcher to ask and is never rendered.
    bool OnAudioThread(YSE::THREAD thread) const;

    // Write `bpm` onto the bound clock over `rampSeconds`, by whichever route
    // this thread may use. A no-op with nothing bound: a tempo at a clock that
    // does not exist is not a tempo.
    void PushTempo(float bpm, float rampSeconds, YSE::THREAD thread);

    // Max's start / restart and stop, and the one place either is decided.
    void Start(YSE::THREAD thread);
    void Stop();

    // Max's `clock <name>` / bare `clock`, through the patcher's bridge. Binds
    // wait-free on whichever thread the message arrived on; a name that does not
    // fit, a bridge that is full, or a standalone object all leave the object
    // where it was, silently, since this may be the audio thread.
    void SetClock(const char* name, std::size_t length);

    // Take a fresh baseline on the bound clock and re-arm, keeping the count.
    // A `clock <name>` under a running metronome, which moves the grid to
    // another clock without restarting the cycle.
    void Rebase();

    // A multiplier or division change under a running metronome: rebase the grid
    // at the current beat rather than jumping the phase, and wrap the count into
    // a cycle that may have shrunk.
    void Retime();

    // Arm the next wakeup at the *absolute* next grid point rather than one unit
    // from here, which is half of what keeps the run from drifting; the other
    // half is that the delivery reads its tick count off the clock. Guard held.
    // Falls back to a plain unit while the binding has not resolved, there being
    // no beat yet to measure a grid point from.
    void ArmNext(clockBridge::Handle bound);

    // Drop the pending wakeup, if there is one. Guard held.
    void CancelWakeup();

    // The domain clock's name as the object was created with it, and the only
    // thing SetParent binds. Written once by SetParams before the object is
    // published and never again; `clock <name>` re-binds without touching it, so
    // this stays what the patch file says.
    std::string clockname;

    // The BPM the metronome asserts on its clock when started. Atomic because a
    // live SetParams re-parse writes it from the audio thread.
    aFlt tempo;
    // Max's beat multiplier and rhythmic value. Atomic for `tempo`'s reason.
    aInt multiplier;
    aInt division;

    // The patcher-wide binding for the counted clock, or 0. Taken in SetParent
    // and by `clock <name>`; clockBridge hands the same handle to every object
    // naming the same clock, so this shares a slot with the `.transport` driving
    // it rather than costing one of its own.
    std::atomic<clockBridge::Handle> binding{0};

    // Whether the metronome is running. Outside the guard, so that a wakeup
    // which lost the guard to a stop still finds it false and does not re-arm.
    std::atomic<bool> running{false};

    // Where in the cycle the run stands — the number last emitted. Written only
    // under the guard; atomic so Count() may read it from anywhere.
    std::atomic<std::int64_t> count{0};

    // The beat grid, guarded: written by the inlets on whichever thread
    // dispatched them and by a wakeup on the audio thread.
    std::atomic<bool> busy{false};
    // The beat tick 0 of this run stood on, and whether it has been taken yet —
    // it cannot be while the binding is unresolved, so the first wakeup that
    // finds a clock takes it instead.
    double beatBase = 0.0;
    bool beatBased = false;
    // The unit the current grid is built on. Compared against the live
    // UnitBeats() at every wakeup, which is how the SetParams route reaches a
    // running metronome.
    double gridBeats = 0.0;
    // Ticks emitted since `beatBase`, and the index the next grid point is at.
    std::int64_t emitted = 0;
    // The wakeup this object is waiting on, or 0. One clock per object.
    messageScheduler::Handle pending = 0;
  };
}
}
