#include "dDivide.h"

using namespace YSE::PATCHER;

#define className dDivide

CONSTRUCT_DSP() {

  // default data
  rightFloatIn = 1.f;

  // in 0: audio buffer
  ADD_IN_0;
  REG_SIGNAL_IN;

  // in 1: divisor (int, float or audio)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetRightFloat);
  REG_INT_IN(SetRightInt);

  // out 0: audio output
  ADD_OUT_BUFFER;

  ADD_PARAM(rightFloatIn);

  ADD_DESCRIPTION("Divides an audio signal by another audio signal or by a float. A float of 0 "
                  "leaves the signal unchanged.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "left", "Audio signal.", "any float");
  INLET_DOC(1, "right",
            "Audio signal, or a number used while no audio is connected. A number of 0 leaves the "
            "signal unchanged.",
            "any non-zero float");
  OUTLET_DOC(0, "out", "left / right.", "any float");
  PARAM_DOC("right", "1.0",
            "Float used while no audio is connected to the right inlet. 0 leaves the signal "
            "unchanged.",
            "any non-zero float");
}

FLOAT_IN(SetRightFloat) {
  rightFloatIn = value;
}

INT_IN(SetRightInt) {
  rightFloatIn = static_cast<float>(value);
}

CALC() {
  // Unconnected inlets read as constants (issue #1036): the left as 0, the
  // right as its float. A connected right signal wins over the float.
  output = inputs[0].Signal(0.f);

  if (inputs[1].IsConstant()) {
    if (rightFloatIn != 0.f) output /= rightFloatIn;
  } else {
    output /= inputs[1].Signal(rightFloatIn);
  }

  outputs[0].SendBuffer(&output, thread);
}
