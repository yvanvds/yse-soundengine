#include "gReceive.h"
#include "../../implementations/logImplementation.h"
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
  REG_PARM_PARSE;

  ADD_OUT_ANY;

  ADD_DESCRIPTION("Sends out whatever is sent to its name: by a .s or .forward in this patcher or "
                  "in a patcher with the same name, by the host's PassData, or on the named bus.");
  ADD_CATEGORY(pCategory::IO);
  INLET_DOC(0, "in", "Bang, int, float or list, passed straight to the outlet.", "");
  OUTLET_DOC(0, "out", "Every value sent to this name.", "");
  PARAM_DOC("dataName", "",
            "The name to listen to. A name longer than 63 characters is refused and logged, and "
            "the object receives nothing.",
            "any name, at most 63 characters");
  PARAM_DOC("globalOnly", "0", "Accepted for symmetry with .s, and ignored.", "0 or 1");
}

// gSend's refusal (issue #922), for the same reason: the subscription below is
// keyed on the whole scoped address, and a matching .s refuses the same name,
// so accepting it here would only ever wait on an address nothing publishes
// whole. Control thread only, before SetParent subscribes.
PARM_PARSE() {
  if (dataName.size() > patcherImplementation::MAX_SLOT_NAME_LENGTH) {
    INTERNAL::LogImpl().emit(E_ERROR,
                             "patcher: .r dataName \"" + dataName + "\" is longer than " +
                                 std::to_string(patcherImplementation::MAX_SLOT_NAME_LENGTH) +
                                 " characters; ignored");
    dataName.clear();
  }
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
  const std::string address = p->ScopedAddress(dataName);
  // Ignores its own patcher's .s / .forward publishes (issue #943): the
  // in-patcher path already delivered those, and taking the echo too made this
  // object fire twice per value. Re-read on every resubscribe, but a patcher's
  // origin never changes.
  busHandle = Bus().subscribe(
      address,
      [this](const BusValue& v) {
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
      },
      p->BusOrigin());
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
