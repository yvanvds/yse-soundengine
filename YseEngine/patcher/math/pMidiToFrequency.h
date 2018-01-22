#pragma once
#include "..\pObject.h"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(pMidiToFrequency, YSE::OBJ::MIDITOFREQUENCY)
      _NO_MESSAGES
      _DO_CALCULATE

      _FLOAT_IN(SetMidi)

    private:
      float midiValue;
    };

  }
}