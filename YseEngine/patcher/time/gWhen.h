
#pragma once

#include "../pObject.h"
#include "clockBridge.h"
#include <atomic>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Max's ``when``: "report the current transport time" — here, the
     *         beat position and tempo of a **named** domain clock, on demand
     *         (issue #514).
     *
     *  ### The read half of ``.transport``, and why that is its own object
     *
     *  ``.transport`` (#513) can already report: bang it and it sends the bound
     *  clock's beat and tempo. So the question this object has to answer is why
     *  a patch would not simply bang a ``.transport``, and the answer is the
     *  ownership contract, which is the whole of #513. A ``.transport`` **makes
     *  the clock it names** the moment it joins a patcher. Dropping one into a
     *  patch merely to ask what beat ``main`` is on would therefore bring a
     *  stopped ``main`` into being if the host had not made one yet, and every
     *  ``.timepoint``, ``.tempo``, ``.metro`` and ``.delay`` bound to that name
     *  would then be sitting on a clock nobody started — the exact failure
     *  #507 refused to allow when it made ``.timepoint`` a pure reader.
     *
     *  A ``.when`` is that same pure reader, for the one thing ``.timepoint``
     *  and ``.tempo`` cannot do: answer *now*, rather than wake up later. It
     *  **never creates a clock and never destroys one**. A name nothing has
     *  claimed simply reports nothing, and starts reporting the moment a
     *  ``.transport`` or a host-side ``createClock`` brings the clock into
     *  existence — ``clockBridge::Poll`` picks the name up within
     *  ``clockBridge::RESOLVE_INTERVAL_BLOCKS`` blocks, and the by-name route
     *  below sees it immediately.
     *
     *  The second difference is the inlet. A ``.transport``'s int and float are
     *  *start and stop*, which makes it hostile to the thing a query object is
     *  wired into: a ``.metro`` into a ``.transport`` toggles the clock every
     *  tick. On a ``.when``, as in Max, **every input reports** — bang, int,
     *  float, or any list — so it is the object you put on the end of anything
     *  that already fires at the moments you want to sample the clock at.
     *
     *  ### What is reported, and why it is not Max's pair
     *
     *  Max sends bars.beats.units out the left outlet and ticks out the right.
     *  Neither exists here: a ``CLOCK::domainClock`` is a bare beat accumulator
     *  with no origin, no meter and no tick resolution, which is the same fact
     *  that cost ``.transport`` its ``seek`` and ``.timepoint`` its rewind. So
     *  this object reports what a domain clock actually has, in ``.transport``'s
     *  own layout so the two agree to the digit: the **beat position** out the
     *  left outlet and the **tempo in BPM** out the right, sent right to left as
     *  Max orders outlets.
     *
     *  With no clock to read, **nothing at all is emitted** — not a zero.
     *  ``.transport``'s rule, for its reason: beat 0 at 0 BPM is a perfectly
     *  ordinary state for a real clock to be in, so a zero here would be
     *  indistinguishable from an answer and whatever this is wired into would
     *  act on a position that does not exist.
     *
     *  ### No ``clock <name>``
     *
     *  ``.metro``, ``.timepoint`` and ``.tempo`` take one; this object does not,
     *  and the omission is Max's as much as it is #513's. Max's ``when`` has no
     *  ``clock`` method — it is configured by a ``@transport`` attribute and
     *  every *message* it receives reports the time, so a ``clock main`` sent to
     *  a Max ``when`` prints the beat rather than re-pointing it. Reserving the
     *  word here would take a message away from the one thing this object does.
     *  It also matches ``.transport``'s reading of the same question: a
     *  transport's identity *is* its name, given as a creation argument, and
     *  re-pointing it is a re-create.
     *
     *  ### Two routes to the clock, and why there are two
     *
     *  ``.transport``'s, unchanged, because reading has the same problem writing
     *  does. A patcher message handler runs on **whichever thread dispatched the
     *  message** — in-patcher delivery dispatches ``T_DSP``, and the drains at
     *  the top of ``patcherImplementation::Calculate`` dispatch ``T_GUI`` *from
     *  the audio callback* — so a ``.metro`` wired into this inlet puts a report
     *  on the audio thread, where ``CLOCK::Manager()``'s mutex is out. The
     *  thread is asked of ``patcherImplementation::CallingThread`` (#690) rather
     *  than read off the ``THREAD`` tag, because the tag is dispatch semantics
     *  and not thread identity:
     *
     *  - **Off the audio callback**, the manager answers by name. It needs no
     *    resolved binding, so a bang sent in the same breath as ``CreateObject``
     *    reports immediately rather than staying silent until the background
     *    pool has found the clock.
     *  - **On it**, the binding's two acquire loads per value. Wait-free, no
     *    allocation, no lock. An unresolved binding reports nothing, and the
     *    bridge's ``Poll`` has it resolved within
     *    ``clockBridge::RESOLVE_INTERVAL_BLOCKS`` blocks of the object being
     *    created, after which every bang answers.
     *
     *  The two routes agree on every clock that exists. They part on one the
     *  host has **destroyed**: the manager no longer has it, so the by-name
     *  route goes quiet, while a resolved binding keeps the clock alive and
     *  frozen (#707) and reports the values it stopped at. That is inherited
     *  from ``clockBridge`` rather than papered over — a destroyed clock is a
     *  stopped clock to everything bound to it.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven entirely by its inlet.
     *  No handler allocates, locks or blocks when it turns out to be running on
     *  the audio callback — the list handler reads nothing out of the message
     *  (its *arrival* is the whole content), and the clock is reached through
     *  the bridge's two acquire loads. The blocking by-name route is taken only
     *  when the object has established it is *not* on the callback.
     */
    PATCHER_CLASS(gWhen, YSE::OBJ::G_WHEN)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(ReportBang)
    _INT_IN(ReportInt)
    _FLOAT_IN(ReportFloat)
    _LIST_IN(ReportList)

    /** @brief Control thread. Bind the named clock — never create it. See the
     *         ownership contract in the class notes. */
    void SetParent(pObject* parent) override;

    /** @brief The clock this object reads, or ``""``. The storage is the
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

  private:
    // Whether the handler currently running is on the audio callback — the
    // question a `THREAD` tag cannot answer (#690). False for a standalone
    // object, which has no patcher to ask and is never rendered.
    bool OnAudioThread(YSE::THREAD thread) const;

    // The clock's beat position and tempo, by whichever route this thread may
    // use. False when there is no clock to read, in which case nothing is
    // written and nothing is emitted.
    bool ReadClock(YSE::THREAD thread, double& beat, float& bpm) const;

    // Max's one behaviour: send the pair, right to left. Silent when there is
    // no clock. Every inlet handler is this and nothing else.
    void Report(YSE::THREAD thread);

    // The domain clock's name, and this object's identity. Written once by
    // SetParams before the object is published and never again, so a handler on
    // any thread may read it — but only the non-audio route ever does.
    std::string clockname;

    // The patcher-wide binding for `clockname`, or 0. Taken in SetParent and
    // never released — clockBridge hands the same handle to every object naming
    // the same clock, so this shares a slot with the `.transport` driving it
    // rather than costing one of its own.
    std::atomic<clockBridge::Handle> binding{0};
  };
}
}
