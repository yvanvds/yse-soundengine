#include "pSine.h"
#include "..\pObjectList.hpp"

using namespace YSE::PATCHER;

pObject * pSine::Create() { return new pSine(); }

pSine::pSine() 
{
  inputs.emplace_back(PIN_TYPE::PIN_FLOAT, 0, this);
  outputs.emplace_back(PIN_TYPE::PIN_DSP_BUFFER, 0, this);

  // set starting frequency
  inputs[0].SetData(440.f);
}

const char * pSine::Type() const {
  return YSE::OBJ::D_SINE;
}

void pSine::RequestData() {
  UpdateInputs();

  // generate buffer and send to output
  outputs[0].SetData(&sine(inputs[0].GetFloat()));
}

