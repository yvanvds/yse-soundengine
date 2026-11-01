#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/generators/pulseTrain.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~train``: a 0/1 pulse train with its period in milliseconds,
     *         wrapping ``DSP::pulseTrain`` (issue #1059).
     *
     *  Ported from Max's ``train~``. The wrapper only maps pins and
     *  parameters onto the DSP class.
     *
     *  - Left inlet: the period in ms, as a signal or a number (``float`` /
     *    ``int``; the number is used while no signal is connected, see
     *    #1036). A bang restarts the cycle so the on part begins at the next
     *    block. Also takes ``interval <ms>``, ``width <0-1>``,
     *    ``phase <0-1>`` and ``resetmode <0|1>``.
     *  - Middle inlet: the pulse width, 0..1, as a signal or a number.
     *  - Right inlet: the phase of the onset, 0..1, as a signal or a number.
     *  - Left outlet: the pulse train, 0 or 1.
     *  - Right outlet: a bang after a block in which a pulse began, armed in
     *    CALC() and sent after the traversal (issue #1037). Several onsets in
     *    one block are one bang.
     *  - Parameters: ``interval`` (argument 1, default 1000), ``width``
     *    (argument 2, default 0.5), ``phase`` (argument 3, default 0) and
     *    ``resetmode`` (argument 4, default 0). A later number or message is
     *    not saved, as for ``~slide``.
     *
     *  Control values reach the audio thread through atomics, read at the
     *  start of each block.
     */
    PATCHER_CLASS(dTrain, YSE::OBJ::D_TRAIN)
    _DO_MESSAGES
    _DO_CALCULATE

    _BANG_IN(Restart)
    _FLOAT_IN(SetInterval)
    _INT_IN(SetIntervalInt)
    _FLOAT_IN(SetWidth)
    _INT_IN(SetWidthInt)
    _FLOAT_IN(SetPhase)
    _INT_IN(SetPhaseInt)

  private:
    DSP::pulseTrain fn;

    aFlt interval;
    aFlt width;
    aFlt phase;
    aInt resetmode;
    std::atomic<bool> restart{false};
  };

} // namespace PATCHER
} // namespace YSE
