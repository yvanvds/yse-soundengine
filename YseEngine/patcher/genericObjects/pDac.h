#pragma once
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    // Audio output of a patcher graph: the DSP sink that collects the graph's
    // per-channel result, which the patcher hands to its host. The mirror of
    // pAdc.
    PATCHER_CLASS(pDac, YSE::OBJ::D_DAC)
    pDac(int channels);
    _NO_MESSAGES
    _DO_RESET
    _NO_CALCULATE

    _BUFFER_IN(SetBuffer)

    // used by patcher implementation
    YSE::DSP::buffer* GetBuffer(unsigned int channel);

  private:
    // Shared construction body for the default (registry / metadata) and the
    // channel-count constructors.
    void build(int channels);

    std::vector<DSP::buffer*> channels;
  };

}
}
