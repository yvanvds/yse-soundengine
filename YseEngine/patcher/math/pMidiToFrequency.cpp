#include "pMidiToFrequency.h"
#include "..\pObjectList.hpp"
#include "dsp\math.hpp"

using namespace YSE::PATCHER;

#define className pMidiToFrequency

CONSTRUCT() {
  
  midiValue = 0.f;

  ADD_IN_0;
  REG_FLOAT_IN(pMidiToFrequency::SetMidi);
  
  ADD_OUT_FLOAT;
}

FLOAT_IN(pMidiToFrequency::SetMidi) {
  midiValue = value;
}

CALC() {
  outputs[0].SendFloat(YSE::DSP::MidiToFreq(midiValue));
}