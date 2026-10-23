#include "dModulo.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className dModulo

CONSTRUCT_DSP() {
  leftFloatIn = 0.f;
  rightFloatIn = 1.f;

  // in 0: the value divided (signal, float or int)
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetLeftFloat);
  REG_INT_IN(SetLeftInt);

  // in 1: the divisor (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetRightFloat);
  REG_INT_IN(SetRightInt);

  // out 0: the remainder
  ADD_OUT_BUFFER;

  ADD_PARAM(rightFloatIn);

  ADD_DESCRIPTION("Divides the left signal by the right one and outputs the remainder, with the "
                  "sign of the left signal. Wraps phases and indexes. A divisor of 0 gives 0.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "left",
            "The value divided: a signal, or a number used while no signal is connected.",
            "any float");
  INLET_DOC(1, "right",
            "The divisor: a signal, or a number used while no signal is connected. 0 makes the "
            "result 0.",
            "any float");
  OUTLET_DOC(0, "out",
             "The remainder of left / right, with the sign of left, or 0 where right is 0.",
             "any float");
  PARAM_DOC("right", "1", "Divisor used while no signal is connected to the right inlet.",
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
  DSP::buffer& left = inputs[0].Signal(leftFloatIn);
  DSP::buffer& out =
      inputs[1].IsConstant() ? op(left, rightFloatIn) : op(left, inputs[1].Signal(rightFloatIn));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
