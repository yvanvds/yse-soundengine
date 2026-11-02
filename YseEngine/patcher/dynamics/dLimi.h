#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "../pObject.h"
#include "../../dsp/dynamics/lookaheadLimiter.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~limi``: a brickwall lookahead peak limiter over one or more
     *         linked channels, wrapping ``DSP::lookaheadLimiter`` (issue
     *         #1115).
     *
     *  Ported from Max's ``limi~``. The wrapper only maps the pins, messages
     *  and parameter onto the DSP class.
     *
     *  - Inlet *n*: the signal of channel *n* (unconnected, the last number
     *    sent to it, else 0, #1036). Inlet 0 also takes the settings as
     *    messages: ``threshold <dB>``, ``release <ms>``, ``lookahead
     *    <samples>``, ``preamp <dB>``, ``postamp <dB>``, ``mode <0|1>``,
     *    ``dcblock <0|1>``, ``bypass <0|1>``, and ``clear``.
     *  - Outlet *n*: channel *n* limited, ``lookahead`` samples late.
     *  - Parameter: ``settings``, the channel count (1-64, default 1) and the
     *    delay-line size in samples (default 512, the longest lookahead),
     *    then any of the setting words with a value. It is saved with the
     *    patch; a later message is not.
     *
     *  Settings reach the audio thread through atomics, applied to the DSP
     *  class at the start of each block.
     */
    PATCHER_CLASS(dLimi, YSE::OBJ::D_LIMI)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief How many channels (inlet / outlet pairs) the object has. */
    int ChannelCount() const {
      return static_cast<int>(fn->channels());
    }

    /** @brief The DSP class, for tests. Not while the object renders. */
    const DSP::lookaheadLimiter& Limiter() const {
      return *fn;
    }

  private:
    // Rebuild the pins and the limiter from the settings. Control thread,
    // before the object is wired or published: the constructor and the two
    // parameter callbacks (a list parameter rebuilds the object, #234).
    void ShapePorts();
    // Apply one setting word, from the settings or a message.
    bool ApplyWord(std::string_view word, float value);

    std::vector<std::string> settings;
    std::unique_ptr<DSP::lookaheadLimiter> fn;
    std::vector<DSP::buffer*> ins;

    std::array<aFlt, DSP::lookaheadLimiter::MAX_CHANNELS> constants;
    aFlt threshold;
    aFlt release;
    aFlt preamp;
    aFlt postamp;
    aInt lookahead;
    aInt mode;
    std::atomic<bool> dcblock{false};
    std::atomic<bool> bypass{false};
    std::atomic<bool> clearRequested{false};
  };

} // namespace PATCHER
} // namespace YSE
