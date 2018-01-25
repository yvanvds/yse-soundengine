#pragma once
#include "..\pObject.h"
#include <atomic>

namespace YSE{
  namespace PATCHER {

    PATCHER_CLASS(gInt, YSE::OBJ::G_INT)
      _NO_MESSAGES
      _DO_CALCULATE

      _INT_IN(SetValue)
      _BANG_IN(Bang)

    private:
      int value;
  };
  }
}
