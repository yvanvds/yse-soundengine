// `~inlet` (issue #764) — the audio-rate half of a subpatcher's inlet boundary.
// See dInlet.h for the model; the body is a store, a reset and a forward,
// because forwarding a pointer is literally all the render has to do here.
#include "dInlet.h"

#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className dInlet

namespace {

  constexpr char kInletDoc[] =
      "Where the parent patch's signal cord lands. Nothing inside the subpatcher connects here — "
      "patcherImplementation resolves Connect(source, n, subpatcher, N) to this object and records "
      "the edge directly, so what arrives is whatever signal the parent wired to the subpatcher's "
      "inlet N. Audio only: this inlet registers a buffer handler and nothing else, so a float, an "
      "int, a bang or a list arriving here is dropped, exactly as it is on any other DSP object's "
      "signal inlet. Use .inlet for the pins that carry messages; a subpatcher may have some of "
      "each, numbered in one shared index space.";

  constexpr char kOutletDoc[] =
      "The signal that arrived at the subpatcher's boundary, sent on inside the subpatcher. The "
      "buffer is passed by pointer and not copied: what leaves here is the very buffer the "
      "upstream source computed into, so crossing the boundary adds no buffer, no copy and no "
      "indirection to the audio path — just one Calculate and one send, however deeply the "
      "subpatcher is nested.";

  constexpr char kIndexDoc[] =
      "Which inlet of the enclosing subpatcher this object is. Inlet numbers are the parent's view "
      "of the subpatcher: Connect(source, 0, subpatcher, 2) resolves to the boundary object whose "
      "index is 2. The index space is shared with .inlet, because a subpatcher has one set of pins "
      "and inlet 2 is one pin whether it carries a signal or a message; a ~inlet 2 and a .inlet 2 "
      "in the same subpatcher are therefore two objects claiming one pin, which is not an error "
      "but resolves to whichever is found first, so give them distinct numbers. Changing the index "
      "live re-points which boundary inlet future connections resolve to and does not move cords "
      "that already exist.";

} // namespace

CONSTRUCT_DSP() {
  in = nullptr;

  ADD_IN_0;
  REG_BUFFER_IN(SetInBuffer);

  ADD_OUT_BUFFER;

  ADD_PARAM(index);

  ADD_DESCRIPTION(
      "One audio-rate inlet of the subpatcher it lives in — the signal half of '.inlet' (Max's "
      "'inlet' carrying a signal). Give it index N inside a 'patcher' object and inlet N of that "
      "subpatcher is this object, so the signal the parent wires there comes out of this outlet "
      "inside the subpatcher, and a ~adc outside can reach a filter inside. What the render does "
      "at the boundary is forward a pointer and nothing else: this patcher's signal path already "
      "passes buffers by address, so Calculate sends on the exact buffer that arrived rather than "
      "a copy of it. Crossing a subpatcher with a signal therefore costs one store, one Calculate "
      "and one send — no buffer copy, no extra buffer, no added indirection, and nothing that "
      "scales with block size or nesting depth. Boundary resolution is the control-rate one: "
      "patcherImplementation finds the boundary object claiming inlet N among the subpatcher's "
      "contents and records an ordinary cord to this object's inlet 0, so the compiled graph holds "
      "a plain DSP edge between two plain objects. The index space is shared with .inlet, since a "
      "subpatcher has one set of pins and inlet N is one pin whichever rate is behind it. This is "
      "a separate object rather than a signal-capable .inlet because IsDSPObject() decides start-"
      "point selection, T_GUI dispatch and what every palette is told the object is, and one "
      "object cannot answer both honestly — the same split as '.+' and '~+'. One creation "
      "argument, the boundary inlet number, default 0. Calculate is a null check and a send; "
      "nothing allocates, locks or blocks.");
  ADD_CATEGORY(pCategory::GENERIC);

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
