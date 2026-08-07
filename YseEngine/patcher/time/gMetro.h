
#pragma once

#include "../pObject.h"
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

    ~gMetro() override;

  private:
    // Push the current `period` onto the running timer, if any. No-op when the
    // metro is stopped or the interval has not moved (issue #625).
    void ApplyPeriod();

    // `period` clamped to the documented 1+ ms range, in the timer's unit.
    timerThread::millisec Interval() const;

    aInt period;
    // Written by the toggle inlet on the control thread, read by Bang() on the
    // timer thread — atomic so the live reschedule is not a data race. Ids are
    // handed out monotonically and never recycled, so a stale id read here can
    // only miss, never hit the wrong timer.
    std::atomic<timerThread::timerID> id;
  };
}
}
