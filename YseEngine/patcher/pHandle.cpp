#include "pHandle.hpp"
#include "pEnums.h"

using namespace YSE;

pHandle::pHandle(PATCHER::pObject * object) : object(object) {}

const char * pHandle::Type() const {
  if (!object) {
    return "Invalid Handle";
  }
  else {
    return object->Type();
  }
}

void YSE::pHandle::SetData(unsigned int inlet, float value)
{
  object->GetInlet(inlet)->SetFloat(value);
}

void YSE::pHandle::SetParam(unsigned int pos, float value) {
  object->SetParam(pos, value);
}

bool YSE::pHandle::IsDSPInput(unsigned int inlet) {
  return object->GetInlet(inlet)->AcceptsDSP();
}

YSE::OUT_TYPE YSE::pHandle::OutputDataType(unsigned int pin) {
  return object->GetOutputType(pin);
}

int YSE::pHandle::GetInputs() {
  return object->NumInputs();
}

int YSE::pHandle::GetOutputs() {
  return object->NumOutputs();
}