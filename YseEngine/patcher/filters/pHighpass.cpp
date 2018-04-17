#include "pHighpass.h"

using namespace YSE::PATCHER;

#define className pHighpass

CONSTRUCT_DSP() {

  // in 0: audio buffer
  // in 1: frequency (float)

  // out 0: audio output
  ADD_IN_0;
  REG_BUFFER_IN(SetBuffer);

  ADD_IN_1;
  REG_FLOAT_IN(SetFrequency);

  ADD_OUT_BUFFER;

  ADD_PARAM(frequency);

  buffer = nullptr;
  frequency = 0.f;
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