#include "pLowpass.h"

using namespace YSE::PATCHER;

#define className pLowpass

CONSTRUCT_DSP() {

  // in 0: audio buffer
  ADD_IN_0;
  REG_BUFFER_IN(pLowpass::SetBuffer);

  // in 1: frequency (float)
  ADD_IN_1;
  REG_FLOAT_IN(pLowpass::SetFrequency);

  // out 0: audio output
  ADD_OUT_BUFFER;

  ADD_PARAM(frequency);

  buffer = nullptr;
  frequency = 0.f;
}

RESET() // {
buffer = nullptr;
}

BUFFER_IN(pLowpass::SetBuffer) {
  this->buffer = buffer;
}

FLOAT_IN(pLowpass::SetFrequency) {
  frequency = value;
}

CALC() {
  filter.setFrequency(frequency);
  outputs[0].SendBuffer(&filter(*buffer));
}