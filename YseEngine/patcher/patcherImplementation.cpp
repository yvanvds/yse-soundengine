#include "patcherImplementation.h"
#include "pRegistry.h"
#include "pObjectList.hpp"
#include "headers\enums.hpp"
#include "genericObjects\pDac.h"
#include "pHandle.hpp"

using namespace YSE::PATCHER;

patcherImplementation::patcherImplementation(int mainOutputs, YSE::patcher * head)
  : pObject(false) 
  , controlledBySound(false)
  , head(head)
{
  output.resize(mainOutputs);
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

void patcherImplementation::Calculate() {
  // works a bit different in case of patchers!
  // only called by the main patcher to generate output
  
  // invalidate all dsp buffers
  ResetDSP();

  // calculate all objects
  for (const auto& any : objects) {
    if (any.second->IsDSPStartPoint()) {
      any.second->Calculate();
    }
  }

  // clear output 
  for (unsigned int i = 0; i < output.size(); i++) {
    output[i] = 0;
  }

  // sum outputs
  int counter = 0;
  for (const auto& any : objects) {
    if (any.second->Type() == YSE::OBJ::D_DAC) {
      for (unsigned int i = 0; i < output.size(); i++) {
        YSE::DSP::buffer * ptr = ((pDac*)any.second)->GetBuffer(i);
        if (ptr != nullptr) {
          output[i] = *ptr;
        }
      }
      counter++;
    }
  }

  // normalize output 
  if (counter > 1) {
    for (unsigned int i = 0; i < output.size(); i++) {
      output[i] /= counter;
    }
  }
}

void patcherImplementation::ResetDSP() {
  for (const auto& any : objects) {
    any.second->ResetDSP();
  }
}


void patcherImplementation::Connect(YSE::pHandle * from, int outlet, YSE::pHandle * to, int inlet) {
  PATCHER::outlet * out = from->object->GetOutlet(outlet);
  PATCHER::inlet * in = to->object->GetInlet(inlet);
  from->object->ConnectOutlet(in, outlet);
  to->object->ConnectInlet(out, inlet);
}

void patcherImplementation::Disconnect(YSE::pHandle * from, int outlet, YSE::pHandle * to, int inlet) {
  to->object->DisconnectInlet(from->object->GetOutlet(outlet), inlet);
}

YSE::pHandle * patcherImplementation::CreateObject(const char * type) {
  YSE::pHandle * handle = nullptr;
  pObject * object = nullptr;

  if (strcmp(type, OBJ::D_DAC) == 0) {
    object = new pDac(output.size());
  }
  else {
    object = Register().Get(type);
  }

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