#pragma once
#include "..\pObject.h"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(pFrequencyToMidi, YSE::OBJ::FREQUENCYTOMIDI)
      _NO_MESSAGES
      _DO_CALCULATE

      _FLOAT_IN(SetFrequency)
      _INT_IN(SetFreqInt)

    private:
      float frequency;
    };

  }
}