#include "pBandpass.h"
#include "../pObjectList.hpp"


using namespace YSE::PATCHER;

#define className pBandpass

CONSTRUCT_DSP() {

  // in 0: audio buffer
  // in 1: frequency (float)
  // in 2: Q (float)

  // out 0: audio output
  ADD_IN_0;
  REG_BUFFER_IN(SetBuffer);

  ADD_IN_1;
  REG_FLOAT_IN(SetFrequency);

  ADD_IN_2;
  REG_FLOAT_IN(SetQ);

  ADD_OUT_BUFFER;

  ADD_PARAM(frequency);
  ADD_PARAM(Q);

  buffer = nullptr;
  frequency = Q = 0.f;

  ADD_DESCRIPTION("Bandpass filter. Passes a frequency band centered on 'frequency' with bandwidth shaped by 'Q' (resonance).");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "in", "Audio input buffer.", "-1.0 to 1.0");
  INLET_DOC(1, "freq", "Center frequency in Hz.", "0-20000 Hz");
  INLET_DOC(2, "Q", "Resonance / inverse bandwidth — higher narrows the band.", "0.1-100");
  OUTLET_DOC(0, "out", "Filtered audio output.", "-1.0 to 1.0");
  PARAM_DOC("frequency", "0", "Initial center frequency in Hz.", "0-20000 Hz");
  PARAM_DOC("Q", "0", "Initial Q / resonance.", "0.1-100");
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

FLOAT_IN(SetQ) {
  Q = value;
}

CALC() {
  if (buffer == nullptr) return;
  filter.set(frequency, Q);
  outputs[0].SendBuffer(&filter(*buffer), thread);
}
