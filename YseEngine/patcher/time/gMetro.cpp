
#include "gMetro.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gMetro

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(Toggle);

  ADD_IN_1;
  REG_FLOAT_IN(SetFloatPeriod);
  REG_INT_IN(SetIntPeriod);

  ADD_OUT_BANG;

  ADD_PARAM(period);

  period = 1000;
  id = 0;

  ADD_DESCRIPTION("Periodic bang generator. Once toggled on, emits a bang every 'period' "
                  "milliseconds (and immediately on start). Toggle off to stop.");
  ADD_CATEGORY(pCategory::TIME);
  INLET_DOC(0, "on/off", "Non-zero int starts the metronome; 0 stops it.", "0 or 1");
  INLET_DOC(1, "period",
            "Sets the bang interval in milliseconds. Retimes a running metronome immediately, "
            "without restarting it.",
            "1+ ms");
  OUTLET_DOC(0, "out", "Periodic bang.", "");
  PARAM_DOC("period", "1000",
            "Interval in milliseconds. Re-editing it while the metronome runs takes effect from "
            "the next bang.",
            "1+ ms");
}

// A period change must reach a *running* metro, not just the next start
// (issue #625). Two routes write `period` and neither can be dropped:
//
//  - the cold inlet runs on the caller's thread, so it reschedules eagerly and
//    the new interval is in force before the call returns;
//  - a live SetParams re-parse stores straight into `period` from the audio
//    thread (patcherImplementation::ApplyPendingParams) and notifies nobody,
//    so Bang() re-asserts the interval on every tick as well. That route
//    therefore lands one tick later — the running cycle plays out at the old
//    interval and every cycle after it uses the new one.
//
// Neither path may restart the metro: rescheduling keeps the phase, so a tempo
// tweak does not re-trigger whatever the bang drives.

timerThread::millisec gMetro::Interval() const {
  const Int ms = period.load();
  return ms > 0 ? static_cast<timerThread::millisec>(ms) : 1;
}

void gMetro::ApplyPeriod() {
  const timerThread::timerID running = id.load();
  if (running != 0) TimerThread().SetPeriod(running, Interval());
}

INT_IN(Toggle) {
  // Stop first on either edge: a restart while running must not leak the
  // previous timer (the double-start case).
  const timerThread::timerID running = id.exchange(0);
  if (running != 0) {
    TimerThread().ClearTimer(running);
  }

  if (value != 0) {
    const timerThread::millisec ms = Interval();
    id.store(TimerThread().Add(ms, ms, std::bind(&gMetro::Bang, this)));
    // send first bang instantly
    Bang();
  }
}

INT_IN(SetIntPeriod) {
  period = value;
  ApplyPeriod();
}

FLOAT_IN(SetFloatPeriod) {
  period = (int)value;
  ApplyPeriod();
}

void gMetro::Bang() {
  // Timer thread. Picks up an interval stored by the parameter path, which has
  // no way to call in here itself. SetPeriod is safe from inside the callback:
  // the worker holds no lock across it, and the timer is not queued at this
  // point, so this only updates the value the worker reschedules with.
  ApplyPeriod();
  outputs[0].SendBang(T_GUI);
}

gMetro::~gMetro() {
  const timerThread::timerID running = id.exchange(0);
  if (running != 0) {
    // Pre-existing defect, left alone here on purpose: lowercase `timerThread`
    // is the class, so this clears the id on a throwaway instance instead of
    // the singleton. Tracked as #663 — it needs its own regression test.
    timerThread().ClearTimer(running);
  }
}
