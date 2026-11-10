#include "dSend.h"

using namespace YSE::PATCHER;

#define className dSend

CONSTRUCT_DSP() {
  constant = 0.f;

  // in 0: the signal to send; a number; set, clear. Cold: a message only
  // stores, and the block's render sends.
  inputs.emplace_back(this, false, 0);
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetValue);
  REG_INT_IN(SetIntValue);
  REG_LIST_IN(SetList);

  ADD_PARAM(name);
  REG_PARM_PARSE;

  ADD_DESCRIPTION("Sends a signal to every ~receive with the same name, without a cord. Several "
                  "~send objects with one name are summed.");
  ADD_CATEGORY(pCategory::ROUTING);
  INLET_DOC(0, "in",
            "The signal to send (several cords sum). Unconnected, the last number sent here is "
            "sent as a constant. 'set <name>' sends to another name from a later block (no name "
            "sends nowhere); 'clear' sends silence for one block, which empties a feedback loop.",
            "any float");
  PARAM_DOC("name", "",
            "The name to send to, at most 63 characters; a longer one is refused and logged. A "
            "'set' message does not change what is saved.",
            "any name, at most 63 characters");
}

PARM_PARSE() {
  port.Reset(name, YSE::OBJ::D_SEND);
  Rebind();
}

void dSend::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  Rebind();
}

void dSend::OnPatcherRenamed() {
  Rebind();
}

void dSend::Rebind() {
  sender.bind(port.Bus(parent));
}

bool dSend::ApplySet() {
  if (!port.TakePending()) return false;
  Rebind();
  return true;
}

FLOAT_IN(SetValue) {
  (void)inlet;
  (void)thread;
  constant = value;
}

INT_IN(SetIntValue) {
  (void)inlet;
  (void)thread;
  constant = static_cast<Flt>(value);
}

// A word-first list reaches SetMessage instead (#1199); any other list is
// not something ~send takes.
LIST_IN(SetList) {
  (void)value;
  (void)inlet;
  (void)thread;
}

MESSAGES() {
  (void)value;
  if (port.RequestSet(*this, message)) return;
  if (signalBusPort::FirstWordIs(message, "clear")) sender.clear();
}

CALC() {
  (void)thread;
  // The patcher is the context: a ~receive in it hears this block's signal
  // when the graph put it after this object, the last block's otherwise.
  sender.write(inputs[0].Signal(constant), parent);
}

#undef className
