#include "pHighpass.h"
#include "..\pObjectList.hpp"

using namespace YSE::PATCHER;

pObject * pHighpass::Create() { return new pHighpass(); }

pHighpass::pHighpass() {

  // in 0: audio buffer
  // in 1: multiplier (float or audio)

  // out 0: audio output

  inputs.emplace_back(PIN_TYPE::PIN_DSP_BUFFER, 0, this);
  inputs.emplace_back(PIN_TYPE::PIN_INT | PIN_TYPE::PIN_FLOAT, 1, this);
  outputs.emplace_back(PIN_TYPE::PIN_DSP_BUFFER, 0, this);

  inputs[1].SetData(1.f);
}

const char * pHighpass::Type() const {
  return YSE::OBJ::HIGHPASS;
}

void pHighpass::RequestData() {
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