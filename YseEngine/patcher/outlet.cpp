#include "outlet.h"
#include "inlet.h"
#include "pObject.h"
#include "dsp\buffer.hpp"

using namespace YSE::PATCHER;

outlet::outlet(YSE::OUT_TYPE type)
  : type(type)
{}

outlet::~outlet() {
  for (unsigned int i = 0; i < connections.size(); i++) {
    connections[i]->Disconnect(this);
  }
}

void outlet::SendBang(YSE::THREAD thread) {
  for (unsigned int i = 0; i < connections.size(); i++) {
    connections[i]->SetBang(thread);
  }
}

void outlet::SendFloat(float value, YSE::THREAD thread) {
  for (unsigned int i = 0; i < connections.size(); i++) {
    connections[i]->SetFloat(value, thread);
  }
}

void outlet::SendInt(int value, YSE::THREAD thread) {
  for (unsigned int i = 0; i < connections.size(); i++) {
    connections[i]->SetInt(value, thread);
  }
}

void outlet::SendList(const std::string & value, YSE::THREAD thread) {
  for (unsigned int i = 0; i < connections.size(); i++) {
    connections[i]->SetList(value, thread);
  }
}

void outlet::SendBuffer(YSE::DSP::buffer * value, YSE::THREAD thread) {
  for (unsigned int i = 0; i < connections.size(); i++) {
    connections[i]->SetBuffer(value, thread);
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
  json["Count"] = connections.size();
  for (unsigned int i = 0; i < connections.size(); i++) {
    json[std::to_string(i)]["Object"] = connections[i]->GetObjectID();
    json[std::to_string(i)]["Inlet"] = connections[i]->GetPosition();
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