#pragma once
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(gSend, YSE::OBJ::G_SEND)
      _NO_MESSAGES
      _NO_CALCULATE

      _INT_IN(SetIntValue)
      _FLOAT_IN(SetFloatValue)
      _BANG_IN(SetBangValue)
      _LIST_IN(SetListValue)
  };
}
}