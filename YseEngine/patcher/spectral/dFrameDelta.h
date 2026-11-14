#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/spectral/frameDelta.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~framedelta``: the change of every bin since the previous
     *         frame, wrapping ``DSP::frameDelta`` (issue #1161).
     *
     *  Ported from MSP's ``framedelta~``. The wrapper only maps the pins,
     *  messages and parameter onto the DSP class, and tells it which bins a
     *  block holds:
     *
     *  - Inside a ``~pfft`` (``CurrentSpectralPass()`` not null), a block is
     *    a pass, bins ``firstBin`` .. ``+ count - 1`` of the spectral frame.
     *  - Anywhere else, a block is a frame of its own length, as MSP treats
     *    the signal vector outside a ``pfft~``.
     *
     *  - Inlet 0: the frames, a signal; unconnected it reads the last number
     *    sent to it, else 0 (#1036). Also takes ``clear``, which zeroes the
     *    stored frame (inside a ``~pfft`` at the start of the next frame, so
     *    a frame is never half cleared). Not saved.
     *  - Outlet: each bin minus the same bin of the frame before.
     *  - Parameter: ``bins``, the largest frame kept (default 4096: a half
     *    spectrum of an 8192-point FFT). Bins past it put out 0. Changing it
     *    rebuilds the object, so the storage is allocated on the control
     *    thread.
     */
    PATCHER_CLASS(dFramedelta, YSE::OBJ::D_FRAMEDELTA)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)

  private:
    DSP::frameDelta fn;

    int bins = static_cast<int>(DSP::frameDelta::DEFAULT_CAPACITY);

    std::atomic<float> number{0.f};
    std::atomic<bool> clearPending{false};
  };

  /**
   *  @brief ``~frameaccum``: a running sum of every bin over successive
   *         frames, wrapping ``DSP::frameAccumulate`` (issue #1161).
   *
   *  Ported from MSP's ``frameaccum~``. Frames, the inlet, ``clear`` and the
   *  ``bins`` parameter as ``~framedelta``.
   *
   *  - Outlet: each bin plus the sum of the same bin over the frames
   *    before.
   *  - Parameter ``wrap`` (MSP's phase-wrap flag, the first argument):
   *    nonzero wraps the sums into -pi .. pi, as ``~phasewrap`` would after
   *    the object, while keeping them small. Default 0. ``bins`` is the
   *    second argument.
   */
  PATCHER_CLASS(dFrameaccum, YSE::OBJ::D_FRAMEACCUM)
  _DO_MESSAGES
  _DO_CALCULATE

  _PARM_CLEAR
  _PARM_PARSE

  _FLOAT_IN(SetFloat)
  _INT_IN(SetInt)

private:
  DSP::frameAccumulate fn;

  int wrap = 0;
  int bins = static_cast<int>(DSP::frameAccumulate::DEFAULT_CAPACITY);

  std::atomic<float> number{0.f};
  std::atomic<bool> clearPending{false};
};

} // namespace PATCHER
} // namespace YSE
