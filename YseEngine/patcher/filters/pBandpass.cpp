#include "pBandpass.h"
#include "..\pObjectList.hpp"


using namespace YSE::PATCHER;

pObject * pBandpass::Create() { return new pBandpass(); }

pBandpass::pBandpass() {

  // in 0: audio buffer
  // in 1: frequency (float)
  // in 2: Q (float)

  // out 0: audio output

  inputs.emplace_back(PIN_TYPE::PIN_DSP_BUFFER, 0, this);
  inputs.emplace_back(PIN_TYPE::PIN_FLOAT, 1, this);
  inputs.emplace_back(PIN_TYPE::PIN_FLOAT, 2, this);
  outputs.emplace_back(PIN_TYPE::PIN_DSP_BUFFER, 0, this);

  inputs[1].SetData(1.f);
}

const char * pBandpass::Type() const {
  return YSE::OBJ::BANDPASS;
}

void pBandpass::RequestData() {
  UpdateInputs();

  filter.set(inputs[1].GetFloat(), inputs[2].GetFloat());

  if (inputs[0].IsConnected()) {
    outputs[0].SetData(&filter(*inputs[0].GetBuffer()));
  }
}