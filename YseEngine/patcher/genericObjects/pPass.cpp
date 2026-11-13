// `~pass` (issue #1137): see pPass.h. Muted, the render sends its silence.
#include "pPass.h"

using namespace YSE::PATCHER;

#define className pPass

CONSTRUCT_DSP() {
  ADD_IN_0;
  REG_SIGNAL_IN;

  ADD_OUT_BUFFER;

  ADD_DESCRIPTION("Passes a signal on unchanged, and outputs silence while the subpatcher it is in "
                  "is muted by ~mute.");
  ADD_CATEGORY(pCategory::ENCAPSULATION);

  INLET_DOC(0, "in", "The signal to pass on. Unconnected, it reads 0.", "-1.0 to 1.0");
  OUTLET_DOC(0, "out", "The input, or silence while the enclosing subpatcher is muted.",
             "-1.0 to 1.0");
}

CALC() {
  // The arriving buffer itself, as ~inlet forwards it: no copy.
  outputs[0].SendBuffer(&inputs[0].Signal(0.f), thread);
}

#undef className
