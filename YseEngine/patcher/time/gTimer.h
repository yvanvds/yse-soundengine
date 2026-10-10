
#pragma once

#include "../pObject.h"
#include "clockBridge.h"
#include <atomic>
#include <cstdint>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Max's ``timer``: "report elapsed time between two events"
     *         (issue #506).
     *
     *  ### What it is, and which way round it points
     *
     *  Every timing object the patcher had before this one *produces* time:
     *  ``.metro`` bangs at an interval, ``.delay`` waits one out, ``.clocker``
     *  reports how long a run of its own has been going, ``.timepoint`` waits
     *  for a beat to arrive. This object *consumes* it. Two events go in — a
     *  bang in the left inlet and a bang in the right — and what comes out is
     *  how far apart they were. That is the input side of anything rhythm-aware:
     *  the tempo of a tapped-in beat, the length of a gesture, how long a note
     *  was held, how long a patch took to get from one state to the next.
     *
     *  ### The two events, and why the right one does not stop anything
     *
     *  Max: "``bang`` — In left inlet: Starts or restarts the ``timer``. In
     *  right inlet: Sends out the time elapsed since the ``timer`` was started."
     *  Read carefully, the right inlet **measures** rather than stops: it takes
     *  a reading and leaves the baseline exactly where it was. Banging it again
     *  gives a larger number, measured from the same left-inlet bang. That is
     *  worth stating because "start / stop" is the natural way to describe the
     *  object and it is subtly wrong — a lap timer is what this is, and a patch
     *  that wants split times gets them for free by banging the right inlet more
     *  than once.
     *
     *  A right-inlet bang before any left-inlet bang emits **nothing at all**.
     *  There is no interval between one event and no event, and the alternative
     *  — measuring from construction, or emitting 0 — would hand whatever this
     *  is wired into a duration that no pair of events in the patch produced.
     *  Max does not document the case; ``.transport`` already answers the same
     *  shape of question ("a bang with no clock to read emits nothing rather
     *  than a zero that would look like a real position") and this follows it.
     *
     *  ### The milliseconds are measured, never counted
     *
     *  The reading is ``std::chrono::steady_clock``, and that is not a
     *  preference: it is the clock ``timerThread`` schedules on
     *  (``timerThread::Clock``), the one ``INTERNAL::time`` measures the
     *  engine's ticks with, and the one ``MIDI::midiOutSender`` dates its
     *  outbound events by — the sibling ``.clocker`` (#505) reads it for the
     *  same reason. So a ``.timer`` started by a ``.metro`` tick and read on the
     *  next one reports the interval that tick really landed at rather than the
     *  one it was nominally due at, and the number never disagrees with the
     *  events that produced it. A *wall* clock — ``system_clock`` — would be the
     *  wrong quantity twice over: it is not monotonic, so an NTP correction
     *  between the two bangs would stretch, shrink or reverse the interval.
     *
     *  ### The second outlet: the same interval in beats
     *
     *  Issue #506 asks for it directly — "a second outlet reporting the interval
     *  in beats against a bound domain clock — that would make it directly
     *  useful with YSE's polytemporal clocks" — and it is what Max's own right
     *  outlet is for, which reports the elapsed time "in the time format
     *  specified by the ``format`` attribute" against the transport named by his
     *  ``transport`` attribute. YSE's equivalent of that attribute is this
     *  object's creation argument: ``.timer main`` measures against the domain
     *  clock called ``main``.
     *
     *  Beats are measured the same way milliseconds are — the clock's beat
     *  position is read at each of the two events and subtracted — so a tempo
     *  change, a ``requestTempo`` ramp or a pause between them is accounted for
     *  exactly, which is the whole point of asking a clock rather than dividing
     *  milliseconds by a BPM figure. The two outlets are therefore not two
     *  spellings of one number: 500 ms is half a beat at 60 BPM and a whole one
     *  at 120, and if the tempo moved in between it is neither.
     *
     *  Nothing is emitted on that outlet when there is no beat to report — no
     *  creation argument, a clock nothing has created yet, or a clock that
     *  appeared only after the left-inlet bang, in which case the interval has
     *  no beat baseline to be measured from. Silence rather than 0, for the
     *  reason the whole object emits nothing before its first start.
     *
     *  **This object never creates a clock and never destroys one.** #513
     *  settled that: ``.transport`` is the sole creator, and everything else —
     *  ``.timepoint``, ``.tempo``, ``.metro``, and this — binds a name and
     *  reads. A ``.timer`` on a name nobody has claimed simply has no beats to
     *  report, and starts having them when something creates the clock.
     *
     *  ### What is deliberately out
     *
     *  Max's ``clock <name>`` message names a ``setclock`` — an *alternative
     *  millisecond clock* to run the measurement on. YSE has no setclock, and
     *  the millisecond reading here is on the engine's own monotonic clock by
     *  design, so the message would have nothing to select. Spelling it as "pick
     *  the beat domain" instead would give a Max word a different meaning in a
     *  patch that looks like Max's, which is worse than not having it: the beat
     *  domain is the creation argument, as ``.transport``'s clock is.
     *
     *  Max's ``format`` attribute (ticks, bars.beats.units, samples, hz,
     *  hh:mm:ss) stays out with it. The two units this object has are spelled by
     *  its two outlets, and the rest need either a meter and an origin a YSE
     *  domain clock does not have (bars.beats.units) or a conversion that
     *  belongs in an object of its own.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven entirely by its two
     *  inlets. A patcher message handler runs on **whichever thread dispatched
     *  the message** — in-patcher delivery dispatches ``T_DSP``, and the drains
     *  at the top of ``patcherImplementation::Calculate`` dispatch ``T_GUI``
     *  *from the audio callback* — so a ``.metro`` wired into either inlet puts
     *  a whole measurement on the audio thread. Nothing there allocates, locks
     *  or blocks:
     *
     *  - the clock read is ``clockBridge::Beat``, two acquire loads (#688). The
     *    by-name route past it, which takes ``CLOCK::Manager``'s mutex, is taken
     *    only for a binding this object holds *and* only once it has established
     *    from ``patcherImplementation::CallingThread`` (#690) that it is not on
     *    the callback — ``.transport``'s arrangement, and for its reason: a
     *    binding resolves on the background pool, so without it a measurement
     *    made in the same breath as the object's creation would have no beats.
     *    It is a bridge over that gap and nothing more: an object that bound no
     *    name — nameless, unparented, or refused by a full bridge — reports no
     *    beats rather than reaching past the patcher for a clock;
     *  - the time base is one ``steady_clock::now()``, a
     *    ``QueryPerformanceCounter`` on Windows and a vDSO ``clock_gettime`` on
     *    Linux and Android, which ``INTERNAL::time::update`` already reads from
     *    the audio callback every block (#667);
     *  - the state is three atomics and there is no string handling on any
     *    message path at all, both inlets taking a bang and nothing else.
     */
    PATCHER_CLASS(gTimer, YSE::OBJ::G_TIMER)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(Start)
    _BANG_IN(Report)

    /** @brief Control thread. Bind the named clock — never create it — so the
     *         beats outlet has something to measure against. */
    void SetParent(pObject* parent) override;

    /** @brief The domain clock the beats outlet is measured on, or ``""``. The
     *         storage is the object's own creation argument and never changes. */
    const char* ClockName() const {
      return clockname.c_str();
    }

    /** @brief Whether a ``clockBridge`` slot was taken for the name. False for
     *         a nameless or unparented object, and for a bridge that was full. */
    bool Bound() const {
      return binding.load(std::memory_order_relaxed) != 0;
    }

    /** @brief Whether a left-inlet bang has ever arrived. A timer that has not
     *         been started reports nothing — see the class notes. */
    bool Started() const {
      return started.load(std::memory_order_acquire);
    }

    /** @brief The milliseconds a right-inlet bang would report right now, or 0
     *         when the timer has never been started. */
    double ElapsedMs() const;

  private:
    // steady_clock now, in nanoseconds. The engine's monotonic time source —
    // `timerThread::Clock`, `INTERNAL::time` and `MIDI::nowNs` are the same
    // clock — kept in nanoseconds so the millisecond figure the outlet carries
    // is a rounding of a finer reading rather than a difference of coarse ones.
    static std::int64_t NowNs();

    // Whether the handler currently running is on the audio callback, which is
    // the question a `THREAD` tag cannot answer (#690). False for a standalone
    // object, which has no patcher to ask and is never rendered.
    bool OnAudioThread(YSE::THREAD thread) const;

    // The bound clock's beat position, by whichever route this thread may use.
    // False when there is no clock to read, in which case @p beat is untouched
    // and the interval simply has no beats half.
    bool ReadBeat(YSE::THREAD thread, double& beat) const;

    // The domain clock the beats outlet is measured on. Written once by
    // SetParams before the object is published and never again, so a handler on
    // any thread may read it — but only the non-audio route ever does.
    std::string clockname;

    // Whether a left-inlet bang has ever arrived. Released rather than relaxed:
    // it is what publishes the two baselines below to a reader that may be
    // another thread entirely, so a report that sees a start sees its baselines.
    std::atomic<bool> started{false};

    // The steady_clock instant, in nanoseconds, the interval is measured from —
    // the last left-inlet bang. Meaningless until `started`.
    std::atomic<std::int64_t> startNs{0};

    // The clock's beat position at that same instant, or NaN when there was no
    // clock to read then. A NaN sentinel rather than a second flag so the
    // baseline is one word: a report that sees a fresh `started` cannot then see
    // a beat baseline from the previous measurement paired with a fresh
    // millisecond one.
    std::atomic<double> startBeat{0.0};

    // The patcher-wide binding for `clockname`, or 0. Taken in SetParent and
    // never released — clockBridge hands the same handle to every object naming
    // the same clock, so this shares a slot with the `.transport` driving it
    // rather than costing one of its own.
    std::atomic<clockBridge::Handle> binding{0};
  };
}
}
