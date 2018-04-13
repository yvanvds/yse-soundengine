#pragma once
#include "..\pObject.h"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(pDac, YSE::OBJ::D_DAC)
      pDac(int channels);
      _NO_MESSAGES
      _DO_RESET
      _NO_CALCULATE

      _BUFFER_IN(SetBuffer)

      // used by patcher implementation
      YSE::DSP::buffer * GetBuffer(unsigned int channel);

    private:
      std::vector<DSP::buffer *> channels;
    };

  }
}