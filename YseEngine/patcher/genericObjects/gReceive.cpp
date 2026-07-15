#include "gReceive.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"
#include "../../internal/global.h"

using namespace YSE::PATCHER;
using YSE::INTERNAL::Bus;
using YSE::INTERNAL::BusValue;
#define className gReceive

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(SetBangValue);
  REG_INT_IN(SetIntValue);
  REG_FLOAT_IN(SetFloatValue);
  REG_LIST_IN(SetListValue);

  ADD_PARAM(dataName);
  ADD_PARAM(globalOnly);

  ADD_OUT_ANY;

  ADD_DESCRIPTION("Named receive endpoint. Forwards values arriving from any matching gSend (same "
                  "dataName) in the patcher, and from any gSend in any patcher with the same name "
                  "via the global bus (\"<patcherName>.<dataName>\").");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "in", "Wired inlet (rarely used — receives typically pair with gSend by name).", "");
  OUTLET_DOC(0, "out", "Forwarded value from matching gSend nodes.", "");
  PARAM_DOC("dataName", "",
            "Name to listen for; must match the dataName of one or more gSend nodes.",
            "any identifier");
  PARAM_DOC("globalOnly", "0",
            "Reserved for future receive-side filters; ignored today (the bus subscription is "
            "always active).",
            "0 or 1");
}

gReceive::~gReceive() {
  unsubscribeIfNeeded();
}

void gReceive::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // Whenever the parent changes (including for the first time during
  // patcherImplementation::CreateObject), re-anchor the bus subscription
  // under the new patcher's name.
  unsubscribeIfNeeded();
  subscribeFromParent();
}

void gReceive::Resubscribe() {
  unsubscribeIfNeeded();
  subscribeFromParent();
}

void gReceive::unsubscribeIfNeeded() {
  if (busHandle == 0) return;
  // Guard the bus access for the case where this receiver outlives the
  // engine session — destructors running after `System::close()` would
  // otherwise hit the global bus assertion / nullptr deref.
  if (YSE::INTERNAL::Global().isActive()) {
    Bus().unsubscribe(busHandle);
  }
  busHandle = 0;
}

void gReceive::subscribeFromParent() {
  if (parent == nullptr) return;
  // No bus before `System::init()` and after `System::close()`. Tests that
  // exercise the patcher graph without spinning up the engine fall through
  // to the inlet-driven local routing path that existed before issue #122.
  if (!YSE::INTERNAL::Global().isActive()) return;
  auto* p = static_cast<patcherImplementation*>(parent);
  // Callback fires either synchronously from a T_GUI publisher or from
  // `NamedBus::drainPending` on the main thread. Either way it is safe to
  // route through the receive object's outlet with T_GUI semantics.
  const std::string address = p->Name() + "." + dataName;
  busHandle = Bus().subscribe(address, [this](const BusValue& v) {
    if (std::holds_alternative<int>(v)) {
      outputs[0].SendInt(std::get<int>(v), YSE::T_GUI);
    } else if (std::holds_alternative<float>(v)) {
      outputs[0].SendFloat(std::get<float>(v), YSE::T_GUI);
    } else if (std::holds_alternative<std::string>(v)) {
      outputs[0].SendList(std::get<std::string>(v), YSE::T_GUI);
    } else if (std::holds_alternative<std::monostate>(v)) {
      // Bang carries no payload — the monostate sentinel is the signal.
      outputs[0].SendBang(YSE::T_GUI);
    }
    // vector<float> payloads are not currently exposed by gSend; ignore.
  });
}

BANG_IN(SetBangValue) {
  outputs[0].SendBang(thread);
}

INT_IN(SetIntValue) {
  outputs[0].SendInt(value, thread);
}

FLOAT_IN(SetFloatValue) {
  outputs[0].SendFloat(value, thread);
}

LIST_IN(SetListValue) {
  outputs[0].SendList(value, thread);
}
