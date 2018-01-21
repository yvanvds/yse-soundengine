#pragma once
#include "..\pObject.h"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(pDac, YSE::OBJ::D_DAC)
      pDac(int channels);
      _NO_PARAMS
      _NO_MESSAGES
      _HAS_DSP_RESET
      _NO_CALCULATE

      BUFFER_IN(SetBuffer)

      // used by patcher implementation
      YSE::DSP::buffer * GetBuffer(int channel);

    private:
      std::vector<DSP::buffer *> channels;
    };

  }
}