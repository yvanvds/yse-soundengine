// `.outlet` (issue #545) — one outlet of the subpatcher it lives in. See
// gOutlet.h for the model; the body is four forwards.
#include "gOutlet.h"

#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gOutlet

namespace {

  constexpr char kInletDoc[] =
      "A message from inside the subpatcher, to send out of it. Bang, int, float and list.";

  constexpr char kOutletDoc[] =
      "The subpatcher's outlet with this index, seen from the parent. A cord the parent connects "
      "to that outlet starts here. The message leaves unchanged.";

  constexpr char kIndexDoc[] =
      "Which outlet of the enclosing subpatcher this object is, shared with ~outlet. Give every "
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
      "A message outlet of the subpatcher it is in. With index N, whatever arrives here leaves the "
      "subpatcher through its outlet N.");
  ADD_CATEGORY(pCategory::ENCAPSULATION);

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
