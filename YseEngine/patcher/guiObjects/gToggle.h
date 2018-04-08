#pragma once
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {
    PATCHER_CLASS(gToggle, YSE::OBJ::G_TOGGLE)
      _NO_MESSAGES
      _DO_CALCULATE

      _INT_IN(SetValue)
      _BANG_IN(Bang)

      _HAS_GUI

private:
  std::atomic<bool> value;
  };
  }
}