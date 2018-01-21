#include "pMidiToFrequency.h"
#include "..\pObjectList.hpp"
#include "dsp\math.hpp"

using namespace YSE::PATCHER;

#define className pMidiToFrequency

CONSTRUCT() {
  
  midiValue = 0.f;

  ADD_INLET_0;
  REG_FLOAT_FUNC(pMidiToFrequency::SetMidi);
  
  ADD_OUTLET_FLOAT;
}

FLOAT_IN_FUNC(pMidiToFrequency::SetMidi) {
  midiValue = value;
}

CALC_FUNC() {
  outputs[0].SendFloat(YSE::DSP::MidiToFreq(midiValue));
}