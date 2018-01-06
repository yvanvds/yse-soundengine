#include "pin.h"
#include "pObject.h"
#include "dsp/buffer.hpp"

using namespace YSE::PATCHER;

pin::pin(PIN_DIR dir, int position, pObject * thisObject)
  : direction(dir)
  , position(position)
  , thisObject(thisObject) 
  , dataIsReady(false)
{}



void pin::SetData(bool data) {
  vBool = data;
  currentDataType = PIN_BOOL;
  dataIsReady = true;
}

void pin::SetData(int data) {
  vInt = data;
  currentDataType = PIN_INT;
  dataIsReady = true;
}

void pin::SetData(float data) {
  vFloat = data;
  currentDataType = PIN_FLOAT;
  dataIsReady = true;
}

void pin::SetData(const char * data) {
  vString = data;
  currentDataType = PIN_STRING;
  dataIsReady = true;
}

void pin::SetData(YSE::DSP::buffer * data) {
  vBuffer = data;
  currentDataType = PIN_DSP_BUFFER;
  dataIsReady = true;
}

////////////////////////////////////////////
// pinIn
////////////////////////////////////////////

pinIn::pinIn(int allowedTypes, int position, pObject * thisObject)
  : pin(PIN_DIR::IN, position, thisObject)
  , allowedTypes(allowedTypes)
  , connection(nullptr)
{}

bool pinIn::Accepts(PIN_TYPE type) {
  return (allowedTypes & type);
}

void pinIn::Connect(pinOut * pin) {
  if (connection != nullptr) connection->DisConnect(this);
  connection = pin;
}

void pinIn::Disconnect() {
  if (connection != nullptr) connection->DisConnect(this);
  connection = nullptr;
}

void pinIn::RequestData() {
  connection->RequestData(this);
}

////////////////////////////////////////////
// pinOut
////////////////////////////////////////////

pinOut::pinOut(PIN_TYPE type, int position, pObject * thisObject)
  : pin(PIN_DIR::OUT, position, thisObject)
  , type(type)
{}

bool pinOut::IsConnected() {
  return connections.size() > 0;
}

bool pinOut::HasConnection(pinIn * input) {
  for (unsigned int i = 0; i < connections.size(); i++) {
    if (connections[i] == input) return true;
  }
  return false;
}

bool pinOut::Connect(pinIn * input) {
  if (HasConnection(input)) return true;
  if (!input->Accepts(type)) return false;

  connections.push_back(input);
  return true;
}

void pinOut::DisConnect(pinIn * input) {
  for (unsigned int i = 0; i < connections.size(); i++) {
    if (connections[i] == input) {
      connections.erase(connections.begin() + i);
      return;
    }
  }
}

void pinOut::RequestData(pinIn * input) {
  if (!HasData()) thisObject->RequestData();
  SendData(input);
}

void pinOut::SendData(pinIn * input) {
  switch (type) {
    case PIN_TYPE::PIN_BOOL: input->SetData(vBool); break;
    case PIN_TYPE::PIN_INT: input->SetData(vInt); break;
    case PIN_TYPE::PIN_FLOAT: input->SetData(vFloat); break;
    case PIN_TYPE::PIN_STRING: input->SetData(vString); break;
    case PIN_TYPE::PIN_DSP_BUFFER: input->SetData(vBuffer); break;
  }
}

void pinOut::SetData(pinIn * source) {
  switch (type) {
  case PIN_TYPE::PIN_BOOL: vBool = source->GetBool(); break;
  case PIN_TYPE::PIN_INT: vInt = source->GetInt(); break;
  case PIN_TYPE::PIN_FLOAT: vFloat = source->GetFloat(); break;
  case PIN_TYPE::PIN_STRING: vString = source->GetString(); break;
  case PIN_TYPE::PIN_DSP_BUFFER: vBuffer = source->GetBuffer(); break;
  }
}


