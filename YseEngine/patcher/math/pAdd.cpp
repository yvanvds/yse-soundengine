#include "pAdd.h"
#include "..\pObjectList.hpp"

using namespace YSE::PATCHER;

pObject * pAdd::Create() { return new pAdd(); }

pAdd::pAdd() {

  // in 0: audio buffer
  // in 1: multiplier (float or audio)

  // out 0: audio output

  inputs.emplace_back(PIN_TYPE::PIN_DSP_BUFFER, 0, this);
  inputs.emplace_back(PIN_TYPE::PIN_FLOAT | PIN_TYPE::PIN_DSP_BUFFER, 1, this);
  outputs.emplace_back(PIN_TYPE::PIN_DSP_BUFFER, 0, this);

  inputs[1].SetData(1.f);
}

const char * pAdd::Type() const {
  return YSE::OBJ::D_ADD;
}

void pAdd::RequestData() {
  UpdateInputs();

  if (inputs[0].IsConnected()) {
    output = *inputs[0].GetBuffer();
  }

  if (inputs[1].GetCurentDataType() == PIN_FLOAT) {
    output += inputs[1].GetFloat();
  }
  else if (inputs[1].GetCurentDataType() == PIN_DSP_BUFFER) {
    output += *inputs[1].GetBuffer();
  }

  outputs[0].SetData(&output);
}