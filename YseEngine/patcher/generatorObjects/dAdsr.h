#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/generators/adsr.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~adsr``: a gate- or number-triggered ADSR envelope with its
     *         times in milliseconds, wrapping ``DSP::adsr`` (issue #1064).
     *
     *  Ported from Max's ``adsr~``. The wrapper only maps pins and
     *  parameters onto the DSP class.
     *
     *  - Left inlet: the trigger. A signal is a gate: a rise from 0 starts
     *    the envelope at the signal's value, a return to 0 releases it. A
     *    number (``float`` / ``int``) is an event: non-zero starts (or
     *    restarts) the envelope at that amplitude at the next block, 0
     *    releases it. A list of up to four numbers sets attack, decay,
     *    sustain and release. Also takes ``attack``, ``decay``, ``sustain``,
     *    ``release``, ``legato``, ``retrigger``, ``maxsustain`` and
     *    ``triggermode`` messages with one number.
     *  - Inlets 2-5: attack (ms), decay (ms), sustain (a factor of the
     *    amplitude) and release (ms), each a signal or a number used while no
     *    signal is connected (#1036).
     *  - Left outlet: the envelope.
     *  - Middle outlet: 1 during attack, decay and sustain, else 0.
     *  - Right outlet: 0 when the envelope starts from rest and 1 when it
     *    comes to rest, the mute state for a voice, armed in CALC() and sent
     *    after the traversal (issue #1037).
     *  - Parameters, from the arguments in this order: ``attack`` (0),
     *    ``decay`` (0), ``sustain`` (1), ``release`` (0), ``legato`` (0),
     *    ``retrigger`` (5), ``maxsustain`` (-1) and ``triggermode`` (0). A
     *    later number, list or message is not saved, as for ``~train``.
     *
     *  Control values reach the audio thread through atomics, read at the
     *  start of each block.
     */
    PATCHER_CLASS(dAdsr, YSE::OBJ::D_ADSR)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(Trigger)
    _INT_IN(TriggerInt)
    _LIST_IN(SetList)
    _FLOAT_IN(SetAttack)
    _INT_IN(SetAttackInt)
    _FLOAT_IN(SetDecay)
    _INT_IN(SetDecayInt)
    _FLOAT_IN(SetSustain)
    _INT_IN(SetSustainInt)
    _FLOAT_IN(SetRelease)
    _INT_IN(SetReleaseInt)

  private:
    void Event(float amplitude);

    DSP::adsr fn;

    aFlt attack;
    aFlt decay;
    aFlt sustain;
    aFlt release;
    aInt legato;
    aFlt retrigger;
    aFlt maxsustain;
    aInt triggermode;
    aFlt amplitude;
    std::atomic<bool> pending{false};
  };

} // namespace PATCHER
} // namespace YSE
