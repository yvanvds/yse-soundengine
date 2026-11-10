#pragma once

#include <array>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/routing/signalSelector.hpp"
#include "selectorControl.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~selector``: passes one of several input signals to its
     *         outlet, or none, wrapping ``DSP::signalSelector`` (issue
     *         #1133).
     *
     *  Ported from Max's ``selector~``. The wrapper only maps the pins,
     *  messages and parameter onto the DSP class.
     *
     *  - Inlet 0: the open input, from 1 (0 closes them all), as a number or
     *    a signal read per sample. Unconnected, it reads the last number
     *    sent (#1036). ``next`` and ``bang`` open the next input, wrapping;
     *    ``ramptime <ms>`` (alias ``ramp``) and ``stepmode <0|1>`` are Max's
     *    attributes. Text through a receive is read the same way.
     *  - Inlets 1 to N: the input signals (unconnected read 0).
     *  - Outlet: the open input, or silence when closed.
     *  - Parameter: ``arguments``, the creation arguments: the number of
     *    inputs (1-128, default 1) and the input open at the start (default
     *    0, closed), with ``ramptime <ms>`` and ``stepmode <0|1>`` among
     *    them (``~selector 4 1 ramptime 10``), as the patcher has no
     *    attribute syntax. The number of inputs rebuilds the object, so
     *    the DSP class is only ever allocated on an object no audio thread
     *    is running. The arguments are saved; later messages are not.
     */
    PATCHER_CLASS(dSelector, YSE::OBJ::D_SELECTOR)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetOpen)
    _INT_IN(SetOpenInt)
    _BANG_IN(SetNext)

    /** @brief The number of signal inputs. */
    UInt Channels() const {
      return fn.channels();
    }

  private:
    // Rebuild the DSP class and the pins from the creation arguments.
    // Control thread, before the object is wired or published.
    void ShapePorts();

    DSP::signalSelector fn;
    // This block's inputs, null for an unconnected one. Audio thread.
    std::array<DSP::buffer*, DSP::signalSelector::MAX_CHANNELS> sources{};
    selectorControl control;

    std::vector<std::string> arguments;
  };

} // namespace PATCHER
} // namespace YSE
