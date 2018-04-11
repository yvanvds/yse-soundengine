#pragma once
#include "../pObject.h"

/*
  This object can receive data from outside a patcher.
  The SetData methods will pass data if the argument
  is the same as the argument of this object.
*/

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(gReceive, YSE::OBJ::G_RECEIVE)
      _NO_MESSAGES
      _NO_CALCULATE

      _INT_IN(SetIntValue)
      _FLOAT_IN(SetFloatValue)
      _BANG_IN(SetBangValue)
      _LIST_IN(SetListValue)

  };
}
}