
#pragma once

#include "../pObject.h"
#include "clockBridge.h"
#include <atomic>
#include <cstddef>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Max's ``transport``: start, stop, retempo and read back a
     *         **named** musical clock, from inside the patcher (issue #513).
     *
     *  ### It controls an engine clock; it does not implement one
     *
     *  Everything this object does lands on the ``CLOCK::domainClock`` of the
     *  name it was created with — the same clocks ``YSE::system::createClock`` /
     *  ``setTempo`` / ``beatPosition`` / ``currentTempo`` address, and the same
     *  ones a clip transport plays on and ``.qlist`` / ``.seq`` / ``.delay`` /
     *  ``.metro`` count their beats on through ``clockBridge`` (#688). There is
     *  no tempo, no beat accumulator and no schedule here: a ``.transport`` is a
     *  remote control, and its whole state is *which* clock and *what it wants
     *  that clock to be doing*.
     *
     *  That is the point of the object. A patch and its host share clocks by
     *  name, so `.transport main` in the patch and ``system::setTempo("main",
     *  …)`` in the host are two hands on one lever, and every object already
     *  bound to ``main`` follows both without knowing either exists.
     *
     *  ### The ownership contract (the design gate of issue #513)
     *
     *  **Who creates the clock.** This object does, once, in ``SetParent`` — the
     *  control thread, at construction, and again at every live-``SetParams``
     *  rebuild. ``CLOCK::Manager().createClock`` is *first registration wins*,
     *  so a clock the host already made is left exactly as it is; only a name
     *  nobody has claimed produces a new clock. Creation is the one thing in
     *  this object that needs the manager's mutex, and ``SetParent`` is the one
     *  place in a ``pObject``'s life that is guaranteed to be the control
     *  thread — the same hook ``.s`` anchors its bus address in and the one
     *  ``pObject::EnableFileIO`` documents for exactly this kind of work.
     *
     *  **A clock this object creates is created stopped** — tempo 0, which is
     *  how the whole engine already spells "paused" (``.qlist`` and ``.seq``
     *  hold on a tempo-0 domain). So dropping a ``.transport`` into a patch
     *  never starts anything; ``start`` does, at the object's own ``tempo``.
     *  That is also Max's reading, where a transport is stopped until started.
     *
     *  **Who destroys it: nobody here.** A ``.transport`` never calls
     *  ``destroyClock``, not on delete and not on a rename. A remote control
     *  that unplugged the studio clock when it was thrown away would take every
     *  other object bound to that name with it, and the patcher has no way to
     *  know whether the host, a clip or another patch is still on it. Teardown
     *  is the host's: ``system::destroyClock`` or ``System::close``.
     *
     *  **What happens when the named clock does not exist.** For a parented
     *  ``.transport`` with a name, it always does — it made it. The two cases
     *  that remain answer by doing nothing at all, silently:
     *
     *  - **No name** (``.transport`` with no argument). Nothing is created,
     *    nothing is bound, every command is a no-op and ``bang`` emits nothing.
     *    An object that reported beat 0 for a clock it does not have would be
     *    lying to whatever it is wired into.
     *  - **No patcher** (a standalone object, as the unit tests build them).
     *    Nothing is created and nothing is bound, ``SetParent`` being where
     *    both of those happen. Its commands still go out by name, so one built
     *    beside a clock the host made does reach it; what it will never do is
     *    bring a clock into being, which is the half of the contract that
     *    matters.
     *
     *  **What happens on ``System::close``.** ``CLOCK::Manager().clear()`` drops
     *  every clock in the process. This object holds no clock pointer of its
     *  own, so there is nothing here to dangle; what it holds is a
     *  ``clockBridge`` binding, and #707's rule applies unchanged — the binding
     *  keeps its (now frozen, no longer advancing) clock alive and goes on
     *  reading it rather than reading freed memory. It does **not** re-resolve
     *  onto a same-named clock created after the re-init, because ``Poll`` only
     *  retries bindings that never resolved; that is clockBridge's documented
     *  consequence and this object inherits it rather than working around it.
     *  A patcher rebuilt after the re-init (or a single object re-created) runs
     *  ``SetParent`` again and gets a fresh clock and a fresh binding, which is
     *  the supported way across a close.
     *
     *  ### Two routes to the clock, and why there are two
     *
     *  A patcher message handler runs on **whichever thread dispatched the
     *  message** — in-patcher delivery dispatches ``T_DSP``, and the drains at
     *  the top of ``patcherImplementation::Calculate`` dispatch ``T_GUI`` *from
     *  the audio callback* — so a ``.delay`` wired into this left inlet puts a
     *  ``start`` on the audio thread. ``CLOCK::Manager()``'s by-name calls all
     *  take its mutex and are therefore out there. ``.metro``'s answer (#718) is
     *  this object's answer, decided the same way, from
     *  ``patcherImplementation::CallingThread`` (#690) rather than from the
     *  ``THREAD`` tag, because the tag is dispatch semantics and not thread
     *  identity:
     *
     *  - **Off the audio callback** the manager is called by name, inline. It
     *    needs no resolved binding, so a ``start`` sent in the same breath as
     *    ``CreateObject`` — before the background pool has resolved anything —
     *    takes effect immediately, which is what a host driving a patch from its
     *    own thread expects.
     *  - **On it**, the bound clock is written directly through
     *    ``clockBridge::RequestTempo``: three atomic stores the clock consumes
     *    on its next block. Wait-free, no allocation, no lock. The one cost is
     *    that it needs the binding to have *resolved*; an unresolved binding
     *    drops the command, and the bridge's own ``Poll`` has it resolved within
     *    ``clockBridge::RESOLVE_INTERVAL_BLOCKS`` blocks of the object being
     *    created, after which every command lands.
     *
     *  Reading splits the same way — the manager by name off the callback, the
     *  binding's two acquire loads on it — so ``bang`` answers on either thread.
     *
     *  ### What is deliberately not here
     *
     *  - **No ``seek``, no ``bars.beats.units``, no meter.** A ``domainClock``
     *    is a bare beat accumulator: its position is the running integral of its
     *    tempo, with no origin to seek to and no bar structure to name. Giving
     *    this object a rewind would mean giving the clock one, which is a change
     *    to the engine's time model and not to a patcher object. ``stop`` is
     *    therefore a **pause** — the beat holds where it stands and ``start``
     *    carries on from there.
     *  - **No ``clock <name>`` message.** Max routes ``setclock`` at ``metro``,
     *    ``line`` and ``pipe``; a ``transport``'s identity *is* its name, given
     *    as a creation argument. Re-pointing it at another clock is a re-create,
     *    which is also what keeps creation on the control thread.
     *  - **No ``destroyClock``** — see the ownership contract above.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: this object is driven entirely by its
     *  inlets. No message handler allocates, locks or blocks when it turns out
     *  to be running on the audio callback — the command words are matched
     *  against the message in place (a ``substr`` would allocate), the numbers
     *  are read by ``pListArgs.h``'s allocation-free readers, and the clock is
     *  reached through the bridge's wait-free pair. The blocking by-name route
     *  is taken only when the object has established it is *not* on the
     *  callback.
     */
    PATCHER_CLASS(gTransport, YSE::OBJ::G_TRANSPORT)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(ToggleInt)
    _FLOAT_IN(ToggleFloat)
    _BANG_IN(Report)
    _LIST_IN(Command)
    _INT_IN(SetIntTempo)
    _FLOAT_IN(SetFloatTempo)

    /** @brief Control thread. Create the named clock if nobody has, and bind
     *         it. See the ownership contract in the class notes. */
    void SetParent(pObject* parent) override;

    /** @brief The clock this transport controls, or ``""``. The storage is the
     *         object's own creation argument and never changes. */
    const char* ClockName() const {
      return clockname.c_str();
    }

    /** @brief Whether a ``clockBridge`` slot was taken for the name. False for
     *         a nameless or unparented object, and for a bridge that was full.
     */
    bool Bound() const {
      return binding.load(std::memory_order_relaxed) != 0;
    }

    /** @brief Whether the transport has been started. This is the *transport's*
     *         wanted state, not a query of the clock: a host that retempos the
     *         same clock behind its back does not change it. */
    bool Running() const {
      return running.load(std::memory_order_relaxed);
    }

    /** @brief The BPM ``start`` runs the clock at. */
    float WantedTempo() const {
      return tempo.load(std::memory_order_relaxed);
    }

    /** @brief The glide, in seconds, every tempo write is made over. */
    float WantedRamp() const {
      return ramp.load(std::memory_order_relaxed);
    }

  private:
    // Whether the handler currently running is on the audio callback — the
    // question a `THREAD` tag cannot answer (#690). False for a standalone
    // object, which has no patcher to ask and is never rendered.
    bool OnAudioThread(YSE::THREAD thread) const;

    // Max's start / stop, and the one place either is decided. Both are a tempo
    // write and nothing else: the clock has no run flag, tempo 0 *is* stopped.
    void Run(bool on, YSE::THREAD thread);

    // Drive the clock to what this transport currently wants — `tempo` over
    // `ramp` while running, an instant 0 while stopped. Picks its route from
    // `thread`; see the class notes.
    void Push(YSE::THREAD thread);

    // The clock's beat position and tempo, by whichever route this thread may
    // use. False when there is no clock to read, in which case nothing is
    // written and `bang` emits nothing.
    bool ReadClock(YSE::THREAD thread, double& beat, float& bpm) const;

    // The domain clock's name, and this object's identity. Written once by
    // SetParams before the object is published and never again, so a handler on
    // any thread may read it — but only the non-audio route ever does.
    std::string clockname;

    // The BPM `start` asks for, and the glide it is asked over. Atomic because
    // a live SetParams re-parse writes them from the audio thread.
    aFlt tempo;
    aFlt ramp;

    // Started or stopped. The transport's own wanted state; see Running().
    std::atomic<bool> running{false};

    // The patcher-wide binding for `clockname`, or 0. Taken in SetParent and
    // never released — clockBridge hands the same handle to every object naming
    // the same clock, so this shares a slot with any `.metro clock <name>` in
    // the same patcher rather than costing one of its own.
    std::atomic<clockBridge::Handle> binding{0};
  };
}
}
