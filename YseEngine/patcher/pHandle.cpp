#include "pHandle.hpp"
#include "patcher.hpp"
#include "pEnums.h"
#include "pObject.h"
#include "implementations\logImplementation.h"

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

void YSE::pHandle::SetBang(unsigned int inlet) {
  object->GetInlet(inlet)->SetBang(T_GUI);
}

void YSE::pHandle::SetIntData(unsigned int inlet, int value) {
  object->GetInlet(inlet)->SetInt(value, T_GUI);
}

void YSE::pHandle::SetFloatData(unsigned int inlet, float value)
{
  object->GetInlet(inlet)->SetFloat(value, T_GUI);
}

void YSE::pHandle::SetListData(unsigned int inlet, const std::string & value) {
  object->GetInlet(inlet)->SetList(value, T_GUI);
}

void YSE::pHandle::SetParams(const std::string & args) {
  INTERNAL::LogImpl().emit(E_DEBUG, "Handle: Passing arguments: " + args);
  object->SetParams(args);
}

std::string YSE::pHandle::GetGuiProperty(const std::string & key) {
  return object->GetGuiProperty(key);
}

void YSE::pHandle::SetGuiProperty(const std::string & key, const std::string & value) {
  object->SetGuiProperty(key, value);
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

std::string YSE::pHandle::GetName() {
  return object->Type();
}

std::string YSE::pHandle::GetParams() {
  return object->GetParams();
}

unsigned int YSE::pHandle::GetID() {
  return object->GetID();
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

std::string YSE::pHandle::GetGuiValue() {
  return object->GetGuiValue();
}

