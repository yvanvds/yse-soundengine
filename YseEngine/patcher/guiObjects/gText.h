#pragma once
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(gText, YSE::OBJ::G_TEXT)
      _NO_MESSAGES
      _NO_CALCULATE

private:
  std::string text;
  };
}
}