#include "gSend.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"
#include "../../internal/namedBus.h"
#include "../../internal/global.h"

using namespace YSE::PATCHER;
#define className gSend

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(SetBangValue);
  REG_INT_IN(SetIntValue);
  REG_FLOAT_IN(SetFloatValue);
  REG_LIST_IN(SetListValue);

  ADD_PARAM(dataName);
  ADD_PARAM(globalOnly);

  ADD_DESCRIPTION(
      "Named send endpoint. Broadcasts incoming values to every gReceive in the patcher whose "
      "dataName matches, and publishes them on the global bus as \"<patcherName>.<dataName>\" so "
      "cross-patcher routing works without explicit wiring.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "in", "Value inlet — accepts bang / int / float / list.", "");
  PARAM_DOC("dataName", "",
            "Name to broadcast on; matching gReceive nodes will emit the forwarded value.",
            "any identifier");
  PARAM_DOC("globalOnly", "0",
            "When 1, skip in-patcher delivery and publish only to the global bus.", "0 or 1");
}

// RT-safety: on T_DSP the bus publish path is allocation-free and lock-free
// for int/float; bang and list payloads silently drop on T_DSP per the bus
// contract. The address is precomputed off the audio thread (SetParent /
// RefreshBusAddress) into busAddress_ so no std::string is built here.
namespace {
  using YSE::INTERNAL::Bus;
  using YSE::INTERNAL::BusValue;

  // The bus is owned by INTERNAL::Global() between init() and close(); skip
  // publishing outside that window so tests instantiating a patcher without
  // first calling `System::init()` keep working through the local path.
  inline bool busAvailable() {
    return YSE::INTERNAL::Global().isActive();
  }
} // namespace

// `parent` is a patcherImplementation by construction (the patcher hands
// itself to every object via SetParent); the cast mirrors the existing
// PassData calls below.
void gSend::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  RefreshBusAddress();
}

void gSend::RefreshBusAddress() {
  if (parent == nullptr) {
    busAddress_.clear();
    return;
  }
  auto* p = static_cast<patcherImplementation*>(parent);
  busAddress_ = p->Name() + "." + dataName;
}

BANG_IN(SetBangValue) {
  if (parent == nullptr) return;
  auto* p = static_cast<patcherImplementation*>(parent);
  if (!globalOnly) {
    p->PassBang(dataName, thread);
  }
  if (busAvailable()) {
    // Bang on the bus is a monostate publish — only delivered on T_GUI.
    Bus().publish(busAddress_, BusValue{}, thread);
  }
}

INT_IN(SetIntValue) {
  if (parent == nullptr) return;
  auto* p = static_cast<patcherImplementation*>(parent);
  if (!globalOnly) {
    p->PassData(value, dataName, thread);
  }
  if (busAvailable()) {
    Bus().publish(busAddress_, BusValue{value}, thread);
  }
}

FLOAT_IN(SetFloatValue) {
  if (parent == nullptr) return;
  auto* p = static_cast<patcherImplementation*>(parent);
  if (!globalOnly) {
    p->PassData(value, dataName, thread);
  }
  if (busAvailable()) {
    Bus().publish(busAddress_, BusValue{value}, thread);
  }
}

LIST_IN(SetListValue) {
  if (parent == nullptr) return;
  auto* p = static_cast<patcherImplementation*>(parent);
  if (!globalOnly) {
    p->PassData(value, dataName, thread);
  }
  if (busAvailable()) {
    Bus().publish(busAddress_, BusValue{value}, thread);
  }
}
