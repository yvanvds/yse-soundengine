// `~outlet` (issue #764) — the audio-rate half of a subpatcher's outlet
// boundary. See dOutlet.h for the model and dInlet.h for the argument; the body
// is a store, a reset and a forward.
#include "dOutlet.h"

#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className dOutlet

namespace {

  constexpr char kInletDoc[] =
      "Where the encapsulated DSP graph hands a signal to the boundary. Wire whatever the "
      "subpatcher computes into here and it leaves the subpatcher through this object's outlet. "
      "Audio only: a float, an int, a bang or a list arriving here is dropped, as on any other DSP "
      "object's signal inlet. Use .outlet for the pins that carry messages.";

  constexpr char kOutletDoc[] =
      "The subpatcher's audio outlet, seen from outside. Nothing inside the subpatcher connects "
      "here — patcherImplementation resolves Connect(subpatcher, N, dest, n) to this object and "
      "records the edge directly, so what leaves reaches whatever the parent patch connected to "
      "the subpatcher's outlet N. The buffer is passed by pointer and not copied: the object "
      "outside is handed the very buffer the object inside computed into.";

  constexpr char kIndexDoc[] =
      "Which outlet of the enclosing subpatcher this object is. Outlet numbers are the parent's "
      "view of the subpatcher: Connect(subpatcher, 1, dest, 0) resolves to the boundary object "
      "whose index is 1. The index space is shared with .outlet, because a subpatcher has one set "
      "of pins and outlet 1 is one pin whether it carries a signal or a message; a ~outlet 1 and a "
      ".outlet 1 in the same subpatcher are two objects claiming one pin, which is not an error "
      "but resolves to whichever is found first, so give them distinct numbers. Changing the index "
      "live re-points which boundary outlet future connections resolve to and does not move cords "
      "that already exist.";

} // namespace

CONSTRUCT_DSP() {
  in = nullptr;

  ADD_IN_0;
  REG_BUFFER_IN(SetInBuffer);

  ADD_OUT_BUFFER;

  ADD_PARAM(index);

  ADD_DESCRIPTION(
      "One audio-rate outlet of the subpatcher it lives in — the signal half of '.outlet', and "
      "'~inlet' read the other way round. Give it index N inside a 'patcher' object, wire the "
      "encapsulated DSP graph into its inlet, and outlet N of that subpatcher is this object: the "
      "signal that arrives here leaves the subpatcher and reaches whatever the parent connected to "
      "outlet N, so a ~sine inside a subpatcher can reach a ~dac outside it. What the render does "
      "at the boundary is forward a pointer: Calculate sends on the exact buffer that arrived, so "
      "the object outside is handed the very buffer the object inside computed into — no copy, no "
      "buffer of this object's own, no added indirection, and a cost independent of nesting depth. "
      "Boundary resolution is the control-rate one: patcherImplementation finds the boundary "
      "object claiming outlet N among the subpatcher's contents and records an ordinary cord from "
      "this object's outlet, so the compiled graph holds a plain DSP edge between two plain "
      "objects. The index space is shared with .outlet, since a subpatcher has one set of pins and "
      "outlet N is one pin whichever rate is behind it. One creation argument, the boundary outlet "
      "number, default 0. Calculate is a null check and a send; nothing allocates, locks or "
      "blocks.");
  ADD_CATEGORY(pCategory::GENERIC);

  INLET_DOC(0, "from patch", kInletDoc, "-1.0 to 1.0");
  OUTLET_DOC(0, "to parent", kOutletDoc, "-1.0 to 1.0");
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
  // See dInlet.cpp — the same forward, in the other direction. A null `in` is
  // the subpatcher having computed nothing into this outlet this block, and the
  // boundary then sends nothing.
  if (in == nullptr) return;
  outputs[0].SendBuffer(in, thread);
}

#undef className
