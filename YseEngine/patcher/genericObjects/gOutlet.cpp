// `.outlet` (issue #545) — one outlet of the subpatcher it lives in. See
// gOutlet.h for the model; the body is four forwards.
#include "gOutlet.h"

#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gOutlet

namespace {

  constexpr char kInletDoc[] =
      "Where the encapsulated graph hands a message to the boundary. Wire whatever the subpatcher "
      "computes into here and it leaves the subpatcher through this object's outlet. Bang, int, "
      "float and list cross; audio signals leave through a ~outlet instead, which shares this "
      "object's index space (issue #764).";

  constexpr char kOutletDoc[] =
      "The subpatcher's outlet, seen from outside. Nothing inside the subpatcher connects here — "
      "patcherImplementation resolves Connect(subpatcher, N, dest, n) to this object and records "
      "the edge directly, so what leaves reaches whatever the parent patch connected to the "
      "subpatcher's outlet N. Max's outlet object appears to have no outlet because a patcher "
      "window draws the boundary; headless the cord has to leave from something real, which is "
      "what keeps a boundary edge indistinguishable from any other edge to the graph compiler, "
      "Disconnect, UnwireFromPeers and DumpJSON. Kind is preserved on the way out.";

  constexpr char kIndexDoc[] =
      "Which outlet of the enclosing subpatcher this object is. Outlet numbers are the parent's "
      "view of the subpatcher: Connect(subpatcher, 1, dest, 0) resolves to the .outlet whose index "
      "is 1. Changing it live re-points which boundary outlet future connections resolve to and "
      "does not move cords that already exist — those stay attached to this object, which is where "
      "they were recorded. Two .outlet objects claiming the same index in one subpatcher is not an "
      "error; the first one found wins and which that is is unspecified, so give them distinct "
      "numbers.";

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_OUT_ANY;

  ADD_PARAM(index);

  ADD_DESCRIPTION(
      "One outlet of the subpatcher it lives in — Max's 'outlet', and '.inlet' read the other way "
      "round. Give it index N inside a 'patcher' object, wire the encapsulated graph into its "
      "inlet, and outlet N of that subpatcher is this object: what arrives here leaves the "
      "subpatcher and reaches whatever the parent connected to outlet N. There is no boundary at "
      "run time. patcherImplementation resolves Connect(subpatcher, N, dest, n) on the control "
      "thread by finding the .outlet with index N among the subpatcher's contents and recording an "
      "ordinary cord from this object's outlet straight to the destination's inlet, so the "
      "compiled graph holds a plain edge between two plain objects and crossing a subpatcher costs "
      "exactly one pass-through object however deeply it is nested. That is also why this object "
      "has an outlet where Max's appears not to: Max hides it because a patcher window draws the "
      "boundary, and headless the cord has to leave from something real for Disconnect, "
      "UnwireFromPeers and the serialiser to handle it as the ordinary edge it is. Bang, int, "
      "float and list cross here; a signal pin is a ~outlet instead, because IsDSPObject() decides "
      "start-point selection, T_GUI dispatch and what every palette is told an object is, and one "
      "object cannot answer both honestly. The two share one index space — a subpatcher has one "
      "set of outlet pins and outlet N is one pin whichever rate is behind it. One creation "
      "argument, the boundary outlet number, default 0. Every handler is one send; nothing "
      "allocates, locks or blocks.");
  ADD_CATEGORY(pCategory::GENERIC);

  INLET_DOC(0, "from patch", kInletDoc, "");
  OUTLET_DOC(0, "to parent", kOutletDoc, "");
  PARAM_DOC("index", "0", kIndexDoc, "0+");
}

BANG_IN(BangIn) {
  (void)inlet;
  outputs[0].SendBang(thread);
}

INT_IN(IntIn) {
  (void)inlet;
  outputs[0].SendInt(value, thread);
}

FLOAT_IN(FloatIn) {
  (void)inlet;
  outputs[0].SendFloat(value, thread);
}

LIST_IN(ListIn) {
  (void)inlet;
  outputs[0].SendList(value, thread);
}

#undef className
