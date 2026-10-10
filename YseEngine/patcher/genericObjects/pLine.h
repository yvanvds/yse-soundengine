
#pragma once

#include "../pObject.h"
#include "../../dsp/ramp.hpp"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(pLine, YSE::OBJ::D_LINE)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetTarget)
    _FLOAT_IN(SetTime)

  private:
    aFlt target;
    aFlt time;
    // `stop` asks the next block to freeze the ramp. `targetSerial` counts the
    // targets received; `stoppedAt` is its value when the last stop arrived, so
    // the object holds while the two are equal (#1019).
    aBool stop;
    aInt targetSerial;
    aInt stoppedAt;
    DSP::ramp ramp;
  };
}
}
