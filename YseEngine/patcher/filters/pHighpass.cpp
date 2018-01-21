#include "pHighpass.h"

using namespace YSE::PATCHER;

#define className pHighpass

CONSTRUCT_DSP() {

  // in 0: audio buffer
  // in 1: frequency (float)

  // out 0: audio output
  ADD_INLET_0;
  REG_BUFFER_FUNC(pHighpass::SetBuffer);

  ADD_INLET_1;
  REG_FLOAT_FUNC(pHighpass::SetFrequency);

  ADD_OUTLET_BUFFER;

  buffer = nullptr;
  frequency = 0.f;
}

PARAMS_FUNC() {
  if (pos == 0) frequency = value;
}

RESET_FUNC() // {
  buffer = nullptr;
}

BUFFER_IN_FUNC(pHighpass::SetBuffer) {
  this->buffer = buffer;
}

FLOAT_IN_FUNC(pHighpass::SetFrequency) {
  frequency = value;
}

CALC_FUNC() {
  filter.setFrequency(frequency);
  outputs[0].SendBuffer(&filter(*buffer));
}