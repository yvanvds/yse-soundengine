#include "pBandpass.h"
#include "..\pObjectList.hpp"


using namespace YSE::PATCHER;

#define className pBandpass

CONSTRUCT_DSP() {

  // in 0: audio buffer
  // in 1: frequency (float)
  // in 2: Q (float)

  // out 0: audio output
  ADD_INLET_0;
  REG_BUFFER_FUNC(pBandpass::SetBuffer);

  ADD_INLET_1;
  REG_FLOAT_FUNC(pBandpass::SetFrequency);

  ADD_INLET_2;
  REG_FLOAT_FUNC(pBandpass::SetQ);

  ADD_OUTLET_BUFFER;

  buffer = nullptr;
  frequency = Q = 0.f;
}

PARAMS_FUNC() {
  switch (pos) {
    case 0: frequency = value; break;
    case 1: Q = value; break;
  }
}

RESET_FUNC() // {
  buffer = nullptr;
}

BUFFER_IN_FUNC(pBandpass::SetBuffer) {
  this->buffer = buffer;
}

FLOAT_IN_FUNC(pBandpass::SetFrequency) {
  frequency = value;
}

FLOAT_IN_FUNC(pBandpass::SetQ) {
  Q = value;
}

CALC_FUNC() {
  filter.set(frequency, Q);
  outputs[0].SendBuffer(&filter(*buffer));
}