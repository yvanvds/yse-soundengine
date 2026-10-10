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

  ADD_DESCRIPTION(
      "Sends a random integer from 0 to range-1 on each bang, every value equally likely.");
  ADD_CATEGORY(pCategory::RANDOM);
  INLET_DOC(0, "bang", "A bang sends a new random number.", "bang");
  INLET_DOC(1, "range", "Sets the range for the next numbers. A float is truncated.", "1+");
  OUTLET_DOC(0, "out", "A random integer from 0 to range-1.", "0 to range-1");
  PARAM_DOC("range", "2", "How many different values can come out.", "1+");
}

// Empty on purpose: inlet 0 is hot, so a bang's arrival runs Calculate(),
// which draws and sends. A range of 0 or less sends 0 (YSE::Random's rule).
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
