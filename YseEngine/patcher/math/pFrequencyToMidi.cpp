#include "pFrequencyToMidi.h"
#include "..\pObjectList.hpp"
#include "dsp\math.hpp"

using namespace YSE::PATCHER;

YSE::PATCHER::pFrequencyToMidi::pFrequencyToMidi()
{
  // in 0: int or float
  // out 0: audio output

  inputs.emplace_back(PIN_INT | PIN_FLOAT, 0, this);
  outputs.emplace_back(PIN_FLOAT, 0, this);

  inputs[0].SetData(0.f);
}

const char * YSE::PATCHER::pFrequencyToMidi::Type() const
{
  return YSE::OBJ::FREQUENCYTOMIDI;
}

void YSE::PATCHER::pFrequencyToMidi::RequestData()
{
  UpdateInputs();

  if (inputs[0].GetCurentDataType() == PIN_INT) {
    outputs[0].SetData(YSE::DSP::FreqToMidi(inputs[0].GetInt()));
  }
  else if (inputs[0].GetCurentDataType() == PIN_FLOAT) {
    outputs[0].SetData(YSE::DSP::FreqToMidi(inputs[0].GetFloat()));
  }
}

pObject * YSE::PATCHER::pFrequencyToMidi::Create()
{
  return new pFrequencyToMidi();
}
