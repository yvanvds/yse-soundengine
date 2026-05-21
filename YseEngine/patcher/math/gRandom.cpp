#include "gRandom.h"
#include "../../utils/misc.hpp"

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

  ADD_DESCRIPTION("Random integer generator. On bang, emits a uniformly distributed integer in [0, range).");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "bang", "Trigger — emits a fresh random integer.", "");
  INLET_DOC(1, "range", "Sets the exclusive upper bound of the output range.", "1+");
  OUTLET_DOC(0, "out", "Random integer in [0, range).", "0 to range-1");
  PARAM_DOC("range", "2", "Initial exclusive upper bound.", "1+");
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
