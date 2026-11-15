#pragma once

#include <array>
#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/analysis/levelMeter.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~gridmeter``: the levels of several signals as the
     *         brightness of a grid of cells, one ``DSP::levelMeter`` per
     *         signal (issue #1166).
     *
     *  Ported from Max's ``gridmeter~``, with one signal inlet per cell
     *  where Max takes one multichannel signal. The engine measures, the
     *  host draws: the GUI value has two cells per channel, first every
     *  channel's brightness, then every channel's mute state. A brightness
     *  is the peak level after the ballistics on a scale from ``-range`` dB
     *  (0) to 0 dB (1), above 1 past 0 dB (Max's hot colour). A mute state
     *  is 1 for a muted cell, 0 for an active one. It is not settable.
     *
     *  - Inlets: one signal per cell (the first argument, default 1);
     *    unconnected they read as silence. The first also takes ``mute
     *    <cell> <0|1>``, ``muteall <0|1>``, ``solo <cell>`` (every other cell
     *    muted; 0 unmutes all), a list of mute states from cell 1, and
     *    ``range <dB>``, ``attack <ms>``, ``release <ms>`` and ``interval
     *    <ms>``. Cells count from 1.
     *  - Outlet: the mute states, one 0 or 1 per cell, after every change,
     *    sent after the next block's traversal (issue #1037).
     *  - Parameter ``gridmeter``: the cell count (1-64), the range in dB
     *    (default 60), the attack and release in ms (300 each) and the
     *    update interval in ms (20, 10 to 2000): ``~gridmeter 8 48``. A list
     *    parameter, so a new cell count rebuilds the object off the audio
     *    thread; later messages are not saved.
     *
     *  Max's int and float, which set how many cells a multichannel signal
     *  shows, have no counterpart: the cell count is the argument. The
     *  layout and look (``columns``, ``cellwidth``, ``cellheight``,
     *  ``dividersize``, ``shape``, ``contrast``, the colours, ``style``) are
     *  drawing, left to the host; a click is the host sending ``mute``.
     *
     *  The settings reach the audio thread through atomics read at the
     *  start of each block. The wrapper contains no sample math.
     */
    PATCHER_CLASS(dGridmeter, YSE::OBJ::D_GRIDMETER)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    std::string GetGuiValue() override;
    unsigned int GetGuiValueCount() const override;
    std::string GetGuiValueAt(unsigned int index) override;

    _LIST_IN(SetMutes)

    void FlushArmedMessages(YSE::THREAD thread) override;

    static constexpr unsigned int MAX_CELLS = 64;

  private:
    // Rebuild the inlets from the arguments. Control thread, before the
    // object is wired or published.
    void ShapePorts();

    std::array<DSP::levelMeter, MAX_CELLS> fn;
    std::vector<std::string> arguments;
    std::atomic<unsigned int> cells{1};

    aFlt range{60.f};
    aFlt attack{300.f};
    aFlt release{300.f};
    aFlt interval{20.f};

    std::array<std::atomic<bool>, MAX_CELLS> muted{};
    std::array<std::atomic<float>, MAX_CELLS> shownDb{};

    // A mute changed: the list goes out after the next block. Set on the
    // thread the change came in on, taken by CALC().
    std::atomic<bool> mutesChanged{false};
    // Render thread only: armed by CALC(), sent by FlushArmedMessages.
    bool mutesPending = false;
    // The mute list, built in room reserved by the constructor. Render
    // thread only.
    std::string scratch;
  };

} // namespace PATCHER
} // namespace YSE
