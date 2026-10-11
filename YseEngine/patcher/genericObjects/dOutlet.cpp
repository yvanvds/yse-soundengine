// `~outlet` (issue #764) — the audio-rate half of a subpatcher's outlet
// boundary. See dOutlet.h for the model and dInlet.h for the argument; the body
// is a store, a reset and a forward.
#include "dOutlet.h"

#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className dOutlet

namespace {

  constexpr char kInletDoc[] =
      "A signal from inside the subpatcher, to send out of it. Audio only: a message arriving here "
      "is dropped.";

  constexpr char kOutletDoc[] =
      "The subpatcher's outlet with this index, seen from the parent. The signal leaves unchanged.";

  constexpr char kIndexDoc[] =
      "Which outlet of the enclosing subpatcher this object is, shared with .outlet. Give every "
      "boundary object its own number.";

} // namespace

CONSTRUCT_DSP() {
  ADD_IN_0;
  REG_SIGNAL_IN;

  ADD_OUT_BUFFER;

  ADD_PARAM(index);

  ADD_DESCRIPTION(
      "A signal outlet of the subpatcher it is in. With index N, the signal that arrives here "
      "leaves the subpatcher through its outlet N.");
  ADD_CATEGORY(pCategory::ENCAPSULATION);

  INLET_DOC(0, "from patch", kInletDoc, "-1.0 to 1.0");
  OUTLET_DOC(0, "to parent", kOutletDoc, "-1.0 to 1.0");
  PARAM_DOC("index", "0", kIndexDoc, "0+");
}

CALC() {
  // See dInlet.cpp — the same forward, in the other direction. Nothing
  // computed into this outlet inside the subpatcher reads as silence.
  outputs[0].SendBuffer(&inputs[0].Signal(0.f), thread);
}

#undef className
