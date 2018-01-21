#include "pFrequencyToMidi.h"
#include "..\pObjectList.hpp"
#include "dsp\math.hpp"

using namespace YSE::PATCHER;

#define className pFrequencyToMidi

CONSTRUCT()
{
  frequency = 0.f;

  ADD_INLET_0;
  REG_FLOAT_FUNC(pFrequencyToMidi::SetFrequency);

  ADD_OUTLET_FLOAT;
}

FLOAT_IN_FUNC(pFrequencyToMidi::SetFrequency) {
  frequency = value;
}

CALC_FUNC()
{
  outputs[0].SendFloat(YSE::DSP::FreqToMidi(frequency));
}