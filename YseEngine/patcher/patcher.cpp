#include "patcher.hpp"
#include "patcherImplementation.h"

using namespace YSE;

patcher::patcher()
  : pimpl(nullptr)
{

}

patcher::~patcher() {
  if (pimpl != nullptr) {
    if (pimpl->controlledBySound) {
      pimpl->head.store(nullptr);
    }
    else {
      delete pimpl;
    }
  }
}

void patcher::create(int mainOutputs) {
  if (pimpl != nullptr) return;
  pimpl = new PATCHER::patcherImplementation(mainOutputs, this);
}

pHandle * patcher::AddObject(const char * type) {
  if (pimpl == nullptr) return nullptr;
  return pimpl->AddObject(type);
}

void patcher::Connect(pHandle * from, int pinOut, pHandle * to, int pinIn) {
  if (pimpl == nullptr) return;
  pimpl->Connect(from, pinOut, to, pinIn);
}

void patcher::Disconnect(pHandle * to, int pinIn) {
  if (pimpl == nullptr) return;
  pimpl->Disconnect(to, pinIn);
}

pHandle * patcher::GetOutputHandle(unsigned int output) {
  if (pimpl == nullptr) return nullptr;
  return pimpl->GetOutputHandle(output);
}