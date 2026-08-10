
#pragma once

#include "../pObject.h"
#include "clockBridge.h"
#include <atomic>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Max's ``setclock``: "create and control an alternative clock for
     *         other timing objects" — here, a **named** domain clock that is
     *         turning from the moment the patch loads (issue #515).
     *
     *  ### The clock-surface's three objects, and which one this is
     *
     *  The patcher now has three ways to touch a domain clock, and they differ
     *  by exactly one thing each:
     *
     *  - ``.when`` (#514) **only looks**. It binds a name and reads it; it
     *    creates nothing.
     *  - ``.transport`` (#513) **drives** a clock: it creates the name it is
     *    given *stopped*, at tempo 0, and its int and float are ``start`` and
     *    ``stop``. It is a transport, so nothing plays until something starts
     *    it.
     *  - ``.setclock`` **is a clock**. It creates the name it is given
     *    **running**, at its ``tempo`` argument, and its int and float are the
     *    *speed*. That is the whole difference, and it is Max's difference: a
     *    ``setclock`` is a time source other timing objects reference, not a
     *    play button. ``.setclock fast 240`` is a second tempo domain that
     *    exists and is turning as soon as the patch does, which is what makes
     *    the polytemporal case (``.metro clock fast`` next to ``.metro clock
     *    main``) a single object rather than an object plus a loadbang.
     *
     *  The inlet is the other half of that. A ``.metro`` or a slider into a
     *  ``.transport`` toggles the clock on and off every value it sends; into a
     *  ``.setclock`` the same wire is a tempo control, which is the thing you
     *  actually want to sweep. ``.when`` made the same complaint about
     *  ``.transport``'s inlet and resolved it the other way, towards *query*;
     *  this object resolves it towards *speed*. Nothing here starts or stops:
     *  tempo 0 is what "stopped" means to a domain clock, so sending 0 stops it
     *  and there is no second piece of state remembering a tempo to come back
     *  to.
     *
     *  ### Ownership, and the lifetime rule #515 asked for
     *
     *  **Who creates the clock.** This object does, once, in ``SetParent`` —
     *  the control thread, at construction and at every live-``SetParams``
     *  rebuild — with ``tempo`` as the clock's initial tempo. Creation is
     *  *first registration wins*: a clock the host, a ``.transport`` or another
     *  ``.setclock`` already made is left **exactly as it is**, not re-tempoed
     *  to this object's argument. ``CreatedClock()`` reports which of the two
     *  happened. Creation is the one call here that needs
     *  ``CLOCK::Manager()``'s mutex, and ``SetParent`` is the one hook in a
     *  ``pObject``'s life that is guaranteed to be the control thread.
     *
     *  **Who destroys it: nobody here.** Max deletes a ``setclock``'s clock
     *  with the object, and #515 explicitly left the choice open. It is the
     *  wrong choice in this engine, for two mechanical reasons rather than a
     *  preference:
     *
     *  - **Bindings are never released.** A ``clockBridge`` binding owns a
     *    *share* of its clock's lifetime and is held forever by design (#707).
     *    So a ``destroyClock`` here would not give a ``.metro clock fast`` its
     *    clock back — it would freeze that metro on a clock that has stopped
     *    advancing, permanently, because ``Poll`` only ever retries bindings
     *    that never resolved. "Reverts to the default clock", Max's outcome, is
     *    not reachable from here.
     *  - **A live ``SetParams`` rebuilds rather than mutates.**
     *    ``ReplaceObjectUnlocked`` constructs the replacement and runs its
     *    ``SetParent`` *before* the original is handed to the reclaimer, so the
     *    replacement's ``createClock`` loses to the original that still holds
     *    the name, and the original's destructor would then destroy the clock
     *    the replacement is now pointing at. Editing a ``.setclock``'s tempo
     *    would take its clock down with it.
     *
     *  So the clock outlives the object, and teardown belongs to the host —
     *  ``system::destroyClock`` or ``System::close`` — which is
     *  ``.transport``'s rule for the same underlying reason. What the object
     *  leaves behind is a clock still turning at the tempo it was last given.
     *
     *  **The two cases with nothing to control** answer by doing nothing at
     *  all, silently: a ``.setclock`` with no name creates nothing, binds
     *  nothing, ignores every command and emits nothing on bang; a standalone
     *  object (no patcher) creates and binds nothing either, ``SetParent``
     *  being where both happen, though its commands still go out by name so one
     *  built beside a clock the host made does reach it.
     *
     *  ### What Max has that a domain clock cannot have
     *
     *  - **The modes** — ``pass``, ``add``, ``mul``, ``interp`` — and the
     *    ``int``/``float``-sets-the-time behaviour they select between. All
     *    four are ways of deriving one *millisecond* time from another, and a
     *    ``CLOCK::domainClock`` is a bare beat accumulator whose position is
     *    the running integral of its tempo: there is no origin, no settable
     *    position and nothing to derive from. This is the same fact that costs
     *    ``.transport`` its ``seek`` and ``.when`` its bars.beats.units. What
     *    survives of "how fast does this clock run" is the tempo, which is what
     *    the inlet sets.
     *  - **The reporting interval** (Max's right inlet, ``set`` and ``reset``).
     *    The engine advances every domain clock once per audio block from
     *    ``CLOCK::Manager().update``; nothing polls one, so there is no
     *    interval to set. Hence the single inlet.
     *  - **``clock <name>``.** ``.metro``, ``.timepoint`` and ``.tempo`` take
     *    one because they are clients choosing a clock. This object *is* the
     *    clock, and its identity is its creation argument — ``.transport``'s
     *    reading, and what keeps ``createClock`` on the control thread.
     *
     *  What it gains instead is ``ramp``: YSE's clocks glide between tempi, so
     *  ``tempo 140 2`` reaches 140 over two seconds. Max's setclock cannot
     *  accelerando.
     *
     *  ### One outlet, and it is Max's
     *
     *  ``bang`` sends the clock's **beat position** out the single outlet, as
     *  Max's ``bang`` outputs its clock's current time. The tempo is not
     *  reported beside it — unlike ``.transport`` and ``.when``, which report
     *  the pair — because on this object the tempo is the thing you just set;
     *  a patch that wants to read one back has ``.when``. With no clock to
     *  read, **nothing at all is emitted**, not a zero: beat 0 is a perfectly
     *  ordinary position for a real clock to be at, so a zero would be
     *  indistinguishable from an answer.
     *
     *  ### Two routes to the clock, and why there are two
     *
     *  ``.transport``'s and ``.when``'s, unchanged. A patcher message handler
     *  runs on **whichever thread dispatched the message** — in-patcher
     *  delivery dispatches ``T_DSP``, and the drains at the top of
     *  ``patcherImplementation::Calculate`` dispatch ``T_GUI`` *from the audio
     *  callback* — so a ``.metro`` wired into this inlet writes a tempo from
     *  the audio thread, where ``CLOCK::Manager()``'s mutex is out. The thread
     *  is asked of ``patcherImplementation::CallingThread`` (#690) rather than
     *  read off the ``THREAD`` tag, because the tag is dispatch semantics and
     *  not thread identity:
     *
     *  - **Off the audio callback**, the manager answers by name. It needs no
     *    resolved binding, so a tempo sent in the same breath as
     *    ``CreateObject`` lands immediately.
     *  - **On it**, the binding: ``RequestTempo`` is three atomic stores the
     *    clock consumes on its next block, and a read is two acquire loads.
     *    Wait-free, no allocation, no lock. An unresolved binding drops the
     *    command and reports nothing; ``Poll`` has it resolved within
     *    ``clockBridge::RESOLVE_INTERVAL_BLOCKS`` blocks of the object being
     *    created, after which every message lands.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven entirely by its
     *  inlet. No handler allocates, locks or blocks when it turns out to be
     *  running on the audio callback — the ``tempo`` word is matched against
     *  the message in place (a ``substr`` would allocate) and its numbers read
     *  by ``pListArgs.h``'s allocation-free readers, and the clock is reached
     *  through the bridge. The blocking by-name route is taken only when the
     *  object has established it is *not* on the callback.
     */
    PATCHER_CLASS(gSetClock, YSE::OBJ::G_SETCLOCK)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(Report)
    _INT_IN(SetIntTempo)
    _FLOAT_IN(SetFloatTempo)
    _LIST_IN(Command)

    /** @brief Control thread. Create the named clock running at ``tempo`` if
     *         nobody has claimed the name, and bind it. See the ownership
     *         contract in the class notes. */
    void SetParent(pObject* parent) override;

    /** @brief The clock this object is. The storage is the object's own
     *         creation argument and never changes; ``""`` means it is nothing.
     */
    const char* ClockName() const {
      return clockname.c_str();
    }

    /** @brief Whether a ``clockBridge`` slot was taken for the name. False for
     *         a nameless or unparented object, and for a bridge that was full.
     */
    bool Bound() const {
      return binding.load(std::memory_order_relaxed) != 0;
    }

    /** @brief Whether this object is the one that brought the clock into
     *         being. False when the name was already claimed — creation is
     *         first-registration-wins and leaves such a clock untouched — and
     *         false for a nameless or unparented object. */
    bool CreatedClock() const {
      return created.load(std::memory_order_relaxed);
    }

    /** @brief The BPM the clock was last told to run at. */
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

    // Drive the clock to `bpm` over `seconds`, by whichever route this thread
    // may use. The object's one write; see the class notes.
    void Push(float bpm, float seconds, YSE::THREAD thread);

    // Set the running speed and push it. Every number this object receives,
    // from either the inlet or the `tempo` message, ends here.
    void Retempo(float bpm, YSE::THREAD thread);

    // The clock's beat position, by whichever route this thread may use. False
    // when there is no clock to read, in which case nothing is emitted.
    bool ReadBeat(YSE::THREAD thread, double& beat) const;

    // The domain clock's name, and this object's identity. Written once by
    // SetParams before the object is published and never again, so a handler on
    // any thread may read it — but only the non-audio route ever does.
    std::string clockname;

    // The BPM the clock runs at, and the glide every change is made over.
    // Atomic because a live SetParams re-parse writes them from the audio
    // thread.
    aFlt tempo;
    aFlt ramp;

    // Whether SetParent's createClock won the name. See CreatedClock().
    std::atomic<bool> created{false};

    // The patcher-wide binding for `clockname`, or 0. Taken in SetParent and
    // never released — clockBridge hands the same handle to every object naming
    // the same clock, so this shares a slot with every `.metro clock <name>`
    // running on it rather than costing one of its own.
    std::atomic<clockBridge::Handle> binding{0};
  };
}
}
