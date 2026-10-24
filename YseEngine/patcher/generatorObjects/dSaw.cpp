#include "dSaw.h"
#include "syncTrig.h"

using namespace YSE::PATCHER;

#define className dSaw

CONSTRUCT_DSP() {
  frequency = 440.f;
  threshold = 0.5f;

  // in 0: frequency (signal, float or int), and the synctrig message
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFrequency);
  REG_INT_IN(SetFrequencyInt);

  // in 1: sync (signal)
  ADD_IN_1;
  REG_SIGNAL_IN;

  ADD_OUT_BUFFER;

  ADD_PARAM(frequency);

  ADD_DESCRIPTION("Antialiased sawtooth oscillator, rising from -1 to 1 once per cycle: every "
                  "harmonic, for bright subtractive-synthesis sounds. A sync signal restarts the "
                  "cycle, for hard sync.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "freq",
            "Frequency in Hz: a signal (FM), or a number used while no signal is connected. A "
            "negative frequency runs the ramp backwards. Also takes 'synctrig <x>', the level "
            "the sync signal must rise across.",
            "0-20000 Hz");
  INLET_DOC(1, "sync",
            "Optional sync signal: the cycle restarts whenever it rises across the sync "
            "threshold (0.5 unless set with synctrig), as a ~phasor of another frequency does.",
            "any float");
  OUTLET_DOC(0, "out", "Band-limited sawtooth wave.", "-1.0 to 1.0");
  PARAM_DOC("frequency", "440", "Frequency in Hz used while no signal is connected.", "0-20000 Hz");
}

FLOAT_IN(SetFrequency) {
  frequency = value;
}

INT_IN(SetFrequencyInt) {
  frequency = static_cast<float>(value);
}

// `synctrig <x>`. The text is walked in place, so this allocates nothing on
// whichever thread delivers it.
MESSAGES() {
  (void)value;
  float number = 0.f;
  if (ReadSyncTrig(message, number)) threshold = number;
}

CALC() {
  saw.syncThreshold(threshold);
  // A connected signal wins over the number (issue #1036).
  DSP::buffer* out = nullptr;
  if (!inputs[1].IsConstant())
    out = &saw(inputs[0].Signal(frequency), inputs[1].Signal(0.f));
  else if (!inputs[0].IsConstant())
    out = &saw(inputs[0].Signal(frequency));
  else
    out = &saw(frequency);
  outputs[0].SendBuffer(out, thread);
}

#undef className
