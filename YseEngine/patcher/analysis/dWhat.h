#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/generators/phasorWhat.hpp"
#include "../generatorObjects/dSubdiv.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~what``: an impulse each time a 0..1 ramp passes one of a
     *         list of phase values, wrapping ``DSP::phasorWhat`` (issue
     *         #1079).
     *
     *  Ported from Max's ``what~``. The wrapper only maps the pins, messages
     *  and parameter onto the DSP class.
     *
     *  - Inlet: the ramp to watch, usually a ``~phasor`` (unconnected reads
     *    0, a ramp that never moves). A ``float`` / ``int`` replaces the list with
     *    that one value; a list replaces it with its numbers. Messages:
     *    ``matches <values...>`` (Max's ``matches`` attribute; none empties
     *    the list), ``clear``, ``triggermode <0|1|2|3>``,
     *    ``syncupdate <0|1>``. A list that starts with a word (text through
     *    a receive) is read as a message.
     *  - Outlet: the impulses, a 1 for one sample.
     *  - Parameter: ``values``, the creation arguments. Numbers are the
     *    first list of values (Max's ``values`` argument).
     *    ``triggermode <n>`` and ``syncupdate <n>`` among them set those
     *    attributes, as the patcher has no attribute syntax:
     *    ``~what triggermode 0 0 0.5``. They are saved with the patch; a
     *    later message is not, as for ``~click``'s ``set``.
     *
     *  Scalars reach the audio thread through atomics. A list travels in
     *  ``dSubdiv``'s mailbox (a fixed table of atomics under a sequence
     *  counter; a count of 0 empties the list). One message thread at a
     *  time is assumed, as for every object's messages.
     */
    PATCHER_CLASS(dWhat, YSE::OBJ::D_WHAT)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetValue)
    _INT_IN(SetValueInt)
    _LIST_IN(SetList)

  private:
    static_assert(DSP::phasorWhat::MAX_MATCHES == DSP::phasorSubdiv::MAX_PATTERN,
                  "the list travels in dSubdiv's mailbox");

    void PostValue(float value);
    // The numbers of `text` from `cursor` on, posted as the new list.
    void PostList(const std::string& text, std::size_t cursor);

    DSP::phasorWhat fn;

    std::vector<std::string> arguments;

    std::atomic<int> triggermode{DSP::phasorWhat::BOTH};
    std::atomic<int> syncupdate{0};

    dSubdiv::mailbox<float> lists;
  };

} // namespace PATCHER
} // namespace YSE
