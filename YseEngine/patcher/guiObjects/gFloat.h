#pragma once
#include "..\pObject.h"
#include <atomic>

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(gFloat, YSE::OBJ::G_FLOAT)
      _NO_MESSAGES
      _DO_CALCULATE

      _FLOAT_IN(SetFloat)
      _INT_IN(SetInt)
      _BANG_IN(Bang)

      _HAS_GUI

private:
  std::atomic<float> value;
  };
}
}

