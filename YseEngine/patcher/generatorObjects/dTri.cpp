#include "dTri.h"
#include "syncTrig.h"

using namespace YSE::PATCHER;

#define className dTri

CONSTRUCT_DSP() {
  frequency = 0.f;
  duty = 0.5f;
  threshold = 0.5f;

  // in 0: frequency (signal, float or int), and the synctrig message
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFrequency);
  REG_INT_IN(SetFrequencyInt);

  // in 1: duty cycle (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetDuty);
  REG_INT_IN(SetDutyInt);

  // in 2: sync (signal)
  ADD_IN_2;
  REG_SIGNAL_IN;

  ADD_OUT_BUFFER;

  ADD_PARAM(frequency);
  ADD_PARAM(duty);

  ADD_DESCRIPTION("Antialiased triangle oscillator. A duty cycle of 0.5 is a symmetric triangle; "
                  "towards 0.05 or 0.95 it leans into a sawtooth and brightens. A sync signal "
                  "restarts the cycle, for hard sync.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "freq",
            "Frequency in Hz: a signal (FM), or a number used while no signal is connected. Also "
            "takes 'synctrig <x>', the level the sync signal must rise across.",
            "0-20000 Hz");
  INLET_DOC(1, "duty",
            "Duty cycle: the part of each cycle spent rising. A signal, or a number used while "
            "no signal is connected. Wrapped into 0-1, then held within 0.05-0.95.",
            "0.05-0.95");
  INLET_DOC(2, "sync",
            "Optional sync signal: the cycle restarts whenever it rises across the sync "
            "threshold (0.5 unless set with synctrig), as a ~phasor of another frequency does.",
            "any float");
  OUTLET_DOC(0, "out", "Band-limited triangle wave.", "-1.0 to 1.0");
  PARAM_DOC("frequency", "0", "Frequency in Hz used while no signal is connected.", "0-20000 Hz");
  PARAM_DOC("duty", "0.5", "Duty cycle used while no signal is connected.", "0.05-0.95");
}

FLOAT_IN(SetFrequency) {
  frequency = value;
}

INT_IN(SetFrequencyInt) {
  frequency = static_cast<float>(value);
}

FLOAT_IN(SetDuty) {
  duty = value;
}

INT_IN(SetDutyInt) {
  duty = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  float number = 0.f;
  if (ReadSyncTrig(message, number)) threshold = number;
}

CALC() {
  tri.syncThreshold(threshold);
  // A connected signal wins over the number (issue #1036).
  DSP::buffer* out = nullptr;
  if (!inputs[2].IsConstant()) {
    out = &tri(inputs[0].Signal(frequency), inputs[1].Signal(duty), inputs[2].Signal(0.f));
  } else if (!inputs[1].IsConstant()) {
    out = &tri(inputs[0].Signal(frequency), inputs[1].Signal(duty));
  } else {
    tri.duty(duty);
    out = inputs[0].IsConstant() ? &tri(frequency) : &tri(inputs[0].Signal(frequency));
  }
  outputs[0].SendBuffer(out, thread);
}

#undef className
