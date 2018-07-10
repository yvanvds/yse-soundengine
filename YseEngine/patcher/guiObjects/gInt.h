#pragma once
#include "../pObject.h"
#include <atomic>

namespace YSE{
  namespace PATCHER {

    PATCHER_CLASS(gInt, YSE::OBJ::G_INT)
      _NO_MESSAGES
      _DO_CALCULATE

      _INT_IN(SetInt)
      _FLOAT_IN(SetFloat)
      _BANG_IN(Bang)

      _HAS_GUI

    private:
      std::atomic<int> value;
  };
  }
}
