#include "pSine.h"

using namespace YSE::PATCHER;

#define className pSine

CONSTRUCT_DSP() 
{
  frequency = 440.f;
  freqBuffer = nullptr;

  ADD_IN_0;
  REG_BUFFER_IN(SetFrequencyBuffer);
  REG_FLOAT_IN(SetFrequency);

  ADD_OUT_BUFFER;

  ADD_PARAM(frequency);
}

RESET() // {
  freqBuffer = nullptr;
}

FLOAT_IN(SetFrequency) {
  frequency = value;
}

BUFFER_IN(SetFrequencyBuffer) {
  freqBuffer = buffer;
}

CALC() {
  if (freqBuffer != nullptr) {
    outputs[0].SendBuffer(&sine(*freqBuffer), thread);
  }
  else {
    outputs[0].SendBuffer(&sine(frequency), thread);
  }
}
