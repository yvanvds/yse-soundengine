#include "pSine.h"

using namespace YSE::PATCHER;

#define className pSine

CONSTRUCT_DSP() 
{
  frequency = 440.f;
  freqBuffer = nullptr;

  ADD_IN_0;
  REG_BUFFER_IN(pSine::SetFrequencyBuffer);
  REG_FLOAT_IN(pSine::SetFrequency);

  ADD_OUT_BUFFER;

  ADD_PARAM(frequency);
}

RESET() // {
  freqBuffer = nullptr;
}

FLOAT_IN(pSine::SetFrequency) {
  frequency = value;
}

BUFFER_IN(pSine::SetFrequencyBuffer) {
  freqBuffer = buffer;
}

CALC() {
  if (freqBuffer != nullptr) {
    outputs[0].SendBuffer(&sine(*freqBuffer));
  }
  else {
    outputs[0].SendBuffer(&sine(frequency));
  }
}
