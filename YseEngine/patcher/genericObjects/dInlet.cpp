// `~inlet` (issue #764) — the audio-rate half of a subpatcher's inlet boundary.
// See dInlet.h for the model; the body is a store, a reset and a forward,
// because forwarding a pointer is literally all the render has to do here.
#include "dInlet.h"

#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className dInlet

namespace {

  constexpr char kInletDoc[] =
      "Receives the signal the parent patch connects to the subpatcher's inlet with this index. "
      "Audio only: a message arriving here is dropped.";

  constexpr char kOutletDoc[] = "The incoming signal, unchanged, inside the subpatcher.";

  constexpr char kIndexDoc[] =
      "Which inlet of the enclosing subpatcher this object is, shared with .inlet. Give every "
      "boundary object its own number.";

} // namespace

CONSTRUCT_DSP() {
  ADD_IN_0;
  REG_SIGNAL_IN;

  ADD_OUT_BUFFER;

  ADD_PARAM(index);

  ADD_DESCRIPTION(
      "A signal inlet of the subpatcher it is in. With index N, the signal the parent connects to "
      "the subpatcher's inlet N comes out of this object, inside the subpatcher.");
  ADD_CATEGORY(pCategory::ENCAPSULATION);

  INLET_DOC(0, "from parent", kInletDoc, "-1.0 to 1.0");
  OUTLET_DOC(0, "out", kOutletDoc, "-1.0 to 1.0");
  PARAM_DOC("index", "0", kIndexDoc, "0+");
}

CALC() {
  // The whole boundary. The inlet's buffer is the upstream object's own
  // buffer, alive for this block, so it is handed straight on with no copy.
  // With several cords it is the inlet's sum, and with none (the parent left
  // the pin unconnected) silence, so everything behind the boundary still
  // runs, as it would in the parent (issue #1036).
  outputs[0].SendBuffer(&inputs[0].Signal(0.f), thread);
}

#undef className
