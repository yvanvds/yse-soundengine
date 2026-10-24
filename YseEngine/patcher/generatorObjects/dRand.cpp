#include "dRand.h"

using namespace YSE::PATCHER;

#define className dRand

CONSTRUCT_DSP() {
  frequency = 0.f;

  // in 0: frequency (signal, float or int)
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFrequency);
  REG_INT_IN(SetFrequencyInt);

  // out 0: the random signal
  ADD_OUT_BUFFER;

  ADD_PARAM(frequency);

  ADD_DESCRIPTION("Band-limited random signal: picks a new random value at a given frequency and "
                  "glides to it in a straight line. Smooth random modulation for vibrato drift or "
                  "a wandering filter.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "freq",
            "How often a new random value is picked, in Hz: a signal, or a number used while no "
            "signal is connected. The sign is ignored; 0 holds the current value.",
            "0 to the sample rate");
  OUTLET_DOC(0, "out", "The random signal, starting at 0.", "-1 to 1");
  PARAM_DOC("frequency", "0", "Frequency in Hz used while no signal is connected.",
            "0 to the sample rate");
}

FLOAT_IN(SetFrequency) {
  frequency = value;
}

INT_IN(SetFrequencyInt) {
  frequency = static_cast<float>(value);
}

CALC() {
  // A connected frequency signal wins over the number (issue #1036).
  DSP::buffer& out =
      inputs[0].IsConstant() ? random(frequency) : random(inputs[0].Signal(frequency));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
