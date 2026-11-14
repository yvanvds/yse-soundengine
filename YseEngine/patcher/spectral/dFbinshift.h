#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/buffer.hpp"
#include "../../dsp/spectral/spectralFreqShift.hpp"
#include "spectralFrameStore.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~fbinshift``: a frequency shifter for the spectral frames of
     *         a ``~pfft``, wrapping ``DSP::spectralFreqShift`` (issue #1165).
     *
     *  Ported from MSP's ``fbinshift~``. The wrapper only maps the pins,
     *  messages and parameters onto the DSP class:
     *
     *  - Inside a ``~pfft`` it is frame-wide (SpectralFrameWide, issue
     *    #1280): a shift down needs bins of a later pass, so the object
     *    takes in the whole frame (the consume step, through a
     *    spectralFrameStore), shifts it once at its first emit step and
     *    hands it out a pass at a time. What follows it runs in the next
     *    stage of the ~pfft's frame.
     *  - Outside a ``~pfft`` there are no spectral frames to shift, and it
     *    passes its real and imaginary inputs through unchanged (MSP's
     *    ``fbinshift~`` does nothing there).
     *
     *  - Inlet 0: the real parts (``~fftin``'s outlet 0), a signal; also
     *    ``clip <0|1>`` (drop the bins moved out of 0 to Nyquist instead of
     *    folding them back; default 0). Not saved.
     *  - Inlet 1: the imaginary parts (``~fftin``'s outlet 1), a signal.
     *  - Inlet 2: the frequency shift in Hz, a signal (read at the first bin
     *    of each frame) or a number while unconnected.
     *  - Outlets: the real and imaginary parts of the shifted frame, for
     *    ``~fftout``.
     *  - Parameters: ``shift`` (MSP's frequency-shift argument, in Hz,
     *    default 0) and ``bins`` (the largest frame kept, default 4096).
     *    Changing ``bins`` rebuilds the object, so the storage is allocated
     *    on the control thread.
     *
     *  Unconnected signal inlets read their numbers (#1036): 0 for the real
     *  and imaginary parts, the shift for inlet 2.
     */
    PATCHER_CLASS(dFbinshift, YSE::OBJ::D_FBINSHIFT)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)

    bool SpectralFrameWide() const override {
      return true;
    }

  private:
    DSP::spectralFreqShift fn;
    spectralFrameStore store{2, 2};
    DSP::buffer through[2];

    float shift = 0.f;
    int bins = static_cast<int>(spectralFrameStore::DEFAULT_CAPACITY);

    std::atomic<float> numbers[3] = {{0.f}, {0.f}, {0.f}};
    std::atomic<bool> clipping{false};
    float frameShift = 0.f; // audio thread: the shift of the frame being taken in
  };

} // namespace PATCHER
} // namespace YSE
