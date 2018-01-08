#include "pLowpass.h"
#include "..\pObjectList.hpp"

using namespace YSE::PATCHER;

pObject * pLowpass::Create() { return new pLowpass(); }

pLowpass::pLowpass() {

  // in 0: audio buffer
  // in 1: frequency (float or int)

  // out 0: audio output

  inputs.emplace_back(PIN_TYPE::PIN_DSP_BUFFER, 0, this);
  inputs.emplace_back(PIN_TYPE::PIN_INT | PIN_TYPE::PIN_FLOAT, 1, this);
  outputs.emplace_back(PIN_TYPE::PIN_DSP_BUFFER, 0, this);

  inputs[1].SetData(1.f);
}

const char * pLowpass::Type() const {
  return YSE::OBJ::LOWPASS;
}

void pLowpass::RequestData() {
  UpdateInputs();

  if (inputs[0].IsConnected()) {
    output = *inputs[0].GetBuffer();
  }

  if (inputs[1].GetCurentDataType() == PIN_FLOAT) {
    output *= inputs[1].GetFloat();
  }
  else if (inputs[1].GetCurentDataType() == PIN_INT) {
    output *= inputs[1].GetInt();
  }

  outputs[0].SetData(&output);
}