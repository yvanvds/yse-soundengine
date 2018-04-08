#pragma once
#include "..\pObject.h"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(gRandom, YSE::OBJ::G_RANDOM)
      _NO_MESSAGES
      _DO_CALCULATE

      _INT_IN(SetIntRange)
      _FLOAT_IN(SetFloatRange)
      _BANG_IN(Bang)

private:
  aInt range;

  };
}
}