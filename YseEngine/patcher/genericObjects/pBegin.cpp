// `~begin` (issue #1137): see pBegin.h.
#include "pBegin.h"

using namespace YSE::PATCHER;

#define className pBegin

CONSTRUCT_DSP() {
  ADD_OUT_BUFFER;

  ADD_DESCRIPTION(
      "Outputs a constant signal of 0, to mark the start of a chain. Connected to a "
      "signal inlet it adds nothing. To switch a chain off, use ~mute on a subpatcher.");
  ADD_CATEGORY(pCategory::ENCAPSULATION);

  OUTLET_DOC(0, "out", "A constant 0.", "0");
}

CALC() {
  zeros = 0.f;
  outputs[0].SendBuffer(&zeros, thread);
}

#undef className
