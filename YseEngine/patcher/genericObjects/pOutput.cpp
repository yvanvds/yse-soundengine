#include "pOutput.h"
#include "../pObjectList.hpp"


using namespace YSE::PATCHER;

pObject * pOutput::Create() { return new pOutput(); }

pOutput::pOutput()
  : ready(false)
{
}

const char * pOutput::Type() const {
  return OBJ::D_OUT;
}

bool pOutput::Setup(YSE::PIN_TYPE type) {
  if (ready) return false;
  inputs.emplace_back(type, 0, this);
  outputs.emplace_back(type, 0, this);
  ready = true;
  return true;
}

void pOutput::RequestData() {
  if (!ready) return;

  UpdateInputs();

  outputs[0].SetData(&inputs[0]);
}

YSE::DSP::buffer * pOutput::GetBuffer(int pin) {
  if (!ready) return nullptr;
  return inputs[pin].GetBuffer();
}