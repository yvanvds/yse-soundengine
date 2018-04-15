#include "dClip.h"

using namespace YSE::PATCHER;

#define className dClip

CONSTRUCT_DSP() {
  // in 0: audio buffer
  ADD_IN_0;
  REG_BUFFER_IN(SetBuffer);

  // in 1: low value (float)
  ADD_IN_1;
  REG_FLOAT_IN(SetLow);

  // in 2: high value (float)
  ADD_IN_2;
  REG_FLOAT_IN(SetHigh);

  // out 0: audio output
  ADD_OUT_BUFFER;

  ADD_PARAM(low);
  ADD_PARAM(high);

  buffer = nullptr;
  low = -1.0f;
  high = 1.0f;
}

RESET() // {
buffer = nullptr;
}

BUFFER_IN(SetBuffer) {
  this->buffer = buffer;
}

FLOAT_IN(SetLow) {
  low = value;
}

FLOAT_IN(SetHigh) {
  high = value;
}

CALC() {
  clip.set(low, high);
  outputs[0].SendBuffer(&clip(*buffer), thread);
}