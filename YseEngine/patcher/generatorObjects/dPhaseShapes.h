#pragma once

#include "../pObject.h"
#include "../../dsp/generators/phaseShapes.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~triangle``: a triangle with a movable peak read by a 0..1
     *         phase, wrapping ``DSP::triangleShape`` (issue #1063).
     *
     *  Ported from Max's ``triangle~``. The wrapper only maps pins,
     *  messages and parameters onto the DSP class.
     *
     *  - Left inlet: the phase, usually a ``~phasor``, as a signal or a
     *    number (``float`` / ``int``; the number is used while no signal is
     *    connected, see #1036). Also takes ``lo <x>`` and ``hi <x>``, Max's
     *    attributes, which set the output range.
     *  - Right inlet: the peak position, 0..1, as a signal or a number.
     *  - Parameters: ``peak`` (argument 1, Max's peak-value-phase-offset,
     *    default 0.5), ``lo`` (argument 2, default -1) and ``hi``
     *    (argument 3, default 1).
     *
     *  Control values reach the audio thread through atomics. A ``lo`` or
     *  ``hi`` message is not saved: ``DumpJSON`` writes the arguments, as for
     *  ``~mtof``'s ``base``.
     */
    PATCHER_CLASS(dTriangle, YSE::OBJ::D_TRIANGLE)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetPhase)
    _INT_IN(SetPhaseInt)
    _FLOAT_IN(SetPeak)
    _INT_IN(SetPeakInt)

  private:
    DSP::triangleShape fn;

    aFlt phase;
    aFlt peak;
    aFlt lo;
    aFlt hi;
  };

  /**
   *  @brief ``~trapezoid``: a rise, a plateau and a fall read by a 0..1
   *         phase, wrapping ``DSP::trapezoidShape`` (issue #1063).
   *
   *  Ported from Max's ``trapezoid~``. The wrapper only maps pins, messages
   *  and parameters onto the DSP class.
   *
   *  - Left inlet: the phase, as a signal or a number (used while no signal
   *    is connected, #1036). Also takes ``lo <x>``, ``hi <x>`` and
   *    ``wrapmode <0|1>``, Max's attributes.
   *  - Middle inlet: the ramp-up point, 0..1, as a signal or a number.
   *  - Right inlet: the ramp-down point, 0..1, as a signal or a number.
   *  - Parameters: ``up`` (argument 1, default 0.1), ``down`` (argument 2,
   *    default 0.9), ``lo`` (argument 3, default 0), ``hi`` (argument 4,
   *    default 1) and ``wrapmode`` (argument 5, default 1: wrap; 0 clips).
   *
   *  Control values reach the audio thread through atomics. Messages are
   *  not saved; the arguments are.
   */
  PATCHER_CLASS(dTrapezoid, YSE::OBJ::D_TRAPEZOID)
  _DO_MESSAGES
  _DO_CALCULATE

  _FLOAT_IN(SetPhase)
  _INT_IN(SetPhaseInt)
  _FLOAT_IN(SetUp)
  _INT_IN(SetUpInt)
  _FLOAT_IN(SetDown)
  _INT_IN(SetDownInt)

private:
  DSP::trapezoidShape fn;

  aFlt phase;
  aFlt up;
  aFlt down;
  aFlt lo;
  aFlt hi;
  aInt wrapmode;
};

} // namespace PATCHER
} // namespace YSE
