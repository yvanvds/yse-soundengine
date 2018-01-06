#include "pSubstract.h"
#include "..\pObjectList.hpp"

using namespace YSE::PATCHER;

pObject * pSubstract::Create() { return new pSubstract(); }

pSubstract::pSubstract() {

  // in 0: audio buffer
  // in 1: multiplier (float or audio)

  // out 0: audio output

  inputs.emplace_back(PIN_TYPE::PIN_DSP_BUFFER, 0, this);
  inputs.emplace_back(PIN_TYPE::PIN_FLOAT | PIN_TYPE::PIN_DSP_BUFFER, 1, this);
  outputs.emplace_back(PIN_TYPE::PIN_DSP_BUFFER, 0, this);

  inputs[1].SetData(1.f);
}

const char * pSubstract::Type() const {
  return YSE::OBJ::SUBSTRACT;
}

void pSubstract::RequestData() {
  UpdateInputs();

  if (inputs[0].IsConnected()) {
    output = *inputs[0].GetBuffer();
  }

  if (inputs[1].GetCurentDataType() == PIN_FLOAT) {
    output -= inputs[1].GetFloat();
  }
  else if (inputs[1].GetCurentDataType() == PIN_DSP_BUFFER) {
    output -= *inputs[1].GetBuffer();
  }

  outputs[0].SetData(&output);
}