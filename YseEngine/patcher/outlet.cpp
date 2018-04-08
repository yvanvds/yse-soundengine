#include "outlet.h"
#include "inlet.h"
#include "pObject.h"
#include "dsp\buffer.hpp"

using namespace YSE::PATCHER;

outlet::outlet(pObject * obj, YSE::OUT_TYPE type)
  : obj(obj)
  , type(type)
{}

outlet::~outlet() {
  for (unsigned int i = 0; i < connections.size(); i++) {
    connections[i]->Disconnect(this);
  }
}

void outlet::SendBang() {
  for (unsigned int i = 0; i < connections.size(); i++) {
    connections[i]->SetBang();
  }
}

void outlet::SendFloat(float value) {
  for (unsigned int i = 0; i < connections.size(); i++) {
    connections[i]->SetFloat(value);
  }
}

void outlet::SendInt(int value) {
  for (unsigned int i = 0; i < connections.size(); i++) {
    connections[i]->SetInt(value);
  }
}

void outlet::SendList(const std::string & value) {
  for (unsigned int i = 0; i < connections.size(); i++) {
    connections[i]->SetList(value);
  }
}

void outlet::SendBuffer(YSE::DSP::buffer * value) {
  for (unsigned int i = 0; i < connections.size(); i++) {
    connections[i]->SetBuffer(value);
  }
}

void outlet::Connect(inlet * in) {
  for (unsigned int i = 0; i < connections.size(); i++) {
    if (connections[i] == in) return;
  }
  connections.push_back(in);
}

void outlet::Disconnect(inlet * in) {
  for (unsigned int i = 0; i < connections.size(); i++) {
    if (connections[i] == in) {
      connections.erase(connections.begin() + i);
    }
  }
}

void outlet::DumpJSON(nlohmann::json::value_type & json) {
  for (unsigned int i = 0; i < connections.size(); i++) {
    json["Object"] = connections[i]->GetObjectID();
    json["Inlet"] = connections[i]->GetPosition();
  }
}

unsigned int outlet::GetConnections() {
  return connections.size();
}

unsigned int outlet::GetTarget(unsigned int connection) {
  if (connection < connections.size()) {
    return connections[connection]->GetObjectID();
  }
  return 0;
}

unsigned int outlet::GetTargetInlet(unsigned int connection) {
  if (connection < connections.size()) {
    return connections[connection]->GetPosition();
  }
  return 0;
}