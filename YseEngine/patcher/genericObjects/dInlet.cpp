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
  in = nullptr;

  ADD_IN_0;
  REG_BUFFER_IN(SetInBuffer);

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

BUFFER_IN(SetInBuffer) {
  (void)inlet;
  (void)thread;
  in = buffer;
}

RESET() // {
in = nullptr;
}

CALC() {
  // The whole boundary. `in` is the upstream object's own buffer, alive for
  // this block, so it is handed straight on — no copy, and no buffer of our
  // own to copy into. Nothing arrived when the parent left this inlet
  // unconnected (the object is then a DSP start point and this runs with a null
  // `in`), in which case the boundary sends nothing, exactly as every other DSP
  // object does with a missing input.
  if (in == nullptr) return;
  outputs[0].SendBuffer(in, thread);
}

#undef className
