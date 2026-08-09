
#pragma once

#include "../pObject.h"
#include "clockBridge.h"
#include "messageScheduler.h"
#include <atomic>
#include <cstddef>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Max's ``timepoint``: "output a bang when a transport reaches a
     *         specified time" — here, when a **named** domain clock reaches a
     *         beat position (issue #507).
     *
     *  ### What it is for, and what it is not
     *
     *  ``.delay`` and ``.metro`` measure time *from now*: a wait, and a wait
     *  repeated. This object is the third kind, and the patcher had none of it —
     *  a fixed point on a musical timeline that a patch can hang an event on.
     *  "At beat 128, change the patch" is arranged, score-like behaviour, and it
     *  is the shape a generative patch needs to have a form rather than only a
     *  texture.
     *
     *  It is therefore an **absolute** position and not an interval, which is the
     *  one thing to keep hold of when reading the rest of this. A ``.delay 4n``
     *  fires four beats after the bang that started it, wherever the clock
     *  happened to be; a ``.timepoint main 4`` fires when clock ``main`` reads 4,
     *  and if the clock is already past 4 there is no later moment at which that
     *  becomes true again.
     *
     *  ### The clock, and the ownership contract it inherits (issue #513)
     *
     *  The position is measured on a ``CLOCK::domainClock`` addressed by name —
     *  the same clocks ``YSE::system::createClock`` / ``beatPosition`` address
     *  from the host, the same ones a clip transport plays on, and the same ones
     *  ``.transport``, ``.metro``, ``.delay``, ``.qlist`` and ``.seq`` reach
     *  through the patcher's ``clockBridge`` (#688). The name is the creation
     *  argument, and ``clock <name>`` re-points it the way it re-points a
     *  ``.metro``.
     *
     *  **This object never creates a clock and never destroys one.** #513 settled
     *  that: ``.transport`` is the sole creator, in ``SetParent``, first
     *  registration wins, and teardown belongs to the host. A ``.timepoint`` is a
     *  *reader* — it binds a name and watches. Creating a clock here would mean a
     *  patch could bring a stopped clock into being by asking a question about it,
     *  and every object bound to that name would then be sitting on a clock
     *  nobody started. So a name nothing has claimed simply never comes due, which
     *  is ``clockBridge``'s own answer for a clock that does not exist and
     *  ``messageScheduler``'s for a wait on one. A later ``.transport main`` or a
     *  host-side ``createClock("main", …)`` is picked up by the bridge's ``Poll``
     *  within ``clockBridge::RESOLVE_INTERVAL_BLOCKS`` blocks, and the object
     *  starts watching the moment its clock starts existing.
     *
     *  ### Reaching a time is a crossing, not a position
     *
     *  This is the decision the object turns on, and the reason it is a decision
     *  is that a domain clock has no origin: its beat is the running integral of
     *  its tempo, there is no ``seek``, and #513 spelled out why there cannot be
     *  one. So "reaches beat 128" can only be read one of two ways, and they part
     *  company for a clock that is *already* past 128 when the object starts
     *  watching.
     *
     *  A ``.timepoint`` reads it as a **crossing**: it arms only while the clock
     *  is strictly before the target, and bangs when the clock arrives there. A
     *  target already in the past when the object is armed is **spent** — the
     *  moment happened before anyone was watching, and it is not going to happen
     *  again. Nothing is emitted and no wakeup is left behind.
     *
     *  The alternative — bang at once because the condition already holds — is
     *  what makes the object useless in the case it exists for. A patch loaded, or
     *  an object dropped in, while the clock stands at bar 40 would fire its whole
     *  score in one block, every "at bar 33, change the patch" landing at once.
     *  Max's ``timepoint`` reads it as a crossing too, and it has a rewind to
     *  recover with; this object does not, so the reading matters more here rather
     *  than less.
     *
     *  What re-arms a spent object is the same short list that arms a fresh one:
     *  a new time value, ``active 1``, or a new clock. What does *not* is the
     *  clock running backwards under it — a domain clock accepts a negative tempo,
     *  so beat 128 can come round a second time, but noticing that would mean
     *  polling every block for a condition (``beat < target``) the scheduler
     *  cannot arm on, and burning a wakeup per block forever on a case a patch has
     *  to go out of its way to produce is the wrong trade. A patch that really
     *  does rewind re-arms its timepoints the way it rewound them: by saying so.
     *
     *  ### How the wait is armed, and why there are two arms
     *
     *  The wait rides ``messageScheduler::ScheduleBangOnClock``, which takes a
     *  distance in beats and turns it into an absolute deadline on the bound
     *  clock. That is exactly right for this object — the deadline the scheduler
     *  stores *is* the target — and it means a tempo change, a ``requestTempo``
     *  ramp or a pause is inherited for free: the target arrives sooner, later,
     *  smoothly, or never, and two objects on the same clock stay in step with
     *  each other and with every clip on that domain.
     *
     *  Converting the target into a distance needs the clock's current beat, and
     *  that is not available at the moment the object joins its patcher: binding
     *  is wait-free but *resolution* happens on the background pool, because the
     *  name lookup takes the clock manager's mutex. So the arm comes in two steps:
     *
     *  - **The baseline probe.** With the binding unresolved, a wakeup is armed at
     *    0 beats, which ``messageScheduler`` stores relative to
     *    ``clockBridge::ResolveBeat`` and delivers at the first block after the
     *    clock appears. That delivery is the object's first sight of the clock; it
     *    either goes spent or arms the real deadline. Its baseline is
     *    ``ResolveBeat`` — where the clock stood the moment it appeared — and not
     *    the beat the probe came back on, because ``clockBridge::Poll`` retries an
     *    unresolved binding only every ``RESOLVE_INTERVAL_BLOCKS`` blocks and a
     *    target inside that window would otherwise be swallowed. So the resolve
     *    latency costs a late bang rather than a lost one. A clock that never
     *    appears never delivers the probe at all, which is the honest reading of
     *    "at beat 128 on a clock that does not exist".
     *  - **The deadline.** With a beat in hand, one wakeup at ``target - beat``.
     *    Its delivery re-reads the clock and bangs. If the double arithmetic
     *    behind ``now + (target - now)`` lands the deadline a hair before the
     *    target, the delivery finds ``beat < target`` and re-arms the remainder
     *    rather than banging early — a block's slip in the worst case, and never a
     *    bang at the wrong beat.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlet and by its
     *  clock. Nothing on any path allocates, locks or blocks, which it may not —
     *  a patcher message handler runs on whichever thread dispatched the message,
     *  and the drains at the top of ``patcherImplementation::Calculate`` dispatch
     *  *from the audio callback*, so a ``.delay`` wired into this inlet puts a
     *  ``clock main`` on the audio thread. Binding is the bridge's wait-free
     *  claim, arming and cancelling are wait-free, reading a beat is two acquire
     *  loads, the command words are matched against the message in place (a
     *  ``substr`` would allocate) and the numbers are read by ``pListArgs.h``'s
     *  allocation-free readers. Unlike ``.transport``, this object needs no
     *  by-name route at all: it never calls ``CLOCK::Manager()``, because the one
     *  thing that needs the manager's mutex is creating a clock and this object
     *  does not create one.
     *
     *  The wakeup state is guarded by ``.metro``'s non-blocking ``busy`` exchange,
     *  whose loser does nothing rather than waiting: this object is reachable from
     *  the control thread and from a rendering graph alike, a mutex is out on the
     *  second of those, and there is no single writer to build a seqlock around.
     */
    PATCHER_CLASS(gTimepoint, YSE::OBJ::G_TIMEPOINT)
    _NO_MESSAGES
    _NO_CALCULATE

    _FLOAT_IN(SetFloatTime)
    _INT_IN(SetIntTime)
    _LIST_IN(Command)

    /** @brief Control thread. Bind the named clock — never create it — and take
     *         the first arm. See the ownership contract in the class notes. */
    void SetParent(pObject* parent) override;

    /** @brief The clock this object watches, or ``""``. The storage belongs to
     *         the patcher's bridge and never changes, so this is safe from any
     *         thread — and it is the *live* name, so it follows a
     *         ``clock <name>`` where the creation parameter does not. */
    const char* ClockName() const;

    /** @brief Whether a ``clockBridge`` slot was taken for the name. False for a
     *         nameless or unparented object, for a bare ``clock``, and for a
     *         bridge that was full. */
    bool Bound() const {
      return binding.load(std::memory_order_relaxed) != 0;
    }

    /** @brief The beat position the object bangs at. */
    double Target() const;

    /** @brief Whether the object is watching at all — Max's ``active``
     *         attribute. An inactive timepoint arms nothing and bangs never. */
    bool IsActive() const {
      return active.load(std::memory_order_relaxed) != 0;
    }

    /**
     *  @brief Whether a wakeup is outstanding: the object is watching a clock for
     *         a target it has not reached yet.
     *
     *  False for a fresh object with no clock, for an inactive one, and — the
     *  case worth naming — for a **spent** one, which is what a timepoint becomes
     *  the moment it bangs and what it is born as when its target is already in
     *  the past. See the class notes on why that is a crossing and not a state.
     */
    bool Armed() const {
      return armed.load(std::memory_order_relaxed);
    }

    // The scheduler coming back with a wakeup that has come due.
    void DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) override;

  private:
    /**
     *  @brief Non-blocking exclusive access to the wakeup state.
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

    // Max's `clock <name>` / bare `clock`, through the patcher's bridge. Binds
    // wait-free on whichever thread the message arrived on; a name that does not
    // fit, a bridge that is full, or a standalone object all leave the object
    // where it was, silently, since this may be the audio thread.
    void SetClock(const char* name, std::size_t length);

    // Start watching from scratch: drop whatever is armed and take a new arm at
    // the current target. Takes the guard; a caller that loses it does nothing,
    // the thread holding it arming whatever it decides on.
    void Rearm();

    // Rearm's body, guard held.
    void RearmLocked();

    // Drop the pending wakeup, if there is one. Guard held.
    void CancelWakeup();

    // Arm one wakeup `ahead` beats out on the bound clock. Guard held.
    void ArmWakeup(clockBridge::Handle bound, double ahead);

    // The domain clock's name as the object was created with it, and the only
    // thing SetParent binds. Written once by SetParams before the object is
    // published and never again; `clock <name>` re-binds without touching it, so
    // this stays what the patch file says.
    std::string clockname;

    // The beat position to bang at. Atomic because the inlet writes it on
    // whichever thread dispatched and a delivery reads it on the audio thread.
    aFlt time;

    // Max's `active` attribute: 0 disarms the object entirely. Atomic for
    // `time`'s reason.
    aInt active;

    // The patcher-wide binding for the watched clock, or 0. Taken in SetParent
    // and by `clock <name>`; clockBridge hands the same handle to every object
    // naming the same clock, so this shares a slot with the `.transport` driving
    // it rather than costing one of its own.
    std::atomic<clockBridge::Handle> binding{0};

    // Whether a wakeup is outstanding and wanted. Written only under the guard,
    // read from anywhere — a delivery that finds it false was cancelled by a
    // thread that could not take the guard, and drops rather than banging.
    std::atomic<bool> armed{false};

    // The wakeup state, guarded: written by the inlet on whichever thread
    // dispatched it and by a delivery on the audio thread.
    std::atomic<bool> busy{false};
    // The wakeup this object is waiting on, or 0. One clock per object.
    messageScheduler::Handle pending = 0;
    // Whether this arm has seen the clock yet. False for the baseline probe an
    // unresolved binding gets, true once a real beat has been read — which is
    // what tells a delivery whether it is the object's first sight of the clock
    // (take the baseline, or go spent) or the deadline coming due (bang).
    bool based = false;
  };
}
}
