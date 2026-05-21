
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

  ADD_DESCRIPTION("Periodic bang generator. Once toggled on, emits a bang every 'period' milliseconds (and immediately on start). Toggle off to stop.");
  ADD_CATEGORY(pCategory::TIME);
  INLET_DOC(0, "on/off", "Non-zero int starts the metronome; 0 stops it.", "0 or 1");
  INLET_DOC(1, "period", "Sets the bang interval in milliseconds.", "1+ ms");
  OUTLET_DOC(0, "out", "Periodic bang.", "");
  PARAM_DOC("period", "1000", "Initial interval in milliseconds.", "1+ ms");
}

INT_IN(Toggle) {
  if (value == 0) {
    // stop
    if (id != 0) {
      TimerThread().ClearTimer(id);
      id = 0;
    }
  }
  else {
    // start
    if (id != 0) {
      TimerThread().ClearTimer(id);
    }
    id = TimerThread().Add(period, period, std::bind(&gMetro::Bang, this));
    // send first bang instantly
    Bang();
  }
}

INT_IN(SetIntPeriod) {
  period = value;
}

FLOAT_IN(SetFloatPeriod) {
  period = (int)value;
}

void gMetro::Bang() {
  outputs[0].SendBang(T_GUI);
}

gMetro::~gMetro() {
  if (id != 0) {
    timerThread().ClearTimer(id);
  }
}
