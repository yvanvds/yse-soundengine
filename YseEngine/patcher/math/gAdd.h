#pragma once
#include "..\pObject.h"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(gAdd, YSE::OBJ::G_ADD)
      _HAS_PARAMS
      _NO_MESSAGES
      _HAS_CALCULATE

      FLOAT_IN(SetLeftValue)
      FLOAT_IN(SetRightValue)

    private:
      float leftIn;
      float rightIn;
      float result;
    };
  }
}