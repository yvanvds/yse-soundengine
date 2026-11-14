#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/spectral/frameAverage.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~frameaverage``: the running average of every bin over the
     *         last frames, wrapping ``DSP::frameAverage`` (issue #1162).
     *
     *  Ported from MSP's ``frameaverage~``. The wrapper only maps the pins,
     *  messages and parameters onto the DSP class, and tells it which bins a
     *  block holds:
     *
     *  - Inside a ``~pfft`` (``CurrentSpectralPass()`` not null), a block is
     *    a pass, bins ``firstBin`` .. ``+ count - 1`` of the spectral frame.
     *  - Anywhere else, the class's block overload cuts the stream into
     *    frames of ``framesize`` samples, or with ``framesize`` 0 makes every
     *    block a frame, as MSP treats the signal vector outside a ``pfft~``.
     *
     *  - Inlet 0: the frames, a signal; unconnected it reads the last number
     *    sent to it, else 0 (#1036). Also takes ``clear``, which zeroes the
     *    stored frames (inside a ``~pfft`` at the start of the next frame,
     *    so a frame is never half cleared; elsewhere at the next block). Not
     *    saved.
     *  - Outlet: each bin's mean over the current and previous frames.
     *  - Parameters: ``framecount`` (MSP's attribute, default 4, 1-256),
     *    ``bins`` (the largest frame kept, default 4096) and ``framesize``
     *    (MSP's attribute, outside a ``~pfft`` only, default 0). Changing
     *    one rebuilds the object, so the storage is allocated on the control
     *    thread.
     */
    PATCHER_CLASS(dFrameaverage, YSE::OBJ::D_FRAMEAVERAGE)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)

  private:
    DSP::frameAverage fn;

    int framecount = static_cast<int>(DSP::frameAverage::DEFAULT_FRAMES);
    int bins = static_cast<int>(DSP::frameAverage::DEFAULT_CAPACITY);
    int framesize = 0;

    std::atomic<float> number{0.f};
    std::atomic<bool> clearPending{false};
  };

  /**
   *  @brief ``~framesmooth``: a running average along every frame that
   *         never reaches into the next, wrapping ``DSP::frameSmooth``
   *         (issue #1162).
   *
   *  Ported from MSP's ``framesmooth~``. Frames, the inlet, ``clear`` and
   *  ``framesize`` as ``~frameaverage``.
   *
   *  - Outlet: each value's mean with the ``smoothness - 1`` values before
   *    it in the same frame (fewer at the start of a frame).
   *  - Parameters: ``smoothness`` (MSP's attribute, default 3, 1-1024) and
   *    ``framesize``. Changing one rebuilds the object.
   */
  PATCHER_CLASS(dFramesmooth, YSE::OBJ::D_FRAMESMOOTH)
  _DO_MESSAGES
  _DO_CALCULATE

  _PARM_CLEAR
  _PARM_PARSE

  _FLOAT_IN(SetFloat)
  _INT_IN(SetInt)

private:
  DSP::frameSmooth fn;

  int smoothness = static_cast<int>(DSP::frameSmooth::DEFAULT_SMOOTHNESS);
  int framesize = 0;

  std::atomic<float> number{0.f};
  std::atomic<bool> clearPending{false};
};

} // namespace PATCHER
} // namespace YSE
