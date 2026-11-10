#pragma once

#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/routing/signalSelector.hpp"
#include "selectorControl.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~gate``: sends a signal to one of several outlets, or to
     *         none, wrapping ``DSP::signalSelector`` (issue #1133).
     *
     *  Ported from Max's ``gate~``. The wrapper only maps the pins, messages
     *  and parameter onto the DSP class.
     *
     *  - Inlet 0: the open outlet, from 1 (0 closes them all), as a number
     *    or a signal read per sample. Unconnected, it reads the last number
     *    sent (#1036). ``next`` and ``bang`` open the next outlet, wrapping;
     *    ``ramptime <ms>`` (alias ``ramp``) and ``stepmode <0|1>`` are Max's
     *    attributes. Text through a receive is read the same way.
     *  - Inlet 1: the signal to route (unconnected reads 0).
     *  - Outlets: one per channel, outlet 0 for channel 1. The open one
     *    carries the input, the others silence.
     *  - Parameter: ``arguments``, the creation arguments: the number of
     *    outlets (1-128, default 1) and the outlet open at the start
     *    (default 0, closed), with ``ramptime <ms>`` and ``stepmode <0|1>``
     *    among them (``~gate 4 1 ramptime 10``), as the patcher has no
     *    attribute syntax. The number of outlets rebuilds the object, so
     *    the DSP class is only ever allocated on an object no audio thread
     *    is running. The arguments are saved; later messages are not.
     */
    PATCHER_CLASS(dGate, YSE::OBJ::D_GATE)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetOpen)
    _INT_IN(SetOpenInt)
    _BANG_IN(SetNext)

    /** @brief The number of outlets. */
    UInt Channels() const {
      return fn.channels();
    }

  private:
    // Rebuild the DSP class and the pins from the creation arguments.
    // Control thread, before the object is wired or published.
    void ShapePorts();

    DSP::signalSelector fn;
    selectorControl control;

    std::vector<std::string> arguments;
  };

} // namespace PATCHER
} // namespace YSE
