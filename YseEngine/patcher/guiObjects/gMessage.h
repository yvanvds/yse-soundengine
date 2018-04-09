#pragma once
#include "..\pObject.h"
#include <string>

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(gMessage, YSE::OBJ::G_MESSAGE)
      _NO_MESSAGES
      _NO_CALCULATE

      _BANG_IN(Bang)
      _LIST_IN(SetValue)

      _HAS_GUI
private:
  std::string message;
  };
}
}