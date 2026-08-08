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
//
// The tag alone does not say which thread we are on, so every publish below
// goes through `patcherImplementation::CallingThread` (issue #690). Two things
// were wrong without it, both on a `.s` reached from a deferred delivery — a
// T_GUI tag on the audio callback:
//
//   - `NamedBus::publish` sends a non-control-thread T_GUI publish to its
//     parked-message list, which takes `pendingMutex_` and grows a
//     `std::vector<std::pair<std::string, BusValue>>`. A lock and two
//     allocations on the audio thread; the comment there stating the audio
//     callback "only ever publishes on T_DSP" is the invariant this restores.
//   - `BusValue{value}` copy-constructs the payload into the variant *before*
//     publish can decide anything, so a list allocated past the small-string
//     buffer even on the T_DSP path where the bus drops it. The list and bang
//     publishes are therefore skipped outright when the answer is T_DSP,
//     rather than built and thrown away.
//
// Dropping bang/list on the audio thread is the bus's documented contract
// (namedBus.h), not a new limitation: non-trivial payloads are routed through
// main-thread bridges. What changes is that a deferred `.s` is now honestly
// treated as the audio thread it runs on. int and float still reach every
// subscriber, through the lock-free per-thread queue.
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
  if (busAvailable() && p->CallingThread(thread) == YSE::T_GUI) {
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
    Bus().publish(busAddress_, BusValue{value}, p->CallingThread(thread));
  }
}

FLOAT_IN(SetFloatValue) {
  if (parent == nullptr) return;
  auto* p = static_cast<patcherImplementation*>(parent);
  if (!globalOnly) {
    p->PassData(value, dataName, thread);
  }
  if (busAvailable()) {
    Bus().publish(busAddress_, BusValue{value}, p->CallingThread(thread));
  }
}

LIST_IN(SetListValue) {
  if (parent == nullptr) return;
  auto* p = static_cast<patcherImplementation*>(parent);
  if (!globalOnly) {
    p->PassData(value, dataName, thread);
  }
  // Built only when it can be delivered: the variant copy is the allocation.
  if (busAvailable() && p->CallingThread(thread) == YSE::T_GUI) {
    Bus().publish(busAddress_, BusValue{value}, thread);
  }
}
