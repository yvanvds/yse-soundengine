#pragma once
#include "..\pObject.h"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(pMidiToFrequency, YSE::OBJ::MIDITOFREQUENCY)
      _NO_PARAMS
      _NO_MESSAGES
      _HAS_CALCULATE

      FLOAT_IN(SetMidi)

    private:
      float midiValue;
    };

  }
}