#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/spectral/streamingFft.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~fft``: a Fourier transform of a signal, frame by frame, put
     *         out as signals one bin per sample, wrapping
     *         ``DSP::streamingFft`` (issue #1160).
     *
     *  Ported from MSP's ``fft~``. The wrapper only maps the pins, messages
     *  and parameters onto the DSP class.
     *
     *  - Inlet 0: the real input, a signal; unconnected it reads the last
     *    number sent to it, else 0 (#1036). Also takes ``phase <n>`` and
     *    ``offset <n>``, which set the offset (not saved).
     *  - Inlet 1: the imaginary input, likewise.
     *  - Outlets: the real part, the imaginary part, and the sync signal
     *    (the index of the bin put out, 0 outside an output period).
     *  - Parameters: ``size`` (default 512), ``interval`` (default: the
     *    size) and ``offset`` (default 0), MSP's arguments and its
     *    ``fftsize`` / ``interval`` / ``offset`` attributes, fitted as the
     *    DSP class fits them. Changing one rebuilds the object, so the frame
     *    storage is allocated on the control thread.
     *
     *  Dropped from MSP: ``float32`` and ``legacy`` (one single-precision
     *  implementation), and the real FFT MSP runs when only the left pins
     *  are connected (an unconnected imaginary inlet reads 0, which gives
     *  the same result).
     */
    PATCHER_CLASS(dFft, YSE::OBJ::D_FFT)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)

  private:
    DSP::streamingFft fn;

    int size = static_cast<int>(DSP::streamingTransform::DEFAULT_SIZE);
    int interval = 0;
    int offset = 0;

    // The numbers unconnected inlets read: real, imaginary.
    std::atomic<float> numbers[2] = {{0.f}, {0.f}};
    // A new offset from a message, -1 when there is none.
    std::atomic<int> pendingOffset{-1};
  };

  /**
   *  @brief ``~ifft``: an inverse Fourier transform of a stream of bins,
   *         frame by frame, wrapping ``DSP::streamingInverseFft`` (issue
   *         #1160).
   *
   *  Ported from MSP's ``ifft~``, with the same pins, messages and
   *  parameters as ``~fft``: the real and imaginary bins in (bin ``k`` at
   *  sample ``k`` of a frame), the real and imaginary output and the sync
   *  signal out. The output is not divided by the size. After a ``~fft``
   *  with the same arguments (an offset ``size`` later when the interval is
   *  larger than the size), it gives back the input ``size`` times larger,
   *  ``2 * size`` samples later.
   */
  PATCHER_CLASS(dIfft, YSE::OBJ::D_IFFT)
  _DO_MESSAGES
  _DO_CALCULATE

  _PARM_CLEAR
  _PARM_PARSE

  _FLOAT_IN(SetFloat)
  _INT_IN(SetInt)

private:
  DSP::streamingInverseFft fn;

  int size = static_cast<int>(DSP::streamingTransform::DEFAULT_SIZE);
  int interval = 0;
  int offset = 0;

  std::atomic<float> numbers[2] = {{0.f}, {0.f}};
  std::atomic<int> pendingOffset{-1};
};

} // namespace PATCHER
} // namespace YSE
