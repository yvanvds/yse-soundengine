#pragma once
#include "..\pObject.h"
#include <atomic>
#include "TimerThread.h"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(gMetro, YSE::OBJ::G_METRO)
      _NO_MESSAGES
      _NO_CALCULATE

      _INT_IN(Toggle)
      _INT_IN(SetIntPeriod)
      _FLOAT_IN(SetFloatPeriod)

      void Bang();

    ~gMetro();
private:
  aInt period;
  timerThread::timerID id;
  };
}
}