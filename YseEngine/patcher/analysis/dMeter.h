#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/analysis/levelMeter.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~meter``: the peak level of a signal for an LED meter,
     *         wrapping ``DSP::levelMeter`` (issue #1166).
     *
     *  Ported from Max's ``meter~``. The engine measures, the host draws:
     *  the level, the held peak and the clip indicator are polled through
     *  the GUI value, three cells ``<level> <hold> <clip>``, the level and
     *  the hold as amplitudes (1 is 0 dBFS) and the clip 0 or 1. It is not
     *  settable.
     *
     *  - Inlet: the signal; unconnected it reads as silence. A number is
     *    shown as if a signal with that peak had arrived, and is sent out
     *    the outlet, as in Max. Also takes ``interval <ms>``, ``hold <ms>``
     *    and ``clear``, which turns the clip indicator off and drops the
     *    held peak.
     *  - Outlet: the peak of every update interval, armed in CALC() and sent
     *    after the traversal (issue #1037).
     *  - Parameters: ``interval`` in ms (argument 1, default 50, 10 to 2000
     *    as in Max) and ``hold`` in ms (argument 2, default 0: no hold).
     *    Later numbers and messages are not saved.
     *
     *  Max's LED attributes (``numleds``, ``dbperled``, the LED counts and
     *  colours, ``orientation``, ``style``) and the mouse are drawing, left
     *  to the host; it can keep them in the object's GUI properties.
     *
     *  Values reach the audio thread through atomics read at the start of
     *  each block. The wrapper contains no sample math.
     */
    PATCHER_CLASS(dMeter, YSE::OBJ::D_METER)
    _DO_MESSAGES
    _DO_CALCULATE

    std::string GetGuiValue() override;
    unsigned int GetGuiValueCount() const override;
    std::string GetGuiValueAt(unsigned int index) override;

    _FLOAT_IN(SetLevel)
    _INT_IN(SetLevelInt)

  private:
    DSP::levelMeter fn;

    aFlt interval;
    aFlt hold;

    std::atomic<float> pending{0.f};
    std::atomic<bool> setRequested{false};
    std::atomic<bool> clearRequested{false};

    // What the host polls, written after every block.
    std::atomic<float> shownLevel{0.f};
    std::atomic<float> shownHold{0.f};
    std::atomic<bool> shownClip{false};
  };

} // namespace PATCHER
} // namespace YSE
