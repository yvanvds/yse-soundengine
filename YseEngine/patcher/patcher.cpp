#include "patcher.hpp"
#include "patcherImplementation.h"
#include "pRegistry.h"

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

pHandle * patcher::CreateObject(const char * type) {
  if (pimpl == nullptr) return nullptr;
  return pimpl->CreateObject(type);
}

void patcher::DeleteObject(pHandle * obj) {
  if (pimpl == nullptr) return;
  pimpl->DeleteObject(obj);
}

void patcher::Connect(pHandle * from, int outlet, pHandle * to, int inlet) {
  if (pimpl == nullptr) return;
  pimpl->Connect(from, outlet, to, inlet);
}

void patcher::Disconnect(pHandle * from, int outlet, pHandle * to, int inlet) {
  if (pimpl == nullptr) return;
  pimpl->Disconnect(from, outlet, to, inlet);
}

bool patcher::IsValidObject(const char * type) {
  return YSE::PATCHER::Register().IsValidObject(type);
}