#include "dReceive.h"

using namespace YSE::PATCHER;

#define className dReceive

CONSTRUCT_DSP() {
  // in 0: set. Cold: a message only stores, and the block's render reads.
  inputs.emplace_back(this, false, 0);
  REG_LIST_IN(SetList);

  // out 0: the received signal
  ADD_OUT_BUFFER;

  ADD_PARAM(name);
  REG_PARM_PARSE;

  ADD_DESCRIPTION("Outputs the sum of every ~send with the same name, without a cord. A ~send in "
                  "this patcher is heard in the same block, or one block later when that closes a "
                  "feedback loop; one in another patcher is heard one block later.");
  ADD_CATEGORY(pCategory::ROUTING);
  INLET_DOC(0, "set",
            "'set <name>' receives another name from a later block; no name receives silence.", "");
  OUTLET_DOC(0, "out", "The sum of every ~send of the name, or silence when there is none.", "");
  PARAM_DOC("name", "",
            "The name to receive, at most 63 characters; a longer one is refused and logged. A "
            "'set' message does not change what is saved.",
            "any name, at most 63 characters");
}

PARM_PARSE() {
  port.Reset(name, YSE::OBJ::D_RECEIVE);
  Rebind();
}

void dReceive::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  Rebind();
}

void dReceive::OnPatcherRenamed() {
  Rebind();
}

void dReceive::Rebind() {
  receiver.bind(port.Bus(parent));
}

bool dReceive::ApplySet() {
  if (!port.TakePending()) return false;
  Rebind();
  return true;
}

// A word-first list reaches SetMessage instead (#1199); any other list is
// not something ~receive takes.
LIST_IN(SetList) {
  (void)value;
  (void)inlet;
  (void)thread;
}

MESSAGES() {
  (void)value;
  port.RequestSet(*this, message);
}

CALC() {
  // The patcher is the context; see ~send.
  outputs[0].SendBuffer(&receiver.read(parent), thread);
}

#undef className
