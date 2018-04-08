#include "inlet.h"
#include "outlet.h"
#include "pObject.h"


using namespace YSE::PATCHER;

inlet::inlet(pObject * obj, bool active, int position)
  : obj(obj)
  , active(active)
  , position(position)
  , dspReady(false)
  , dspConnection(nullptr)
  , onInt(nullptr)
  , onBang(nullptr)
  , onFloat(nullptr)
  , onList(nullptr)
  , onBuffer(nullptr)
{}

inlet::~inlet() {
  if (dspConnection != nullptr) {
    dspConnection->Disconnect(this);
  }
  for (unsigned int i = 0; i < connections.size(); i++) {
    connections[i]->Disconnect(this);
  }
}

void inlet::RegisterInt(intFunc f) {
  onInt = f;
}

void inlet::RegisterBang(voidFunc f) {
  onBang = f;
}

void inlet::RegisterFloat(floatFunc f) {
  onFloat = f;
}

void inlet::RegisterList(listFunc f) {
  onList = f;
}

void inlet::RegisterBuffer(bufferFunc f) {
  onBuffer = f;
}

void inlet::SetInt(int value) {
  if (onInt) {
    onInt(value, position);
    if (active) obj->CalculateIfReady();
  }
}

void inlet::SetBang() {
  if (onBang) {
    onBang(position);
    if (active) obj->CalculateIfReady();
  }
}

void inlet::SetFloat(float value) {
  if (onFloat) {
    onFloat(value, position);
    if (active) obj->CalculateIfReady();
  }
}

void inlet::SetList(const std::string & value) {
  if (onList) {
    onList(value, position);
    if (active) obj->CalculateIfReady();
  }
}

void inlet::SetBuffer(YSE::DSP::buffer * buffer) {
  if (onBuffer) {
    onBuffer(buffer, position);
    dspReady = true;
    obj->CalculateIfReady();
  }
}

void inlet::SetMessage(const std::string & message, float value) {
  obj->SetMessage(message, value);
  if (active) obj->CalculateIfReady();
}

bool inlet::WaitingForDSP() const {
  if (dspConnection == nullptr) return false;
  return !dspReady;
}

bool inlet::Connect(outlet * out) {
  if (out->Type() == OUT_TYPE::BUFFER) {
    if (dspConnection == nullptr) {
      dspConnection = out;
      return true;
    }
    else return false;
  }
  else {
    for (unsigned int i = 0; i < connections.size(); i++) {
      if (connections[i] == out) return false;
    } 
    connections.push_back(out);
    return true;
  }
}

void inlet::Disconnect(outlet * out) {
  if (dspConnection == out) {
    dspConnection->Disconnect(this);
    dspConnection = nullptr;
    return;
  }
  for (unsigned int i = 0; i < connections.size(); i++) {
    if (connections[i] == out) {
      connections[i]->Disconnect(this);
      connections.erase(connections.begin() + i);
    }
  }
}

bool inlet::AcceptsDSP() const {
  if (onBuffer) return true;
  return false;
}

bool inlet::HasActiveDSPConnection() const {
  return dspConnection != nullptr;
}

int inlet::GetObjectID() {
  return obj->GetID();
}

int inlet::GetPosition() {
  return position;
}