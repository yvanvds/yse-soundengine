#include "dTypeRoute.h"

using namespace YSE::PATCHER;

#define className dTypeRoute

CONSTRUCT_DSP() {
  // Cold: a message is passed on by its handler and has nothing to recompute.
  inputs.emplace_back(this, false, 0);
  REG_SIGNAL_IN;
  REG_BANG_IN(PassBang);
  REG_INT_IN(PassInt);
  REG_FLOAT_IN(PassFloat);
  REG_LIST_IN(PassList);

  ADD_OUT_BUFFER;
  ADD_OUT_ANY;

  ADD_DESCRIPTION("Sorts what arrives at its one inlet by type: a signal goes out the left "
                  "outlet, every message (bang, number, list or word message) out the right "
                  "one. Lets one inlet of a subpatcher take either.");
  ADD_CATEGORY(pCategory::ROUTING);
  INLET_DOC(0, "in",
            "A signal, or any message: bang, int, float, a list, or a message that starts with "
            "a word. Several signal cords are summed.",
            "");
  OUTLET_DOC(0, "signal",
             "The signal that arrived, unchanged; silence when no signal is connected. A number "
             "never reaches this outlet.",
             "");
  OUTLET_DOC(1, "messages",
             "Every message that arrived, at once and with its type kept: a bang as a bang, an "
             "int as an int, a float as a float, a list or a word message as it was spelled.",
             "");
}

BANG_IN(PassBang) {
  (void)inlet;
  outputs[1].SendBang(thread);
}

INT_IN(PassInt) {
  (void)inlet;
  outputs[1].SendInt(value, thread);
}

FLOAT_IN(PassFloat) {
  (void)inlet;
  outputs[1].SendFloat(value, thread);
}

LIST_IN(PassList) {
  // A word message reaches this handler too: without a command channel, the
  // inlet hands text that starts with a word to the list handler (#933), and
  // sent on as text it reads as the same message downstream (#1199).
  (void)inlet;
  outputs[1].SendList(value, thread);
}

CALC() {
  // The buffer that arrived, or silence when no signal is connected: the
  // fallback is 0, not the last number, which went out the other outlet.
  outputs[0].SendBuffer(&inputs[0].Signal(0.f), thread);
}

#undef className
