#include "pHandle.hpp"
#include "pEnums.h"
#include "pObject.h"

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

void YSE::pHandle::SetParams(const std::string & args) {
  object->SetParams(args);
}

void YSE::pHandle::SetPosition(const YSE::Pos & pos) {
  object->SetPosition(pos);
}

const YSE::Pos & YSE::pHandle::GetPosition() {
  return object->GetPosition();
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

const std::string & YSE::pHandle::GetName() {
  return object->Type();
}

const std::string & YSE::pHandle::GetParams() {
  return object->GetParams();
}

unsigned int YSE::pHandle::GetConnections(unsigned int outlet) {
  return object->GetConnections(outlet);
}

unsigned int YSE::pHandle::GetConnectionTarget(unsigned int outlet, unsigned int connection) {
  return object->GetConnectionTarget(outlet, connection);
}

unsigned int YSE::pHandle::GetConnectionTargetInlet(unsigned int outlet, unsigned int connection) {
  return object->GetConnectionTargetInlet(outlet, connection);
}