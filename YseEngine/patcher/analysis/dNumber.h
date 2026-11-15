#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/filters/rampSmooth.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~number``: a signal monitor and a constant signal generator,
     *         the debugging number box of MSP (issue #1167).
     *
     *  Ported from Max's ``number~``. The engine keeps the state, the host
     *  draws: the GUI value is three cells ``<value> <mode> <allow>``. The
     *  value is what the box shows, the monitored input in monitor mode and
     *  the last number in output mode; the mode is 1 (signal output) or 2
     *  (signal monitor), and allow is which modes there are (1 output only,
     *  2 monitor only, 3 both). It is not settable: a host that lets the user
     *  type a number sends it to inlet 0, and a click on the mode triangle
     *  sends ``mode``.
     *
     *  - Left inlet: the signal to monitor; unconnected it reads as silence.
     *    A number (float or int) is clamped to the minimum and maximum,
     *    shown, and becomes the constant the signal outlet ramps to; a list
     *    ``<value> <ramp ms>`` sets the ramp time first. ``set <n>`` shows a
     *    number without changing the output, and a bang in output mode makes
     *    the shown number the output. Also takes ``mode <1|2>`` (when that
     *    mode is allowed), ``allow <1-3>``, ``monitormode <0|1>`` and
     *    ``sigoutmode <0|1>`` (the two halves of allow), ``min [n]`` and
     *    ``max [n]`` (no number removes the limit), ``interval <ms>`` and
     *    ``ft1 <ms>`` (the ramp time).
     *  - Right inlet: the ramp time in milliseconds, as Max's ``ft1``.
     *  - Left outlet: the constant signal, ramped linearly over the ramp
     *    time to each new number by ``DSP::rampSmooth``. It ignores the
     *    signal input and the mode, as in Max.
     *  - Right outlet: the monitored input, one sample every update interval,
     *    armed in CALC() and sent after the traversal (issue #1037), in
     *    either mode.
     *  - Parameters: ``ramp`` in ms (argument 1, default 0: jump),
     *    ``interval`` in ms (argument 2, default 100, held to 10 - 2000 like
     *    the meters'), ``mode`` (argument 3, default 2: monitor) and
     *    ``allow`` (argument 4, default 3). Later numbers and messages are
     *    not saved.
     *
     *  No DSP class for the monitor: reading one sample per interval is all
     *  the signal work there is, as for ``~snapshot``. The ramp is
     *  ``DSP::rampSmooth``, its length the ramp time at the live sample rate,
     *  worked out every block. Max's ``bgcolor``, ``textcolor``, ``style``,
     *  ``numdecimalplaces`` and the mouse are drawing, left to the host;
     *  ``flags`` is a legacy message whose codes Max no longer documents
     *  (its attributes replaced it).
     *
     *  Values reach the audio thread through atomics read at the start of
     *  each block; nothing on the audio path allocates or locks.
     */
    PATCHER_CLASS(dNumber, YSE::OBJ::D_NUMBER)
    _DO_MESSAGES
    _DO_CALCULATE

    std::string GetGuiValue() override;
    unsigned int GetGuiValueCount() const override;
    std::string GetGuiValueAt(unsigned int index) override;

    _BANG_IN(Output)
    _FLOAT_IN(SetValue)
    _INT_IN(SetValueInt)
    _LIST_IN(SetList)
    _FLOAT_IN(SetRamp)
    _INT_IN(SetRampInt)

    /** @brief The display mode in use: the mode setting, or the one mode
     *         ``allow`` leaves. 1 output, 2 monitor. Any thread. */
    int Mode() const;

  private:
    // A number from the left inlet: clamped, shown and, unless `showOnly`,
    // the new output. NaN is ignored. Any thread.
    void Take(float number, bool showOnly);

    DSP::rampSmooth fn;
    // The output value as a block, filled every CALC().
    DSP::buffer target;

    aFlt ramp;
    aFlt interval;
    aInt mode;
    aInt allow;

    aFlt value{0.f};
    std::atomic<bool> hasMinimum{false};
    std::atomic<bool> hasMaximum{false};
    aFlt minimum{0.f};
    aFlt maximum{0.f};

    // What the host polls: the shown number (output mode) and the input
    // sampled at the last interval (monitor mode).
    aFlt shownNumber{0.f};
    aFlt shownInput{0.f};

    // Audio thread only: samples from the start of the next block to the
    // next interval report.
    double samplesLeft = 0.0;
  };

} // namespace PATCHER
} // namespace YSE
