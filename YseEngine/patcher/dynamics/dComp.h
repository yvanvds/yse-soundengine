#pragma once

#include <array>
#include <atomic>
#include <string>
#include <string_view>
#include <vector>
#include "../pObject.h"
#include "../../dsp/modules/compressor.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~comp``: a stereo-linked feed-forward compressor with an
     *         optional sidechain and gain-reduction metering, wrapping
     *         ``DSP::MODULES::compressor`` (issue #1116).
     *
     *  Covers the role of Max's ``omx.comp~`` with YSE's own compressor, not
     *  OctiMax's algorithm. The wrapper only maps the pins, messages and
     *  parameter onto the DSP class.
     *
     *  - Inlets 0 and 1: the left and right signal (unconnected, the last
     *    number sent there, else 0, #1036). Inlet 0 also takes ``threshold
     *    <dB>``, ``ratio <n>``, ``attack <ms>``, ``release <ms>``, ``makeup
     *    <dB>``, ``detector <0|1>``, ``mix <0-1>``, ``bypass <0|1>``,
     *    ``meters <0|1>``, ``meterrate <ms>`` and ``meterdata``.
     *  - Inlet 2: the sidechain. While a signal is connected the detector
     *    listens to it instead of the left and right signal; unconnected,
     *    the compressor listens to its own input.
     *  - Outlets 0 and 1: the left and right signal compressed.
     *  - Outlet 2: the deepest gain reduction since the last report in dB (0
     *    or less), read at the end of each block, every ``meterrate`` ms
     *    while ``meters`` is on, and after the next block on ``meterdata``.
     *    Armed in ``CALC()`` (#1037).
     *  - Parameter: ``settings``, any of the words above with a value. It is
     *    saved with the patch; a later message is not.
     *
     *  Not ported from ``omx.comp~``: the AGC, noise gate, limiter, dual
     *  band, gating, freeze, progressive release, gain smoothing, lookahead
     *  delay, range, channel coupling (always linked here), sidechain filter
     *  and presets (``choosepreset``, ``savesettings``; the settings
     *  parameter is saved with the patch). ``DSP::MODULES::compressor`` has
     *  none of them.
     */
    PATCHER_CLASS(dComp, YSE::OBJ::D_COMP)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief The meter interval until it is set, in ms. */
    static constexpr float DEFAULT_METER_RATE = 100.f;

    /** @brief The DSP class, for tests. Not while the object renders. */
    DSP::MODULES::compressor& Compressor() {
      return fn;
    }

  private:
    void Defaults();
    bool ApplyWord(std::string_view word, float value);

    std::vector<std::string> settings;
    DSP::MODULES::compressor fn;
    // The signal handed to the DSP class (it compresses in place) and the
    // sidechain. Sized in the constructor.
    MULTICHANNELBUFFER stereo;
    MULTICHANNELBUFFER key;

    std::array<aFlt, 2> constants;
    aFlt threshold;
    aFlt ratio;
    aFlt attack;
    aFlt release;
    aFlt makeup;
    aFlt mix;
    aFlt meterRate;
    aInt detector;
    std::atomic<bool> bypass{false};
    std::atomic<bool> meters{true};
    std::atomic<bool> meterRequested{false};

    // Audio thread only.
    double samplesSinceReport = 0.0;
    float deepest = 0.f;
  };

} // namespace PATCHER
} // namespace YSE
