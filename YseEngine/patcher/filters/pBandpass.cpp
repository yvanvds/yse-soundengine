#include "pBandpass.h"
#include "..\pObjectList.hpp"


using namespace YSE::PATCHER;

#define className pBandpass

CONSTRUCT_DSP() {

  // in 0: audio buffer
  // in 1: frequency (float)
  // in 2: Q (float)

  // out 0: audio output
  ADD_IN_0;
  REG_BUFFER_IN(pBandpass::SetBuffer);

  ADD_IN_1;
  REG_FLOAT_IN(pBandpass::SetFrequency);

  ADD_IN_2;
  REG_FLOAT_IN(pBandpass::SetQ);

  ADD_OUT_BUFFER;

  ADD_PARAM(frequency);
  ADD_PARAM(Q);

  buffer = nullptr;
  frequency = Q = 0.f;
}

RESET() // {
  buffer = nullptr;
}

BUFFER_IN(pBandpass::SetBuffer) {
  this->buffer = buffer;
}

FLOAT_IN(pBandpass::SetFrequency) {
  frequency = value;
}

FLOAT_IN(pBandpass::SetQ) {
  Q = value;
}

CALC() {
  filter.set(frequency, Q);
  outputs[0].SendBuffer(&filter(*buffer));
}