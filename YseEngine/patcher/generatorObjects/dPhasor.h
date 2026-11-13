#pragma once

#include <atomic>
#include <string>
#include "../pObject.h"
#include "../time/signalClock.h"
#include "../../dsp/generators/phasor.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~phasor``: a 0..1 phase ramp, wrapping ``DSP::phasor``
     *         (issue #1045).
     *
     *  Ported from Max's ``phasor~``. The wrapper only maps pins and
     *  parameters onto the DSP class; the ramp itself is ``DSP::phasor``.
     *
     *  - Left inlet: frequency in Hz, as a signal or a number (the number is
     *    used while no signal is connected, see #1036). Also takes ``reset``
     *    (jump to ``phaseoffset``), ``phaseoffset <x>``, a tempo-relative
     *    interval (``4n``, ``8nt``, ``1nd``, ``960 ticks``; issue #1278),
     *    ``lock <0/1>``, ``syncupdate <0/1>`` and ``bang`` (take a held
     *    frequency now). A number sets the interval back to 0.
     *  - Right inlet: a number jumps to that phase now, without changing
     *    ``phaseoffset``.
     *  - Parameters: ``frequency`` (creation argument 1), ``phaseoffset``
     *    (argument 2), which is also the phase the ramp starts from, then
     *    ``periodbeats`` (the interval in beats, 0 for Hz), ``lock``,
     *    ``syncupdate`` and ``transport`` (the domain clock's name).
     *
     *  **The clock** (issue #1278). With ``periodbeats`` above 0 the ramp
     *  takes its speed from the ``transport`` clock through ``signalClock``:
     *  one cycle per interval, following tempo changes, still while the clock
     *  is stopped. ``lock 1`` makes the phase the clock's position. The clock
     *  is bound in ``SetParent`` (control thread); ``CALC()`` reads it with
     *  the bridge's wait-free loads. With no clock the ramp holds. Max's
     *  ``transport`` defaults to the global ``internal`` transport; YSE has no
     *  global clock, so an empty name follows none.
     *
     *  Not ported: ``jitter`` and ``limit`` (their exact behaviour could not be
     *  confirmed against the reference), and ``bars.beats.units`` intervals
     *  (a domain clock has no meter, as for ``.metro``).
     *
     *  Control-thread values reach the audio thread through atomics: a phase
     *  jump or a reset is posted as a request and applied at the top of the
     *  next block.
     */
    PATCHER_CLASS(dPhasor, YSE::OBJ::D_PHASOR)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFrequency)
    _INT_IN(SetFrequencyInt)
    _FLOAT_IN(SetPhase)
    _INT_IN(SetPhaseInt)
    _BANG_IN(Update)

    /** @brief Control thread. Joining a patcher binds the ``transport``
     *         clock. */
    void SetParent(pObject* newParent) override;

    /** @brief Whether a ``transport`` clock name was bound. Test surface. */
    bool ClockBound() const {
      return clock.Bound();
    }

  private:
    DSP::phasor phasor;
    signalClock clock;

    aFlt frequency;
    aFlt phaseoffset;
    aFlt periodbeats;
    aInt lock;
    aInt syncupdate;
    std::string transport;

    // A phase jump (right inlet) and a reset (message) waiting for the audio
    // thread. The flags are exchanged in CALC(), so each request is applied
    // once.
    aFlt pendingPhase;
    std::atomic<bool> phasePending{false};
    std::atomic<bool> resetPending{false};
    std::atomic<bool> updatePending{false};
    // Audio thread only: whether the first block has run, so the ramp starts
    // at phaseoffset.
    bool started = false;
  };

} // namespace PATCHER
} // namespace YSE
