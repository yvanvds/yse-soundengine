#include "patcherImplementation.h"
#include "pRegistry.h"
#include "pObjectList.hpp"
#include "headers\enums.hpp"
#include "genericObjects/pOutput.h"
#include "pHandle.hpp"

using namespace YSE::PATCHER;

patcherImplementation::patcherImplementation(int mainOutputs, YSE::patcher * head)
  : controlledBySound(false)
  , head(head)
{
  output.resize(mainOutputs);

  for (int i = 0; i < mainOutputs; i++) {
    AddOutput(PIN_TYPE::PIN_DSP_BUFFER); 
  }
}

patcherImplementation::~patcherImplementation() {
  // memory cleanup
  for (auto it = objects.begin(); it != objects.end(); ++it) {
    delete it->first;
    delete it->second;
  }
}

const char * patcherImplementation::Type() const {
  return YSE::OBJ::PATCHER;
}

void patcherImplementation::RequestData() {
  // works a bit different in case of patchers!
  // only called by the main patcher to generate output
  
  // invalidate all data
  ResetData();

  // calculate all objects
  for (const auto& any : outputObjects) {
    any->object->RequestData();
  }

  // copy buffer to output
  for (unsigned int i = 0; i < output.size(); i++) {
    output[i] = *((pOutput*)outputObjects[i]->object)->GetBuffer(0);
  }
}

void patcherImplementation::ResetData() {
  for (const auto& any : objects) {
    any.second->ResetData();
  }
}

YSE::pHandle * patcherImplementation::AddObject(const char * type) {
  return CreateObject(type);
}

YSE::pHandle * patcherImplementation::AddOutput(PIN_TYPE type) {
  
  pHandle * handle = CreateObject(YSE::OBJ::OUT);
  ((pOutput*)handle->object)->Setup(type);

  outputObjects.push_back(handle);
  return handle;
}

void patcherImplementation::Connect(YSE::pHandle * from, int fromPin, YSE::pHandle * to, int toPin) {
  pinOut * pOut = from->object->GetOutput(fromPin);
  pinIn * pIn = to->object->GetInput(toPin);
  from->object->ConnectOutput(pIn, fromPin);
  to->object->ConnectInput(pOut, toPin);
}

void patcherImplementation::Disconnect(YSE::pHandle * to, int pinIn) {
  to->object->DisconnectInput(pinIn);
}

YSE::pHandle * patcherImplementation::CreateObject(const char * type) {
  YSE::pHandle * handle = nullptr;
  pObject * object = Register().Get(type);

  if (object == nullptr) return nullptr;

  handle = new YSE::pHandle(object);
  objects.insert(std::pair<YSE::pHandle*, pObject*>(handle, object));

  return handle;
}

void patcherImplementation::DeleteObject(YSE::pHandle * handle) {
  objects.erase(handle);
  delete handle->object;
  delete handle;
}

YSE::pHandle * patcherImplementation::GetOutputHandle(unsigned int output) {
  if (output >= outputObjects.size()) return nullptr;
  return outputObjects[output];
}