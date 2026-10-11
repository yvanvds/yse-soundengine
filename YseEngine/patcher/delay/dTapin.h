#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/delay/delayLine.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~tapin``: the writing end of a delay line, wrapping
     *         ``DSP::delayLine`` (issues #1038, #1105).
     *
     *  Ported from Max's ``tapin~``. Its outlet is a *link* (ADD_OUT_LINK): it
     *  connects only to the left inlet of ``~tapout``, and shares the delay
     *  line with it instead of sending samples. A link is not a dependency,
     *  so ``~tapout`` -> ... -> ``~tapin`` is a legal feedback loop, where a
     *  loop of ordinary signal cords is refused (#1038).
     *
     *  - Left inlet: the signal to record. A number sets the maximum delay in
     *    ms (within the capacity the ``maxdelay`` argument allocated) and
     *    clears the line. Also takes ``clear`` and ``freeze <0/1>``.
     *  - Parameter: ``maxdelay`` (creation argument 1, default 100 ms), the
     *    capacity of the line. Changing it rebuilds the object, so the
     *    allocation happens on the control thread.
     *
     *  Requests from the control thread (a new maximum, ``clear``, ``freeze``)
     *  are applied in ResetDSP(), at the top of the block before anything
     *  reads the line, together with the line's ``beginBlock``. So every
     *  ``~tapout`` hears them in the same block whether it runs before or
     *  after this object.
     */
    PATCHER_CLASS(dTapin, YSE::OBJ::D_TAPIN)
    _DO_MESSAGES
    _DO_CALCULATE
    _DO_RESET

    _FLOAT_IN(SetMaximum)
    _INT_IN(SetMaximumInt)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief The line, for the ``~tapout`` objects linked to this one.
     *
     *  Audio thread, while the snapshot naming this object is pinned.
     */
    const DSP::delayLine& Line() const {
      return line;
    }

  private:
    DSP::delayLine line;

    // The capacity in ms: the creation argument. Read on the control thread
    // only, when the line is built.
    aFlt maxdelay;

    // Requests waiting for the next ResetDSP. The flags are exchanged there,
    // so each is applied once.
    aFlt pendingMaximum;
    std::atomic<bool> maximumPending{false};
    std::atomic<bool> clearPending{false};
    std::atomic<bool> frozen{false};
  };

} // namespace PATCHER
} // namespace YSE
