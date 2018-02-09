#pragma once
#include "..\pObject.h"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(gSubstract, YSE::OBJ::G_SUBSTRACT)
      _NO_MESSAGES
      _DO_CALCULATE

      _FLOAT_IN(SetLeftValue)
      _FLOAT_IN(SetRightValue)

private:
  float leftIn;
  float rightIn;
  float result;
  };
}
}