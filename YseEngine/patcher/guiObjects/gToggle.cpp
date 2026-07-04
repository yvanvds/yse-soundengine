#include "gToggle.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gToggle

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(SetValue);
  REG_BANG_IN(Bang);

  ADD_OUT_INT;

  value = false;

  ADD_DESCRIPTION("Latching on/off toggle. Int sets the state directly (0 = off, non-zero = on); "
                  "bang flips it. Emits 0 or 1 every calculate tick.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "set/toggle", "Int sets state (0 = off, !=0 = on); bang flips the current state.",
            "0 or 1");
  OUTLET_DOC(0, "out", "Current state — 0 or 1.", "0 or 1");
}

INT_IN(SetValue) {
  if (value == 0)
    this->value = false;
  else
    this->value = true;
}

BANG_IN(Bang) {
  // Atomic flip. A plain `value = !value` is an atomic load followed by a
  // separate atomic store, so two concurrent bangs (GUI/timer thread racing
  // the audio thread) can both read the same state and collapse into a single
  // flip — a lost update (issue #197). compare_exchange makes it a real RMW;
  // the retry loop is lock-free and never allocates, so it is audio-thread
  // safe. std::atomic<bool> has no fetch_xor, hence the CAS.
  bool current = value.load(std::memory_order_relaxed);
  while (!value.compare_exchange_weak(current, !current, std::memory_order_relaxed)) {
    // `current` is refreshed with the latest value on failure; retry.
  }
}

GUI_VALUE() {
  return value == 0 ? "off" : "on";
}

CALC() {
  outputs[0].SendInt(value == false ? 0 : 1, thread);
}
