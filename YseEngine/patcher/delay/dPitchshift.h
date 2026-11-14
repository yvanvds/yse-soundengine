#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "../pObject.h"
#include "../../dsp/delay/pitchShifter.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~pitchshift``: real-time pitch shifting of one or more
     *         channels, wrapping ``DSP::pitchShifter`` (issue #1153).
     *
     *  Ported from Max's ``pitchshift~``. The wrapper only maps the pins,
     *  messages and parameter onto one DSP class per channel.
     *
     *  - Inlet *n* (one per channel): the signal of channel *n*
     *    (unconnected, the last number sent to it, else 0, #1036).
     *  - The last inlet: the shift, a ratio or (``usecents 1``) cents, as a
     *    number or a signal read per sample. A number is taken as what
     *    ``usecents`` says when it arrives; unconnected, the last one holds.
     *  - Every inlet takes the attributes as messages: ``pitchshift <ratio>``,
     *    ``pitchshiftcent <cents>``, ``quality <basic|good|better|best>``,
     *    ``constantlatency <0|1>``, ``enabled <0|1>``, ``reportlatency
     *    <0|1>``, ``usecents <0|1>``, and ``getlatency``, which reports the
     *    latency once (``getlatency <0|1>`` is ``reportlatency``).
     *  - Outlet *n*: channel *n* shifted. The last outlet: the latency in
     *    samples, an int, sent after the block (#1037) when reporting is
     *    turned on, whenever it changes while it is on, and on
     *    ``getlatency``.
     *  - Parameter: ``settings``, the channel count (1-16, default 1), then
     *    any of the attribute words with a value. It is saved with the
     *    patch; a later message is not.
     *
     *  Settings reach the audio thread through atomics, applied to the DSP
     *  classes at the start of each block.
     */
    PATCHER_CLASS(dPitchshift, YSE::OBJ::D_PITCHSHIFT)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief The most channels an object has. */
    static constexpr UInt MAX_CHANNELS = 16;

    /** @brief How many channels (signal inlet / outlet pairs) the object
     *         has. */
    int ChannelCount() const {
      return static_cast<int>(fn.size());
    }

    /** @brief Channel @p c's DSP class, for tests. Not while the object
     *         renders. */
    const DSP::pitchShifter& Shifter(UInt c) const {
      return *fn[c];
    }

  private:
    // Rebuild the pins and the shifters from the settings. Control thread,
    // before the object is wired or published: the constructor and the two
    // parameter callbacks (a list parameter rebuilds the object, #234).
    void ShapePorts();
    // Apply one attribute word with its value, from the settings or a
    // message. False when the word or value is not one.
    bool ApplyWord(std::string_view word, std::string_view value);

    std::vector<std::string> settings;
    std::vector<std::unique_ptr<DSP::pitchShifter>> fn;

    std::array<aFlt, MAX_CHANNELS> constants;
    aFlt ratio{1.f};
    aInt quality{DSP::pitchShifter::BASIC};
    std::atomic<bool> constantLatency{false};
    std::atomic<bool> enabled{true};
    std::atomic<bool> useCents{false};
    std::atomic<bool> reportLatency{false};
    std::atomic<bool> reportOnce{false};

    // The latency last sent. Audio thread only.
    int reported = -1;
  };

} // namespace PATCHER
} // namespace YSE
