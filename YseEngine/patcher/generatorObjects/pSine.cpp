
#include "pSine.h"
#include "../../implementations/logImplementation.h"
#include <string>

using namespace YSE::PATCHER;

#define className pSine

CONSTRUCT_DSP()
{
  frequency = 440.f;
  freqBuffer = nullptr;

  ADD_IN_0;
  REG_BUFFER_IN(SetFrequencyBuffer);
  REG_FLOAT_IN(SetFrequency);

  ADD_OUT_BUFFER;

  ADD_PARAM(frequency);

  ADD_DESCRIPTION("Audio-rate sine oscillator. Frequency can be set with a float or modulated with a DSP buffer.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "freq", "Oscillator frequency in Hz. Accepts a DSP buffer (FM) or a float.", "0-20000 Hz");
  OUTLET_DOC(0, "out", "Sine wave audio output.", "-1.0 to 1.0");
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
    outputs[0].SendBuffer(&sine(*freqBuffer), thread);
  }
  else {
		DSP::buffer & buffer = sine(frequency);
		outputs[0].SendBuffer(&buffer, thread);
  }
}
