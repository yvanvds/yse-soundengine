#include "pObject.h"
#include "pHandle.hpp"
#include "headers\enums.hpp"

using namespace YSE::PATCHER;

void pObject::ResetData() {
  for (unsigned int i = 0; i < inputs.size(); i++) {
    inputs[i].ResetData();
  }

  for (unsigned int i = 0; i < outputs.size(); i++) {
    outputs[i].ResetData();
  }
}

bool YSE::PATCHER::pObject::SetData(unsigned int pin, bool value)
{
  if (pin >= inputs.size()) return false;
  if (inputs[pin].Accepts(PIN_TYPE::PIN_BOOL)) {
    inputs[pin].SetData(value);
    return true;
  }
  return false;
}

bool YSE::PATCHER::pObject::SetData(unsigned int pin, int value)
{
  if (pin >= inputs.size()) return false;
  if (inputs[pin].Accepts(PIN_TYPE::PIN_INT)) {
    inputs[pin].SetData(value);
    return true;
  }
  return false;
}

bool YSE::PATCHER::pObject::SetData(unsigned int pin, float value)
{
  if (pin >= inputs.size()) return false;
  if (inputs[pin].Accepts(PIN_TYPE::PIN_FLOAT)) {
    inputs[pin].SetData(value);
    return true;
  }
  return false;
}

bool YSE::PATCHER::pObject::SetData(unsigned int pin, const char * value)
{
  if (pin >= inputs.size()) return false;
  if (inputs[pin].Accepts(PIN_TYPE::PIN_BOOL)) {
    inputs[pin].SetData(value);
    return true;
  }
  return false;
}

void pObject::ConnectInput(pinOut * from, int toPin)
{
  inputs[toPin].Connect(from);
}

void pObject::DisconnectInput(int pin) {
  inputs[pin].Disconnect();
}

void pObject::ConnectOutput(pinIn * dest, int toPin) {
  outputs[toPin].Connect(dest);
}

void pObject::UpdateInputs() {
  for (unsigned int i = 0; i < inputs.size(); i++) {
    if (inputs[i].IsConnected() && !inputs[i].HasData()) {
      inputs[i].RequestData();
    }
  }
}

PIN_TYPE pObject::GetOutputType(unsigned int output) const {
  if (output < 0 || output >= outputs.size()) return PIN_TYPE::PIN_INVALID;

  return outputs[output].type;
}

int pObject::GetInputTypes(unsigned int input) const {
  if (input < 0 || input >= inputs.size()) return PIN_TYPE::PIN_INVALID;

  return inputs[input].allowedTypes;
}

pinIn * pObject::GetInput(int pin) {
  return &(inputs[pin]);
}

pinOut * pObject::GetOutput(int pin) {
  return &(outputs[pin]);
}

