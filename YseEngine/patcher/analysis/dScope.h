#pragma once

#include <mutex>
#include "../pObject.h"
#include "../../dsp/analysis/scopeCapture.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~scope``: an oscilloscope, wrapping ``DSP::scopeCapture``
     *         (issue #1169).
     *
     *  Ported from Max's ``scope~``. The engine captures, the host draws:
     *  every finished trace is published to the capture's triple buffer,
     *  and the host polls the newest one through the GUI value, ``3 + 2 *
     *  points`` cells: ``<mode> <points> <serial>`` and then two numbers
     *  per point. Mode 0 is a waveform, whose points are each a minimum and
     *  a maximum; mode 1 is X/Y, whose points are each an X and a Y.
     *  ``serial`` counts the traces, so a host sees when a new one arrived;
     *  before the first it is ``0 0 0``. It is not settable.
     *
     *  The bulk read and cell 0 take the newest finished trace; the count
     *  and cells 1 on read the trace cell 0 or the bulk read took, so a host
     *  walking the cells reads cell 0 first, then the count, then the rest.
     *
     *  - Left inlet: the signal, or X; unconnected it reads as silence. An
     *    int (or a float, its whole part) sets ``calccount``. Also takes
     *    ``calccount <n>``, ``bufsize <n>``, ``delay <ms>``,
     *    ``trigger <0|1|2>`` and ``triglevel <x>``.
     *  - Right inlet: Y. With a cord on it the scope draws X/Y, without one
     *    a waveform. An int (or a float) sets ``bufsize``.
     *  - No outlets.
     *  - Parameters: ``calccount``, the samples per point (argument 1,
     *    default 256, 2 to 8192); ``bufsize``, the points per trace
     *    (argument 2, default 128, 2 to 8192); ``delay`` in ms between
     *    traces (argument 3, default 0); ``trigger`` (argument 4, default 0:
     *    none, 1 up, 2 down); ``triglevel`` (argument 5, default 0). Later
     *    numbers and messages are not saved.
     *
     *  Max's ``bgcolor``, ``fgcolor``, ``gridcolor``, ``style``,
     *  ``drawstyle``, ``range`` and the mouse are drawing, left to the
     *  host; ``displaychan``, ``displaysinglechannel``, ``inactivealpha``
     *  and ``mctrigchan`` pick channels of a multichannel signal, which YSE
     *  signals are not; ``automatic`` is undocumented.
     *
     *  Settings reach the audio thread through atomics read at the start of
     *  each block. CALC() never copies a trace: the capture writes into the
     *  triple buffer and publishes with one atomic exchange. The host's
     *  reads take a mutex only host readers share, so two host threads
     *  polling at once do not both act as the one reader the triple buffer
     *  allows; the audio thread never touches it. The wrapper contains no
     *  sample math.
     */
    PATCHER_CLASS(dScope, YSE::OBJ::D_SCOPE)
    _DO_MESSAGES
    _DO_CALCULATE

    // Read only: no _HAS_GUI_CELLS, which promises the write half.
    std::string GetGuiValue() override;
    unsigned int GetGuiValueCount() const override;
    std::string GetGuiValueAt(unsigned int index) override;

    _FLOAT_IN(SetPeriod)
    _INT_IN(SetPeriodInt)
    _FLOAT_IN(SetSize)
    _INT_IN(SetSizeInt)

  private:
    DSP::scopeCapture fn;

    aFlt calccount;
    aFlt bufsize;
    aFlt delay;
    aFlt trigger;
    aFlt triglevel;

    // Host readers only: the triple buffer has one reader at a time.
    mutable std::mutex reading;
  };

} // namespace PATCHER
} // namespace YSE
