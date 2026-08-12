// `.inlet` (issue #545) — one inlet of the subpatcher it lives in. See
// gInlet.h for the model; the body is four forwards.
#include "gInlet.h"

#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gInlet

namespace {

  constexpr char kInletDoc[] =
      "Where the parent patch's cord lands. Max's inlet object appears to have no inlet because a "
      "patcher window draws the boundary for you; headless the cord has to attach to something, "
      "and attaching it to a real inlet is what lets the graph compiler, Disconnect, "
      "UnwireFromPeers and DumpJSON all treat a boundary cord as the ordinary edge it is. Nothing "
      "connects here from inside the subpatcher — patcherImplementation resolves "
      "Connect(source, n, subpatcher, N) to this object and records the edge directly, so what "
      "arrives is whatever the parent sent to the subpatcher's inlet N. Bang, int, float and list "
      "cross; audio signals arrive on a ~inlet instead, which shares this object's index space "
      "(issue #764).";

  constexpr char kOutletDoc[] =
      "The message that arrived at the subpatcher's boundary, sent on unchanged inside the "
      "subpatcher. Wire it to whatever the encapsulated graph starts with. Kind is preserved: an "
      "int arrives as an int, a list as a list, a bang as a bang — a boundary that normalised them "
      "would be a different object to everything downstream of it.";

  constexpr char kIndexDoc[] =
      "Which inlet of the enclosing subpatcher this object is. Inlet numbers are the parent's view "
      "of the subpatcher: Connect(source, 0, subpatcher, 2) resolves to the .inlet whose index is "
      "2. Changing it live re-points which boundary inlet future connections resolve to and does "
      "not move cords that already exist — those stay attached to this object, which is where they "
      "were recorded. Two .inlet objects claiming the same index in one subpatcher is not an "
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
      "One inlet of the subpatcher it lives in — Max's 'inlet'. A pass-through with a number: give "
      "it index N inside a 'patcher' object and inlet N of that subpatcher is this object, so "
      "whatever the parent patch sends there comes out of this outlet inside the subpatcher. There "
      "is no boundary at run time. patcherImplementation resolves Connect(source, 0, subpatcher, "
      "N) on the control thread by finding the .inlet with index N among the subpatcher's contents "
      "and recording an ordinary cord from the source's outlet straight to this object's inlet 0, "
      "so the compiled graph holds a plain edge between two plain objects and crossing a "
      "subpatcher costs exactly one pass-through object however deeply it is nested. That is also "
      "why this object has an inlet where Max's appears not to: Max hides it because a patcher "
      "window draws the boundary, and headless the cord has to land on something real for "
      "Disconnect, UnwireFromPeers and the serialiser to handle it as the ordinary edge it is. "
      "Bang, int, float and list cross here; a signal pin is a ~inlet instead, because "
      "IsDSPObject() decides start-point selection, T_GUI dispatch and what every palette is told "
      "an object is, and one object cannot answer both honestly. The two share one index space — a "
      "subpatcher has one set of inlet pins and inlet N is one pin whichever rate is behind it — "
      "so one subpatcher may have pins of both kinds, with distinct numbers. One creation "
      "argument, the boundary inlet number, default 0. Every handler is one send; nothing "
      "allocates, locks or blocks.");
  ADD_CATEGORY(pCategory::GENERIC);

  INLET_DOC(0, "from parent", kInletDoc, "");
  OUTLET_DOC(0, "out", kOutletDoc, "");
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
