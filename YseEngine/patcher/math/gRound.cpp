#include "gRound.h"
#include "../pObjectList.hpp"
#include <cmath>

using namespace YSE::PATCHER;

#define className gRound

CONSTRUCT() {
  ADD_IN_0;
  REG_FLOAT_IN(SetLeftFloat);
  REG_INT_IN(SetLeftInt);

  ADD_IN_1;
  REG_FLOAT_IN(SetRightFloat);
  REG_INT_IN(SetRightInt);

  ADD_OUT_FLOAT;

  leftIn = 0.f;
  // The step defaults to 1 (round to the nearest integer), so it has to be set
  // before Register() hands the field to the parameter system — a creation
  // argument overwrites it, no argument leaves it at 1.
  rightIn = 1.f;
  ADD_PARAM(rightIn);

  ADD_DESCRIPTION("Control-rate rounding. Emits the input snapped to the nearest multiple of the "
                  "step whenever inlet 0 fires — a step of 0.5 snaps to halves, the default step "
                  "of 1 rounds to the nearest integer, and a step of 0 passes the input through "
                  "unchanged. Halfway values round away from zero.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "in", "Value to round — fires the evaluation.", "any float");
  INLET_DOC(1, "step",
            "Step to round to a multiple of — stored until the next evaluation. Zero disables "
            "rounding.",
            "any float");
  OUTLET_DOC(0, "out", "The input snapped to the nearest multiple of the step.", "any float");
  PARAM_DOC("step", "1", "Initial step. 0 passes the input through unchanged.", "any float");
}

FLOAT_IN(SetLeftFloat) {
  leftIn = value;
}

FLOAT_IN(SetRightFloat) {
  rightIn = value;
}

INT_IN(SetLeftInt) {
  leftIn = (float)value;
}

INT_IN(SetRightInt) {
  rightIn = (float)value;
}

CALC() {
  // A step of 0 would divide by zero; Max treats it as "no rounding", which is
  // more useful here than the 0 the other math objects emit — it lets a patch
  // switch rounding off without rewiring. Branch only: no allocation, no lock,
  // no I/O.
  float result = leftIn;
  if (rightIn != 0.f) {
    result = std::round(leftIn / rightIn) * rightIn;
    // A tiny step can push the intermediate quotient past the float range; fall
    // back to the unrounded value rather than emitting an infinity or a NaN.
    if (!std::isfinite(result)) {
      result = leftIn;
    }
  }
  outputs[0].SendFloat(result, thread);
}
