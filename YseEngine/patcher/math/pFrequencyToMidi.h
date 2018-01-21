#pragma once
#include "..\pObject.h"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(pFrequencyToMidi, YSE::OBJ::FREQUENCYTOMIDI)
      _NO_PARAMS
      _NO_MESSAGES
      _HAS_CALCULATE

      FLOAT_IN(SetFrequency)

    private:
      float frequency;
    };

  }
}