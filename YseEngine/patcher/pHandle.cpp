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

bool YSE::pHandle::SetData(unsigned int pin, bool value)
{
  return object->SetData(pin, value);
}

bool YSE::pHandle::SetData(unsigned int pin, int value)
{
  return object->SetData(pin, value);
}

bool YSE::pHandle::SetData(unsigned int pin, float value)
{
  return object->SetData(pin, value);
}

bool YSE::pHandle::SetData(unsigned int pin, const char * value)
{
  return object->SetData(pin, value);
}

int YSE::pHandle::InputDataTypes(unsigned int pin) {
  return object->GetInputTypes(pin);
}

YSE::PIN_TYPE YSE::pHandle::OutputDataType(unsigned int pin) {
  return object->GetOutputType(pin);
}

int YSE::pHandle::GetInputs() {
  return object->NumInputs();
}

int YSE::pHandle::GetOutputs() {
  return object->NumOutputs();
}