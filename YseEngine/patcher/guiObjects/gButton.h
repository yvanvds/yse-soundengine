#pragma once
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(gButton, YSE::OBJ::G_BUTTON)
      _NO_MESSAGES
      _DO_CALCULATE

      _INT_IN(SetInt)
      _FLOAT_IN(SetFloat)
      _BANG_IN(Bang)

      _HAS_GUI

private:
  std::atomic<bool> on;
  };
}
}