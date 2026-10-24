#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/generators/stepFunction.hpp"
#include "dSubdiv.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~stepfun``: a breakpoint function played one segment per
     *         cycle of an input ramp, wrapping ``DSP::stepFunction`` (issue
     *         #1081).
     *
     *  Ported from Max's ``stepfun~``. The wrapper only maps the pins,
     *  messages and parameter onto the DSP class.
     *
     *  - Inlet 0: the ramp, usually a ``~phasor`` or ``~stepdiv``'s ramp
     *    (unconnected reads 0). A ``bang`` sets the step back to 0. A
     *    ``float`` / ``int`` makes the pattern that one value and, as in
     *    Max, is kept as the initial value of a list that follows it at
     *    once. A list is a breakpoint function in line format, as
     *    ``.function`` sends it: ``value time`` pairs with ``curvemode`` 0
     *    or 2 (auto: ``.function`` sends pairs), ``value time curve``
     *    triples with 1, which also set the curves. Messages:
     *    ``pattern <values...>``, ``curvepattern <curves...>`` (none clears
     *    either), ``step <n>``, ``curvemode <0|1|2>``,
     *    ``syncupdate <0|1|2>``. A list that starts with a word (text
     *    through a receive) is read as a message.
     *  - Inlet 1: the step, a signal (``~stepdiv``'s index) that overrides
     *    the counting of the ramp's cycles while connected. A number sets
     *    the step, as ``step <n>`` does; it has no effect while a signal is
     *    connected.
     *  - Outlet: the function.
     *  - Parameter: ``pattern``, the creation arguments. Numbers are the
     *    first pattern; ``curvepattern`` makes the numbers after it the
     *    curves, ``pattern`` makes them values again, and ``curvemode <n>``
     *    and ``syncupdate <n>`` set those attributes, as the patcher has no
     *    attribute syntax: ``~stepfun 0 1 0.5 curvepattern 0 0.5 -0.5``.
     *    They are saved with the patch; a later message is not.
     *
     *  Scalars, the reset and the step reach the audio thread through
     *  atomics. Values and curves travel in two of ``dSubdiv``'s mailboxes
     *  (a fixed table of atomics under a sequence counter); a list that sets
     *  both closes the curves first and the values last, and the audio
     *  thread takes the values first, so both land in the same block. In a
     *  block the sync mode applies first, then a new pattern, then a reset,
     *  then a step. One message thread at a time is assumed, as for every
     *  object's messages.
     */
    PATCHER_CLASS(dStepFun, YSE::OBJ::D_STEPFUN)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _BANG_IN(Reset)
    _FLOAT_IN(SetValue)
    _INT_IN(SetValueInt)
    _LIST_IN(SetList)
    _FLOAT_IN(SetStep)
    _INT_IN(SetStepInt)

  private:
    static_assert(DSP::stepFunction::MAX_STEPS == DSP::phasorSubdiv::MAX_PATTERN,
                  "the pattern travels in dSubdiv's mailbox");

    void PostValue(float value);
    // The numbers of `text` from `cursor` on, posted to `box`.
    void PostNumbers(dSubdiv::mailbox<float>& box, const std::string& text, std::size_t cursor);
    // A breakpoint list: values, and curves with curvemode 1.
    void PostBreakpoints(const std::string& text);
    void PostStep(int n);

    DSP::stepFunction fn;

    std::vector<std::string> arguments;

    std::atomic<int> curvemode{2};
    std::atomic<int> syncupdate{DSP::stepFunction::IMMEDIATE};
    std::atomic<bool> resetPending{false};
    std::atomic<int> stepTarget{0};
    std::atomic<bool> stepPending{false};

    // The message thread's: the last number, kept for a list that follows.
    float initial = 0.f;
    bool haveInitial = false;

    dSubdiv::mailbox<float> values;
    dSubdiv::mailbox<float> curves;
  };

} // namespace PATCHER
} // namespace YSE
