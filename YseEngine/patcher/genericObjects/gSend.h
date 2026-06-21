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

    private:
      // 0 = also fire the in-patcher PassData/PassBang path (default),
      // 1 = publish only to the global bus. Parsed from the second
      // constructor argument (issue #122).
      int globalOnly = 0;
  };
}
}