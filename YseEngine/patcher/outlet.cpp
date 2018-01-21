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