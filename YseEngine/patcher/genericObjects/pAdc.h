#pragma once
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    // Audio input into a patcher graph: the mirror of pDac. Where pDac is a
    // DSP sink that collects the graph's per-channel output, pAdc is a DSP
    // start point that injects an externally-supplied, per-channel buffer into
    // the graph. It is the entry point that lets a patcher run as a channel /
    // sound insert (issue #167): the host adapter (DSP::patcherInsert) points
    // each channel at the host's incoming audio just before the graph renders.
    PATCHER_CLASS(pAdc, YSE::OBJ::D_ADC)
    pAdc(int channels);
    _NO_MESSAGES
    _DO_CALCULATE

    // Point one output channel at an externally-owned buffer, borrowed for the
    // duration of the next Calculate(). A null pointer means "no input for this
    // channel this block" (the outlet stays silent). Out-of-range channels are
    // ignored. Called by the host adapter on the audio thread; a plain pointer
    // store, so RT-safe.
    void SetChannelBuffer(unsigned int channel, YSE::DSP::buffer* buffer);

    // Number of audio channels (== number of buffer outlets).
    unsigned int NumChannels() const;

  private:
    // Shared construction body for the default (registry / metadata) and the
    // channel-count constructors.
    void build(int channels);

    // Borrowed, externally-owned input buffers, one per outlet. Deliberately
    // not cleared by ResetDSP: the host adapter sets these immediately before
    // Calculate(), and Calculate() runs ResetDSP() on every object first, so
    // clearing here would wipe the freshly injected input before it is read.
    std::vector<DSP::buffer*> channels;
  };

} // namespace PATCHER
} // namespace YSE
