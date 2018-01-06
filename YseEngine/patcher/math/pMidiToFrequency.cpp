#include "pMidiToFrequency.h"
#include "..\pObjectList.hpp"
#include "dsp\math.hpp"

using namespace YSE::PATCHER;

pObject * pMidiToFrequency::Create() { return new pMidiToFrequency(); }

pMidiToFrequency::pMidiToFrequency() {
  // in 0: int or float
  // out 0: audio output

  inputs.emplace_back(PIN_INT | PIN_FLOAT, 0, this);
  outputs.emplace_back(PIN_FLOAT, 0, this);

  inputs[0].SetData(0.f);
}

const char * pMidiToFrequency::Type() const {
  return YSE::OBJ::MIDITOFREQUENCY;
}

void pMidiToFrequency::RequestData() {
  UpdateInputs();

  if (inputs[0].GetCurentDataType() == PIN_INT) {
    outputs[0].SetData(YSE::DSP::MidiToFreq(inputs[0].GetInt()));
  }
  else if (inputs[0].GetCurentDataType() == PIN_FLOAT) {
    outputs[0].SetData(YSE::DSP::MidiToFreq(inputs[0].GetFloat()));
  }
}