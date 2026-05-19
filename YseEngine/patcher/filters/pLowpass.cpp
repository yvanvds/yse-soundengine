#include "pLowpass.h"

using namespace YSE::PATCHER;

#define className pLowpass

CONSTRUCT_DSP() {

  // in 0: audio buffer
  ADD_IN_0;
  REG_BUFFER_IN(SetBuffer);

  // in 1: frequency (float)
  ADD_IN_1;
  REG_FLOAT_IN(SetFrequency);

  // out 0: audio output
  ADD_OUT_BUFFER;

  ADD_PARAM(frequency);

  buffer = nullptr;
  frequency = 0.f;

  ADD_DESCRIPTION("Single-pole lowpass filter. Attenuates content above the cutoff frequency.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "in", "Audio input buffer.", "-1.0 to 1.0");
  INLET_DOC(1, "cutoff", "Cutoff frequency in Hz.", "0-20000 Hz");
  OUTLET_DOC(0, "out", "Filtered audio output.", "-1.0 to 1.0");
  PARAM_DOC("frequency", "0", "Initial cutoff frequency in Hz.", "0-20000 Hz");
}

RESET() // {
buffer = nullptr;
}

BUFFER_IN(SetBuffer) {
  this->buffer = buffer;
}

FLOAT_IN(SetFrequency) {
  frequency = value;
}

CALC() {
  if (buffer == nullptr) return;
  filter.setFrequency(frequency);
  outputs[0].SendBuffer(&filter(*buffer), thread);
}