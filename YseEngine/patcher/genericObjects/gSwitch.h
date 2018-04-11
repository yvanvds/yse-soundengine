#pragma once
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(gSwitch, YSE::OBJ::G_SWITCH)
      _NO_MESSAGES
      _NO_CALCULATE

      _INT_IN(SetActiveInlet)
      _INT_IN(SetIntValue)
      _FLOAT_IN(SetFloatValue)
      _BANG_IN(SetBangValue)
      _LIST_IN(SetListValue)

      _PARM_CLEAR
      _PARM_PARSE

private:
  std::atomic<int> activeInlet;
  std::atomic<int> numInlets;
  };
}
}