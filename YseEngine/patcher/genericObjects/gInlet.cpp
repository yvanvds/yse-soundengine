// `.inlet` (issue #545) — one inlet of the subpatcher it lives in. See
// gInlet.h for the model; the body is four forwards.
#include "gInlet.h"

#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gInlet

namespace {

  constexpr char kInletDoc[] =
      "Receives what the parent patch sends to the subpatcher's inlet with this index. A cord "
      "connected to that inlet lands here. Bang, int, float and list.";

  constexpr char kOutletDoc[] =
      "The incoming message, unchanged, inside the subpatcher. An int stays an int, a list a list.";

  constexpr char kIndexDoc[] =
      "Which inlet of the enclosing subpatcher this object is, shared with ~inlet. Give every "
      "boundary object its own number.";

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
      "A message inlet of the subpatcher it is in. With index N, whatever the parent sends to the "
      "subpatcher's inlet N comes out of this object, inside the subpatcher.");
  ADD_CATEGORY(pCategory::ENCAPSULATION);

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
