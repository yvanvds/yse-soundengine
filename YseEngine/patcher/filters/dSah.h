#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/filters.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~sah``: sample and hold, wrapping ``DSP::sampleHold`` (issue
     *         #1082).
     *
     *  Ported from Max's ``sah~``. The wrapper only maps the pins, messages
     *  and parameter onto the DSP class.
     *
     *  - Inlet 0: the signal to sample (unconnected reads the last number
     *    sent to it, 0 at first, #1036). Messages: ``thresh <x>``,
     *    ``triggermode <0-6>`` (Max's modes, see ``DSP::sampleHold``),
     *    ``duration <samples>`` (alias ``dur``). A list that starts with a
     *    word (text through a receive) is read as a message.
     *  - Inlet 1: the trigger signal (unconnected reads 0, which never
     *    triggers in the default mode); a number sets the threshold, as
     *    Max's ``float`` does.
     *  - Outlet: the held value.
     *  - Parameter: ``thresh``, the creation arguments. A number is the
     *    threshold (Max's ``initial-trigger-value``); ``thresh <x>``,
     *    ``triggermode <n>`` and ``duration <n>`` / ``dur <n>`` among them
     *    set those attributes, as the patcher has no attribute syntax:
     *    ``~sah 0.5 triggermode 1``. They are saved with the patch; a later
     *    message is not, as for ``~what``.
     *
     *  Everything reaches the audio thread through atomics, applied to the
     *  DSP class at the start of each block.
     */
    PATCHER_CLASS(dSah, YSE::OBJ::D_SAH)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetInput)
    _INT_IN(SetInputInt)
    _LIST_IN(SetList)
    _FLOAT_IN(SetThresh)
    _INT_IN(SetThreshInt)

  private:
    // An attribute keyword (dSah.cpp's keyword enum) and its number; a
    // number out of range is ignored.
    void SetAttribute(int which, float number);

    DSP::sampleHold fn;

    std::vector<std::string> arguments;

    std::atomic<float> input{0.f};
    std::atomic<float> thresh{0.f};
    std::atomic<int> triggermode{DSP::sampleHold::ASCENDING};
    std::atomic<unsigned int> duration{0};
  };

} // namespace PATCHER
} // namespace YSE
