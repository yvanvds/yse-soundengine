#pragma once

#include <atomic>
#include "../pObject.h"
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
     *    (jump to ``phaseoffset``) and ``phaseoffset <x>``.
     *  - Right inlet: a number jumps to that phase now, without changing
     *    ``phaseoffset``.
     *  - Parameters: ``frequency`` (creation argument 1) and ``phaseoffset``
     *    (argument 2), which is also the phase the ramp starts from.
     *
     *  Not ported: ``jitter`` and ``limit`` (their exact behaviour could not be
     *  confirmed against the reference), and the transport attributes
     *  ``lock``, ``syncupdate``, ``transport`` and tempo-relative frequencies,
     *  which need the patcher's clock bridge wired to signal objects.
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

  private:
    DSP::phasor phasor;

    aFlt frequency;
    aFlt phaseoffset;

    // A phase jump (right inlet) and a reset (message) waiting for the audio
    // thread. The flags are exchanged in CALC(), so each request is applied
    // once.
    aFlt pendingPhase;
    std::atomic<bool> phasePending{false};
    std::atomic<bool> resetPending{false};
    // Audio thread only: whether the first block has run, so the ramp starts
    // at phaseoffset.
    bool started = false;
  };

} // namespace PATCHER
} // namespace YSE
