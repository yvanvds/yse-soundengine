#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/spectral/vectral.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~vectral``: per-index smoothing of successive frames,
     *         wrapping ``DSP::vectral`` (issue #1132).
     *
     *  Ported from MSP's ``vectral~``. The wrapper only maps the pins,
     *  messages and parameter onto the DSP class.
     *
     *  - Inlet 0: the output index, a signal; unconnected it reads the last
     *    number sent to it, else 0 (#1036).
     *  - Inlet 1: the input index, likewise.
     *  - Inlet 2: the frame values, likewise.
     *  - Any inlet takes ``rampsmooth <up> [down]``, ``slide <up> [down]``
     *    (one number sets both), ``deltaclip <lowest> <highest>``,
     *    ``size <n>`` (clamped to 1 .. the ``size`` parameter) and
     *    ``clear``. Not saved.
     *  - Outlet: the smoothed frames.
     *  - Parameter: ``size``, the frame size and capacity (default 512,
     *    clamped to 1 .. ``DSP::vectral::MAXIMUM_SIZE``). Changing it rebuilds
     *    the object, so the frame is allocated on the control thread, never on
     *    an object the audio thread is running.
     *
     *  Numbers and messages reach the audio thread through atomics, applied
     *  to the DSP class at the start of each block: the last mode chosen
     *  with the settings it was last given, the last size, and a clear.
     */
    PATCHER_CLASS(dVectral, YSE::OBJ::D_VECTRAL)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)

  private:
    DSP::vectral fn;

    int size = static_cast<int>(DSP::vectral::DEFAULT_SIZE);

    // The numbers unconnected inlets read: output index, input index, value.
    std::atomic<float> numbers[3] = {{0.f}, {0.f}, {0.f}};

    std::atomic<int> mode{DSP::vectral::RAMPSMOOTH};
    std::atomic<float> rampUp{DSP::rampSmooth::DEFAULT_RAMP};
    std::atomic<float> rampDown{DSP::rampSmooth::DEFAULT_RAMP};
    std::atomic<float> slideUp{1.f};
    std::atomic<float> slideDown{1.f};
    std::atomic<float> clipLowest{0.f};
    std::atomic<float> clipHighest{0.f};
    std::atomic<int> frameSize{static_cast<int>(DSP::vectral::DEFAULT_SIZE)};
    std::atomic<bool> clearPending{false};
  };

} // namespace PATCHER
} // namespace YSE
