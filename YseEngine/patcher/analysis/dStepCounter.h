#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/generators/stepCounter.hpp"
#include "../generatorObjects/dSubdiv.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~stepcounter``: counts the jumps in a signal (the resets of a
     *         ``~phasor``) through a sequence of counts, wrapping
     *         ``DSP::stepCounter`` (issue #1080).
     *
     *  Ported from Max's ``stepcounter~``. The wrapper only maps the pins,
     *  messages and parameter onto the DSP class.
     *
     *  - Inlet: the signal to count the jumps of, usually a ``~phasor``
     *    (unconnected reads 0, which never jumps). A ``float`` / ``int``
     *    replaces the sequence with that one count; a list replaces it with
     *    its numbers. Messages: ``seq <counts...>`` (Max's ``seq``
     *    attribute; none empties the sequence), ``reset``,
     *    ``direction <0|1|2>``, ``startmode <0|1>``, ``syncupdate <0|1|2>``,
     *    ``thresh <x>``. A list that starts with a word (text through a
     *    receive) is read as a message.
     *  - Outlets: the impulses (a 1 for one sample when a count is reached),
     *    the count of the step playing, and the step's index in the sequence.
     *  - Parameter: ``seq``, the creation arguments. Numbers are the first
     *    sequence; ``direction <n>``, ``startmode <n>``, ``syncupdate <n>``
     *    and ``thresh <x>`` among them set those attributes, as the patcher
     *    has no attribute syntax: ``~stepcounter direction 2 2 1 3``. They
     *    are saved with the patch; a later message is not, as for
     *    ``~what``.
     *
     *  Scalars and the reset reach the audio thread through atomics. A
     *  sequence travels in ``dSubdiv``'s mailbox (a fixed table of atomics
     *  under a sequence counter; a count of 0 empties the sequence). In a
     *  block, the modes apply first, then a new sequence, then a reset. One
     *  message thread at a time is assumed, as for every object's messages.
     */
    PATCHER_CLASS(dStepCounter, YSE::OBJ::D_STEPCOUNTER)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetValue)
    _INT_IN(SetValueInt)
    _LIST_IN(SetList)

  private:
    static_assert(DSP::stepCounter::MAX_STEPS == DSP::phasorSubdiv::MAX_PATTERN,
                  "the sequence travels in dSubdiv's mailbox");

    // An attribute keyword (dStepCounter.cpp's keyword enum) and its number;
    // a number out of range is ignored.
    void SetAttribute(int which, float number);
    void PostValue(float value);
    // The numbers of `text` from `cursor` on, posted as the new sequence.
    void PostList(const std::string& text, std::size_t cursor);

    DSP::stepCounter fn;

    std::vector<std::string> arguments;

    std::atomic<int> direction{DSP::stepCounter::BOTH};
    std::atomic<int> startmode{1};
    std::atomic<int> syncupdate{DSP::stepCounter::IMMEDIATE};
    std::atomic<float> thresh{0.75f};
    std::atomic<bool> resetPending{false};

    dSubdiv::mailbox<float> lists;
  };

} // namespace PATCHER
} // namespace YSE
