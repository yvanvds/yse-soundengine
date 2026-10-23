#include "dDivide.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className dDivide

CONSTRUCT_DSP() {

  // default data
  leftFloatIn = 0.f;
  rightFloatIn = 1.f; // the identity of division

  // in 0: dividend (signal, float or int)
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetLeftFloat);
  REG_INT_IN(SetLeftInt);

  // in 1: divisor (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetRightFloat);
  REG_INT_IN(SetRightInt);

  // out 0: audio output
  ADD_OUT_BUFFER;

  ADD_PARAM(rightFloatIn);

  ADD_DESCRIPTION("Divides an audio signal by another audio signal or by a number. A divisor of "
                  "0 gives 0.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "left", "Audio signal, or a number used while no audio is connected.", "any float");
  INLET_DOC(1, "right",
            "Audio signal, or a number used while no audio is connected. A divisor of 0 "
            "gives 0.",
            "any float");
  OUTLET_DOC(0, "out", "left / right, or 0 where right is 0.", "any float");
  PARAM_DOC("right", "1",
            "Number used while no audio is connected to the right inlet. 0 makes the "
            "result 0.",
            "any float");
}

FLOAT_IN(SetLeftFloat) {
  leftFloatIn = value;
}

INT_IN(SetLeftInt) {
  leftFloatIn = static_cast<float>(value);
}

FLOAT_IN(SetRightFloat) {
  rightFloatIn = value;
}

INT_IN(SetRightInt) {
  rightFloatIn = static_cast<float>(value);
}

CALC() {
  // Unconnected inlets read as their numbers; a connected signal wins (#1036).
  output = inputs[0].Signal(leftFloatIn);

  if (inputs[1].IsConstant()) {
    output /= rightFloatIn;
  } else {
    output /= inputs[1].Signal(rightFloatIn);
  }

  outputs[0].SendBuffer(&output, thread);
}

#undef className