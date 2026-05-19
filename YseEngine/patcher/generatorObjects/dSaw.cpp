#include "dSaw.h"

using namespace YSE::PATCHER;

#define className dSaw

CONSTRUCT_DSP()
{
  frequency = 440.f;
  freqBuffer = nullptr;

  ADD_IN_0;
  REG_BUFFER_IN(SetFrequencyBuffer);
  REG_FLOAT_IN(SetFrequency);

  ADD_OUT_BUFFER;

  ADD_PARAM(frequency);

  ADD_DESCRIPTION("Audio-rate sawtooth oscillator. Frequency can be set with a float or modulated with a DSP buffer.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "freq", "Oscillator frequency in Hz. Accepts a DSP buffer (FM) or a float.", "0-20000 Hz");
  OUTLET_DOC(0, "out", "Sawtooth wave audio output.", "-1.0 to 1.0");
  PARAM_DOC("frequency", "440", "Initial frequency in Hz.", "0-20000 Hz");
}

RESET() // {
freqBuffer = nullptr;
}

FLOAT_IN(SetFrequency) {
  frequency = value;
}

BUFFER_IN(SetFrequencyBuffer) {
  freqBuffer = buffer;
}

CALC() {
  if (freqBuffer != nullptr) {
    outputs[0].SendBuffer(&saw(*freqBuffer), thread);
  }
  else {
    outputs[0].SendBuffer(&saw(frequency), thread);
  }
}
