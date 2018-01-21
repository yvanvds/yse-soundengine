#include "pSine.h"

using namespace YSE::PATCHER;

#define className pSine

CONSTRUCT_DSP() 
{
  frequency = 440.f;
  freqBuffer = nullptr;

  ADD_INLET_0;
  REG_BUFFER_FUNC(pSine::SetFrequencyBuffer);
  REG_FLOAT_FUNC(pSine::SetFrequency);

  ADD_OUTLET_BUFFER;
}

PARAMS_FUNC() {
  if (pos == 0) frequency = value;
}

RESET_FUNC() // {
  freqBuffer = nullptr;
}

FLOAT_IN_FUNC(pSine::SetFrequency) {
  frequency = value;
}

BUFFER_IN_FUNC(pSine::SetFrequencyBuffer) {
  freqBuffer = buffer;
}

CALC_FUNC() {
  if (freqBuffer != nullptr) {
    outputs[0].SendBuffer(&sine(*freqBuffer));
  }
  else {
    outputs[0].SendBuffer(&sine(frequency));
  }
}
