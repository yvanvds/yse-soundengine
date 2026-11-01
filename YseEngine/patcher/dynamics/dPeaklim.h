#pragma once

#include <array>
#include <atomic>
#include <string>
#include <string_view>
#include <vector>
#include "../pObject.h"
#include "../../dsp/dynamics/lookaheadLimiter.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~peaklim``: a stereo brickwall peak limiter with gain and
     *         gain-reduction metering, wrapping ``DSP::lookaheadLimiter``
     *         (issue #1115).
     *
     *  Covers the role of Max's ``omx.peaklim~`` with the limiter of
     *  ``~limi``, not OctiMax's algorithm. Two linked channels, a fixed
     *  lookahead of ``LOOKAHEAD`` samples. The wrapper only maps the pins,
     *  messages and parameter onto the DSP class.
     *
     *  - Inlets 0 and 1: the left and right signal (unconnected, the last
     *    number sent there, else 0, #1036). Inlet 0 also takes ``threshold
     *    <dB>``, ``ingain <dB>``, ``outgain <dB>``, ``mode <0|1>``, ``bypass
     *    <0|1>``, ``meters <0|1>``, ``meterrate <ms>`` and ``meterdata``.
     *  - Outlets 0 and 1: the left and right signal limited.
     *  - Outlet 2: the deepest gain reduction since the last report in dB (0
     *    or less), every ``meterrate`` ms while ``meters`` is on, and after
     *    the next block on ``meterdata``. Armed in ``CALC()`` (#1037).
     *  - Parameter: ``settings``, any of the words above with a value. It is
     *    saved with the patch; a later message is not.
     *
     *  Not ported: ``savesettings``, which stores the settings in a Max
     *  preset; here the settings parameter is saved with the patch.
     */
    PATCHER_CLASS(dPeaklim, YSE::OBJ::D_PEAKLIM)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief The fixed lookahead, in samples. */
    static constexpr UInt LOOKAHEAD = 64;
    /** @brief The meter interval until it is set, in ms. */
    static constexpr float DEFAULT_METER_RATE = 100.f;

    /** @brief The DSP class, for tests. Not while the object renders. */
    const DSP::lookaheadLimiter& Limiter() const {
      return fn;
    }

  private:
    void Defaults();
    bool ApplyWord(std::string_view word, float value);

    std::vector<std::string> settings;
    DSP::lookaheadLimiter fn;

    std::array<aFlt, 2> constants;
    aFlt threshold;
    aFlt ingain;
    aFlt outgain;
    aFlt meterRate;
    aInt mode;
    std::atomic<bool> bypass{false};
    std::atomic<bool> meters{true};
    std::atomic<bool> meterRequested{false};

    // Audio thread only.
    double samplesSinceReport = 0.0;
    float deepest = 0.f;
  };

} // namespace PATCHER
} // namespace YSE
