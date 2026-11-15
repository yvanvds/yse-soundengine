#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/analysis/levelMeter.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~levelmeter``: the RMS level of a signal in dB with the
     *         ballistics of a VU meter or a PPM, wrapping
     *         ``DSP::levelMeter`` (issue #1166).
     *
     *  Ported from Max's ``levelmeter~``. The engine measures, the host
     *  draws: the level, the held peak and the clip indicator are polled
     *  through the GUI value, three cells ``<dB> <hold dB> <clip>``, the
     *  readings against the offset, the dBFS that reads 0 (-200 for
     *  silence), and the clip 0 or 1. It is not settable.
     *
     *  - Inlet: the signal; unconnected it reads as silence. A bang sends
     *    the latest reading; a number is shown as that reading in dB and
     *    sent out, as in Max. Also takes ``mode <0-5>`` (Max's standard
     *    ballistics: fast, VU, DIN, BBC, Nordic, EBU), ``attack <ms>``,
     *    ``release <ms>``, ``offset <dB>``, ``interval <ms>``, ``hold <ms>``
     *    and ``clear``, which turns the clip indicator off and drops the held
     *    peak.
     *  - Outlet: the reading in dB every update interval, armed in CALC()
     *    and sent after the traversal (issue #1037); a float where Max sends
     *    an int.
     *  - Parameters: ``mode`` (argument 1, default 0), ``interval`` in ms
     *    (argument 2, default 20, 10 to 2000) and ``hold`` in ms (argument
     *    3, default 0). Later numbers and messages are not saved; ``mode``
     *    is how a patch saves its ballistics.
     *
     *  Max's ``range``, ``markers``, ``markersused``, colours,
     *  ``inactivealpha``, ``style`` and the mouse are drawing, left to the
     *  host; ``displaychan`` picks a channel of a multichannel signal, which
     *  YSE's signals are not.
     *
     *  Values reach the audio thread through atomics read at the start of
     *  each block. The wrapper contains no sample math.
     */
    PATCHER_CLASS(dLevelmeter, YSE::OBJ::D_LEVELMETER)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    std::string GetGuiValue() override;
    unsigned int GetGuiValueCount() const override;
    std::string GetGuiValueAt(unsigned int index) override;

    _BANG_IN(Report)
    _FLOAT_IN(SetReading)
    _INT_IN(SetReadingInt)

  private:
    // The attack, release and offset of a mode. Any thread.
    void ApplyMode(int value);
    void SetReadingFrom(float dB, YSE::THREAD thread);

    DSP::levelMeter fn;

    aInt mode;
    aFlt interval;
    aFlt hold;

    aFlt attack{0.f};
    aFlt release{0.f};
    aFlt offset{0.f};

    std::atomic<float> pending{0.f};
    std::atomic<bool> setRequested{false};
    std::atomic<bool> report{false};
    std::atomic<bool> clearRequested{false};

    // What the host polls, written after every block.
    std::atomic<float> shownLevel{DSP::levelMeter::SILENCE};
    std::atomic<float> shownHold{DSP::levelMeter::SILENCE};
    std::atomic<bool> shownClip{false};
  };

} // namespace PATCHER
} // namespace YSE
