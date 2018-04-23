#include "pObject.h"
#include "pHandle.hpp"
#include "headers\enums.hpp"
#include "implementations\logImplementation.h"

using namespace YSE::PATCHER;

unsigned int LastPatcherObjectID = 0;
unsigned int pObject::CreateID() {
  return LastPatcherObjectID++;
}

pObject::pObject(bool isDSPObject, pObject * parent) 
  : parent(parent)
  , DSP(isDSPObject)
  , ID(CreateID())
{}

bool pObject::IsDSPStartPoint() {
  if(!DSP) return false;

  for (unsigned int i = 0; i < inputs.size(); i++) {
    if (inputs[i].HasActiveDSPConnection()) return false;
  }

  // at this point either the dsp pin is not connected or there is none
  return true;
}

void pObject::ResetDSP() {
  for (unsigned int i = 0; i < inputs.size(); i++) {
    inputs[i].ResetDSP();
  }
}

void pObject::CalculateIfReady(YSE::THREAD thread) {
  // make sure all dsp inputs are ready
  for (unsigned int i = 0; i < inputs.size(); i++) {
    if (inputs[i].WaitingForDSP()) {
      return;
    }
  }
  Calculate(thread);
}

void pObject::SetParent(pObject * parent) {
  this->parent = parent;
}

void pObject::SetParams(const std::string & args) {
  parms.Set(args);
}

void pObject::ConnectInlet(outlet * from, int inlet)
{
  inputs[inlet].Connect(from);
}

void pObject::DisconnectInlet(outlet * from, int inlet) {
  inputs[inlet].Disconnect(from);
}

void pObject::ConnectOutlet(inlet * dest, int outlet) {
  outputs[outlet].Connect(dest);
}

YSE::OUT_TYPE pObject::GetOutputType(unsigned int output) const {
  if (output >= outputs.size()) return OUT_TYPE::INVALID;

  return outputs[output].Type();
}


YSE::PATCHER::inlet * pObject::GetInlet(int number) {
  return &(inputs[number]);
}

YSE::PATCHER::outlet * pObject::GetOutlet(int number) {
  return &(outputs[number]);
}


void pObject::DumpJson(nlohmann::json::value_type & json) {
  json["type"] = Type();
  json["ID"] = ID;
  json["parms"] = parms.Get();

  for (unsigned int i = 0; i < outputs.size(); i++) {
    outputs[i].DumpJSON(json["outputs"]["output " + std::to_string(i)]);
  }

  for (auto const & x : guiProperties) {
    json["gui"][x.first] = x.second;
  }
}

const std::string & pObject::GetParams() {
  return parms.Get();
}

unsigned int pObject::GetConnections(unsigned int outlet) {
  return outputs[outlet].GetConnections();
}

unsigned int pObject::GetConnectionTarget(unsigned int outlet, unsigned int connection) {
  return outputs[outlet].GetTarget(connection);
}

unsigned int pObject::GetConnectionTargetInlet(unsigned int outlet, unsigned int connection) {
  return outputs[outlet].GetTargetInlet(connection);
}

std::string pObject::GetGuiProperty(const std::string & key) {
  auto pos = guiProperties.find(key);
  if (pos == guiProperties.end()) {
    return "";
  }
  return pos->second;
}

void pObject::SetGuiProperty(const std::string & key, const std::string & value) {
  guiProperties[key] = value;
}