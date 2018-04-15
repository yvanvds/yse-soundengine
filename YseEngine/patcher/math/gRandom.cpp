#include "gRandom.h"
#include "utils\misc.hpp"

using namespace YSE::PATCHER;

#define className gRandom

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(Bang);

  ADD_IN_1;
  REG_INT_IN(SetIntRange);
  REG_FLOAT_IN(SetFloatRange);

  ADD_PARAM(range);

  ADD_OUT_INT;

  range = 2;
}

BANG_IN(Bang) {}

INT_IN(SetIntRange) {
  range = value;
}

FLOAT_IN(SetFloatRange) {
  range = (int)value;
}

CALC() {
  outputs[0].SendInt(YSE::Random(range), thread);
}
