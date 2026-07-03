#pragma once
#include "../pObject.h"
#include <atomic>

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(gCounter, YSE::OBJ::G_COUNTER)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(Bang)
    _LIST_IN(SetListValue)
    _INT_IN(SetIntValue)

    _HAS_GUI

  private:
    aInt step{1};
    aInt startValue{0};
    aInt currentValue{0};
  };
}
}
