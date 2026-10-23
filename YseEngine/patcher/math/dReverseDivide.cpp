#include "dReverseDivide.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className dReverseDivide

CONSTRUCT_DSP() {
  leftFloatIn = 0.f;
  rightFloatIn = 1.f;

  // in 0: the divisor (signal, float or int)
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetLeftFloat);
  REG_INT_IN(SetLeftInt);

  // in 1: the value divided (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetRightFloat);
  REG_INT_IN(SetRightInt);

  // out 0: right / left
  ADD_OUT_BUFFER;

  ADD_PARAM(rightFloatIn);

  ADD_DESCRIPTION("Divides the right signal by the left one: ~/ with the inlets reversed. With a "
                  "right number of 1 it gives the reciprocal. A left sample of 0 gives 0.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "left",
            "The divisor: a signal, or a number used while no signal is connected. 0 makes the "
            "result 0.",
            "any float");
  INLET_DOC(1, "right",
            "The value divided: a signal, or a number used while no signal is connected.",
            "any float");
  OUTLET_DOC(0, "out", "right / left, or 0 where left is 0.", "any float");
  PARAM_DOC("right", "1", "Number used while no signal is connected to the right inlet.",
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
