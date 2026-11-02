#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "../pObject.h"
#include "../../dsp/dynamics/multibandCompressor.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~multiband``: a stereo 2 to 5 band compressor with a
     *         gain-reduction outlet per band, wrapping
     *         ``DSP::multibandCompressor`` (issue #1117).
     *
     *  Covers the role of Max's ``omx.4band~`` and ``omx.5band~`` with
     *  YSE's own algorithm (``~multiband 4``, ``~multiband 5``). The wrapper
     *  only maps the pins, messages and parameter onto the DSP class.
     *
     *  - Inlets 0 and 1: the left and right signal (unconnected, the last
     *    number sent there, else 0, #1036). Inlet 0 also takes the setting
     *    words below as messages, and ``meterdata``.
     *  - Outlets 0 and 1: the left and right signal compressed.
     *  - Outlet 2 + *n*: band *n*'s deepest gain reduction since the last
     *    report in dB (0 or less), read at the end of each block, every
     *    ``meterrate`` ms while ``meters`` is on, and after the next block on
     *    ``meterdata``. Armed in ``CALC()`` (#1037).
     *  - Parameter: ``settings``, the band count (2-5, default 4), then any
     *    of ``threshold``, ``ratio``, ``attack``, ``release``, ``makeup``,
     *    ``drive`` and ``level`` (every band, or one band with its number
     *    after the word: ``threshold2``), ``freq1`` .. ``freq4`` (the
     *    crossovers), ``output``, ``detector``, ``bypass``, ``meters`` and
     *    ``meterrate`` with a value. It is saved with the patch and the pins
     *    are rebuilt from it; a later message is not saved.
     *
     *  Not ported from the OctiMax objects: the input AGC, noise gate, final
     *  limiter, per-band limiters and clippers, gating and freeze, progressive
     *  release, bass enhancement, the stereo spatial enhancer, left/right
     *  output trims and presets (``choosepreset``, ``savesettings``; the
     *  settings parameter is saved with the patch).
     */
    PATCHER_CLASS(dMultiband, YSE::OBJ::D_MULTIBAND)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief The meter interval until it is set, in ms. */
    static constexpr float DEFAULT_METER_RATE = 100.f;

    /** @brief How many bands (and gain-reduction outlets) the object has. */
    int BandCount() const {
      return static_cast<int>(fn->bands());
    }

    /** @brief The DSP class, for tests. Not while the object renders. */
    DSP::multibandCompressor& Compressor() {
      return *fn;
    }

  private:
    // Rebuild the pins and the DSP class from the settings. Control thread,
    // before the object is wired or published: the constructor and the two
    // parameter callbacks (a list parameter rebuilds the object, #234).
    void ShapePorts();
    // Apply one setting word, from the settings or a message.
    bool ApplyWord(std::string_view word, float value);

    std::vector<std::string> settings;
    std::unique_ptr<DSP::multibandCompressor> fn;
    // The signal handed to the DSP class (it works in place). Sized in
    // ShapePorts.
    MULTICHANNELBUFFER stereo;

    std::array<aFlt, 2> constants;
    aFlt meterRate;
    std::atomic<bool> meters{true};
    std::atomic<bool> meterRequested{false};

    // Audio thread only.
    double samplesSinceReport = 0.0;
    std::array<float, DSP::multibandCompressor::MAX_BANDS> deepest{};
  };

} // namespace PATCHER
} // namespace YSE
