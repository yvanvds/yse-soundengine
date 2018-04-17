#include "pFrequencyToMidi.h"
#include "..\pObjectList.hpp"
#include "dsp\math.hpp"

using namespace YSE::PATCHER;

#define className pFrequencyToMidi

CONSTRUCT()
{
  frequency = 0.f;

  ADD_IN_0;
  REG_FLOAT_IN(SetFrequency);
  REG_INT_IN(SetFreqInt);

  ADD_OUT_FLOAT;
}

FLOAT_IN(SetFrequency) {
  frequency = value;
}

INT_IN(SetFreqInt) {
  frequency = (float)value;
}

CALC()
{
  outputs[0].SendFloat(YSE::DSP::FreqToMidi(frequency), thread);
}