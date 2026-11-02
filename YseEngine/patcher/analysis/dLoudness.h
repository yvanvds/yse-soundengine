#pragma once

#include <array>
#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/analysis/loudnessMeter.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~loudness``: EBU R 128 loudness of one or more signals,
     *         wrapping ``DSP::loudnessMeter`` (issue #1129).
     *
     *  Ported from Max's ``loudness~``. Reports are armed in CALC() and sent
     *  by the patcher after the block's DSP traversal (issue #1037).
     *
     *  - Inlets: one signal per channel (the first argument, default 1),
     *    measured together; unconnected they read as silence. The first
     *    also takes a bang, which reports; a number, which picks the channel
     *    whose peaks are reported (0 the highest of all, as Max's int and
     *    channel attribute) and reports; ``channel <n>``; ``interval <ms>``;
     *    and ``reset``, which starts the integrated loudness, the loudness
     *    range and the peaks over at the next block, after a report that
     *    came with it.
     *  - Outlets: the momentary, short-term and integrated loudness in LUFS,
     *    the loudness range in LU, the sample peak in dBFS and the true peak
     *    in dBTP (issue #1237), sent right to left. A report gives the
     *    values up to the end of the last block, at the start of the next.
     *  - Parameter ``loudness``: the channel count, then the reporting
     *    interval in ms (``~loudness 2 100``). A list parameter, so a new
     *    channel count rebuilds the object off the audio thread; a later
     *    number or message is not saved.
     *
     *  The numbers, the bang and the reset reach the audio thread
     *  through atomics, read at the start of each block. The wrapper
     *  contains no sample math.
     */
    PATCHER_CLASS(dLoudness, YSE::OBJ::D_LOUDNESS)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _BANG_IN(Report)
    _FLOAT_IN(SetChannel)
    _INT_IN(SetChannelInt)

  private:
    // Rebuild the inlets from the channel count. Control thread, before
    // the object is wired or published.
    void ShapePorts();

    DSP::loudnessMeter fn;
    std::array<DSP::buffer*, DSP::loudnessMeter::MAX_CHANNELS> ins{};

    std::vector<std::string> arguments;

    std::atomic<int> channel{0};
    aFlt interval{0.f};
    std::atomic<bool> report{false};
    std::atomic<bool> resetRequested{false};
  };

} // namespace PATCHER
} // namespace YSE
